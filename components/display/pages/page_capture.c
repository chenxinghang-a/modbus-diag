#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "packet_capture.h"
#include "frame_parser.h"
#include "traffic_stats.h"
#include "health_metrics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_capturing = false;
static char s_msg[48];      /* 启动失败等提示，空串表示无 */
static bool s_hex_view = false;   /* false=解析视图，true=原始字节视图（长按 OK 切换） */

/* 最近帧的本地环形缓冲。
 *
 * 为什么必须有：packet_capture_get_frame() 是**出队**（xQueueReceive），
 * 原来页面里取了两遍——第一遍喂统计、第二遍渲染列表——第二遍队列已经空了，
 * 所以「最近报文」那一栏**永远是空白**。现在只在取帧处取一次，同时存进这个环，
 * 渲染从环里读，不再碰队列。 */
#define RECENT_MAX 8
static captured_frame_t s_recent[RECENT_MAX];
static int s_recent_head = 0;
static int s_recent_count = 0;

static void recent_push(const captured_frame_t *f)
{
    s_recent[s_recent_head] = *f;
    s_recent_head = (s_recent_head + 1) % RECENT_MAX;
    if (s_recent_count < RECENT_MAX) s_recent_count++;
}

static void capture_enter(void)
{
    s_need_redraw = true;
    s_capturing = false;
    s_recent_head = 0;
    s_recent_count = 0;
    packet_capture_init();
    traffic_stats_init();
    /* 诊断页的健康指标由这里喂数据。用 ensure_init 而不是 init：
     * 进抓包页不该把诊断页刚攒的统计清零。 */
    health_metrics_ensure_init();
}

static void capture_exit(void)
{
    if (s_capturing) {
        packet_capture_stop();
        s_capturing = false;
    }
}

static void capture_update(void)
{
    if (!s_need_redraw && !s_capturing) return;

    if (s_capturing) {
        /* FPS/bps 只在 traffic_stats_tick() 里结算，而全工程没人调它 → 页面上
         * 「FPS: 0 / bps: 0」永远是 0。这里按秒节流调一次：没帧的时候也要调，
         * 否则停止发包后 FPS 会一直停在最后一个值。 */
        static TickType_t s_last_tick = 0;
        TickType_t now = xTaskGetTickCount();
        if (s_last_tick == 0) {
            s_last_tick = now;
        } else if (now - s_last_tick >= pdMS_TO_TICKS(1000)) {
            s_last_tick = now;
            traffic_stats_tick();
        }

        /* Process captured frames */
        captured_frame_t frame;
        while (packet_capture_get_frame(&frame)) {
            recent_push(&frame);
            parsed_frame_t parsed;
            if (frame_parser_parse(frame.data, frame.len, &parsed)) {
                traffic_stats_add_frame(frame.crc_valid, parsed.is_exception,
                                        0, frame.len);
                /* is_error 口径：CRC 错、异常响应、解不出帧，都算「这一帧没拿到有效数据」。
                 * response_us 传 0：抓包侧没有请求/响应配对时间戳，0 表示「这次不计时」，
                 * health_metrics_update 里用 if (response_us > 0) 挡住。 */
                health_metrics_update(0,
                                      (!frame.crc_valid) || parsed.is_exception,
                                      parsed.is_exception,
                                      parsed.is_exception ? parsed.exception_code : 0);
            }
        }
    }

    /* 限帧：抓包时每次 update 都整屏重绘一次（50fps × 320×240×2B ≈ 7.5MB/s），
     * 已经超过 40MHz SPI 的实际吞吐 —— UI 任务会一直堵在 SPI 传输上，按键发涩。
     * 数据变化用不着 50fps，限到 10fps。
     * 注意：取帧在上面做完了，限的只是「画」。 */
    static TickType_t s_last_draw = 0;
    TickType_t now_ms = xTaskGetTickCount();
    if (!s_need_redraw && (now_ms - s_last_draw) < pdMS_TO_TICKS(100)) return;
    s_last_draw = now_ms;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Packet Capture", false, s_capturing);

    traffic_stats_t stats;
    traffic_stats_get(&stats);

    char buf[48];
    snprintf(buf, sizeof(buf), "Frames: %lu  Errors: %lu",
             (unsigned long)stats.total_frames, (unsigned long)stats.error_frames);
    tft_draw_string(10, 24, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "ErrRate: %.1f%%  FPS: %lu",
             stats.error_rate * 100.0f, (unsigned long)stats.frames_per_sec);
    tft_draw_string(10, 36, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* 抓包是被动的，没有「请求→响应」配对，量不出响应时间：
     * add_frame 一直传 response_us=0，min 就停在初值 INT32_MAX。
     * 老代码直接打印 avg/max，页面上永远是一对 0，看着像「测得很快」。 */
    if (!traffic_stats_has_timing(&stats)) {
        snprintf(buf, sizeof(buf), "Resp: -- (passive, no timing)");
    } else {
        snprintf(buf, sizeof(buf), "Resp: avg=%ld max=%ld us",
                 (long)stats.avg_response_us, (long)stats.max_response_us);
    }
    tft_draw_string(10, 48, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* Show recent frames —— 从本地环里读，**不要**再调 packet_capture_get_frame()，
     * 那会把队列里的新帧吃掉（原来就是这么写的，导致这一栏永远空白）。 */
    int y = 65;
    for (int i = 0; i < s_recent_count && i < RECENT_MAX; i++) {
        int idx = (s_recent_head - s_recent_count + i + RECENT_MAX) % RECENT_MAX;
        const captured_frame_t *f = &s_recent[idx];
        parsed_frame_t parsed;
        bool parsed_ok = frame_parser_parse(f->data, f->len, &parsed);

        /* 解不出的帧（短帧、CRC 错）在十六进制视图里**必须照样显示**：
         * 现场最想看的恰恰是「这帧为什么不像正常报文」，把它藏起来等于掩盖线索。 */
        if (!parsed_ok && !s_hex_view) {
            continue;
        }

        char desc[64];
        if (s_hex_view) {
            /* 一行最多 13 字节：320px 宽、FONT_SMALL 约 53 字符 */
            if (frame_parser_to_hex(f->data, f->len, desc, sizeof(desc), 13) < 0) {
                snprintf(desc, sizeof(desc), "(too long)");
            }
        } else {
            frame_parser_to_string(&parsed, desc, sizeof(desc));
        }
        char line[80];
        snprintf(line, sizeof(line), "%6lums %s", (unsigned long)f->uptime_ms, desc);
        uint16_t color = f->crc_valid ? THEME_FG : THEME_ERROR;
        tft_draw_string(10, y, line, color, THEME_BG, FONT_SMALL);
        y += 12;
    }

    tft_draw_string(10, TFT_HEIGHT - 18,
                    s_capturing ? "[OK]Stop [OK hold]Hex [BACK]Exit"
                                : "[OK]Start [OK hold]Hex [BACK]Exit",
                    THEME_FG, THEME_BG, FONT_SMALL);
    tft_draw_string(TFT_WIDTH - 52, TFT_HEIGHT - 18,
                    s_hex_view ? "HEX" : "PARSED", THEME_ACCENT, THEME_BG, FONT_SMALL);

    if (s_msg[0]) {
        tft_draw_string(10, TFT_HEIGHT - 32, s_msg, THEME_ERROR, THEME_BG, FONT_SMALL);
    }
}

static void capture_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK - toggle capture */
            if (s_capturing) {
                packet_capture_stop();
                s_capturing = false;
                s_msg[0] = '\0';
            } else {
                /* 启动可能失败（UART 正被 Modbus 主站占用）。原实现忽略返回值、
                 * 照样把 s_capturing 置 true —— 界面显示「正在抓包」但帧数永远 0，
                 * 用户会以为总线没数据。 */
                esp_err_t err = packet_capture_start();
                s_capturing = (err == ESP_OK);
                if (err != ESP_OK) {
                    snprintf(s_msg, sizeof(s_msg), "Start failed (%d): UART busy?", (int)err);
                } else {
                    s_msg[0] = '\0';
                }
            }
            s_need_redraw = true;
            break;
        case 4: /* 长按 OK：切换 解析视图 / 原始字节视图 */
            s_hex_view = !s_hex_view;
            s_need_redraw = true;
            break;
        case 3: /* BACK */
            gui_manager_back();
            break;
    }
}

static void __attribute__((constructor)) register_page(void)
{
    static const page_t page = {
        .id = PAGE_CAPTURE,
        .on_enter = capture_enter,
        .on_exit = capture_exit,
        .on_update = capture_update,
        .on_key = capture_key,
    };
    gui_register_page(&page);
}

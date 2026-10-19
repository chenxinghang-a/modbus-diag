#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "packet_capture.h"
#include "frame_parser.h"
#include "traffic_stats.h"
#include "health_metrics.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_capturing = false;
static int s_scroll = 0;

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
    /* 诊断页的健康指标由这里喂数据；不先 init 的话 mutex 是 NULL，
     * health_metrics_update() 会静默返回，诊断页永远停在「无数据」。 */
    health_metrics_init();
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
    s_need_redraw = false;

    if (s_capturing) {
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

    snprintf(buf, sizeof(buf), "Resp: avg=%ld max=%ld us",
             (long)stats.avg_response_us, (long)stats.max_response_us);
    tft_draw_string(10, 48, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* Show recent frames —— 从本地环里读，**不要**再调 packet_capture_get_frame()，
     * 那会把队列里的新帧吃掉（原来就是这么写的，导致这一栏永远空白）。 */
    int y = 65;
    for (int i = 0; i < s_recent_count && i < RECENT_MAX; i++) {
        int idx = (s_recent_head - s_recent_count + i + RECENT_MAX) % RECENT_MAX;
        const captured_frame_t *f = &s_recent[idx];
        parsed_frame_t parsed;
        if (!frame_parser_parse(f->data, f->len, &parsed)) {
            continue;
        }
        char desc[48];
        char line[64];
        frame_parser_to_string(&parsed, desc, sizeof(desc));
        snprintf(line, sizeof(line), "%6lums %s", (unsigned long)f->uptime_ms, desc);
        uint16_t color = f->crc_valid ? THEME_FG : THEME_ERROR;
        tft_draw_string(10, y, line, color, THEME_BG, FONT_SMALL);
        y += 12;
    }

    tft_draw_string(10, TFT_HEIGHT - 18,
                    s_capturing ? "[OK] Stop  [BACK] Exit" : "[OK] Start [BACK] Exit",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void capture_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK - toggle capture */
            if (s_capturing) {
                packet_capture_stop();
                s_capturing = false;
            } else {
                packet_capture_start();
                s_capturing = true;
            }
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

#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "health_metrics.h"
#include "report_generator.h"
#include "report_export.h"
#include "modbus_common.h"
#include "packet_capture.h"
#include "frame_parser.h"
#include "log_storage.h"
#include "wifi_manager.h"
#include "device_scanner.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"
#include "web_server.h"
#include "mdns_service.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_monitoring = false;
static bool s_uart_busy = false;   /* 上一次点「开始监测」被拒：UART 正被 Modbus 主站占着 */
static int  s_pending = 0;         /* 距上次重绘又收了多少帧（攒够再刷，别每帧全屏重画） */
static health_report_t s_report;

static void diag_drain_frames(void)
{
    captured_frame_t frame;
    while (packet_capture_get_frame(&frame)) {
        parsed_frame_t parsed;
        if (frame_parser_parse(frame.data, frame.len, &parsed)) {
            health_metrics_update(0,
                                  (!frame.crc_valid) || parsed.is_exception,
                                  parsed.is_exception,
                                  parsed.is_exception ? parsed.exception_code : 0);
            s_pending++;
        }
    }
}

static void diag_enter(void)
{
    s_need_redraw = true;
    s_monitoring = packet_capture_is_running();
    s_uart_busy = false;
    /* 用 ensure_init：进本页不该把抓包页刚采到的数据清零 */
    health_metrics_ensure_init();
}

static void diag_exit(void)
{
    /* 离开页面就别占着 UART：否则抓包页一进来 packet_capture_init() 会删掉队列，
     * 而后台那个 capture_task 还在往已删除的队列里写。 */
    if (s_monitoring) {
        packet_capture_stop();
        s_monitoring = false;
    }
}

static void diag_update(void)
{
    if (s_monitoring) {
        diag_drain_frames();
        if (s_pending >= 8) {      /* 攒够 8 帧刷一次屏，避免每帧 fill_screen 卡死 UI */
            s_pending = 0;
            s_need_redraw = true;
        }
    }

    if (!s_need_redraw) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Diag Report", wifi_manager_is_connected(), s_monitoring);

    health_metrics_get_report(&s_report);

    /* 没有采到任何帧时不能说「100 分 EXCELLENT」—— 那是把「没测」当成「没问题」，
     * 现场最容易误判的一种展示。这里显式显示 No Data，分数条也留空。 */
    bool has_data = (s_report.total_frames > 0);

    char buf[48];
    if (!has_data) {
        tft_draw_string(10, 28, "Score: -- (No Data)", THEME_WARNING, THEME_BG, FONT_LARGE);
    } else {
        snprintf(buf, sizeof(buf), "Score: %d/100 (%s)",
                 s_report.health_score, health_metrics_grade(s_report.health_score));
        uint16_t score_color = THEME_SUCCESS;
        if (s_report.health_score < 60) score_color = THEME_ERROR;
        else if (s_report.health_score < 75) score_color = THEME_WARNING;
        tft_draw_string(10, 28, buf, score_color, THEME_BG, FONT_LARGE);
    }

    snprintf(buf, sizeof(buf), "Frames: %lu  Errors: %lu",
             (unsigned long)s_report.total_frames, (unsigned long)s_report.error_frames);
    tft_draw_string(10, 52, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "ErrRate: %.2f%%", s_report.error_rate * 100.0f);
    tft_draw_string(10, 64, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* 有帧 ≠ 有响应时间：被动抓包没有请求/响应配对，min_response_us 会停在 INT32_MAX。
     * 只看 total_frames 的话，这里会打印出 2147483647。 */
    if (has_data && s_report.min_response_us != INT32_MAX) {
        snprintf(buf, sizeof(buf), "Resp: min=%ld avg=%ld max=%ld us",
                 (long)s_report.min_response_us, (long)s_report.avg_response_us,
                 (long)s_report.max_response_us);
    } else {
        snprintf(buf, sizeof(buf), "Resp: -- (no timing)");
    }
    tft_draw_string(10, 76, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "Jitter: %ld us", (long)s_report.jitter_us);
    tft_draw_string(10, 88, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* Health bar —— 无数据时画空条，不要画满 */
    uint16_t bar_color = THEME_SUCCESS;
    if (has_data) {
        if (s_report.health_score < 60) bar_color = THEME_ERROR;
        else if (s_report.health_score < 75) bar_color = THEME_WARNING;
    }
    ui_progress_bar(10, 104, 300, 12, has_data ? s_report.health_score : 0,
                    bar_color, THEME_PROGRESS_BG);

    if (s_monitoring) {
        tft_draw_string(10, 124, "[OK] Stop Monitor", THEME_ERROR, THEME_BG, FONT_NORMAL);
    } else if (s_uart_busy) {
        tft_draw_string(10, 124, "[OK] Start (UART busy!)", THEME_WARNING, THEME_BG, FONT_NORMAL);
    } else {
        tft_draw_string(10, 124, "[OK] Start Monitor", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    }
    tft_draw_string(10, 144, "[UP] Export JSON", THEME_FG, THEME_BG, FONT_NORMAL);
    tft_draw_string(10, 164, "[DN] Start WiFi+Web", THEME_FG, THEME_BG, FONT_NORMAL);

    if (wifi_manager_is_connected()) {
        snprintf(buf, sizeof(buf), "Web: http://%s/", wifi_manager_get_ip());
        tft_draw_string(10, 184, buf, THEME_ACCENT, THEME_BG, FONT_SMALL);
    }

    /* 最近一次异常 + 处置建议：只报 "Illegal Addr" 没用，维护人员要的是「该动哪儿」 */
    if (s_report.last_exception_code) {
        snprintf(buf, sizeof(buf), "Last EXC: %u %s",
                 (unsigned)s_report.last_exception_code,
                 modbus_exception_name(s_report.last_exception_code));
        tft_draw_string(10, 206, buf, THEME_ERROR, THEME_BG, FONT_SMALL);
        snprintf(buf, sizeof(buf), "Fix: %s",
                 modbus_exception_advice(s_report.last_exception_code));
        tft_draw_string(10, 220, buf, THEME_WARNING, THEME_BG, FONT_SMALL);
    } else if (!has_data) {
        tft_draw_string(10, 206, "Go to Capture -> OK to start", THEME_WARNING, THEME_BG, FONT_SMALL);
    }
}

static void diag_key(uint8_t key)
{
    switch (key) {
        case 0: /* UP - export */
        {
            /* 报告里带上「哪台设备、什么参数、跑了多久、最近发生了什么」——
             * FR7 要求含设备信息/通信参数/异常事件记录，之前只有统计与评分。 */
            static char events[512];
            log_storage_read(events, sizeof(events), 12);
            modbus_config_t mcfg;
            modbus_master_get_config(&mcfg);
            const device_info_t *dev = device_scan_selected_device();
            report_meta_t meta = {
                .slave_addr = dev ? dev->slave_addr : 0,
                .baud = mcfg.baud_rate,
                .parity = mcfg.parity,
                .stop_bits = mcfg.stop_bits,
                .fw_version = "v1.0.0",
                .uptime_s = (uint32_t)((xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000),
                .events = events,
            };

            char json[1024];
            report_generate_json(json, sizeof(json), &s_report, "ESP32-ModbusDiag", &meta);
            report_export_to_file(json, "report.json");

            /* 同一份数据再出一份 HTML：浏览器直接能看，Web 的 /report 优先发它。
             * 放静态区，别压在 GUI 任务栈上（3KB 栈很容易爆）。 */
            static char html[4096];
            int n = report_generate_html(html, sizeof(html), &s_report, "ESP32-ModbusDiag", &meta);
            if (n > 0 && (size_t)n < sizeof(html)) {
                report_export_to_file(html, "report.html");
            }
            log_storage_logf("report exported: score=%d frames=%lu errors=%lu",
                             s_report.health_score,
                             (unsigned long)s_report.total_frames,
                             (unsigned long)s_report.error_frames);
            s_need_redraw = true;
            break;
        }
        case 1: /* DOWN - start wifi */
            wifi_manager_init();
            wifi_manager_start_ap();
            mdns_service_init();
            web_server_start();
            s_need_redraw = true;
            break;
        case 2: /* OK - 真的开始/停止采样（原先只翻转一个标志位，页面永远不动） */
            if (!s_monitoring) {
                health_metrics_init();          /* 新一轮采样，从零算 */
                s_pending = 0;
                if (packet_capture_start() == ESP_OK) {
                    s_monitoring = true;
                    s_uart_busy = false;
                } else {
                    s_uart_busy = true;         /* Modbus 主站占着 UART，先停轮询 */
                }
            } else {
                packet_capture_stop();
                s_monitoring = false;
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
        .id = PAGE_DIAG,
        .on_enter = diag_enter,
        .on_exit = diag_exit,
        .on_update = diag_update,
        .on_key = diag_key,
    };
    gui_register_page(&page);
}

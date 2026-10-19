#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "health_metrics.h"
#include "report_generator.h"
#include "report_export.h"
#include "modbus_common.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "mdns_service.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_monitoring = false;
static health_report_t s_report;

static void diag_enter(void)
{
    s_need_redraw = true;
    s_monitoring = false;
    health_metrics_init();
}

static void diag_exit(void)
{
}

static void diag_update(void)
{
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

    if (has_data) {
        snprintf(buf, sizeof(buf), "Resp: min=%ld avg=%ld max=%ld us",
                 (long)s_report.min_response_us, (long)s_report.avg_response_us,
                 (long)s_report.max_response_us);
    } else {
        /* min_response_us 初值是 INT32_MAX，没数据时直接打出来会是 2147483647 */
        snprintf(buf, sizeof(buf), "Resp: -- (no samples)");
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

    tft_draw_string(10, 124, "[OK] Monitor/Stop", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
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
            char json[1024];
            report_generate_json(json, sizeof(json), &s_report, "ESP32-ModbusDiag");
            report_export_to_file(json, "report.json");

            /* 同一份数据再出一份 HTML：浏览器直接能看，Web 的 /report 优先发它。
             * 放静态区，别压在 GUI 任务栈上（3KB 栈很容易爆）。 */
            static char html[3072];
            int n = report_generate_html(html, sizeof(html), &s_report, "ESP32-ModbusDiag");
            if (n > 0 && (size_t)n < sizeof(html)) {
                report_export_to_file(html, "report.html");
            }
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
        case 2: /* OK - toggle monitoring */
            s_monitoring = !s_monitoring;
            if (s_monitoring) health_metrics_init();
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

#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "health_metrics.h"
#include "report_generator.h"
#include "report_export.h"
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

    char buf[48];
    snprintf(buf, sizeof(buf), "Score: %d/100 (%s)",
             s_report.health_score, health_metrics_grade(s_report.health_score));
    uint16_t score_color = THEME_SUCCESS;
    if (s_report.health_score < 60) score_color = THEME_ERROR;
    else if (s_report.health_score < 75) score_color = THEME_WARNING;
    tft_draw_string(10, 28, buf, score_color, THEME_BG, FONT_LARGE);

    snprintf(buf, sizeof(buf), "Frames: %lu  Errors: %lu",
             (unsigned long)s_report.total_frames, (unsigned long)s_report.error_frames);
    tft_draw_string(10, 52, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "ErrRate: %.2f%%", s_report.error_rate * 100.0f);
    tft_draw_string(10, 64, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "Resp: min=%ld avg=%ld max=%ld us",
             (long)s_report.min_response_us, (long)s_report.avg_response_us, (long)s_report.max_response_us);
    tft_draw_string(10, 76, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "Jitter: %ld us", (long)s_report.jitter_us);
    tft_draw_string(10, 88, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* Health bar */
    ui_progress_bar(10, 104, 300, 12, s_report.health_score, score_color, THEME_PROGRESS_BG);

    tft_draw_string(10, 124, "[OK] Monitor/Stop", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    tft_draw_string(10, 144, "[UP] Export JSON", THEME_FG, THEME_BG, FONT_NORMAL);
    tft_draw_string(10, 164, "[DN] Start WiFi+Web", THEME_FG, THEME_BG, FONT_NORMAL);

    if (wifi_manager_is_connected()) {
        snprintf(buf, sizeof(buf), "Web: http://%s/", wifi_manager_get_ip());
        tft_draw_string(10, 190, buf, THEME_ACCENT, THEME_BG, FONT_SMALL);
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

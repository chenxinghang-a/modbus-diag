#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "app_event.h"
#include "app_config.h"
#include "modbus_master.h"
#include "device_scanner.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_scanning = false;
static scan_result_t s_result;
static int s_progress = 0;
static int s_progress_total = 0;
static int s_selected_device = 0;

static ui_menu_t s_menu;
static const char *s_menu_labels[SCANNER_MAX_DEVICES + 1];

static void scan_progress_cb(int current, int total, void *user_data)
{
    s_progress = current;
    s_progress_total = total;
    s_need_redraw = true;
}

static void scan_done_cb(const device_info_t *dev, void *user_data)
{
    s_need_redraw = true;
}

static void do_scan(void)
{
    s_scanning = true;
    s_progress = 0;
    s_need_redraw = true;

    scan_config_t cfg = {
        .baud_rates = NULL,  /* use defaults */
        .baud_count = 0,
        .addr_start = 1,
        .addr_end = 247,
        .timeout_ms = 50,
        .retry_count = 1,
        .on_progress = scan_progress_cb,
        .on_device_found = scan_done_cb,
    };
    device_scan_start(&cfg, &s_result);
    s_scanning = false;
    s_need_redraw = true;
}

static void rtu_enter(void)
{
    s_need_redraw = true;
    s_scanning = false;
    memset(&s_result, 0, sizeof(s_result));
}

static void rtu_exit(void)
{
}

static void rtu_update(void)
{
    if (!s_need_redraw) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("RTU Scan", false, s_scanning);

    if (s_scanning) {
        char buf[32];
        tft_draw_string(20, 40, "Scanning...", THEME_ACCENT, THEME_BG, FONT_LARGE);
        snprintf(buf, sizeof(buf), "%d / %d", s_progress, s_progress_total);
        tft_draw_string(20, 70, buf, THEME_FG, THEME_BG, FONT_NORMAL);
        ui_progress_bar(20, 90, 280, 16,
                        s_progress_total > 0 ? (s_progress * 100 / s_progress_total) : 0,
                        THEME_PROGRESS_FG, THEME_PROGRESS_BG);
    } else if (s_result.device_count > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Found %d devices", s_result.device_count);
        tft_draw_string(20, 28, buf, THEME_SUCCESS, THEME_BG, FONT_NORMAL);

        /* Build menu labels */
        for (int i = 0; i < s_result.device_count; i++) {
            static char labels[SCANNER_MAX_DEVICES][32];
            snprintf(labels[i], sizeof(labels[i]), "Addr:%d Baud:%lu %ldus",
                     s_result.devices[i].slave_addr,
                     (unsigned long)s_result.devices[i].baud_rate,
                     (long)s_result.devices[i].response_time_us);
            s_menu_labels[i] = labels[i];
        }
        s_menu_labels[s_result.device_count] = "[Rescan]";

        ui_menu_init(&s_menu, 20, 48, TFT_WIDTH - 40, 6,
                     s_menu_labels, s_result.device_count + 1);
        ui_menu_draw(&s_menu);
    } else {
        tft_draw_string(20, 40, "No devices found", THEME_FG, THEME_BG, FONT_NORMAL);
        tft_draw_string(20, 70, "[OK] Start Scan", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    }

    tft_draw_string(20, TFT_HEIGHT - 18,
                    "[OK] Scan  [BACK] Return",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void rtu_key(uint8_t key)
{
    if (s_scanning) return;

    switch (key) {
        case 0: /* UP */
            if (s_result.device_count > 0) {
                ui_menu_move_up(&s_menu);
                s_need_redraw = true;
            }
            break;
        case 1: /* DOWN */
            if (s_result.device_count > 0) {
                ui_menu_move_down(&s_menu);
                s_need_redraw = true;
            }
            break;
        case 2: /* OK */
            if (s_result.device_count > 0 && s_menu.selected == s_result.device_count) {
                do_scan();  /* Rescan */
            } else if (s_result.device_count > 0) {
                gui_manager_goto(PAGE_DEVICE_INFO);
            } else {
                do_scan();
            }
            break;
        case 3: /* BACK */
            gui_manager_back();
            break;
    }
}

static void __attribute__((constructor)) register_page(void)
{
    static const page_t page = {
        .id = PAGE_RTU_SCAN,
        .on_enter = rtu_enter,
        .on_exit = rtu_exit,
        .on_update = rtu_update,
        .on_key = rtu_key,
    };
    gui_register_page(&page);
}

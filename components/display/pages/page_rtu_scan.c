#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "app_event.h"
#include "app_config.h"
#include "modbus_master.h"
#include "device_scanner.h"
#include "log_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_scanning = false;
static bool s_quick_scan = false;   /* true = 只扫当前波特率（现场已知波特率时用） */
static scan_result_t s_result;
static int s_progress = 0;
static int s_progress_total = 0;

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

    TickType_t t0 = xTaskGetTickCount();

    /* 快速扫描只喂当前波特率：现场知道波特率时，不必把 5 档各跑一遍（耗时 1/5） */
    modbus_config_t cur_cfg;
    modbus_master_get_config(&cur_cfg);
    uint32_t quick_baud = cur_cfg.baud_rate;
    /* 单地址超时取设置页配的值（原来这里写死 50ms，设置页那项是死的） */
    uint32_t scan_timeout = cur_cfg.timeout_ms ? cur_cfg.timeout_ms : 50;

    scan_config_t cfg = {
        .baud_rates = s_quick_scan ? &quick_baud : NULL,
        .baud_count = s_quick_scan ? 1 : 0,
        .addr_start = 1,
        .addr_end = 247,
        .timeout_ms = scan_timeout,
        .retry_count = 0,   /* 扫描不重试：247 个地址各重试一次，耗时直接翻倍 */
        .on_progress = scan_progress_cb,
        .on_device_found = scan_done_cb,
    };
    device_scan_start(&cfg, &s_result);
    s_scanning = false;
    s_need_redraw = true;

    /* scan_duration_ms 这个字段以前**从来没被赋值过**，但论文 6.x 的测试表里
     * 专门有一列「扫描耗时(s)」。这里把实测耗时填上，页面也显示出来。 */
    s_result.scan_duration_ms = (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);

    log_storage_logf("scan: %d device(s), addr %d-%d, %d baud, %lu ms",
                     s_result.device_count, cfg.addr_start, cfg.addr_end,
                     s_result.baud_rates_tested,
                     (unsigned long)s_result.scan_duration_ms);
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
        char buf[40];
        snprintf(buf, sizeof(buf), "Found %d / %d addr, %lu.%lus",
                 s_result.device_count, s_result.addresses_tested,
                 (unsigned long)(s_result.scan_duration_ms / 1000),
                 (unsigned long)((s_result.scan_duration_ms % 1000) / 100));
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

        /* 扫描模式：全档（5 档波特率各遍历一遍，慢但不会漏）或快速（只扫当前波特率） */
        char buf[40];
        modbus_config_t cur;
        modbus_master_get_config(&cur);
        if (s_quick_scan) {
            snprintf(buf, sizeof(buf), "Mode: QUICK @%lu bps", (unsigned long)cur.baud_rate);
            tft_draw_string(20, 92, buf, THEME_WARNING, THEME_BG, FONT_SMALL);
        } else {
            tft_draw_string(20, 92, "Mode: ALL baud (5 steps)", THEME_FG, THEME_BG, FONT_SMALL);
        }
    }

    tft_draw_string(20, TFT_HEIGHT - 18,
                    "[OK] Scan  [OK hold] Mode  [BACK] Return",
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
                /* 把「选的是哪台」传下去：详情页原来硬编码 addr=1/9600，
                 * 选第二台进去看到的还是第一台。 */
                device_scan_select_device(s_menu.selected);
                gui_manager_goto(PAGE_DEVICE_INFO);
            } else {
                do_scan();
            }
            break;
        case 4: /* 长按 OK：切换 全档/快速 扫描 */
            s_quick_scan = !s_quick_scan;
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
        .id = PAGE_RTU_SCAN,
        .on_enter = rtu_enter,
        .on_exit = rtu_exit,
        .on_update = rtu_update,
        .on_key = rtu_key,
    };
    gui_register_page(&page);
}

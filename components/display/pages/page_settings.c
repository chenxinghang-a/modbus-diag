#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "nvs_config.h"
#include "log_storage.h"
#include "modbus_master.h"
#include "app_config.h"
#include <stdio.h>

static bool s_need_redraw = true;
static int s_selected = 0;

static const char *s_menu_labels[] = {
    "Baud Rate",
    "Frame",
    "Timeout (ms)",
    "Retry Count",
    "AP Name",
    "About",
};

static ui_menu_t s_menu;
static int s_baud_idx = 0;
static const uint32_t s_baud_rates[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
static const int s_baud_count = sizeof(s_baud_rates) / sizeof(s_baud_rates[0]);

/* 帧格式：现场大多数仪表是 8E1/8O1，只支持 8N1 等于连不上 ——
 * FR10 要求数据位、停止位、校验方式都可配且掉电不丢。数据位固定 8（Modbus 规范里也是 8）。 */
typedef struct { const char *label; uint8_t parity; uint8_t stop_bits; } frame_fmt_t;
static const frame_fmt_t s_frames[] = {
    {"8N1", 0, 1},
    {"8E1", 2, 1},
    {"8O1", 1, 1},
    {"8N2", 0, 2},
};
static const int s_frame_count = sizeof(s_frames) / sizeof(s_frames[0]);
static int s_frame_idx = 0;

/* 超时也是可配的：扫描器用它当单地址超时，直接决定扫描耗时（现场调试要能调）。
 * 原来这一项在菜单里摆着、按 OK 什么都不发生。 */
static const uint32_t s_timeouts[] = {50, 100, 200, 500, 1000};
static const int s_timeout_count = sizeof(s_timeouts) / sizeof(s_timeouts[0]);
static int s_timeout_idx = 0;

/* 重试次数：论文 FR1 写了「支持可配置的超时时间和重试次数」，
 * 但 retry_count 之前既没 UI 也没存 NVS，改了配置也没入口。 */
static const uint8_t s_retries[] = {0, 1, 2, 3};
static const int s_retry_count = sizeof(s_retries) / sizeof(s_retries[0]);
static int s_retry_idx = 0;

static void settings_enter(void)
{
    s_need_redraw = true;
    s_selected = 0;
    ui_menu_init(&s_menu, 20, 30, TFT_WIDTH - 40, 6,
                 s_menu_labels, sizeof(s_menu_labels) / sizeof(s_menu_labels[0]));

    /* Load saved baud rate */
    uint32_t baud = 9600;
    nvs_config_get_u32("baud", &baud, 9600);
    for (int i = 0; i < s_baud_count; i++) {
        if (s_baud_rates[i] == baud) { s_baud_idx = i; break; }
    }

    /* Load saved frame format */
    uint32_t par = 0, sb = 1;
    nvs_config_get_u32("parity", &par, 0);
    nvs_config_get_u32("stop_bits", &sb, 1);
    for (int i = 0; i < s_frame_count; i++) {
        if (s_frames[i].parity == par && s_frames[i].stop_bits == sb) { s_frame_idx = i; break; }
    }

    /* Load saved timeout */
    uint32_t tmo = 50;
    nvs_config_get_u32("timeout_ms", &tmo, 50);
    for (int i = 0; i < s_timeout_count; i++) {
        if (s_timeouts[i] == tmo) { s_timeout_idx = i; break; }
    }

    /* Load saved retry count */
    uint32_t rt = 2;
    nvs_config_get_u32("retry_count", &rt, 2);
    for (int i = 0; i < s_retry_count; i++) {
        if (s_retries[i] == rt) { s_retry_idx = i; break; }
    }
}

static void settings_exit(void)
{
}

static void settings_update(void)
{
    if (!s_need_redraw) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Settings", false, false);
    ui_menu_draw(&s_menu);

    /* Show current values */
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_baud_rates[s_baud_idx]);
    ui_list_item_draw(20, 30, TFT_WIDTH - 40, "Baud Rate", buf, s_selected == 0);

    /* 第二项：帧格式 8N1/8E1/8O1/8N2（FR10） */
    ui_list_item_draw(20, 30 + MENU_ITEM_H, TFT_WIDTH - 40, "Frame",
                      s_frames[s_frame_idx].label, s_selected == 1);

    /* 第三项：超时（原来只有标签没有值，按 OK 也不响应） */
    snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)s_timeouts[s_timeout_idx]);
    ui_list_item_draw(20, 30 + 2 * MENU_ITEM_H, TFT_WIDTH - 40, "Timeout (ms)", buf,
                      s_selected == 2);

    /* 第四项：重试次数（FR1 要求可配置） */
    snprintf(buf, sizeof(buf), "%u", (unsigned)s_retries[s_retry_idx]);
    ui_list_item_draw(20, 30 + 3 * MENU_ITEM_H, TFT_WIDTH - 40, "Retry Count", buf,
                      s_selected == 3);

    /* 第四项：只读信息。原来的 "WiFi SSID" 按 OK 什么都不发生、也没有输入界面，
     * 是个空承诺；改成显示当前 AP 名称，"About" 同理只显示版本。 */
    ui_list_item_draw(20, 30 + 4 * MENU_ITEM_H, TFT_WIDTH - 40, "AP Name",
                      WIFI_AP_SSID, s_selected == 4);
    ui_list_item_draw(20, 30 + 5 * MENU_ITEM_H, TFT_WIDTH - 40, "About",
                      "v1.0.0", s_selected == 5);

    tft_draw_string(20, TFT_HEIGHT - 18,
                    "[UP/DN] Navigate  [OK] Change  [BACK] Exit",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void settings_key(uint8_t key)
{
    switch (key) {
        case 0: /* UP */
            ui_menu_move_up(&s_menu);
            s_selected = s_menu.selected;
            s_need_redraw = true;
            break;
        case 1: /* DOWN */
            ui_menu_move_down(&s_menu);
            s_selected = s_menu.selected;
            s_need_redraw = true;
            break;
        case 2: /* OK */
            if (s_selected == 0) {
                /* Cycle baud rate */
                s_baud_idx = (s_baud_idx + 1) % s_baud_count;
                nvs_config_set_u32("baud", s_baud_rates[s_baud_idx]);

                /* 必须先取回整份配置再改。原来写的是
                 *   modbus_config_t cfg = { .baud_rate = ... };
                 * C 的指定初始化会把没提到的字段**全部清零** ——
                 * timeout_ms 变 0 → pdMS_TO_TICKS(0) → 每个请求立刻超时，
                 * retry_count 变 0 → 一次都不重试。表现就是「改完波特率通信全废，
                 * 重启才好」，而重启又把新波特率丢了。 */
                modbus_config_t cfg;
                modbus_master_get_config(&cfg);
                cfg.baud_rate = s_baud_rates[s_baud_idx];
                modbus_master_configure(&cfg);
                log_storage_logf("config: baud=%lu (saved to NVS)",
                                 (unsigned long)cfg.baud_rate);
            } else if (s_selected == 1) {
                /* Cycle 帧格式：现场 8E1/8O1 设备占很大比例，只给 8N1 等于连不上 */
                s_frame_idx = (s_frame_idx + 1) % s_frame_count;
                nvs_config_set_u32("parity", s_frames[s_frame_idx].parity);
                nvs_config_set_u32("stop_bits", s_frames[s_frame_idx].stop_bits);

                modbus_config_t cfg;
                modbus_master_get_config(&cfg);
                cfg.parity = s_frames[s_frame_idx].parity;
                cfg.stop_bits = s_frames[s_frame_idx].stop_bits;
                modbus_master_configure(&cfg);
                log_storage_logf("config: frame=%s (saved to NVS)", s_frames[s_frame_idx].label);
            } else if (s_selected == 2) {
                /* Cycle timeout：扫描的单地址超时直接用它 */
                s_timeout_idx = (s_timeout_idx + 1) % s_timeout_count;
                nvs_config_set_u32("timeout_ms", s_timeouts[s_timeout_idx]);

                modbus_config_t cfg;
                modbus_master_get_config(&cfg);
                cfg.timeout_ms = s_timeouts[s_timeout_idx];
                modbus_master_configure(&cfg);
                log_storage_logf("config: timeout=%lu ms (saved to NVS)",
                                 (unsigned long)cfg.timeout_ms);
            } else if (s_selected == 3) {
                /* Cycle retry count：影响超时/CRC 错时的重发次数 */
                s_retry_idx = (s_retry_idx + 1) % s_retry_count;
                nvs_config_set_u32("retry_count", s_retries[s_retry_idx]);

                modbus_config_t cfg;
                modbus_master_get_config(&cfg);
                cfg.retry_count = s_retries[s_retry_idx];
                modbus_master_configure(&cfg);
                log_storage_logf("config: retry=%u (saved to NVS)",
                                 (unsigned)cfg.retry_count);
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
        .id = PAGE_SETTINGS,
        .on_enter = settings_enter,
        .on_exit = settings_exit,
        .on_update = settings_update,
        .on_key = settings_key,
    };
    gui_register_page(&page);
}

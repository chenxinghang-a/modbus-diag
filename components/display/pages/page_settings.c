#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "nvs_config.h"
#include "modbus_master.h"
#include <stdio.h>

static bool s_need_redraw = true;
static int s_selected = 0;

static const char *s_menu_labels[] = {
    "Baud Rate",
    "Device Address",
    "Timeout (ms)",
    "WiFi SSID",
    "About",
};

static ui_menu_t s_menu;
static int s_baud_idx = 0;
static const uint32_t s_baud_rates[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
static const int s_baud_count = sizeof(s_baud_rates) / sizeof(s_baud_rates[0]);

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
                modbus_config_t cfg = { .baud_rate = s_baud_rates[s_baud_idx] };
                modbus_master_configure(&cfg);
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

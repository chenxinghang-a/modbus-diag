#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "register_classifier.h"
#include "modbus_master.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static device_info_t s_dev;
static int s_reg_view_offset = 0;
static bool s_classifying = false;

static const char *reg_type_str[] = {"Coil", "DiscInp", "Holding", "Input"};

static void dev_enter(void)
{
    s_need_redraw = true;
    /* TODO: get selected device from scan result */
    memset(&s_dev, 0, sizeof(s_dev));
    s_dev.slave_addr = 1;
    s_dev.baud_rate = 9600;
}

static void dev_exit(void)
{
}

static void dev_update(void)
{
    if (!s_need_redraw) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Device Info", false, false);

    char buf[80];
    snprintf(buf, sizeof(buf), "Addr: %d  Baud: %lu", s_dev.slave_addr, (unsigned long)s_dev.baud_rate);
    tft_draw_string(10, 24, buf, THEME_FG, THEME_BG, FONT_NORMAL);

    snprintf(buf, sizeof(buf), "Resp: %ld us", (long)s_dev.response_time_us);
    tft_draw_string(10, 38, buf, THEME_FG, THEME_BG, FONT_NORMAL);

    if (s_dev.device_id[0]) {
        snprintf(buf, sizeof(buf), "ID: %s", s_dev.device_id);
        tft_draw_string(10, 52, buf, THEME_ACCENT, THEME_BG, FONT_SMALL);
    }

    if (s_classifying) {
        tft_draw_string(10, 70, "Classifying registers...", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    } else if (s_dev.reg_group_count > 0) {
        tft_draw_string(10, 70, "Register Map:", THEME_FG, THEME_BG, FONT_NORMAL);
        int y = 86;
        for (int i = 0; i < s_dev.reg_group_count && y < TFT_HEIGHT - 30; i++) {
            snprintf(buf, sizeof(buf), "  %s [%d-%d] %s",
                     reg_type_str[s_dev.reg_groups[i].reg_type],
                     s_dev.reg_groups[i].start_addr,
                     s_dev.reg_groups[i].end_addr,
                     s_dev.reg_groups[i].writable ? "R/W" : "R/O");
            tft_draw_string(10, y, buf, THEME_FG, THEME_BG, FONT_SMALL);
            y += 12;
        }
    }

    tft_draw_string(10, TFT_HEIGHT - 18,
                    "[OK] Classify  [BACK] Return",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void dev_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK - classify */
            s_classifying = true;
            s_need_redraw = true;
            /* Run in update to show progress */
            register_classify(s_dev.slave_addr, &s_dev);
            s_classifying = false;
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
        .id = PAGE_DEVICE_INFO,
        .on_enter = dev_enter,
        .on_exit = dev_exit,
        .on_update = dev_update,
        .on_key = dev_key,
    };
    gui_register_page(&page);
}

#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "app_event.h"
#include "app_config.h"

static const char *menu_labels[] = {
    "1. RTU Scan",
    "2. TCP Connect",
    "3. Packet Capture",
    "4. Waveform",
    "5. Diag Report",
    "6. Settings",
};

static ui_menu_t s_menu;
static bool s_need_redraw = true;

static void home_enter(void)
{
    ui_menu_init(&s_menu, 20, 30, TFT_WIDTH - 40, 7,
                 menu_labels, sizeof(menu_labels) / sizeof(menu_labels[0]));
    s_need_redraw = true;
}

static void home_exit(void)
{
}

static void home_update(void)
{
    if (!s_need_redraw) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Modbus Diag", false, false);
    ui_menu_draw(&s_menu);

    /* Footer hint */
    tft_draw_string(20, TFT_HEIGHT - 18,
                    "[UP/DN] Navigate  [OK] Select",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void home_key(uint8_t key)
{
    switch (key) {
        case 0: /* UP */
            ui_menu_move_up(&s_menu);
            s_need_redraw = true;
            break;
        case 1: /* DOWN */
            ui_menu_move_down(&s_menu);
            s_need_redraw = true;
            break;
        case 2: /* OK */
            switch (s_menu.selected) {
                case 0: gui_manager_goto(PAGE_RTU_SCAN); break;
                case 1: gui_manager_goto(PAGE_TCP); break;
                case 2: gui_manager_goto(PAGE_CAPTURE); break;
                case 3: gui_manager_goto(PAGE_WAVEFORM); break;
                case 4: gui_manager_goto(PAGE_DIAG); break;
                case 5: gui_manager_goto(PAGE_SETTINGS); break;
            }
            break;
        case 3: /* BACK - do nothing at home */
            break;
    }
}

const page_t page_home = {
    .id = PAGE_HOME,
    .on_enter = home_enter,
    .on_exit = home_exit,
    .on_update = home_update,
    .on_key = home_key,
};

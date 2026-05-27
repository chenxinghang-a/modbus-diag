#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "modbus_tcp.h"
#include "modbus_master.h"
#include <stdio.h>

static bool s_need_redraw = true;
static bool s_connected = false;
static char s_ip[32] = "192.168.1.100";
static uint16_t s_port = 502;
static int s_cursor = 0;  /* IP digit cursor */

static void tcp_enter(void)
{
    s_need_redraw = true;
    modbus_tcp_init();
}

static void tcp_exit(void)
{
    modbus_tcp_disconnect();
    s_connected = false;
}

static void tcp_update(void)
{
    if (!s_need_redraw) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("TCP Connect", true, s_connected);

    tft_draw_string(20, 30, "Modbus TCP Client", THEME_ACCENT, THEME_BG, FONT_NORMAL);

    char buf[48];
    snprintf(buf, sizeof(buf), "IP: %s:%d", s_ip, s_port);
    tft_draw_string(20, 55, buf, THEME_FG, THEME_BG, FONT_NORMAL);

    if (s_connected) {
        tft_draw_string(20, 80, "Status: CONNECTED", THEME_SUCCESS, THEME_BG, FONT_NORMAL);
        snprintf(buf, sizeof(buf), "Last resp: %ld us", (long)modbus_master_last_response_time());
        tft_draw_string(20, 100, buf, THEME_FG, THEME_BG, FONT_NORMAL);

        tft_draw_string(20, 120, "[OK] Read Test", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
        tft_draw_string(20, 140, "[BACK] Disconnect", THEME_FG, THEME_BG, FONT_NORMAL);
    } else {
        tft_draw_string(20, 80, "Status: DISCONNECTED", THEME_ERROR, THEME_BG, FONT_NORMAL);
        tft_draw_string(20, 110, "[OK] Connect", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
        tft_draw_string(20, 130, "[UP/DN] Edit IP", THEME_FG, THEME_BG, FONT_NORMAL);
    }
}

static void tcp_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK */
            if (!s_connected) {
                esp_err_t ret = modbus_tcp_connect(s_ip, s_port);
                s_connected = (ret == ESP_OK);
            } else {
                /* Test read */
                modbus_request_t req = {
                    .slave_addr = 1,
                    .function_code = MODBUS_FC_READ_HOLD_REGS,
                    .start_addr = 0,
                    .quantity = 1,
                };
                modbus_response_t resp = {0};
                modbus_tcp_send_recv(&req, &resp);
            }
            s_need_redraw = true;
            break;
        case 3: /* BACK */
            if (s_connected) {
                modbus_tcp_disconnect();
                s_connected = false;
                s_need_redraw = true;
            } else {
                gui_manager_back();
            }
            break;
        case 0: /* UP - increment current IP digit */
            s_need_redraw = true;
            break;
        case 1: /* DOWN */
            s_need_redraw = true;
            break;
    }
}

static void __attribute__((constructor)) register_page(void)
{
    static const page_t page = {
        .id = PAGE_TCP,
        .on_enter = tcp_enter,
        .on_exit = tcp_exit,
        .on_update = tcp_update,
        .on_key = tcp_key,
    };
    gui_register_page(&page);
}

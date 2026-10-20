#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "modbus_tcp.h"
#include "modbus_master.h"
#include "nvs_config.h"
#include "log_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_connected = false;
static char s_ip[32] = "192.168.1.100";
static uint16_t s_port = 502;
static int s_octet = 100;   /* IP 末段，UP/DN 调它（4 键机身上没法逐位编辑） */

/* 只改最后一段：现场多数情况是「同网段换一台设备」，
 * 把光标移到任意八位组需要左右键，本机只有 UP/DN/OK/BACK 四键。 */
static void ip_apply_octet(void)
{
    char *dot = strrchr(s_ip, '.');
    if (!dot) return;
    snprintf(dot + 1, sizeof(s_ip) - (size_t)(dot + 1 - s_ip), "%d", s_octet);
}

static void ip_load(void)
{
    nvs_config_get_str("tcp_ip", s_ip, sizeof(s_ip), "192.168.1.100");
    uint32_t p = 502;
    nvs_config_get_u32("tcp_port", &p, 502);
    s_port = (uint16_t)p;

    char *dot = strrchr(s_ip, '.');
    s_octet = dot ? atoi(dot + 1) : 100;
    if (s_octet < 1 || s_octet > 254) s_octet = 100;
    ip_apply_octet();
}

static void tcp_enter(void)
{
    s_need_redraw = true;
    ip_load();          /* 上次连过的 IP 要记住，否则每次开机都回到 192.168.1.100 */
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
        tft_draw_string(20, 130, "[UP/DN] IP last octet", THEME_FG, THEME_BG, FONT_NORMAL);
    }
}

static void tcp_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK */
            if (!s_connected) {
                esp_err_t ret = modbus_tcp_connect(s_ip, s_port);
                s_connected = (ret == ESP_OK);
                if (s_connected) {
                    /* 连上了才值得记：连不上的地址不覆盖上次可用的 */
                    nvs_config_set_str("tcp_ip", s_ip);
                    nvs_config_set_u32("tcp_port", s_port);
                }
                log_storage_logf("tcp connect %s:%u -> %s", s_ip, (unsigned)s_port,
                                 s_connected ? "OK" : "FAILED");
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
        case 0: /* UP - 末段 +1（1~254 循环） */
            if (!s_connected) {
                s_octet = (s_octet % 254) + 1;
                ip_apply_octet();
                s_need_redraw = true;
            }
            break;
        case 1: /* DOWN - 末段 -1 */
            if (!s_connected) {
                s_octet = (s_octet <= 1) ? 254 : s_octet - 1;
                ip_apply_octet();
                s_need_redraw = true;
            }
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

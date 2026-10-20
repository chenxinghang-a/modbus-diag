#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "register_classifier.h"
#include "device_scanner.h"
#include "modbus_master.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static device_info_t s_dev;
static bool s_have_dev = false;
static bool s_classifying = false;

/* 写操作状态机：VIEW（挑寄存器）→ EDIT（改值）→ 提交后回 VIEW。
 * 原来这一页只读不写，FC05/06 在 UI 上根本没有入口（论文 FR 里却列了写功能码）。 */
static int  s_cursor = 0;          /* 可写寄存器组里的序号 */
static bool s_edit = false;
static uint16_t s_edit_val = 0;
static char s_msg[48] = "";
static bool s_msg_err = false;

static const char *reg_type_str[] = {"Coil", "DiscInp", "Holding", "Input"};

static int writable_count(void)
{
    int n = 0;
    for (int i = 0; i < s_dev.reg_group_count; i++) {
        if (s_dev.reg_groups[i].writable) n++;
    }
    return n;
}

/* 把「可写列表里的第 n 项」映射回 s_dev.reg_groups 的下标 */
static int group_at_cursor(int cursor)
{
    int n = 0;
    for (int i = 0; i < s_dev.reg_group_count; i++) {
        if (!s_dev.reg_groups[i].writable) continue;
        if (n == cursor) return i;
        n++;
    }
    return -1;
}

static void dev_enter(void)
{
    s_need_redraw = true;
    s_cursor = 0;
    s_edit = false;
    s_msg[0] = '\0';
    s_msg_err = false;
    memset(&s_dev, 0, sizeof(s_dev));

    /* 用扫描页真正选中的那台。原来这里留了一句 TODO，然后把 slave_addr=1 /
     * baud_rate=9600 写死 —— 选第几台进去看到的都是同一个。 */
    const device_info_t *sel = device_scan_selected_device();
    if (sel) {
        s_dev = *sel;
        s_have_dev = true;
    } else {
        /* 没有扫描结果时，至少把当前主站参数显示出来，别显示一个假的 9600 */
        modbus_config_t cfg;
        modbus_master_get_config(&cfg);
        s_dev.baud_rate = cfg.baud_rate;
        s_dev.slave_addr = 0;
        s_have_dev = false;
    }
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
    if (!s_have_dev) {
        snprintf(buf, sizeof(buf), "No device selected (Baud: %lu)",
                 (unsigned long)s_dev.baud_rate);
        tft_draw_string(10, 24, buf, THEME_WARNING, THEME_BG, FONT_NORMAL);
        tft_draw_string(10, 44, "Scan first, then pick a device", THEME_FG, THEME_BG, FONT_SMALL);
        tft_draw_string(20, TFT_HEIGHT - 18, "[BACK] Return", THEME_FG, THEME_BG, FONT_SMALL);
        return;
    }

    snprintf(buf, sizeof(buf), "Addr: %d  Baud: %lu", s_dev.slave_addr, (unsigned long)s_dev.baud_rate);
    tft_draw_string(10, 24, buf, THEME_FG, THEME_BG, FONT_NORMAL);

    snprintf(buf, sizeof(buf), "Resp: %ld us", (long)s_dev.response_time_us);
    tft_draw_string(10, 38, buf, THEME_FG, THEME_BG, FONT_NORMAL);

    if (s_dev.device_id[0]) {
        snprintf(buf, sizeof(buf), "ID: %s", s_dev.device_id);
        tft_draw_string(10, 52, buf, THEME_ACCENT, THEME_BG, FONT_SMALL);
    }

    int gi = group_at_cursor(s_cursor);
    uint16_t addr = (gi >= 0) ? s_dev.reg_groups[gi].start_addr : 0;

    if (s_classifying) {
        tft_draw_string(10, 70, "Classifying registers...", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    } else if (s_dev.reg_group_count == 0) {
        tft_draw_string(10, 70, "No register map yet", THEME_WARNING, THEME_BG, FONT_NORMAL);
        tft_draw_string(10, 90, "[OK] Classify registers", THEME_FG, THEME_BG, FONT_SMALL);
    } else {
        tft_draw_string(10, 70, "Writable registers:", THEME_FG, THEME_BG, FONT_SMALL);
        int y = 86, n = 0, shown = 0;
        for (int i = 0; i < s_dev.reg_group_count && y < 150; i++) {
            if (!s_dev.reg_groups[i].writable) continue;
            bool sel = (n == s_cursor);
            snprintf(buf, sizeof(buf), "%s %-7s @%u",
                     sel ? ">" : " ",
                     reg_type_str[s_dev.reg_groups[i].reg_type],
                     (unsigned)s_dev.reg_groups[i].start_addr);
            tft_draw_string(10, y, buf,
                            sel ? THEME_HIGHLIGHT : THEME_FG, THEME_BG, FONT_SMALL);
            y += 12;
            n++;
            shown++;
        }
        if (shown == 0) {
            tft_draw_string(10, 90, "none (all read-only)", THEME_WARNING, THEME_BG, FONT_SMALL);
        }
    }

    /* 编辑态：把当前地址和值放大显示，避免改错寄存器 */
    if (s_edit) {
        snprintf(buf, sizeof(buf), "EDIT @%u = %u", (unsigned)addr, (unsigned)s_edit_val);
        tft_draw_string(10, 158, buf, THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    }

    if (s_msg[0]) {
        tft_draw_string(10, 180, s_msg, s_msg_err ? THEME_ERROR : THEME_SUCCESS,
                        THEME_BG, FONT_SMALL);
    }

    if (s_edit) {
        tft_draw_string(10, TFT_HEIGHT - 18,
                        "[UP/DN] +/-1   [OK] Write   [BACK] Cancel",
                        THEME_FG, THEME_BG, FONT_SMALL);
    } else {
        tft_draw_string(10, TFT_HEIGHT - 18,
                        "[UP/DN] Pick  [OK] Edit/Classify  [BACK] Return",
                        THEME_FG, THEME_BG, FONT_SMALL);
    }
}

static void dev_key(uint8_t key)
{
    int gi = group_at_cursor(s_cursor);
    uint16_t addr = (gi >= 0) ? s_dev.reg_groups[gi].start_addr : 0;

    /* ---------- 编辑态 ---------- */
    if (s_edit) {
        switch (key) {
            case 0:  /* UP +1 */
                s_edit_val++;
                s_need_redraw = true;
                break;
            case 1:  /* DOWN -1 */
                s_edit_val--;
                s_need_redraw = true;
                break;
            case 2: { /* OK 提交 */
                int type = (gi >= 0) ? s_dev.reg_groups[gi].reg_type : REG_TYPE_HOLDING;
                modbus_err_t err;
                if (type == REG_TYPE_HOLDING) {
                    err = modbus_write_single_reg(s_dev.slave_addr, addr, s_edit_val);
                } else if (type == REG_TYPE_COIL) {
                    err = modbus_write_single_coil(s_dev.slave_addr, addr, s_edit_val != 0);
                } else {
                    err = MODBUS_ERR_INVALID_PARAM;   /* 输入寄存器/离散输入本来就只读 */
                }

                if (err == MODBUS_OK) {
                    /* 读回确认：从站回「写入成功」不等于值真的生效（有些设备会钳位） */
                    uint16_t rb = 0;
                    uint16_t cnt = 0;
                    if (type == REG_TYPE_HOLDING) {
                        modbus_read_holding_regs(s_dev.slave_addr, addr, 1, &rb, &cnt);
                    } else {
                        uint8_t coil[1] = {0};
                        if (modbus_read_coils(s_dev.slave_addr, addr, 1, coil, &cnt) == MODBUS_OK) {
                            rb = coil[0] & 1;
                        }
                    }
                    snprintf(s_msg, sizeof(s_msg), "OK @%u readback=%u", (unsigned)addr, (unsigned)rb);
                    s_msg_err = false;
                } else {
                    snprintf(s_msg, sizeof(s_msg), "Write failed (err=%d)", (int)err);
                    s_msg_err = true;
                }
                s_edit = false;
                s_need_redraw = true;
                break;
            }
            case 3:  /* BACK 取消 */
                s_edit = false;
                s_msg[0] = '\0';
                s_need_redraw = true;
                break;
        }
        return;
    }

    /* ---------- 浏览态 ---------- */
    switch (key) {
        case 0: /* UP */
            if (writable_count() > 0) {
                s_cursor = (s_cursor == 0) ? writable_count() - 1 : s_cursor - 1;
                s_need_redraw = true;
            }
            break;
        case 1: /* DOWN */
            if (writable_count() > 0) {
                s_cursor = (s_cursor + 1) % writable_count();
                s_need_redraw = true;
            }
            break;
        case 2: /* OK */
            if (s_dev.reg_group_count == 0) {
                /* 还没分类：先扫寄存器 */
                s_classifying = true;
                s_need_redraw = true;
                register_classify(s_dev.slave_addr, &s_dev);
                s_classifying = false;
                s_cursor = 0;
                s_need_redraw = true;
            } else if (gi >= 0) {
                /* 进编辑前先把现值读出来，别让人从 0 开始瞎猜 */
                s_edit_val = 0;
                uint16_t cnt = 0;
                int type = s_dev.reg_groups[gi].reg_type;
                if (type == REG_TYPE_HOLDING) {
                    modbus_read_holding_regs(s_dev.slave_addr, addr, 1, &s_edit_val, &cnt);
                } else if (type == REG_TYPE_COIL) {
                    uint8_t coil[1] = {0};
                    if (modbus_read_coils(s_dev.slave_addr, addr, 1, coil, &cnt) == MODBUS_OK) {
                        s_edit_val = coil[0] & 1;
                    }
                }
                s_msg[0] = '\0';
                s_edit = true;
                s_need_redraw = true;
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
        .id = PAGE_DEVICE_INFO,
        .on_enter = dev_enter,
        .on_exit = dev_exit,
        .on_update = dev_update,
        .on_key = dev_key,
    };
    gui_register_page(&page);
}

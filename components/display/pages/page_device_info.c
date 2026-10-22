#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "register_classifier.h"
#include "device_scanner.h"
#include "log_storage.h"
#include "ui_edit.h"
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
static ui_edit_t s_editor;        /* 逐位编辑状态机（逻辑在 ui_edit.c，有主机测试） */
static uint16_t s_edit_val = 0;   /* 编辑值：提交时从状态机取出，写操作读它 */
static char s_msg[48] = "";
static bool s_msg_err = false;
static bool s_batch = false;       /* 编辑态：true 表示「批量填充」而不是单点写 */



static const char *reg_type_str[] = {"Coil", "DiscInp", "Holding", "Input"};

/* 第一个可写线圈组的下标（FC15 批量写线圈用），没有则 -1 */
static int first_coil_idx(void)
{
    for (int i = 0; i < s_dev.reg_group_count; i++) {
        if (s_dev.reg_groups[i].writable &&
            s_dev.reg_groups[i].reg_type == REG_TYPE_COIL) {
            return i;
        }
    }
    return -1;
}

/* 第一个可写保持寄存器组的下标（批量填充要用它的起始地址），没有则 -1 */
static int first_holding_idx(void)
{
    for (int i = 0; i < s_dev.reg_group_count; i++) {
        if (s_dev.reg_groups[i].writable &&
            s_dev.reg_groups[i].reg_type == REG_TYPE_HOLDING) {
            return i;
        }
    }
    return -1;
}

/* 列表尾部三个附加行的「相对 writable_count() 的偏移」。
 * 必须动态算：只有线圈没有保持寄存器时，写死偏移会让 Diag 行排到游标到不了的位置。 */
static void row_offsets(int *off_fill, int *off_coil, int *off_diag)
{
    int o = 1;
    *off_fill = (first_holding_idx() >= 0) ? o++ : 0;
    *off_coil = (first_coil_idx() >= 0) ? o++ : 0;
    *off_diag = o;
}

static int writable_count(void){
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

    /* online 字段以前只写不读（扫描命中时置 true，界面从来不显示） */
    snprintf(buf, sizeof(buf), "Resp: %ld us  %s", (long)s_dev.response_time_us,
             s_dev.online ? "ONLINE" : "OFFLINE");
    tft_draw_string(10, 38, buf, s_dev.online ? THEME_SUCCESS : THEME_WARNING,
                    THEME_BG, FONT_NORMAL);

    if (s_dev.device_id[0]) {
        snprintf(buf, sizeof(buf), "ID: %s", s_dev.device_id);
        tft_draw_string(10, 52, buf, THEME_ACCENT, THEME_BG, FONT_SMALL);
    }

    int off_fill, off_coil, off_diag;
    row_offsets(&off_fill, &off_coil, &off_diag);

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

        /* 批量填充行（FC16）：只有存在可写保持寄存器组时才出现 */
        int off_fill, off_coil, off_diag;
        row_offsets(&off_fill, &off_coil, &off_diag);

        int hi = first_holding_idx();
        if (hi >= 0 && y < 150) {
            bool sel = off_fill && (s_cursor == writable_count() + off_fill);
            snprintf(buf, sizeof(buf), "%s[Fill 4 regs @%u]",
                     sel ? ">" : " ", (unsigned)s_dev.reg_groups[hi].start_addr);
            tft_draw_string(10, y, buf,
                            sel ? THEME_HIGHLIGHT : THEME_ACCENT, THEME_BG, FONT_SMALL);
            y += 12;
        }

        /* 批量写线圈行（FC15）：一次置 4 个连续线圈，用于成组复位/启动 */
        int ci = first_coil_idx();
        if (ci >= 0 && y < 150) {
            bool sel = off_coil && (s_cursor == writable_count() + off_coil);
            snprintf(buf, sizeof(buf), "%s[Set 4 coils @%u]",
                     sel ? ">" : " ", (unsigned)s_dev.reg_groups[ci].start_addr);
            tft_draw_string(10, y, buf,
                            sel ? THEME_HIGHLIGHT : THEME_ACCENT, THEME_BG, FONT_SMALL);
            y += 12;
        }

        /* 诊断环回行（FC08 子功能 0）：验证从站协议栈，不动任何寄存器 */
        if (y < 150) {
            bool sel = (s_cursor == writable_count() + off_diag);
            snprintf(buf, sizeof(buf), "%s[Diag: loopback]", sel ? ">" : " ");
            tft_draw_string(10, y, buf,
                            sel ? THEME_HIGHLIGHT : THEME_ACCENT, THEME_BG, FONT_SMALL);
        }
    }

    /* 编辑态：5 个数字框逐位高亮（用 ui_num_input_draw 单个数字位画）。
     * 原来只能 ±1 调值，写 3000 要按三千下 —— 纯属没法用。 */
    if (s_edit) {
        if (s_batch) {
            int hi2 = first_holding_idx();
            snprintf(buf, sizeof(buf), "FILL 4 regs @%u",
                     (unsigned)(hi2 >= 0 ? s_dev.reg_groups[hi2].start_addr : 0));
        } else {
            snprintf(buf, sizeof(buf), "EDIT @%u", (unsigned)addr);
        }
        tft_draw_string(10, 140, buf, THEME_HIGHLIGHT, THEME_BG, FONT_SMALL);
        for (int i = 0; i < 5; i++) {
            int d = (s_edit_val / ui_edit_pow10(i)) % 10;
            ui_num_input_draw(10 + i * 16, 152, d, 1,
                              (i == ui_edit_cursor(&s_editor)),
                              THEME_HIGHLIGHT, THEME_BG);
        }
    } else if (s_msg[0]) {
        /* 非编辑态才显示上一次的写入结果，免得和输入框挤在一起 */
        tft_draw_string(10, 158, s_msg, s_msg_err ? THEME_ERROR : THEME_SUCCESS,
                        THEME_BG, FONT_SMALL);
    }

    if (s_edit) {
        tft_draw_string(10, TFT_HEIGHT - 18,
                        "[UP/DN] Digit  [OK] Next  [OK hold] Write  [BACK] Cancel",
                        THEME_FG, THEME_BG, FONT_SMALL);
    } else {
        tft_draw_string(10, TFT_HEIGHT - 18,
                        "[UP/DN] Pick  [OK] Edit/Classify  [OK hold] Rescan",
                        THEME_FG, THEME_BG, FONT_SMALL);
    }
}

static void dev_classify(void)
{
    s_classifying = true;
    s_need_redraw = true;
    register_classify(s_dev.slave_addr, &s_dev);
    s_classifying = false;
    s_cursor = 0;
    s_need_redraw = true;
    log_storage_logf("classify: addr=%d -> %d register group(s)",
                     s_dev.slave_addr, s_dev.reg_group_count);
}

static void dev_commit_write(int gi, uint16_t addr)
{
    int type = (gi >= 0) ? s_dev.reg_groups[gi].reg_type : REG_TYPE_HOLDING;

    /* ---------- 批量填充：FC16 一次写 4 个连续保持寄存器 ---------- */
    if (s_batch) {
        int hi = first_holding_idx();
        uint16_t base = (hi >= 0) ? s_dev.reg_groups[hi].start_addr : addr;
        uint16_t vals[4] = {s_edit_val, s_edit_val, s_edit_val, s_edit_val};

        modbus_err_t berr = modbus_write_multiple_regs(s_dev.slave_addr, base, vals, 4);
        if (berr == MODBUS_OK) {
            uint16_t rb[4] = {0};
            uint16_t cnt = 0;
            if (modbus_read_holding_regs(s_dev.slave_addr, base, 4, rb, &cnt) == MODBUS_OK
                && cnt == 4) {
                snprintf(s_msg, sizeof(s_msg), "OK 4regs@%u = %u,%u,%u,%u",
                         (unsigned)base, (unsigned)rb[0], (unsigned)rb[1],
                         (unsigned)rb[2], (unsigned)rb[3]);
            } else {
                snprintf(s_msg, sizeof(s_msg), "OK 4regs@%u (readback failed)", (unsigned)base);
            }
            s_msg_err = false;
            log_storage_logf("FC16: addr=%d @%u x4 <- %u, readback %u/%u/%u/%u",
                             s_dev.slave_addr, (unsigned)base, (unsigned)s_edit_val,
                             (unsigned)rb[0], (unsigned)rb[1],
                             (unsigned)rb[2], (unsigned)rb[3]);
        } else {
            snprintf(s_msg, sizeof(s_msg), "FC16 failed: %s", modbus_err_name(berr));
            s_msg_err = true;
            log_storage_logf("FC16 FAILED: addr=%d @%u x4 <- %u err=%d",
                             s_dev.slave_addr, (unsigned)base,
                             (unsigned)s_edit_val, (int)berr);
        }
        s_batch = false;
        s_edit = false;
        s_need_redraw = true;
        return;
    }

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
        log_storage_logf("write: addr=%d @%u -> %u (%s), readback=%u",
                         s_dev.slave_addr, (unsigned)addr, (unsigned)s_edit_val,
                         type == REG_TYPE_COIL ? "coil" : "hr", (unsigned)rb);
    } else {
        snprintf(s_msg, sizeof(s_msg), "Write failed: %s", modbus_err_name(err));
        s_msg_err = true;
        log_storage_logf("write FAILED: addr=%d @%u val=%u err=%d",
                         s_dev.slave_addr, (unsigned)addr,
                         (unsigned)s_edit_val, (int)err);
    }
    s_edit = false;
    s_need_redraw = true;
}

static void dev_key(uint8_t key)
{
    int gi = group_at_cursor(s_cursor);
    uint16_t addr = (gi >= 0) ? s_dev.reg_groups[gi].start_addr : 0;

    /* ---------- 编辑态：逐位改数字（逻辑在 ui_edit.c，主机测试覆盖） ---------- */
    if (s_edit) {
        uint16_t committed = 0;
        bool done = ui_edit_key(&s_editor, key, &committed);
        if (done) {
            if (key == 3) {              /* BACK：取消，不写 */
                s_edit = false;
                s_batch = false;
                s_msg[0] = '\0';
            } else {                     /* OK / 长按 OK：提交 */
                s_edit_val = committed;
                dev_commit_write(gi, addr);
            }
        }
        s_need_redraw = true;
        return;
    }

    /* ---------- 浏览态 ---------- */
    /* 可选行数 = 可写寄存器组 + 「批量填充」行（仅当存在保持寄存器组时） */
    int off_fill, off_coil, off_diag;
    row_offsets(&off_fill, &off_coil, &off_diag);
    int rows = writable_count() + off_diag + 1;   /* +1 = Diag 本身 */

    switch (key) {
        case 0: /* UP */
            if (rows > 0) {
                s_cursor = (s_cursor == 0) ? rows - 1 : s_cursor - 1;
                s_need_redraw = true;
            }
            break;
        case 1: /* DOWN */
            if (rows > 0) {
                s_cursor = (s_cursor + 1) % rows;
                s_need_redraw = true;
            }
            break;
        case 2: /* OK */
            if (s_dev.reg_group_count == 0) {
                /* 还没分类：先扫寄存器 */
                dev_classify();
            } else if (off_coil && s_cursor == writable_count() + off_coil) {
                /* FC15 批量写 4 个线圈：置成编辑值（0=全断，非0=全通）。
                 * 和 FC16 的 Fill 4 regs 对应，覆盖论文里列的 0x0F。 */
                int ci = first_coil_idx();
                if (ci >= 0) {
                    uint16_t base = s_dev.reg_groups[ci].start_addr;
                    uint8_t bits[1] = { s_edit_val ? 0x0F : 0x00 };
                    modbus_err_t cerr = modbus_write_multiple_coils(
                        s_dev.slave_addr, base, bits, 4);
                    if (cerr == MODBUS_OK) {
                        uint8_t rb[1] = {0};
                        uint16_t cnt = 0;
                        if (modbus_read_coils(s_dev.slave_addr, base, 1, rb, &cnt) == MODBUS_OK) {
                            snprintf(s_msg, sizeof(s_msg), "OK 4coils@%u = 0x%02X",
                                     (unsigned)base, rb[0] & 0x0F);
                        } else {
                            snprintf(s_msg, sizeof(s_msg), "OK 4coils@%u (readback failed)",
                                     (unsigned)base);
                        }
                        s_msg_err = false;
                        log_storage_logf("FC15: addr=%d @%u x4 <- %s, readback=0x%02X",
                                         s_dev.slave_addr, (unsigned)base,
                                         s_edit_val ? "ON" : "OFF", rb[0] & 0x0F);
                    } else {
                        snprintf(s_msg, sizeof(s_msg), "FC15 failed: %s",
                                 modbus_err_name(cerr));
                        s_msg_err = true;
                        log_storage_logf("FC15 FAILED: addr=%d @%u x4 err=%s",
                                         s_dev.slave_addr, (unsigned)base,
                                         modbus_err_name(cerr));
                    }
                    s_need_redraw = true;
                }
            } else if (s_cursor == writable_count() + off_diag) {
                /* FC08 子功能 0x0000（Return Query Data）：从站原样回显数据。
                 * 用来验证从站协议栈是否正常，且不写任何寄存器 —— 现场很安全。 */
                uint16_t echoed = 0;
                modbus_err_t derr = modbus_diagnostics(s_dev.slave_addr, 0x0000,
                                                       0xA55A, &echoed);
                if (derr == MODBUS_OK && echoed == 0xA55A) {
                    snprintf(s_msg, sizeof(s_msg), "Diag OK, echo=0x%04X", echoed);
                    s_msg_err = false;
                } else if (derr == MODBUS_OK) {
                    snprintf(s_msg, sizeof(s_msg), "Diag reply bad: 0x%04X", echoed);
                    s_msg_err = true;
                } else {
                    snprintf(s_msg, sizeof(s_msg), "Diag failed: %s", modbus_err_name(derr));
                    s_msg_err = true;
                }
                log_storage_logf("FC08 loopback @%d -> %s",
                                 s_dev.slave_addr, s_msg);
                s_need_redraw = true;
            } else if (off_fill && s_cursor == writable_count() + off_fill) {
                /* 落在「批量填充」行：FC16 一次写 4 个连续寄存器 */
                s_batch = true;
                s_edit_val = 0;
                s_msg[0] = '\0';
                ui_edit_begin(&s_editor, s_edit_val);
                s_edit = true;
                s_need_redraw = true;
            } else if (gi >= 0) {
                /* 进编辑前先把现值读出来，别让人从 0 开始瞎猜 */
                s_batch = false;
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
                ui_edit_begin(&s_editor, 0);
                s_edit = true;
                s_need_redraw = true;
            }
            break;
        case 4: /* 长按 OK：重新分类（寄存器表可能不准，扫一遍刷新） */
            if (s_have_dev) dev_classify();
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

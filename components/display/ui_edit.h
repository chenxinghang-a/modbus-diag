#pragma once
/* 数字编辑状态机（纯逻辑，不依赖任何 ESP-IDF 能力）。
 *
 * 为什么单独抽出来：设备页的「逐位写寄存器」和波形页的「触发阈值」原来是两份
 * 各自手写的 if-else，逻辑相同却行为略有差异（一处能长按提交、一处不能），
 * 而且**完全没法在主机上测试** —— UI 层因此是项目里唯一没有自动化覆盖的部分。
 *
 * 现在所有编辑逻辑收敛到这一个文件，主机测试直接驱动它（见 host_tests/test_ui_edit.c）。
 */

#include <stdint.h>
#include <stdbool.h>

#define UI_EDIT_MAX_DIGITS 5      /* 0~99999，覆盖 16 位寄存器与阈值 */

typedef struct {
    uint16_t value;               /* 当前编辑值 */
    int      digit;               /* 正在改哪一位，0 = 最高位 */
    bool     active;              /* 是否处于编辑态 */
} ui_edit_t;

/* 进入编辑态，载入初值（如写前先读回的现值）。 */
void ui_edit_begin(ui_edit_t *e, uint16_t initial);

/* 退出编辑态（提交或取消都由调用方决定）。 */
void ui_edit_end(ui_edit_t *e);

/* 送入一个按键。
 *  key: 0=UP 1=DOWN 2=OK 3=BACK 4=长按OK
 *  out_commit: 非 NULL 时，若本键导致提交，把最终值写出去
 * 返回 true 表示「编辑态结束」，调用方应据此收尾（提交或取消）。 */
bool ui_edit_key(ui_edit_t *e, uint8_t key, uint16_t *out_commit);

/* 取当前位上的数字（画界面用）。 */
int ui_edit_digit_value(const ui_edit_t *e);

/* 供界面层使用的小工具（画数字框时要知道每位权值和光标位置）。 */
uint16_t ui_edit_pow10(int digit);
int ui_edit_cursor(const ui_edit_t *e);

#include "ui_edit.h"

static const uint16_t k_pow10[UI_EDIT_MAX_DIGITS] = {
    10000, 1000, 100, 10, 1
};

void ui_edit_begin(ui_edit_t *e, uint16_t initial)
{
    if (!e) return;
    e->value = initial;
    /* 从**最高非零位**开始，而不是固定停在万位。
     * 原实现固定 digit=0：写 1234 时光标落在前导零上，用户得先按一次 OK
     * 才能碰到想改的那位 —— 4 键机身上这是纯粹的浪费。 */
    e->digit = 0;
    while (e->digit < UI_EDIT_MAX_DIGITS - 1 &&
           (e->value / k_pow10[e->digit]) == 0) {
        e->digit++;
    }
    e->active = true;
}

void ui_edit_end(ui_edit_t *e)
{
    if (!e) return;
    e->active = false;
    e->digit = 0;
}

int ui_edit_digit_value(const ui_edit_t *e)
{
    if (!e || !e->active) return 0;
    if (e->digit < 0 || e->digit >= UI_EDIT_MAX_DIGITS) return 0;
    return (int)((e->value / k_pow10[e->digit]) % 10);
}

bool ui_edit_key(ui_edit_t *e, uint8_t key, uint16_t *out_commit)
{
    if (!e || !e->active) return false;

    int cur = (int)((e->value / k_pow10[e->digit]) % 10);
    int next = cur;

    switch (key) {
        case 0:                     /* UP：当前位 +1，9 绕回 0 */
            next = (cur + 1) % 10;
            break;

        case 1:                     /* DOWN：当前位 -1，0 绕到 9 */
            next = (cur + 9) % 10;
            break;

        case 2:                     /* OK：下一位；已在最低位则提交 */
            if (e->digit < UI_EDIT_MAX_DIGITS - 1) {
                e->digit++;
                return false;
            }
            if (out_commit) *out_commit = e->value;
            ui_edit_end(e);
            return true;

        case 4:                     /* 长按 OK：不管在第几位，直接提交 */
            if (out_commit) *out_commit = e->value;
            ui_edit_end(e);
            return true;

        case 3:                     /* BACK：取消 */
            ui_edit_end(e);
            return true;

        default:
            return false;
    }

    /* 只改当前位：减掉该位原来的贡献，再加上新值。
     * 曾错写成 `(value/place)*place + next*place` —— 那会把低位一起抹掉
     * （12345 改万位会变成 20000，低位 2345 消失）。 */
    uint16_t place = k_pow10[e->digit];
    uint32_t v = (uint32_t)e->value - (uint32_t)cur * place + (uint32_t)next * place;
    if (v > 65535u) v = 65535u;      /* 16 位上限：钳制，不要静默截断成另一个数 */
    e->value = (uint16_t)v;
    return false;
}

uint16_t ui_edit_pow10(int digit)
{
    if (digit < 0 || digit >= UI_EDIT_MAX_DIGITS) return 1;
    return k_pow10[digit];
}

int ui_edit_cursor(const ui_edit_t *e)
{
    return (e && e->active) ? e->digit : -1;
}

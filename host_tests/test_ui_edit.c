/* UI 编辑状态机测试 —— 补上「UI 层零测试」这个盲区。
 *
 * 逐位编辑是设备页/波形页共用的交互，原来两份手写实现且没有覆盖。
 * 现在逻辑在 components/display/ui_edit.c（纯 C，无 ESP 依赖），可以直接在主机上驱动。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "ui_edit.h"

static int ok = 0, bad = 0;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) { ok++; printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
    else      { bad++; printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
}

int main(void)
{
    printf("-- 初值与进位 --\n");
    {
        ui_edit_t e;
        ui_edit_begin(&e, 1234);
        check("初值被载入", e.value == 1234 && e.active, NULL);
        check("从最高非零位开始（1234 → 千位）", e.digit == 1, NULL);
        check("最高位数字 = 1", ui_edit_digit_value(&e) == 1, NULL);
    }

    printf("-- 逐位修改不影响其它位 --\n");
    {
        ui_edit_t e;
        ui_edit_begin(&e, 1234);
        /* 当前在最高位（千位=1），按 UP 应变 2，低位 234 不变 */
        uint16_t out = 0;
        ui_edit_key(&e, 0, &out);
        check("千位 1 +1 = 2，低位不变", e.value == 2234, NULL);
        check("UP 不会结束编辑态", e.active, NULL);

        ui_edit_key(&e, 2, &out);          /* OK → 移到百位 */
        check("OK 移到下一位（千位→百位）", e.digit == 2, NULL);
        check("百位数字 = 2", ui_edit_digit_value(&e) == 2, NULL);
        ui_edit_key(&e, 1, &out);          /* DOWN：百位 2 -> 1 */
        check("百位 2 -1 = 1", e.value == 2134, NULL);
    }

    printf("-- 9 绕回 0 / 0 绕到 9 --\n");
    {
        ui_edit_t e;
        ui_edit_begin(&e, 9999);
        uint16_t out = 0;
        ui_edit_key(&e, 0, &out);
        check("9 +1 绕回 0", ui_edit_digit_value(&e) == 0 && e.value == 999, NULL);

        /* 0 -1 绕到 9：用 10，光标在个位（值为 0），DOWN 应变 9 → 19 */
        ui_edit_t e2;
        ui_edit_begin(&e2, 10);
        ui_edit_key(&e2, 2, &out);          /* OK → 个位 */
        ui_edit_key(&e2, 1, &out);          /* DOWN：0 -> 9 */
        check("0 -1 绕到 9", ui_edit_digit_value(&e2) == 9 && e2.value == 19, NULL);
    }

    printf("-- 提交与取消 --\n");
    {
        ui_edit_t e;
        ui_edit_begin(&e, 12345);      /* 5 位数，光标从万位开始 */
        uint16_t out = 0xFFFF;
        /* 连按 4 次 OK 走到最低位，第 5 次提交 */
        for (int i = 0; i < 4; i++) {
            bool done = ui_edit_key(&e, 2, &out);
            check("未到最低位不提交", !done, NULL);
        }
        bool done = ui_edit_key(&e, 2, &out);
        check("最低位再按 OK 提交", done && out == 12345, NULL);
        check("提交后退出编辑态", !e.active, NULL);
    }
    {
        ui_edit_t e;
        ui_edit_begin(&e, 7);
        uint16_t out = 0xFFFF;
        bool done = ui_edit_key(&e, 4, &out);      /* 长按 OK：直接提交 */
        check("长按 OK 立即提交", done && out == 7, NULL);
    }
    {
        ui_edit_t e;
        ui_edit_begin(&e, 7);
        uint16_t out = 0xFFFF;
        bool done = ui_edit_key(&e, 3, &out);      /* BACK：取消 */
        check("BACK 结束编辑态", done, NULL);
        check("BACK 不产出提交值", out == 0xFFFF, NULL);
    }

    printf("-- 边界与健壮性 --\n");
    {
        ui_edit_t e;
        ui_edit_begin(&e, 12345);
        uint16_t out = 0;
        ui_edit_key(&e, 0, &out);          /* 万位 1 -> 2，其余不动 */
        check("12345 的万位 +1 = 22345", e.value == 22345, NULL);

        ui_edit_t e3;
        ui_edit_begin(&e3, 60000);
        ui_edit_key(&e3, 0, &out);         /* 6 -> 7 得 70000，超 16 位上限 */
        check("超出 65535 时被钳制而不是静默截断", e3.value == 65535, NULL);

        ui_edit_t e0;
        ui_edit_begin(&e0, 0);
        for (int i = 0; i < 9; i++) ui_edit_key(&e0, 0, &out);   /* 一直按也不越界 */
        check("从 0 连按 9 次 UP 得 9（个位）", e0.value == 9, NULL);

        check("NULL 状态不会崩", !ui_edit_key(NULL, 0, &out), NULL);
        check("非激活状态忽略按键", !ui_edit_key(&e, 0, &out), NULL);

        ui_edit_end(&e);
        check("end 后 digit 归零", e.digit == 0 && !e.active, NULL);
    }

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}

/* 波形信号处理的主机侧测试（纯 C，不需要 ESP-IDF/板子）
 *
 * 跑法： bash host_tests/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "signal_process.h"

static int ok = 0, bad = 0;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) { ok++; printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
    else      { bad++; printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
}

int main(void)
{
    /* ---------- 滑动平均 ---------- */
    printf("-- SMA 滤波 --\n");
    {
        const uint16_t in[6] = {0, 0, 3, 3, 3, 0};
        uint16_t out[6];

        signal_filter_sma(in, out, 6, 1);
        check("window=1 等于原样输出", memcmp(in, out, sizeof(in)) == 0, NULL);

        signal_filter_sma(in, out, 6, 3);
        /* 逐个手算（窗口 3，边界自动缩短）：
         * i0: (0+0)/2=0   i1: (0+0+3)/3=1   i2: (0+3+3)/3=2
         * i3: (3+3+3)/3=3 i4: (3+3+0)/3=2   i5: (3+0)/2=1 */
        const uint16_t want[6] = {0, 1, 2, 3, 2, 1};
        check("window=3 结果与手算一致", memcmp(want, out, sizeof(want)) == 0, NULL);
        char d[64];
        snprintf(d, sizeof(d), "out=%u,%u,%u,%u,%u,%u",
                 out[0], out[1], out[2], out[3], out[4], out[5]);
        printf("         %s\n", d);
    }
    {
        const uint16_t in[2] = {100, 200};
        uint16_t out[2];
        signal_filter_sma(in, out, 2, 0);      /* window<1 应被夹到 1 */
        check("window=0 被夹成 1，不崩", out[0] == 100 && out[1] == 200, NULL);
        signal_filter_sma(in, out, 0, 3);      /* 长度 0 直接返回 */
        check("长度 0 不崩也不写", true, NULL);
    }

    /* ---------- 峰峰值 / 直流分量 ---------- */
    printf("-- 幅值测量 --\n");
    {
        const uint16_t square[8] = {100, 100, 900, 900, 100, 100, 900, 900};
        check("方波 Vpp = 800", signal_measure_vpp(square, 8) == 800, NULL);
        check("方波 DC = 500", signal_measure_dc(square, 8) == 500, NULL);
    }
    {
        const uint16_t flat[4] = {2048, 2048, 2048, 2048};
        check("常量信号 Vpp = 0", signal_measure_vpp(flat, 4) == 0, NULL);
        check("常量信号 DC = 2048", signal_measure_dc(flat, 4) == 2048, NULL);
    }
    {
        /* 原来的坑：len=0 时 vpp 返回 4095-0=4095（凭空一个满幅值），
         * dc 直接 sum/len 除零 —— ESP32 上整数除零是 panic，不是返回 0 */
        check("len=0 时 Vpp = 0（不是 4095）", signal_measure_vpp(NULL, 0) == 0, NULL);
        check("len=0 时 DC = 0（不除零）", signal_measure_dc(NULL, 0) == 0, NULL);
    }

    /* ---------- 触发点 ---------- */
    printf("-- 触发检测 --\n");
    {
        const uint16_t rise[8] = {100, 200, 900, 1000, 900, 200, 100, 50};
        check("上升沿过阈值 500 → 返回交叉索引 2",
              signal_find_trigger(rise, 8, 500) == 2, NULL);
    }
    {
        /* 全程都在阈值以下，只有下降不构成触发 */
        const uint16_t fall[8] = {1000, 900, 400, 100, 100, 400, 300, 200};
        check("只有下降沿/始终低于阈值 → -1", signal_find_trigger(fall, 8, 500) == -1, NULL);
    }
    {
        const uint16_t none[8] = {100, 120, 140, 160, 180, 200, 220, 240};
        check("从未过阈值 → -1", signal_find_trigger(none, 8, 2048) == -1, NULL);
    }
    {
        /* 恰好等于阈值也算触发（>=），且第 0 点不参与判定（从 i=1 起比较） */
        const uint16_t edge[4] = {500, 500, 100, 200};
        check("首点等于阈值不算触发（无前一点可比）",
              signal_find_trigger(edge, 4, 500) == -1, NULL);
        const uint16_t edge2[4] = {499, 500, 100, 200};
        check("恰好越过阈值（>=）算触发", signal_find_trigger(edge2, 4, 500) == 1, NULL);
    }

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}

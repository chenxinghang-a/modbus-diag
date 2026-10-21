/* 通信健康评分模型的主机侧测试（论文创新点之一，之前一直没测过）
 *
 * 测的是「算得对不对」，不测并发（FreeRTOS 互斥量在桩里是空操作）。
 *
 * 跑法： bash host_tests/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "health_metrics.h"

static int ok = 0, bad = 0;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) { ok++; printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
    else      { bad++; printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
}

static void check_int(const char *label, int got, int want)
{
    char d[64];
    snprintf(d, sizeof(d), "got %d, want %d", got, want);
    check(label, got == want, d);
}

int main(void)
{
    /* ---------- 权重与分段线性 ---------- */
    printf("-- 评分模型 --\n");
    {
        /* 三项全满：平均 2ms、错误率 0、抖动 0 → 100 分 */
        health_report_t r; memset(&r, 0, sizeof(r));
        r.avg_response_us = 2000;
        check_int("三项满分 = 100", health_metrics_calculate_score(&r), 100);

        /* 平均 50ms：rt = 100 - (5000/950)*... → 100-(50000-5000)/950 = 52.63
         * 加权 0.3*52.63 + 0.4*100 + 0.3*100 = 85.79 → 85 */
        health_report_t s; memset(&s, 0, sizeof(s));
        s.avg_response_us = 50000;
        check_int("平均 50ms（响应项约 52.6）= 85", health_metrics_calculate_score(&s), 85);

        /* 错误率 5% → err 项 50 分；0.3*100 + 0.4*50 + 0.3*100 = 80 */
        health_report_t e; memset(&e, 0, sizeof(e));
        e.avg_response_us = 2000; e.error_rate = 0.05f;
        check_int("错误率 5%（错误项 50 分）= 80", health_metrics_calculate_score(&e), 80);

        /* 抖动 20ms → stab = 100 - (20000-1000)/490 = 61.2；0.3*100+0.4*100+0.3*61.2 = 88.4 */
        health_report_t j; memset(&j, 0, sizeof(j));
        j.avg_response_us = 2000; j.jitter_us = 20000;
        check_int("抖动 20ms（稳定项约 61.2）= 88", health_metrics_calculate_score(&j), 88);
    }
    {
        /* 三项全崩：各 0 分 */
        health_report_t r; memset(&r, 0, sizeof(r));
        r.avg_response_us = 200000; r.error_rate = 0.5f; r.jitter_us = 100000;
        check_int("三项归零 = 0", health_metrics_calculate_score(&r), 0);

        /* 边界：错误率恰好 10% → 错误项 0；响应 5ms、抖动 1ms 都是满分 → 60 */
        health_report_t b; memset(&b, 0, sizeof(b));
        b.avg_response_us = 5000; b.error_rate = 0.10f; b.jitter_us = 1000;
        check_int("错误率 10%（边界）= 60", health_metrics_calculate_score(&b), 60);
    }

    /* ---------- 等级划分 ---------- */
    printf("-- 等级划分 --\n");
    check("90 → EXCELLENT", strcmp(health_metrics_grade(90), "EXCELLENT") == 0, NULL);
    check("89 → GOOD",      strcmp(health_metrics_grade(89), "GOOD") == 0, NULL);
    check("75 → GOOD",      strcmp(health_metrics_grade(75), "GOOD") == 0, NULL);
    check("74 → FAIR",      strcmp(health_metrics_grade(74), "FAIR") == 0, NULL);
    check("60 → FAIR",      strcmp(health_metrics_grade(60), "FAIR") == 0, NULL);
    check("59 → POOR",      strcmp(health_metrics_grade(59), "POOR") == 0, NULL);
    check("40 → POOR",      strcmp(health_metrics_grade(40), "POOR") == 0, NULL);
    check("39 → CRITICAL",  strcmp(health_metrics_grade(39), "CRITICAL") == 0, NULL);

    /* ---------- 采集与统计 ---------- */
    printf("-- 采集统计 --\n");
    {
        health_metrics_init();
        health_metrics_update(1000, false, false, 0);
        health_metrics_update(2000, false, false, 0);
        health_metrics_update(3000, false, false, 0);

        health_report_t r; health_metrics_get_report(&r);
        check_int("总帧数 = 3", (int)r.total_frames, 3);
        check_int("最小响应 = 1000", (int)r.min_response_us, 1000);
        check_int("最大响应 = 3000", (int)r.max_response_us, 3000);
        check_int("平均响应 = 2000", (int)r.avg_response_us, 2000);
        /* 抖动 = sqrt(E[X²]-E[X]²) = sqrt(4.667e6-4e6) ≈ 816 */
        char d[64];
        snprintf(d, sizeof(d), "jitter=%d us", (int)r.jitter_us);
        check("抖动 ≈ 816us", r.jitter_us >= 800 && r.jitter_us <= 830, d);
        check_int("错误率 0", (int)(r.error_rate * 100), 0);
        check("健康分已算出（>0）", r.health_score > 0, NULL);
    }
    {
        health_metrics_init();
        health_metrics_update(2000, true, false, 0);              /* 1 次错误 */
        health_metrics_update(2000, false, true, 11);             /* 1 次异常，码 11 */
        health_metrics_update(0, true, false, 0);                 /* 失败帧：无响应时间 */

        health_report_t r; health_metrics_get_report(&r);
        check_int("总帧数 = 3", (int)r.total_frames, 3);
        check_int("错误帧 = 2", (int)r.error_frames, 2);
        check_int("异常帧 = 1", (int)r.exception_frames, 1);
        check_int("最近异常码 = 11", r.last_exception_code, 11);
        check("错误率 ≈ 66.7%", fabsf(r.error_rate - 2.0f / 3.0f) < 0.01f, NULL);
        check_int("失败帧不参与响应时间统计（样本数 2 → min 仍是 2000）",
                  (int)r.min_response_us, 2000);
    }
    {
        /* 从未收到数据：min 保持哨兵值，报告层要据此显示 No Data */
        health_metrics_init();
        health_report_t r; health_metrics_get_report(&r);
        check_int("无样本时 min 为 INT32_MAX", (int)r.min_response_us, INT32_MAX);
        check_int("无样本时总帧数为 0", (int)r.total_frames, 0);
    }

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}

/* 流量统计的主机侧测试：论文 6.5 的错误率、FPS、响应时间统计都来自这里。
 *
 * 跑法： bash host_tests/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "traffic_stats.h"

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
    printf("-- 初值与响应时间 --\n");
    {
        traffic_stats_init();
        traffic_stats_t s; traffic_stats_get(&s);
        check_int("初始总帧数 0", (int)s.total_frames, 0);
        check_int("初始 min 为 INT32_MAX（无样本哨兵）", (int)s.min_response_us, INT32_MAX);
        check("初始错误率 0", s.error_rate == 0.0f, NULL);
    }
    {
        traffic_stats_init();
        traffic_stats_add_frame(true, false, 1000, 8);
        traffic_stats_add_frame(true, false, 2000, 8);
        traffic_stats_add_frame(true, false, 3000, 8);

        traffic_stats_t s; traffic_stats_get(&s);
        check_int("总帧数 3", (int)s.total_frames, 3);
        check_int("最小响应 1000", (int)s.min_response_us, 1000);
        check_int("最大响应 3000", (int)s.max_response_us, 3000);
        /* 指数滑动平均 alpha=1/8：1000 → (1000*7+2000)/8=1125 → (1125*7+3000)/8=1359 */
        check_int("滑动平均 1000→1125→1359", (int)s.avg_response_us, 1359);
        check("全正常时错误率 0", s.error_rate == 0.0f, NULL);
    }
    {
        /* 被动抓包量不出往返时间（response_us 传 0 或负数），不应污染统计 */
        traffic_stats_init();
        traffic_stats_add_frame(true, false, -1, 8);
        traffic_stats_add_frame(true, false, 0, 8);
        traffic_stats_t s; traffic_stats_get(&s);
        check_int("总帧数仍计入 2", (int)s.total_frames, 2);
        check_int("无时序样本时 min 保持哨兵值", (int)s.min_response_us, INT32_MAX);
        check_int("无时序样本时 avg 为 0", (int)s.avg_response_us, 0);
    }

    printf("-- 错误与异常 --\n");
    {
        traffic_stats_init();
        traffic_stats_add_frame(true,  false, 1000, 8);
        traffic_stats_add_frame(false, false, 1000, 8);   /* CRC 错 */
        traffic_stats_add_frame(true,  true,  1000, 8);   /* 异常响应 */
        traffic_stats_add_frame(false, true,  1000, 8);   /* 既是 CRC 错又是异常帧 */

        traffic_stats_t s; traffic_stats_get(&s);
        check_int("错误帧 2", (int)s.error_frames, 2);
        check_int("异常帧 2", (int)s.exception_frames, 2);
        check("错误率 50%", fabsf(s.error_rate - 0.5f) < 0.001f, NULL);
    }

    printf("-- 每秒速率 --\n");
    {
        traffic_stats_init();
        traffic_stats_add_frame(true, false, 1000, 8);
        traffic_stats_add_frame(true, false, 1000, 12);

        traffic_stats_t s; traffic_stats_get(&s);
        check_int("tick 前 FPS 为 0（尚未结算）", (int)s.frames_per_sec, 0);

        traffic_stats_tick();
        traffic_stats_get(&s);
        check_int("tick 后 FPS = 2", (int)s.frames_per_sec, 2);
        check_int("tick 后 BPS = 20 字节", (int)s.bytes_per_sec, 20);

        /* 下一秒没有新帧：速率要归零，而不是停在上一次的值 */
        traffic_stats_tick();
        traffic_stats_get(&s);
        check_int("无新帧时 FPS 归零", (int)s.frames_per_sec, 0);
        check_int("无新帧时 BPS 归零", (int)s.bytes_per_sec, 0);
        check_int("总帧数不因 tick 清零", (int)s.total_frames, 2);
    }

    printf("-- 复位 --\n");
    {
        traffic_stats_init();
        traffic_stats_add_frame(false, true, 5000, 8);
        traffic_stats_tick();
        traffic_stats_reset();

        traffic_stats_t s; traffic_stats_get(&s);
        check_int("复位后总帧数 0", (int)s.total_frames, 0);
        check_int("复位后 FPS 0", (int)s.frames_per_sec, 0);
        check_int("复位后 min 回到哨兵值", (int)s.min_response_us, INT32_MAX);
    }

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}

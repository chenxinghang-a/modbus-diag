/* 诊断报告生成器的主机侧测试（JSON + HTML），不需要 ESP-IDF/板子。
 *
 * 为什么值得单独测：报告是「给答辩老师看」的最终产物，占位符错位/字段漏填
 * 都不会让固件编译失败，只会让导出的文件内容胡说八道。
 *
 * 跑法： bash host_tests/run.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "health_metrics.h"
#include "report_generator.h"

static int ok = 0, bad = 0;

static void check(const char *label, bool cond, const char *detail)
{
    if (cond) { ok++; printf("  [ OK ] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
    else      { bad++; printf("  [FAIL] %s%s%s\n", label, detail ? "  " : "", detail ? detail : ""); }
}

/* 桩掉 IDF 侧的评分等级（真实现依赖 FreeRTOS 互斥量，主机上跑不了） */
const char *health_metrics_grade(int score)
{
    if (score >= 90) return "EXCELLENT";
    if (score >= 75) return "GOOD";
    if (score >= 60) return "FAIR";
    if (score >= 40) return "POOR";
    return "CRITICAL";
}

static char buf[4096];

static void scenario_normal(void)
{
    printf("-- 正常：有数据、无异常 --\n");
    health_report_t h;
    memset(&h, 0, sizeof(h));
    h.total_frames = 1000; h.error_frames = 4;
    h.min_response_us = 1800; h.max_response_us = 4500;
    h.avg_response_us = 2100; h.jitter_us = 320;
    h.error_rate = 0.004f; h.health_score = 96;

    int n = report_generate_json(buf, sizeof(buf), &h, "ESP32-ModbusDiag");
    check("JSON 生成成功", n > 0 && (size_t)n < sizeof(buf), NULL);
    check("JSON 以 { 开头 } 结尾", buf[0] == '{' && buf[n - 1] == '}', NULL);
    check("含健康分 96", strstr(buf, "\"health_score\":96") != NULL, NULL);
    check("含等级 EXCELLENT", strstr(buf, "EXCELLENT") != NULL, NULL);
    check("无异常时 code=0", strstr(buf, "\"code\":0") != NULL, NULL);
    check("无异常时 name/advice 为空串",
          strstr(buf, "\"name\":\"\"") != NULL && strstr(buf, "\"advice\":\"\"") != NULL, NULL);

    n = report_generate_html(buf, sizeof(buf), &h, "ESP32-ModbusDiag");
    check("HTML 生成成功", n > 0 && (size_t)n < sizeof(buf), NULL);
    check("HTML 含四张卡片（Device/Response/Traffic/Diagnosis）",
          strstr(buf, "Health:") && strstr(buf, "Response Time") &&
          strstr(buf, "Traffic") && strstr(buf, "Diagnosis"), NULL);
    check("HTML 显示 96/100", strstr(buf, "96/100") != NULL, NULL);
    check("无异常时给出「未发现异常」", strstr(buf, "未发现异常") != NULL, NULL);
    check("HTML 不含 2147483647", strstr(buf, "2147483647") == NULL, NULL);
}

static void scenario_degraded(void)
{
    printf("-- 降级：异常码 11 + 错误率高 + 响应慢 + 抖动大 --\n");
    health_report_t h;
    memset(&h, 0, sizeof(h));
    h.total_frames = 500; h.error_frames = 80; h.exception_frames = 60;
    h.last_exception_code = 11;
    h.min_response_us = 5000; h.max_response_us = 90000;
    h.avg_response_us = 60000; h.jitter_us = 20000;
    h.error_rate = 0.16f; h.health_score = 42;

    report_generate_json(buf, sizeof(buf), &h, "Dev");
    check("JSON 带异常码 11", strstr(buf, "\"code\":11") != NULL, NULL);
    check("JSON 带处置建议 GW Target", strstr(buf, "GW Target") != NULL, NULL);
    check("JSON 建议内容为 Check target device",
          strstr(buf, "Check target device") != NULL, NULL);
    check("JSON 含 error_rate", strstr(buf, "error_rate") != NULL, NULL);

    report_generate_html(buf, sizeof(buf), &h, "Dev");
    check("HTML 标红（POOR 走 #ff0000）", strstr(buf, "#ff0000") != NULL, NULL);
    check("HTML 给出异常处置", strstr(buf, "Check target device") != NULL, NULL);
    check("HTML 给出错误率建议", strstr(buf, "超过 5% 阈值") != NULL, NULL);
    check("HTML 给出响应偏慢建议", strstr(buf, "偏慢") != NULL, NULL);
    check("HTML 给出抖动建议", strstr(buf, "抖动") != NULL, NULL);
}

static void scenario_no_data(void)
{
    printf("-- 无数据：min_response_us = INT32_MAX --\n");
    health_report_t h;
    memset(&h, 0, sizeof(h));
    h.min_response_us = INT32_MAX;      /* 初值，没样本时就是这个 */
    h.health_score = 100;               /* 0 帧算出来的假 100 分 */

    report_generate_json(buf, sizeof(buf), &h, "Dev");
    check("JSON 把 min_us 落成 -1（不是 2147483647）",
          strstr(buf, "\"min_us\":-1") != NULL, NULL);
    check("JSON 里没有 2147483647", strstr(buf, "2147483647") == NULL, NULL);

    report_generate_html(buf, sizeof(buf), &h, "Dev");
    check("HTML 显示 No Data 而不是 100/100", strstr(buf, "No Data") != NULL, NULL);
    check("HTML 响应时间显示 --", strstr(buf, ">--<") != NULL, NULL);
    check("HTML 提示先去抓包",
          strstr(buf, "尚未采集到数据") != NULL, NULL);
}

int main(void)
{
    scenario_normal();
    scenario_degraded();
    scenario_no_data();

    printf("\n合计: OK %d / FAIL %d\n", ok, bad);
    return bad ? 1 : 0;
}

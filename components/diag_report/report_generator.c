#include "report_generator.h"
#include "modbus_common.h"
#include <stdio.h>
#include <string.h>

int report_generate_json(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info)
{
    if (!buf || !health) return 0;

    /* min_response_us 初值是 INT32_MAX：没有计时样本时必须落成 -1，
     * 否则 JSON 里会出现 2147483647，下游画图会把坐标轴拉到天上。 */
    bool has_timing = (health->total_frames > 0) &&
                      (health->min_response_us != INT32_MAX);

    return snprintf(buf, buf_size,
        "{"
        "\"device\":\"%s\","
        "\"health_score\":%d,"
        "\"grade\":\"%s\","
        "\"response\":{"
            "\"min_us\":%ld,"
            "\"max_us\":%ld,"
            "\"avg_us\":%ld,"
            "\"jitter_us\":%ld"
        "},"
        "\"traffic\":{"
            "\"total_frames\":%lu,"
            "\"error_frames\":%lu,"
            "\"exception_frames\":%lu,"
            "\"error_rate\":%.4f,"
            "\"fps\":%lu,"
            "\"bps\":%lu"
        "},"
        "\"last_exception\":{"
            "\"code\":%u,"
            "\"name\":\"%s\","
            "\"advice\":\"%s\""
        "}"
        "}",
        device_info ? device_info : "unknown",
        health->health_score,
        health_metrics_grade(health->health_score),
        (long)(has_timing ? health->min_response_us : -1),
        (long)(has_timing ? health->max_response_us : -1),
        (long)(has_timing ? health->avg_response_us : -1),
        (long)(has_timing ? health->jitter_us : -1),
        (unsigned long)health->total_frames,
        (unsigned long)health->error_frames,
        (unsigned long)health->exception_frames,
        health->error_rate,
        (unsigned long)health->frames_per_sec,
        (unsigned long)health->bytes_per_sec,
        (unsigned)health->last_exception_code,
        health->last_exception_code ? modbus_exception_name(health->last_exception_code) : "",
        health->last_exception_code ? modbus_exception_advice(health->last_exception_code) : ""
    );
}

int report_generate_html(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info)
{
    if (!buf || !health) return 0;

    bool has_data = (health->total_frames > 0);
    const char *color = "#888888";
    if (has_data) {
        color = "#00ff00";
        if (health->health_score < 60) color = "#ff0000";
        else if (health->health_score < 75) color = "#ffaa00";
    }

    /* 分数行：没数据时不能写 100/100 —— 那是把「没测」当成「没问题」 */
    char health_line[64];
    if (has_data) {
        snprintf(health_line, sizeof(health_line), "%d/100 (%s)",
                 health->health_score, health_metrics_grade(health->health_score));
    } else {
        snprintf(health_line, sizeof(health_line), "No Data (no frames)");
    }

    /* 诊断建议：把数字翻成「该干什么」。这一节就是论文里写的「诊断建议」，
     * 用阈值规则生成，不是另写一套判断逻辑 —— 和屏幕上的评分口径完全一致。 */
    char advice[640];
    size_t n = 0;
    advice[0] = '\0';

    if (health->last_exception_code) {
        n += (size_t)snprintf(advice + n, sizeof(advice) - n,
                              "<li>最近一次异常：码 %u（%s）—— %s</li>",
                              (unsigned)health->last_exception_code,
                              modbus_exception_name(health->last_exception_code),
                              modbus_exception_advice(health->last_exception_code));
    }
    if (health->error_rate > 0.05f && n < sizeof(advice)) {
        n += (size_t)snprintf(advice + n, sizeof(advice) - n,
                              "<li>错误率 %.1f%%，超过 5%% 阈值：检查波特率、校验位、"
                              "接线与终端电阻是否匹配</li>",
                              health->error_rate * 100.0f);
    }
    if (health->avg_response_us > 50000 && n < sizeof(advice)) {
        n += (size_t)snprintf(advice + n, sizeof(advice) - n,
                              "<li>平均响应 %.1f ms 偏慢：检查总线负载、波特率与从站处理能力</li>",
                              (double)health->avg_response_us / 1000.0);
    }
    if (health->jitter_us > 10000 && n < sizeof(advice)) {
        n += (size_t)snprintf(advice + n, sizeof(advice) - n,
                              "<li>响应抖动 %.1f ms 偏大：可能存在总线冲突或电磁干扰</li>",
                              (double)health->jitter_us / 1000.0);
    }
    if (n == 0) {
        snprintf(advice, sizeof(advice), "<li>%s</li>",
                 has_data ? "未发现异常：错误率低、响应稳定。"
                          : "尚未采集到数据：先在「报文抓取」页开始抓包，再导出报告。");
    }

    /* 响应时间四个值：有帧不代表有计时样本（被动抓包没有请求/响应配对），
     * 没样本时 min_response_us 会停在 INT32_MAX，直接打出来是天文数字。 */
    bool has_timing = has_data && (health->min_response_us != INT32_MAX);
    char min_s[24], max_s[24], avg_s[24], jit_s[24];
    if (has_timing) {
        snprintf(min_s, sizeof(min_s), "%ld us", (long)health->min_response_us);
        snprintf(max_s, sizeof(max_s), "%ld us", (long)health->max_response_us);
        snprintf(avg_s, sizeof(avg_s), "%ld us", (long)health->avg_response_us);
        snprintf(jit_s, sizeof(jit_s), "%ld us", (long)health->jitter_us);
    } else {
        snprintf(min_s, sizeof(min_s), "--");
        snprintf(max_s, sizeof(max_s), "--");
        snprintf(avg_s, sizeof(avg_s), "--");
        snprintf(jit_s, sizeof(jit_s), "--");
    }

    return snprintf(buf, buf_size,
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width'>"
        "<title>Modbus Diag Report</title>"
        "<style>"
        "body{font-family:monospace;background:#1a1a2e;color:#eee;padding:20px}"
        "h1{color:%s}"
        ".card{background:#16213e;padding:15px;margin:10px 0;border-radius:8px}"
        ".label{color:#888}"
        ".val{color:#0ff;font-size:1.2em}"
        "ul{margin:8px 0 0 18px;padding:0}"
        "li{margin:6px 0;line-height:1.5}"
        "</style></head><body>"
        "<h1>Modbus Diagnostic Report</h1>"
        "<div class='card'>"
        "<p><span class='label'>Device:</span> <span class='val'>%s</span></p>"
        "<p><span class='label'>Health:</span> <span class='val' style='color:%s'>%s</span></p>"
        "</div>"
        "<div class='card'>"
        "<h3>Response Time</h3>"
        "<p>Min: <span class='val'>%s</span></p>"
        "<p>Max: <span class='val'>%s</span></p>"
        "<p>Avg: <span class='val'>%s</span></p>"
        "<p>Jitter: <span class='val'>%s</span></p>"
        "</div>"
        "<div class='card'>"
        "<h3>Traffic</h3>"
        "<p>Total: <span class='val'>%lu frames</span></p>"
        "<p>Errors: <span class='val'>%lu</span></p>"
        "<p>Exceptions: <span class='val'>%lu</span></p>"
        "<p>Error Rate: <span class='val'>%.2f%%</span></p>"
        "</div>"
        "<div class='card'>"
        "<h3>Diagnosis</h3><ul>%s</ul>"
        "</div>"
        "</body></html>",
        color,
        device_info ? device_info : "Unknown",
        color, health_line,
        min_s, max_s, avg_s, jit_s,
        (unsigned long)health->total_frames,
        (unsigned long)health->error_frames,
        (unsigned long)health->exception_frames,
        health->error_rate * 100.0f,
        advice
    );
}

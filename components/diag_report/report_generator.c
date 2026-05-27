#include "report_generator.h"
#include <stdio.h>
#include <string.h>

int report_generate_json(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info)
{
    if (!buf || !health) return 0;
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
        "}"
        "}",
        device_info ? device_info : "unknown",
        health->health_score,
        health_metrics_grade(health->health_score),
        (long)health->min_response_us,
        (long)health->max_response_us,
        (long)health->avg_response_us,
        (long)health->jitter_us,
        (unsigned long)health->total_frames,
        (unsigned long)health->error_frames,
        (unsigned long)health->exception_frames,
        health->error_rate,
        (unsigned long)health->frames_per_sec,
        (unsigned long)health->bytes_per_sec
    );
}

int report_generate_html(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info)
{
    if (!buf || !health) return 0;
    const char *color = "#00ff00";
    if (health->health_score < 60) color = "#ff0000";
    else if (health->health_score < 75) color = "#ffaa00";

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
        "</style></head><body>"
        "<h1>Modbus Diagnostic Report</h1>"
        "<div class='card'>"
        "<p><span class='label'>Device:</span> <span class='val'>%s</span></p>"
        "<p><span class='label'>Health:</span> <span class='val' style='color:%s'>%d/100 (%s)</span></p>"
        "</div>"
        "<div class='card'>"
        "<h3>Response Time</h3>"
        "<p>Min: <span class='val'>%ld us</span></p>"
        "<p>Max: <span class='val'>%ld us</span></p>"
        "<p>Avg: <span class='val'>%ld us</span></p>"
        "<p>Jitter: <span class='val'>%ld us</span></p>"
        "</div>"
        "<div class='card'>"
        "<h3>Traffic</h3>"
        "<p>Total: <span class='val'>%lu frames</span></p>"
        "<p>Errors: <span class='val'>%lu</span></p>"
        "<p>Exceptions: <span class='val'>%lu</span></p>"
        "<p>Error Rate: <span class='val'>%.2f%%</span></p>"
        "</div>"
        "</body></html>",
        color,
        device_info ? device_info : "Unknown",
        color, health->health_score, health_metrics_grade(health->health_score),
        (long)health->min_response_us,
        (long)health->max_response_us,
        (long)health->avg_response_us,
        (long)health->jitter_us,
        (unsigned long)health->total_frames,
        (unsigned long)health->error_frames,
        (unsigned long)health->exception_frames,
        health->error_rate * 100.0f
    );
}

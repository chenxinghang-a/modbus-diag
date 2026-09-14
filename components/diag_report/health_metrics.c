#include "health_metrics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static health_report_t s_report;
static int32_t s_response_sum;
static int64_t s_response_sq_sum;
static uint32_t s_response_count;
static SemaphoreHandle_t s_mutex = NULL;

static const health_weights_t s_weights = {
    .weight_response_time = 0.3f,
    .weight_error_rate = 0.4f,
    .weight_stability = 0.3f,
};

void health_metrics_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(&s_report, 0, sizeof(s_report));
    s_report.min_response_us = INT32_MAX;
    s_response_sum = 0;
    s_response_sq_sum = 0;
    s_response_count = 0;
    xSemaphoreGive(s_mutex);
}

void health_metrics_update(int32_t response_us, bool is_error, bool is_exception)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_report.total_frames++;
    if (is_error) s_report.error_frames++;
    if (is_exception) s_report.exception_frames++;

    if (response_us > 0) {
        if (response_us < s_report.min_response_us) s_report.min_response_us = response_us;
        if (response_us > s_report.max_response_us) s_report.max_response_us = response_us;

        s_response_sum += response_us;
        s_response_sq_sum += (int64_t)response_us * response_us;
        s_response_count++;

        s_report.avg_response_us = s_response_sum / s_response_count;

        /* Variance: E[X^2] - E[X]^2 */
        if (s_response_count > 1) {
            int64_t ex2 = s_response_sq_sum / s_response_count;
            int64_t ex = s_report.avg_response_us;
            s_report.jitter_us = (int32_t)sqrtf((float)(ex2 - ex * ex));
        }
    }

    if (s_report.total_frames > 0) {
        s_report.error_rate = (float)s_report.error_frames / s_report.total_frames;
    }

    xSemaphoreGive(s_mutex);
}

int health_metrics_calculate_score(const health_report_t *report)
{
    if (!report) return 0;

    /* Response time score: <5ms=100, >100ms=0 */
    float rt_score = 100.0f;
    if (report->avg_response_us > 100000) rt_score = 0;
    else if (report->avg_response_us > 5000) {
        rt_score = 100.0f - ((float)report->avg_response_us - 5000) / 950.0f;
    }

    /* Error rate score: 0%=100, >10%=0 */
    float err_score = 100.0f - report->error_rate * 1000.0f;
    if (err_score < 0) err_score = 0;

    /* Stability score (based on jitter): <1ms=100, >50ms=0 */
    float stab_score = 100.0f;
    if (report->jitter_us > 50000) stab_score = 0;
    else if (report->jitter_us > 1000) {
        stab_score = 100.0f - ((float)report->jitter_us - 1000) / 490.0f;
    }

    int score = (int)(rt_score * s_weights.weight_response_time +
                      err_score * s_weights.weight_error_rate +
                      stab_score * s_weights.weight_stability);
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    return score;
}

const char *health_metrics_grade(int score)
{
    if (score >= 90) return "EXCELLENT";
    if (score >= 75) return "GOOD";
    if (score >= 60) return "FAIR";
    if (score >= 40) return "POOR";
    return "CRITICAL";
}

void health_metrics_get_report(health_report_t *out)
{
    if (!out || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_report;
    out->health_score = health_metrics_calculate_score(&s_report);
    xSemaphoreGive(s_mutex);
}

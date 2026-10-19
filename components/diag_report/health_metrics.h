#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Health score weights */
typedef struct {
    float weight_response_time;  /* 0.3 */
    float weight_error_rate;     /* 0.4 */
    float weight_stability;      /* 0.3 */
} health_weights_t;

typedef struct {
    int32_t  min_response_us;
    int32_t  max_response_us;
    int32_t  avg_response_us;
    int32_t  jitter_us;          /* response time variance */
    float    error_rate;         /* 0.0~1.0 */
    float    timeout_rate;       /* 0.0~1.0 */
    uint32_t total_frames;
    uint32_t error_frames;
    uint32_t exception_frames;
    uint32_t frames_per_sec;
    uint32_t bytes_per_sec;
    uint8_t  last_exception_code; /* 最近一次异常响应的异常码，0 = 还没有过异常 */
    int      health_score;       /* 0~100 */
} health_report_t;

void health_metrics_init(void);
/* exception_code：本次帧若是异常响应就传异常码，否则传 0（MODBUS_EX_NONE） */
void health_metrics_update(int32_t response_us, bool is_error, bool is_exception,
                           uint8_t exception_code);
void health_metrics_get_report(health_report_t *out);
int health_metrics_calculate_score(const health_report_t *report);
const char *health_metrics_grade(int score);

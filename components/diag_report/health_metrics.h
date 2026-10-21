#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Health score weights */
typedef struct {
    float weight_response_time;  /* 0.3 */
    float weight_error_rate;     /* 0.4 */
    float weight_stability;      /* 0.3 */
} health_weights_t;

/* 评分的最小样本量。
 * 抓 1 帧成功就能得 100 分是个荒谬结论 —— 样本不足时不给分数（返回 -1）。 */
#define HEALTH_MIN_SAMPLES 10

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
    int      health_score;       /* 0~100；-1 = 样本不足，不给分 */
} health_report_t;

void health_metrics_init(void);
/* 只保证互斥量存在，**不清零已有统计**。
 * 用于「页面切换时别把上一步采到的数据抹掉」这种场景（init 会清零）。 */
void health_metrics_ensure_init(void);
/* exception_code：本次帧若是异常响应就传异常码，否则传 0（MODBUS_EX_NONE） */
void health_metrics_update(int32_t response_us, bool is_error, bool is_exception,
                           uint8_t exception_code);
void health_metrics_get_report(health_report_t *out);
int health_metrics_calculate_score(const health_report_t *report);
const char *health_metrics_grade(int score);

/* 样本是否足够评分（total_frames >= HEALTH_MIN_SAMPLES） */
static inline bool health_has_enough_samples(const health_report_t *r)
{
    return r && r->total_frames >= HEALTH_MIN_SAMPLES;
}

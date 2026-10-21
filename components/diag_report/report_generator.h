#pragma once

#include <stddef.h>     /* size_t —— 别依赖调用方先包含 stdio.h（CI 上就是这么炸的） */
#include <stdint.h>

#include "health_metrics.h"
#include "esp_err.h"

/* 报告里的「上下文」信息。
 *
 * FR7 要求报告包含设备信息、通信参数、统计数据、评分与异常事件记录；
 * 早期版本只有统计与评分，运维拿到报告看不出这是哪台设备、用什么参数测的。
 * 这些字段由调用方填好后传入（可为 NULL，表示不填）。 */
typedef struct {
    uint8_t     slave_addr;     /* 目标从站地址 */
    uint32_t    baud;           /* 波特率 */
    uint8_t     parity;         /* 0=N 1=O 2=E */
    uint8_t     stop_bits;      /* 1 或 2 */
    const char *fw_version;     /* 固件版本，可为 NULL */
    uint32_t    uptime_s;       /* 开机秒数（无 RTC，只能给相对时间） */
    const char *events;         /* 最近若干条操作日志，仅 HTML 报告内嵌 */
} report_meta_t;

/* Generate JSON report to buffer. Returns bytes written. */
int report_generate_json(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info, const report_meta_t *meta);

/* Generate HTML report */
int report_generate_html(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info, const report_meta_t *meta);

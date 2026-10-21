#pragma once

#include <stddef.h>     /* size_t —— 别依赖调用方先包含 stdio.h（CI 上就是这么炸的） */

#include "health_metrics.h"
#include "esp_err.h"

/* Generate JSON report to buffer. Returns bytes written. */
int report_generate_json(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info);

/* Generate HTML report to buffer */
int report_generate_html(char *buf, size_t buf_size, const health_report_t *health,
                         const char *device_info);

#pragma once

#include "esp_err.h"

esp_err_t log_storage_init(void);
esp_err_t log_storage_append(const char *entry);

/* 带开机时间戳的格式化写入（设备没有 RTC，只能用 uptime；格式 "[T+12.345s] ..."）。
 * 内部带容量上限：oplog 超过 LOG_MAX_BYTES 就轮转，避免 64KB 的 SPIFFS 被日志吃满、
 * 连带把诊断报告写失败。 */
esp_err_t log_storage_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

esp_err_t log_storage_read(char *buf, size_t buf_size, int max_lines);
esp_err_t log_storage_clear(void);

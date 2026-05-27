#pragma once

#include "esp_err.h"

esp_err_t log_storage_init(void);
esp_err_t log_storage_append(const char *entry);
esp_err_t log_storage_read(char *buf, size_t buf_size, int max_lines);
esp_err_t log_storage_clear(void);

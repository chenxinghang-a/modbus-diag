#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t nvs_config_init(void);
esp_err_t nvs_config_set_u32(const char *key, uint32_t value);
esp_err_t nvs_config_get_u32(const char *key, uint32_t *value, uint32_t default_val);
esp_err_t nvs_config_set_str(const char *key, const char *value);
esp_err_t nvs_config_get_str(const char *key, char *buf, size_t buf_size, const char *default_val);
esp_err_t nvs_config_set_bool(const char *key, bool value);
esp_err_t nvs_config_get_bool(const char *key, bool *value, bool default_val);

#pragma once

#include "esp_err.h"

esp_err_t file_export_init(void);
esp_err_t file_export_save(const char *filename, const char *data, size_t len);
const char *file_export_get_path(const char *filename);

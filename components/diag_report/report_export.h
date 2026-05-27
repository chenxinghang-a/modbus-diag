#pragma once

#include "esp_err.h"

/* Export report to SPIFFS file */
esp_err_t report_export_to_file(const char *json_content, const char *filename);

/* Get report file path */
const char *report_export_get_path(const char *filename);

#include "report_export.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "export";

esp_err_t report_export_to_file(const char *content, const char *filename)
{
    if (!content || !filename) return ESP_ERR_INVALID_ARG;

    char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s", filename);

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return ESP_FAIL;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    ESP_LOGI(TAG, "Report exported to %s", path);
    return ESP_OK;
}

const char *report_export_get_path(const char *filename)
{
    static char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s", filename);
    return path;
}

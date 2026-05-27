#include "file_export.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "export";

esp_err_t file_export_init(void)
{
    return ESP_OK;
}

esp_err_t file_export_save(const char *filename, const char *data, size_t len)
{
    char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s", filename);
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to save %s", path);
        return ESP_FAIL;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    ESP_LOGI(TAG, "Saved %s (%d bytes)", path, (int)len);
    return ESP_OK;
}

const char *file_export_get_path(const char *filename)
{
    static char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s", filename);
    return path;
}

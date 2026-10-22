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
    /* fwrite 的返回值也要查：SPIFFS 满时它只写进去一部分就失败，
     * 原来直接返回 ESP_OK —— 报告页显示"导出成功"，Web 上打开却是截断的半份。 */
    size_t wn = fwrite(data, 1, len, f);
    int cerr = fclose(f);
    if (wn != len || cerr != 0) {
        ESP_LOGE(TAG, "Save %s incomplete: %u/%u bytes, fclose=%d",
                 path, (unsigned)wn, (unsigned)len, cerr);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Saved %s (%u bytes)", path, (unsigned)len);
    return ESP_OK;
}

const char *file_export_get_path(const char *filename)
{
    static char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s", filename);
    return path;
}

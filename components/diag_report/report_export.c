#include "report_export.h"
#include "file_export.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "export";

/* 这里原先是自己再写一遍 fopen/fwrite，与 storage/file_export.c 完全重复。
 * 统一走 file_export_save()：一处实现、一处错误处理，改路径前缀不用改两遍。
 * report_export_* 保留为「报告语义」的薄封装，调用方不用动。 */
esp_err_t report_export_to_file(const char *content, const char *filename)
{
    if (!content || !filename) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = file_export_save(filename, content, strlen(content));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Report exported to /spiffs/%s", filename);
    }
    return ret;
}

const char *report_export_get_path(const char *filename)
{
    return file_export_get_path(filename);
}

#include "log_storage.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "log";
static const char *LOG_PATH = "/spiffs/oplog.txt";

static bool spiffs_is_mounted(void)
{
    /* Must use the same label as esp_vfs_spiffs_register() in main.c ("storage").
       esp_spiffs_info(NULL, ...) never matches here: the volume is registered with
       a partition label, so by_label is set and a NULL lookup always fails. */
    return esp_spiffs_mounted("storage");
}

esp_err_t log_storage_init(void)
{
    if (!spiffs_is_mounted()) {
        ESP_LOGW(TAG, "SPIFFS not mounted, log storage disabled");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Log storage ready");
    return ESP_OK;
}

esp_err_t log_storage_append(const char *entry)
{
    if (!spiffs_is_mounted()) return ESP_ERR_INVALID_STATE;
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return ESP_FAIL;
    fprintf(f, "%s\n", entry);
    fclose(f);
    return ESP_OK;
}

esp_err_t log_storage_read(char *buf, size_t buf_size, int max_lines)
{
    FILE *f = fopen(LOG_PATH, "r");
    if (!f) { buf[0] = '\0'; return ESP_OK; }
    size_t total = 0;
    char line[128];
    int lines = 0;
    while (fgets(line, sizeof(line), f) && lines < max_lines) {
        size_t len = strlen(line);
        if (total + len < buf_size) {
            memcpy(buf + total, line, len);
            total += len;
        }
        lines++;
    }
    buf[total] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t log_storage_clear(void)
{
    FILE *f = fopen(LOG_PATH, "w");
    if (f) fclose(f);
    return ESP_OK;
}

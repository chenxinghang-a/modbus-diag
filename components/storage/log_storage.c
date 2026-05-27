#include "log_storage.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "log";
static const char *LOG_PATH = "/spiffs/oplog.txt";

esp_err_t log_storage_init(void)
{
    ESP_LOGI(TAG, "Log storage ready");
    return ESP_OK;
}

esp_err_t log_storage_append(const char *entry)
{
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

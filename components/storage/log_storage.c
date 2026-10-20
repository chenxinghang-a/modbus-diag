#include "log_storage.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "log";
static const char *LOG_PATH = "/spiffs/oplog.txt";

/* 单份日志上限。SPIFFS 只有 64KB，而诊断报告也要写在这里；
 * 日志无限增长会把分区吃满，最后报告写不进去、还查不出原因。 */
#define LOG_MAX_BYTES   (8 * 1024)

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
    if (!spiffs_is_mounted() || !entry) return ESP_ERR_INVALID_STATE;

    /* 先看大小，超了就轮转（重开一个空文件并写一行分隔），
     * 而不是让日志无上限地吃掉 SPIFFS。 */
    FILE *probe = fopen(LOG_PATH, "a");
    if (probe) {
        fseek(probe, 0, SEEK_END);
        long size = ftell(probe);
        fclose(probe);
        if (size > LOG_MAX_BYTES) {
            FILE *trunc = fopen(LOG_PATH, "w");
            if (trunc) {
                fprintf(trunc, "[log rotated, previous > %d bytes]\n", LOG_MAX_BYTES);
                fclose(trunc);
            }
        }
    }

    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return ESP_FAIL;
    fprintf(f, "%s\n", entry);
    fclose(f);
    return ESP_OK;
}

esp_err_t log_storage_logf(const char *fmt, ...)
{
    if (!fmt) return ESP_ERR_INVALID_ARG;

    TickType_t ticks = xTaskGetTickCount();
    uint32_t ms = (uint32_t)(ticks * portTICK_PERIOD_MS);

    char line[160];
    int n = snprintf(line, sizeof(line), "[T+%lu.%03lus] ",
                     (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    if (n < 0 || n >= (int)sizeof(line)) return ESP_FAIL;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    va_end(ap);

    return log_storage_append(line);
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

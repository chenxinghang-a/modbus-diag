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
#define LOG_KEEP_BYTES  (4 * 1024)   /* 轮转时保留的尾部字节数 */

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
            /* 轮转保留**尾部** LOG_KEEP_BYTES，而不是整篇清空。
             * 原来超限就把文件清成一行 "log rotated" —— 恰恰在跑久了、最需要回溯的
             * 时候把全部现场证据扔掉。现在留最近 4KB，只丢更早的部分。 */
            static char rot[LOG_KEEP_BYTES + 1];
            FILE *r = fopen(LOG_PATH, "r");
            if (r) {
                if (fseek(r, -(long)LOG_KEEP_BYTES, SEEK_END) != 0) fseek(r, 0, SEEK_SET);
                size_t n = fread(rot, 1, LOG_KEEP_BYTES, r);
                rot[n] = 0;
                fclose(r);

                /* 截断点可能落在半行上，丢掉第一行残片 */
                char *keep = rot;
                char *nl = strchr(rot, '\n');
                if (nl && n == LOG_KEEP_BYTES) keep = nl + 1;

                FILE *w = fopen(LOG_PATH, "w");
                if (w) {
                    fprintf(w, "[log rotated at %d bytes, older entries dropped]\n",
                            LOG_MAX_BYTES);
                    size_t wn = fwrite(keep, 1, strlen(keep), w);
                    int cerr = fclose(w);
                    if (wn != strlen(keep) || cerr != 0) {
                        ESP_LOGE(TAG, "log rotation write failed (%u/%u, %d)",
                                 (unsigned)wn, (unsigned)strlen(keep), cerr);
                    }
                }
            }
        }
    }

    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return ESP_FAIL;
    /* 返回值必须查：SPIFFS 写满时 fprintf 会失败，原来直接返回 ESP_OK，
     * 现场表现是「日志一直在写、文件却不动」，且没有任何线索。 */
    int wn = fprintf(f, "%s\n", entry);
    int cerr = fclose(f);
    if (wn < 0 || cerr != 0) {
        ESP_LOGE(TAG, "log write failed (printf=%d, fclose=%d)", wn, cerr);
        return ESP_FAIL;
    }
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
    if (!buf || buf_size == 0) return ESP_ERR_INVALID_ARG;
    buf[0] = '\0';

    FILE *f = fopen(LOG_PATH, "r");
    if (!f) return ESP_OK;

    /* 取**最后** max_lines 行。
     * 原实现从文件头开始读 N 行 —— 报告写着「最近 12 条」，实际给的是开机时最老的
     * 12 条，等于把最没用的部分端给用户。日志上限 8KB，扫两遍的代价可忽略。 */
    char line[128];
    int total = 0;
    while (fgets(line, sizeof(line), f)) total++;

    int skip = (max_lines > 0 && total > max_lines) ? (total - max_lines) : 0;

    rewind(f);
    size_t out = 0;
    int idx = 0;
    while (fgets(line, sizeof(line), f)) {
        if (idx++ < skip) continue;
        size_t len = strlen(line);
        if (out + len >= buf_size) break;
        memcpy(buf + out, line, len);
        out += len;
    }
    buf[out] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t log_storage_clear(void)
{
    FILE *f = fopen(LOG_PATH, "w");
    if (f) fclose(f);
    return ESP_OK;
}

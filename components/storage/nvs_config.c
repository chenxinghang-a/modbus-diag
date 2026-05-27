#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_cfg";
static const char *NAMESPACE = "config";

esp_err_t nvs_config_init(void)
{
    return ESP_OK;  /* NVS already initialized in main */
}

esp_err_t nvs_config_set_u32(const char *key, uint32_t value)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_u32(h, key, value);
    nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_config_get_u32(const char *key, uint32_t *value, uint32_t default_val)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) { *value = default_val; return ESP_OK; }
    ret = nvs_get_u32(h, key, value);
    if (ret != ESP_OK) *value = default_val;
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_config_set_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_str(h, key, value);
    nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_config_get_str(const char *key, char *buf, size_t buf_size, const char *default_val)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        if (default_val) strncpy(buf, default_val, buf_size);
        return ESP_OK;
    }
    ret = nvs_get_str(h, key, buf, &buf_size);
    if (ret != ESP_OK && default_val) strncpy(buf, default_val, buf_size);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_config_set_bool(const char *key, bool value)
{
    return nvs_config_set_u32(key, value ? 1 : 0);
}

esp_err_t nvs_config_get_bool(const char *key, bool *value, bool default_val)
{
    uint32_t v;
    esp_err_t ret = nvs_config_get_u32(key, &v, default_val ? 1 : 0);
    *value = (v != 0);
    return ret;
}

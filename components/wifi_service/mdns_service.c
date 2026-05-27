#include "mdns_service.h"
#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "mdns";

esp_err_t mdns_service_init(void)
{
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed");
        return ret;
    }
    mdns_hostname_set("modbus-diag");
    mdns_instance_name_set("Modbus Diagnostic Tool");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://modbus-diag.local");
    return ESP_OK;
}

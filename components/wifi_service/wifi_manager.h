#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_ap(void);
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);
void wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
const char *wifi_manager_get_ip(void);

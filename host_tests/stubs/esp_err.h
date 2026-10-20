#pragma once
/* 主机侧测试用的 esp_err.h 桩：只为让 diag_report 的生成器能脱机编译。
 * 固件里用的是 ESP-IDF 的真实现，这里只补测试用得到的几个宏/类型。 */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101

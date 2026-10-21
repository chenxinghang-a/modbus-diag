#pragma once
/* 主机侧测试用的 esp_log.h 桩：把日志宏变成空操作。
 * 只为了让纯逻辑模块（如 modbus_tcp 的组帧/解帧）能在 PC 上编译，不产生实际输出。 */
#include <stdio.h>
#include "esp_err.h"

#define ESP_LOGE(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, fmt, ...) do { (void)(tag); } while (0)

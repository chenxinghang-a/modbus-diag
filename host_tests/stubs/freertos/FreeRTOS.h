#pragma once
/* 主机侧测试用的 FreeRTOS 桩：只提供 health_metrics.c 用到的那一点点。
 * 固件里用的是真 FreeRTOS，这里只是让纯逻辑能在 PC 上跑。 */
#include <stdint.h>
#include <stddef.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(x)        ((TickType_t)(x))
#define portMAX_DELAY           ((TickType_t)0xFFFFFFFFu)
#define pdTRUE                  1
#define pdFALSE                 0

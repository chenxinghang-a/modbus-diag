#pragma once
/* 互斥量桩：单线程测试环境里，take/give 永远是空操作。
 * 注意这不验证并发正确性 —— 并发得靠真机或 ThreadSanitizer，这里只测算法。 */
#include "FreeRTOS.h"

typedef struct fake_sem_s { int dummy; } *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static struct fake_sem_s s_m;
    return &s_m;
}

static inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t t)
{
    (void)s; (void)t;
    return pdTRUE;
}

static inline int xSemaphoreGive(SemaphoreHandle_t s)
{
    (void)s;
    return pdTRUE;
}

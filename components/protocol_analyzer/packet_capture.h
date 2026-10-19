#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CAPTURE_BUF_SIZE    4096
#define CAPTURE_MAX_FRAMES  64

typedef struct {
    uint8_t  data[256];
    uint16_t len;
    /* 开机后的毫秒数（esp_timer_get_time()/1000）。
     * 原来叫 timestamp_us 且用 uint32 存 tv_sec*1000000 —— 1.7e15 截断到 32 位，
     * 值是垃圾，所以从来没人敢显示。改成开机毫秒：uint32 够 49 天，显示也直观。 */
    uint32_t uptime_ms;
    bool     crc_valid;
} captured_frame_t;

esp_err_t packet_capture_init(void);
esp_err_t packet_capture_start(void);
void packet_capture_stop(void);
bool packet_capture_is_running(void);

/* Get next captured frame. Returns false if no more frames. */
bool packet_capture_get_frame(captured_frame_t *frame);

/* Get frame count */
int packet_capture_frame_count(void);

/* Clear captured frames */
void packet_capture_clear(void);

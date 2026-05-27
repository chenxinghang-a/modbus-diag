#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CAPTURE_BUF_SIZE    4096
#define CAPTURE_MAX_FRAMES  64

typedef struct {
    uint8_t  data[256];
    uint16_t len;
    uint32_t timestamp_us;
    bool     is_request;    /* true=request from master, false=response from slave */
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

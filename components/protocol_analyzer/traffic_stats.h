#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t total_frames;
    uint32_t error_frames;       /* CRC errors */
    uint32_t exception_frames;   /* Modbus exceptions */
    uint32_t timeout_frames;
    float    error_rate;         /* 0.0~1.0 */
    int32_t  avg_response_us;
    int32_t  max_response_us;
    int32_t  min_response_us;
    uint32_t frames_per_sec;
    uint32_t bytes_per_sec;
} traffic_stats_t;

void traffic_stats_init(void);
void traffic_stats_add_frame(bool crc_ok, bool is_exception, int32_t response_us, uint16_t frame_len);
void traffic_stats_get(traffic_stats_t *out);
void traffic_stats_reset(void);
void traffic_stats_tick(void);  /* call once per second to update rates */

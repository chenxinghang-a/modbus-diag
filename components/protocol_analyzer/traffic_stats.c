#include "traffic_stats.h"
#include <string.h>

static traffic_stats_t s_stats;
static uint32_t s_frames_this_sec;
static uint32_t s_bytes_this_sec;

void traffic_stats_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.min_response_us = INT32_MAX;
    s_frames_this_sec = 0;
    s_bytes_this_sec = 0;
}

void traffic_stats_add_frame(bool crc_ok, bool is_exception, int32_t response_us, uint16_t frame_len)
{
    s_stats.total_frames++;
    s_frames_this_sec++;
    s_bytes_this_sec += frame_len;

    if (!crc_ok) {
        s_stats.error_frames++;
    }
    if (is_exception) {
        s_stats.exception_frames++;
    }

    if (response_us > 0) {
        if (response_us > s_stats.max_response_us) {
            s_stats.max_response_us = response_us;
        }
        if (response_us < s_stats.min_response_us) {
            s_stats.min_response_us = response_us;
        }
        /* Running average */
        if (s_stats.total_frames == 1) {
            s_stats.avg_response_us = response_us;
        } else {
            s_stats.avg_response_us = (s_stats.avg_response_us * 7 + response_us) / 8;
        }
    }

    if (s_stats.total_frames > 0) {
        s_stats.error_rate = (float)s_stats.error_frames / s_stats.total_frames;
    }
}

void traffic_stats_get(traffic_stats_t *out)
{
    if (out) *out = s_stats;
}

void traffic_stats_reset(void)
{
    traffic_stats_init();
}

void traffic_stats_tick(void)
{
    s_stats.frames_per_sec = s_frames_this_sec;
    s_stats.bytes_per_sec = s_bytes_this_sec;
    s_frames_this_sec = 0;
    s_bytes_this_sec = 0;
}

#pragma once

#include <stdint.h>

/* Apply simple moving average filter */
void signal_filter_sma(const uint16_t *in, uint16_t *out, int len, int window);

/* Find trigger point (rising edge crossing threshold) */
int signal_find_trigger(const uint16_t *data, int len, uint16_t threshold);

/* Measure peak-to-peak amplitude */
uint16_t signal_measure_vpp(const uint16_t *data, int len);

/* Measure DC offset (average) */
uint16_t signal_measure_dc(const uint16_t *data, int len);

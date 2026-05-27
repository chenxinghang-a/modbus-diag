#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t adc_sampler_init(void);
esp_err_t adc_sampler_start(void);
void adc_sampler_stop(void);

/* Get the latest sample buffer (WAVE_SAMPLE_DEPTH points, 12-bit ADC values) */
const uint16_t *adc_sampler_get_buffer(void);
int adc_sampler_get_depth(void);

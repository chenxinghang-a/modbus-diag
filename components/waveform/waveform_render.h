#pragma once

#include <stdint.h>

/* Render waveform to TFT display area */
void waveform_render(int x, int y, int w, int h,
                     const uint16_t *data, int data_len,
                     uint16_t line_color, uint16_t bg_color);

/* Render with grid overlay */
void waveform_render_with_grid(int x, int y, int w, int h,
                               const uint16_t *data, int data_len,
                               uint16_t line_color, uint16_t grid_color);

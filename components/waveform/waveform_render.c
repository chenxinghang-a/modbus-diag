#include "waveform_render.h"
#include "tft_driver.h"

void waveform_render(int x, int y, int w, int h,
                     const uint16_t *data, int data_len,
                     uint16_t line_color, uint16_t bg_color)
{
    tft_fill_rect(x, y, w, h, bg_color);
    if (!data || data_len < 2 || w < 2) return;

    int prev_py = y + h - 1 - (data[0] * (h - 1)) / 4095;
    for (int i = 1; i < data_len && i < w; i++) {
        int px = x + (i * w) / data_len;
        int py = y + h - 1 - (data[i] * (h - 1)) / 4095;
        /* Draw line from prev to current */
        int dy = py - prev_py;
        int steps = (dy > 0) ? dy : -dy;
        if (steps == 0) steps = 1;
        int prev_px = x + ((i - 1) * w) / data_len;
        for (int s = 0; s <= steps; s++) {
            int sy = prev_py + (dy * s) / steps;
            int sx = prev_px + ((px - prev_px) * s) / steps;
            if (sx >= x && sx < x + w && sy >= y && sy < y + h) {
                tft_draw_pixel(sx, sy, line_color);
            }
        }
        prev_py = py;
    }
}

void waveform_render_with_grid(int x, int y, int w, int h,
                               const uint16_t *data, int data_len,
                               uint16_t line_color, uint16_t grid_color)
{
    /* Draw grid */
    for (int gy = y + h / 4; gy < y + h; gy += h / 4) {
        tft_draw_hline(x, gy, w, grid_color);
    }
    for (int gx = x + w / 4; gx < x + w; gx += w / 4) {
        tft_draw_vline(gx, y, h, grid_color);
    }

    /* Draw waveform */
    if (!data || data_len < 2) return;
    int prev_py = y + h - 1 - (data[0] * (h - 1)) / 4095;
    for (int i = 1; i < data_len && i < w; i++) {
        int px = x + (i * w) / data_len;
        int py = y + h - 1 - (data[i] * (h - 1)) / 4095;
        int dy = py - prev_py;
        int steps = (dy > 0) ? dy : -dy;
        if (steps == 0) steps = 1;
        int prev_px = x + ((i - 1) * w) / data_len;
        for (int s = 0; s <= steps; s++) {
            int sy = prev_py + (dy * s) / steps;
            int sx = prev_px + ((px - prev_px) * s) / steps;
            if (sx >= x && sx < x + w && sy >= y && sy < y + h) {
                tft_draw_pixel(sx, sy, line_color);
            }
        }
        prev_py = py;
    }
}

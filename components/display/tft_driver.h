#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_config.h"

/* ILI9341 16-bit color (RGB565) */
#define TFT_COLOR_BLACK       0x0000
#define TFT_COLOR_WHITE       0xFFFF
#define TFT_COLOR_RED         0xF800
#define TFT_COLOR_GREEN       0x07E0
#define TFT_COLOR_BLUE        0x001F
#define TFT_COLOR_CYAN        0x07FF
#define TFT_COLOR_YELLOW      0xFFE0
#define TFT_COLOR_MAGENTA     0xF81F
#define TFT_COLOR_ORANGE      0xFD20
#define TFT_COLOR_GRAY        0x8410
#define TFT_COLOR_DARKGRAY    0x4208
#define TFT_COLOR_LIGHTGRAY   0xC618
#define TFT_COLOR_DARKGREEN   0x03E0
#define TFT_COLOR_DARKBLUE    0x0010

esp_err_t tft_driver_init(void);

void tft_fill_screen(uint16_t color);
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_draw_pixel(int x, int y, uint16_t color);
void tft_draw_hline(int x, int y, int w, uint16_t color);
void tft_draw_vline(int x, int y, int h, uint16_t color);
void tft_draw_rect(int x, int y, int w, int h, uint16_t color);

void tft_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, uint8_t size);
void tft_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg, uint8_t size);

/* Blit a region of RGB565 pixels (for waveform etc.) */
void tft_blit(int x, int y, int w, int h, const uint16_t *data);

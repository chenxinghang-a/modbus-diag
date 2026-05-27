#pragma once

#include "tft_driver.h"

/* Theme colors */
#define THEME_BG            TFT_COLOR_BLACK
#define THEME_FG            TFT_COLOR_WHITE
#define THEME_ACCENT        TFT_COLOR_CYAN
#define THEME_HIGHLIGHT     TFT_COLOR_YELLOW
#define THEME_MENU_BG       0x2104      /* dark blue-gray */
#define THEME_MENU_SEL      0x4A69      /* medium blue */
#define THEME_STATUS_BAR    0x10A2      /* dark teal */
#define THEME_PROGRESS_BG   TFT_COLOR_DARKGRAY
#define THEME_PROGRESS_FG   TFT_COLOR_GREEN
#define THEME_ERROR         TFT_COLOR_RED
#define THEME_SUCCESS       TFT_COLOR_GREEN
#define THEME_WARNING       TFT_COLOR_ORANGE

/* Font sizes */
#define FONT_SMALL   1
#define FONT_NORMAL  1
#define FONT_LARGE   2
#define FONT_TITLE   2

/* Layout constants */
#define STATUS_BAR_H    20
#define MENU_ITEM_H     28
#define MENU_X          10
#define MENU_Y          28
#define SCREEN_W        TFT_WIDTH
#define SCREEN_H        TFT_HEIGHT

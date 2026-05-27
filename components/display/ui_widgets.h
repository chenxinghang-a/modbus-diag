#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Vertical menu widget */
typedef struct {
    int x, y, w;
    int item_count;
    int visible_count;    /* max visible items */
    int selected;         /* current selection index */
    int scroll_offset;    /* first visible item index */
    const char **labels;
} ui_menu_t;

void ui_menu_init(ui_menu_t *m, int x, int y, int w, int visible,
                  const char **labels, int count);
void ui_menu_draw(ui_menu_t *m);
void ui_menu_move_up(ui_menu_t *m);
void ui_menu_move_down(ui_menu_t *m);

/* Progress bar */
void ui_progress_bar(int x, int y, int w, int h, int percent,
                     uint16_t fg, uint16_t bg);

/* Dialog */
void ui_show_dialog(const char *title, const char *msg, uint16_t color);
void ui_close_dialog(void);

/* Status bar */
void ui_status_bar_draw(const char *title, bool wifi_on, bool modbus_active);

/* Text input (simple numeric) */
void ui_num_input_draw(int x, int y, int value, int digits, bool selected,
                       uint16_t fg, uint16_t bg);

/* List with details */
void ui_list_item_draw(int x, int y, int w, const char *label,
                       const char *value, bool selected);

/* Simple bar chart */
void ui_bar_chart(int x, int y, int w, int h,
                  const int *values, int count, int max_val,
                  uint16_t color);

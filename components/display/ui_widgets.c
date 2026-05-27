#include "ui_widgets.h"
#include "tft_driver.h"
#include "ui_theme.h"
#include <string.h>
#include <stdio.h>

void ui_menu_init(ui_menu_t *m, int x, int y, int w, int visible,
                  const char **labels, int count)
{
    m->x = x;
    m->y = y;
    m->w = w;
    m->visible_count = visible;
    m->item_count = count;
    m->selected = 0;
    m->scroll_offset = 0;
    m->labels = labels;
}

void ui_menu_draw(ui_menu_t *m)
{
    for (int i = 0; i < m->visible_count && (i + m->scroll_offset) < m->item_count; i++) {
        int idx = i + m->scroll_offset;
        int iy = m->y + i * MENU_ITEM_H;
        bool sel = (idx == m->selected);
        uint16_t bg = sel ? THEME_MENU_SEL : THEME_MENU_BG;
        uint16_t fg = sel ? THEME_HIGHLIGHT : THEME_FG;

        tft_fill_rect(m->x, iy, m->w, MENU_ITEM_H - 2, bg);
        tft_draw_string(m->x + 8, iy + 6, m->labels[idx], fg, bg, FONT_NORMAL);

        if (sel) {
            /* Selection indicator */
            tft_fill_rect(m->x, iy, 3, MENU_ITEM_H - 2, THEME_ACCENT);
        }
    }
}

void ui_menu_move_up(ui_menu_t *m)
{
    if (m->selected > 0) {
        m->selected--;
        if (m->selected < m->scroll_offset) {
            m->scroll_offset = m->selected;
        }
    }
}

void ui_menu_move_down(ui_menu_t *m)
{
    if (m->selected < m->item_count - 1) {
        m->selected++;
        if (m->selected >= m->scroll_offset + m->visible_count) {
            m->scroll_offset = m->selected - m->visible_count + 1;
        }
    }
}

void ui_progress_bar(int x, int y, int w, int h, int percent,
                     uint16_t fg, uint16_t bg)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    tft_fill_rect(x, y, w, h, bg);
    int fw = (w * percent) / 100;
    if (fw > 0) {
        tft_fill_rect(x, y, fw, h, fg);
    }
}

void ui_show_dialog(const char *title, const char *msg, uint16_t color)
{
    int dw = 260, dh = 100;
    int dx = (TFT_WIDTH - dw) / 2;
    int dy = (TFT_HEIGHT - dh) / 2;

    /* Shadow */
    tft_fill_rect(dx + 2, dy + 2, dw, dh, TFT_COLOR_DARKGRAY);
    /* Background */
    tft_fill_rect(dx, dy, dw, dh, THEME_MENU_BG);
    /* Border */
    tft_draw_rect(dx, dy, dw, dh, color);
    /* Title bar */
    tft_fill_rect(dx + 1, dy + 1, dw - 2, 20, color);
    tft_draw_string(dx + 8, dy + 4, title, TFT_COLOR_WHITE, color, FONT_NORMAL);
    /* Message */
    tft_draw_string(dx + 10, dy + 35, msg, THEME_FG, THEME_MENU_BG, FONT_NORMAL);
    /* OK hint */
    tft_draw_string(dx + 80, dy + 72, "[OK]", THEME_HIGHLIGHT, THEME_MENU_BG, FONT_NORMAL);
}

void ui_close_dialog(void)
{
    /* Will be overwritten by next page draw */
}

void ui_status_bar_draw(const char *title, bool wifi_on, bool modbus_active)
{
    tft_fill_rect(0, 0, TFT_WIDTH, STATUS_BAR_H, THEME_STATUS_BAR);
    tft_draw_string(4, 3, title, THEME_FG, THEME_STATUS_BAR, FONT_NORMAL);

    /* WiFi icon (simple text) */
    if (wifi_on) {
        tft_draw_string(TFT_WIDTH - 60, 3, "WiFi", THEME_SUCCESS, THEME_STATUS_BAR, FONT_NORMAL);
    }

    /* Modbus active indicator */
    if (modbus_active) {
        tft_draw_string(TFT_WIDTH - 28, 3, "M", THEME_ACCENT, THEME_STATUS_BAR, FONT_NORMAL);
    }
}

void ui_num_input_draw(int x, int y, int value, int digits, bool selected,
                       uint16_t fg, uint16_t bg)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%0*d", digits, value);
    uint16_t b = selected ? THEME_MENU_SEL : bg;
    tft_fill_rect(x, y, digits * 6 + 8, 14, b);
    tft_draw_string(x + 4, y + 2, buf, fg, b, FONT_NORMAL);
    if (selected) {
        tft_draw_rect(x, y, digits * 6 + 8, 14, THEME_ACCENT);
    }
}

void ui_list_item_draw(int x, int y, int w, const char *label,
                       const char *value, bool selected)
{
    uint16_t bg = selected ? THEME_MENU_SEL : THEME_BG;
    tft_fill_rect(x, y, w, MENU_ITEM_H - 2, bg);
    tft_draw_string(x + 4, y + 6, label, THEME_FG, bg, FONT_NORMAL);
    if (value) {
        int vw = strlen(value) * 6;
        tft_draw_string(x + w - vw - 4, y + 6, value, THEME_ACCENT, bg, FONT_NORMAL);
    }
    if (selected) {
        tft_fill_rect(x, y, 3, MENU_ITEM_H - 2, THEME_ACCENT);
    }
}

void ui_bar_chart(int x, int y, int w, int h,
                  const int *values, int count, int max_val,
                  uint16_t color)
{
    if (count <= 0 || max_val <= 0) return;
    int bar_w = w / count;
    if (bar_w < 2) bar_w = 2;

    tft_fill_rect(x, y, w, h, THEME_BG);
    /* Baseline */
    tft_draw_hline(x, y + h - 1, w, THEME_FG);

    for (int i = 0; i < count && i * bar_w < w; i++) {
        int bar_h = (values[i] * (h - 2)) / max_val;
        if (bar_h > h - 2) bar_h = h - 2;
        if (bar_h > 0) {
            tft_fill_rect(x + i * bar_w + 1, y + h - 1 - bar_h,
                          bar_w - 2, bar_h, color);
        }
    }
}

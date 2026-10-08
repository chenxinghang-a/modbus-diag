#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "app_config.h"
#include "adc_sampler.h"
#include "signal_process.h"
#include "waveform_render.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_running = false;

static void wave_enter(void)
{
    s_need_redraw = true;
    adc_sampler_init();
}

static void wave_exit(void)
{
    if (s_running) {
        adc_sampler_stop();
        s_running = false;
    }
}

static void wave_update(void)
{
    if (!s_need_redraw && !s_running) return;
    s_need_redraw = false;

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Waveform", false, s_running);

    if (s_running) {
        /* 双缓冲领取一帧（2026-10-08 改造：原来是裸指针直读"正在被写"的缓冲，
         * 必然读到撕裂波形）。无新帧则直接返回 —— 保持当前画面，不重绘。
         * raw 用 static：UI 更新单线程且不可重入，避免 2KB 上栈。 */
        static uint16_t raw[WAVE_SAMPLE_DEPTH];
        int depth = 0;
        if (adc_sampler_read_frame(raw, &depth) <= 0) {
            return;
        }

        /* Filter */
        uint16_t filtered[1024];
        signal_filter_sma(raw, filtered, depth, 3);

        /* Render */
        waveform_render_with_grid(10, 24, 300, 160,
                                  filtered, depth,
                                  TFT_COLOR_GREEN, TFT_COLOR_DARKGRAY);

        /* Measurements */
        char buf[48];
        uint16_t vpp = signal_measure_vpp(filtered, depth);
        uint16_t dc = signal_measure_dc(filtered, depth);
        snprintf(buf, sizeof(buf), "Vpp: %d (%.2fV) DC: %d",
                 vpp, vpp * 3.3f / 4095, dc);
        tft_draw_string(10, 190, buf, THEME_FG, THEME_BG, FONT_SMALL);

        int trig = signal_find_trigger(filtered, depth, 2048);
        snprintf(buf, sizeof(buf), "Trigger: %s  TrigPt: %d",
                 trig >= 0 ? "YES" : "NO", trig);
        tft_draw_string(10, 204, buf, THEME_FG, THEME_BG, FONT_SMALL);
    } else {
        tft_draw_string(20, 60, "485 Signal Viewer", THEME_ACCENT, THEME_BG, FONT_LARGE);
        tft_draw_string(20, 90, "ADC on GPIO5", THEME_FG, THEME_BG, FONT_NORMAL);
        tft_draw_string(20, 120, "[OK] Start", THEME_HIGHLIGHT, THEME_BG, FONT_NORMAL);
    }

    tft_draw_string(10, TFT_HEIGHT - 18,
                    s_running ? "[OK] Stop  [BACK] Exit" : "[OK] Start [BACK] Exit",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void wave_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK */
            if (s_running) {
                adc_sampler_stop();
                s_running = false;
            } else {
                adc_sampler_start();
                s_running = true;
            }
            s_need_redraw = true;
            break;
        case 3: /* BACK */
            gui_manager_back();
            break;
    }
}

static void __attribute__((constructor)) register_page(void)
{
    static const page_t page = {
        .id = PAGE_WAVEFORM,
        .on_enter = wave_enter,
        .on_exit = wave_exit,
        .on_update = wave_update,
        .on_key = wave_key,
    };
    gui_register_page(&page);
}

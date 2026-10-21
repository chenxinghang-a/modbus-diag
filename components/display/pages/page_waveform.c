#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "app_config.h"
#include "adc_sampler.h"
#include "signal_process.h"
#include "waveform_render.h"
#include "esp_err.h"
#include "esp_log.h"
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

    /* 双缓冲领取一帧（2026-10-08 改造：原来是裸指针直读"正在被写"的缓冲，
     * 必然读到撕裂波形）。
     *
     * 顺序很重要：**先取帧，再决定要不要重绘**。原实现是先 tft_fill_screen()
     * 再取帧、取不到就 return —— 屏幕已经被清空，只剩状态栏，看起来就是
     * 波形图每隔几帧闪白一次。raw/filtered 都用 static：UI 更新单线程且不可重入，
     * 避免 4KB 上栈。 */
    static uint16_t raw[WAVE_SAMPLE_DEPTH];
    static uint16_t filtered[WAVE_SAMPLE_DEPTH];
    int depth = 0;

    if (s_running) {
        if (adc_sampler_read_frame(raw, &depth) <= 0 && !s_need_redraw) {
            return;     /* 没有新帧：保持当前画面 */
        }
    }

    s_need_redraw = false;
    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Waveform", false, s_running);

    if (s_running && depth > 0) {
        /* Filter */
        signal_filter_sma(raw, filtered, depth, 3);

        /* Render */
        waveform_render_with_grid(10, 24, 300, 160,
                                  filtered, depth,
                                  TFT_COLOR_GREEN, TFT_COLOR_DARKGRAY);

        /* Measurements —— FR5 要求「空闲电压、幅值、边沿时间」，
         * 原来只有 Vpp/DC/触发，空闲电平与边沿时间根本没算。 */
        char buf[64];
        uint16_t vpp = signal_measure_vpp(filtered, depth);
        uint16_t dc = signal_measure_dc(filtered, depth);
        uint16_t idle = signal_measure_idle(filtered, depth);
        int rise_us = signal_measure_rise_time_us(filtered, depth, WAVE_SAMPLE_RATE);

        /* 明确标注是「ADC 端」电压：前级有 1/4 分压，总线上实际电压是它的 4 倍，
         * 不写清楚容易被当成总线电压。 */
        snprintf(buf, sizeof(buf), "Vpp: %d  ADC %.2fV / bus %.1fV",
                 vpp, vpp * 3.3f / 4095, vpp * 3.3f * 4 / 4095);
        tft_draw_string(10, 184, buf, THEME_FG, THEME_BG, FONT_SMALL);

        snprintf(buf, sizeof(buf), "Idle: %d (%.2fV)  DC: %d", idle,
                 idle * 3.3f * 4 / 4095, dc);
        tft_draw_string(10, 196, buf, THEME_FG, THEME_BG, FONT_SMALL);

        if (rise_us >= 0) {
            snprintf(buf, sizeof(buf), "Rise(10-90%%): %d us", rise_us);
        } else {
            snprintf(buf, sizeof(buf), "Rise: -- (no edge)");
        }
        tft_draw_string(10, 208, buf, THEME_FG, THEME_BG, FONT_SMALL);

        int trig = signal_find_trigger(filtered, depth, 2048);
        snprintf(buf, sizeof(buf), "Trig: %s @%d", trig >= 0 ? "YES" : "NO", trig);
        tft_draw_string(180, 208, buf, THEME_FG, THEME_BG, FONT_SMALL);
    } else if (!s_running) {
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
                /* 返回值不能丢：ADC 初始化失败时若照样把 s_running 置 true，
                 * 界面会一直显示 "running" 但永远没有波形（又一个"看着正常"的假象）。 */
                esp_err_t err = adc_sampler_start();
                s_running = (err == ESP_OK);
                if (err != ESP_OK) {
                    ESP_LOGE("wave", "adc_sampler_start failed: %d", (int)err);
                }
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

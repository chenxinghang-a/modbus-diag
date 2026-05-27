#include "gui_manager.h"
#include "tft_driver.h"
#include "ui_widgets.h"
#include "ui_theme.h"
#include "packet_capture.h"
#include "frame_parser.h"
#include "traffic_stats.h"
#include <stdio.h>
#include <string.h>

static bool s_need_redraw = true;
static bool s_capturing = false;
static int s_scroll = 0;

static void capture_enter(void)
{
    s_need_redraw = true;
    s_capturing = false;
    packet_capture_init();
    traffic_stats_init();
}

static void capture_exit(void)
{
    if (s_capturing) {
        packet_capture_stop();
        s_capturing = false;
    }
}

static void capture_update(void)
{
    if (!s_need_redraw && !s_capturing) return;
    s_need_redraw = false;

    if (s_capturing) {
        /* Process captured frames */
        captured_frame_t frame;
        while (packet_capture_get_frame(&frame)) {
            parsed_frame_t parsed;
            if (frame_parser_parse(frame.data, frame.len, &parsed)) {
                traffic_stats_add_frame(frame.crc_valid, parsed.is_exception,
                                        0, frame.len);
            }
        }
    }

    tft_fill_screen(THEME_BG);
    ui_status_bar_draw("Packet Capture", false, s_capturing);

    traffic_stats_t stats;
    traffic_stats_get(&stats);

    char buf[48];
    snprintf(buf, sizeof(buf), "Frames: %lu  Errors: %lu",
             (unsigned long)stats.total_frames, (unsigned long)stats.error_frames);
    tft_draw_string(10, 24, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "ErrRate: %.1f%%  FPS: %lu",
             stats.error_rate * 100.0f, (unsigned long)stats.frames_per_sec);
    tft_draw_string(10, 36, buf, THEME_FG, THEME_BG, FONT_SMALL);

    snprintf(buf, sizeof(buf), "Resp: avg=%ld max=%ld us",
             (long)stats.avg_response_us, (long)stats.max_response_us);
    tft_draw_string(10, 48, buf, THEME_FG, THEME_BG, FONT_SMALL);

    /* Show recent frames */
    int y = 65;
    captured_frame_t frame;
    int shown = 0;
    while (packet_capture_get_frame(&frame) && shown < 8) {
        parsed_frame_t parsed;
        if (frame_parser_parse(frame.data, frame.len, &parsed)) {
            char desc[48];
            frame_parser_to_string(&parsed, desc, sizeof(desc));
            uint16_t color = frame.crc_valid ? THEME_FG : THEME_ERROR;
            tft_draw_string(10, y, desc, color, THEME_BG, FONT_SMALL);
            y += 12;
            shown++;
        }
    }

    tft_draw_string(10, TFT_HEIGHT - 18,
                    s_capturing ? "[OK] Stop  [BACK] Exit" : "[OK] Start [BACK] Exit",
                    THEME_FG, THEME_BG, FONT_SMALL);
}

static void capture_key(uint8_t key)
{
    switch (key) {
        case 2: /* OK - toggle capture */
            if (s_capturing) {
                packet_capture_stop();
                s_capturing = false;
            } else {
                packet_capture_start();
                s_capturing = true;
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
        .id = PAGE_CAPTURE,
        .on_enter = capture_enter,
        .on_exit = capture_exit,
        .on_update = capture_update,
        .on_key = capture_key,
    };
    gui_register_page(&page);
}

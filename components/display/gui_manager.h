#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Page IDs */
typedef enum {
    PAGE_HOME = 0,
    PAGE_RTU_SCAN,
    PAGE_TCP,
    PAGE_CAPTURE,
    PAGE_WAVEFORM,
    PAGE_DIAG,
    PAGE_SETTINGS,
    PAGE_DEVICE_INFO,
    PAGE_MAX
} page_id_t;

/* Page lifecycle callbacks */
typedef struct {
    page_id_t id;
    void (*on_enter)(void);
    void (*on_exit)(void);
    void (*on_update)(void);
    void (*on_key)(uint8_t key);  /* 0=UP, 1=DOWN, 2=OK, 3=BACK */
} page_t;

esp_err_t gui_manager_init(void);

void gui_manager_goto(page_id_t page);
void gui_manager_back(void);
void gui_manager_update(void);

/* Register a page */
void gui_register_page(const page_t *page);

/* Get current page */
page_id_t gui_current_page(void);

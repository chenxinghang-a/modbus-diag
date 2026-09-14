#include "gui_manager.h"
#include "tft_driver.h"
#include "app_config.h"
#include "app_event.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gui";

static const page_t *s_pages[PAGE_MAX] = {0};
static page_id_t s_page_stack[8] = {0};
static int s_stack_top = 0;
static page_id_t s_current = PAGE_HOME;

/* Forward declare page registrations */
extern const page_t page_home;

void gui_register_page(const page_t *page)
{
    if (page && page->id < PAGE_MAX) {
        s_pages[page->id] = page;
    }
}

static void key_event_handler(const app_event_t *evt)
{
    uint8_t key = 0xFF;
    switch (evt->id) {
        case APP_EVT_KEY_UP:    key = 0; break;
        case APP_EVT_KEY_DOWN:  key = 1; break;
        case APP_EVT_KEY_OK:    key = 2; break;
        case APP_EVT_KEY_BACK:  key = 3; break;
        default: return;
    }
    if (s_current < PAGE_MAX && s_pages[s_current] && s_pages[s_current]->on_key) {
        s_pages[s_current]->on_key(key);
    }
}

esp_err_t gui_manager_init(void)
{
    memset(s_pages, 0, sizeof(s_pages));
    s_stack_top = 0;
    s_current = PAGE_HOME;

    /* Register built-in pages */
    gui_register_page(&page_home);
    /* Other pages register themselves via constructor attributes */

    /* Register key handlers */
    app_event_register(APP_EVT_KEY_UP, key_event_handler);
    app_event_register(APP_EVT_KEY_DOWN, key_event_handler);
    app_event_register(APP_EVT_KEY_OK, key_event_handler);
    app_event_register(APP_EVT_KEY_BACK, key_event_handler);

    /* Enter home page */
    if (s_pages[PAGE_HOME] && s_pages[PAGE_HOME]->on_enter) {
        s_pages[PAGE_HOME]->on_enter();
    }

    ESP_LOGI(TAG, "GUI initialized, current page: HOME");
    return ESP_OK;
}

void gui_manager_goto(page_id_t page)
{
    if (page >= PAGE_MAX || page == s_current) return;

    /* Push current to stack, drop oldest if full */
    if (s_stack_top >= 8) {
        ESP_LOGW(TAG, "Page stack overflow, dropping oldest entry");
        for (int i = 0; i < 7; i++) {
            s_page_stack[i] = s_page_stack[i + 1];
        }
        s_stack_top = 7;
    }
    s_page_stack[s_stack_top++] = s_current;

    /* Exit old page */
    if (s_pages[s_current] && s_pages[s_current]->on_exit) {
        s_pages[s_current]->on_exit();
    }

    s_current = page;

    /* Enter new page */
    if (s_pages[s_current] && s_pages[s_current]->on_enter) {
        s_pages[s_current]->on_enter();
    }
}

void gui_manager_back(void)
{
    if (s_stack_top == 0) return; /* Already at root */

    /* Exit current */
    if (s_pages[s_current] && s_pages[s_current]->on_exit) {
        s_pages[s_current]->on_exit();
    }

    s_current = s_page_stack[--s_stack_top];

    /* Re-enter previous */
    if (s_pages[s_current] && s_pages[s_current]->on_enter) {
        s_pages[s_current]->on_enter();
    }
}

void gui_manager_update(void)
{
    if (s_current < PAGE_MAX && s_pages[s_current] && s_pages[s_current]->on_update) {
        s_pages[s_current]->on_update();
    }
}

page_id_t gui_current_page(void)
{
    return s_current;
}

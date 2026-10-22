#include "gui_manager.h"
#include "tft_driver.h"
#include "app_config.h"
#include "app_event.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gui";

/* 显示是否可用：TFT 初始化失败时置 false，GUI 全部空转（见 set_display_available） */
static bool s_display_available = true;

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
        case APP_EVT_KEY_UP:      key = 0; break;
        case APP_EVT_KEY_DOWN:    key = 1; break;
        case APP_EVT_KEY_OK:      key = 2; break;
        case APP_EVT_KEY_BACK:    key = 3; break;
        case APP_EVT_KEY_OK_LONG: key = 4; break;   /* 长按 OK：页面自行决定用不用 */
        default: return;
    }
    /* 没有界面时按键无意义，且部分页面的 on_key 会直接调 tft 绘制 */
    if (!s_display_available) return;
    if (s_current < PAGE_MAX && s_pages[s_current] && s_pages[s_current]->on_key) {
        s_pages[s_current]->on_key(key);
    }
}

void gui_manager_set_display_available(bool available)
{
    s_display_available = available;
    if (!available) {
        ESP_LOGW(TAG, "display unavailable: GUI drawing disabled");
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
    /* key_scan 会在按住 OK 超过 800ms 时抛 APP_EVT_KEY_OK_LONG，但这里一直没注册 ——
     * 事件发出来没人接，长按等于没反应（论文 5.x 却写了「长按用于功能扩展」）。 */
    app_event_register(APP_EVT_KEY_OK_LONG, key_event_handler);

    /* 启动自检：把没注册的页面点出来。构造函数注册是隐式的，
     * 少一个不会有编译错误，只会在现场表现为"点进去没反应"。 */
    {
        int missing = 0;
        for (int i = 0; i < PAGE_MAX; i++) {
            if (!s_pages[i]) {
                ESP_LOGE(TAG, "page %d not registered!", i);
                missing++;
            }
        }
        if (missing == 0) {
            ESP_LOGI(TAG, "all %d pages registered", (int)PAGE_MAX);
        }
    }

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

    /* 目标页没注册就别切：除首页外的页面都靠 __attribute__((constructor)) 自动注册，
     * 一旦某个页面漏注册（或新增了枚举没写页面），切过去会留下上一页的残影、
     * 按键还全无响应 —— 用户会以为死机。宁可不切并打日志。 */
    if (!s_pages[page]) {
        ESP_LOGE(TAG, "goto page %d failed: not registered", (int)page);
        return;
    }

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
    app_event_post(APP_EVT_PAGE_EXIT, (uint32_t)s_current, NULL);

    s_current = page;

    /* Enter new page */
    if (s_pages[s_current] && s_pages[s_current]->on_enter) {
        s_pages[s_current]->on_enter();
    }
    app_event_post(APP_EVT_PAGE_ENTER, (uint32_t)s_current, NULL);
}

void gui_manager_back(void)
{
    if (s_stack_top == 0) return; /* Already at root */

    /* Exit current */
    if (s_pages[s_current] && s_pages[s_current]->on_exit) {
        s_pages[s_current]->on_exit();
    }
    app_event_post(APP_EVT_PAGE_EXIT, (uint32_t)s_current, NULL);

    s_current = s_page_stack[--s_stack_top];

    /* Re-enter previous */
    if (s_pages[s_current] && s_pages[s_current]->on_enter) {
        s_pages[s_current]->on_enter();
    }
    app_event_post(APP_EVT_PAGE_ENTER, (uint32_t)s_current, NULL);
}

void gui_manager_update(void)
{
    if (!s_display_available) return;      /* 屏没初始化成功：不画，也不进页面逻辑 */
    if (s_current < PAGE_MAX && s_pages[s_current] && s_pages[s_current]->on_update) {
        s_pages[s_current]->on_update();
    }
}

page_id_t gui_current_page(void)
{
    return s_current;
}

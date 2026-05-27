#include "key_scan.h"
#include "app_config.h"
#include "app_event.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "key";

#define KEY_COUNT 4

static const gpio_num_t key_pins[KEY_COUNT] = {
    BTN_PIN_UP, BTN_PIN_DOWN, BTN_PIN_OK, BTN_PIN_BACK
};

static const app_event_id_t key_events[KEY_COUNT] = {
    APP_EVT_KEY_UP, APP_EVT_KEY_DOWN, APP_EVT_KEY_OK, APP_EVT_KEY_BACK
};

static const app_event_id_t key_long_events[KEY_COUNT] = {
    APP_EVT_KEY_UP, APP_EVT_KEY_DOWN, APP_EVT_KEY_OK_LONG, APP_EVT_KEY_BACK
};

static void key_scan_task(void *arg)
{
    uint32_t press_time[KEY_COUNT] = {0};
    bool pressed[KEY_COUNT] = {false};
    bool long_fired[KEY_COUNT] = {false};

    while (1) {
        for (int i = 0; i < KEY_COUNT; i++) {
            /* Active low (pulled up, pressed = 0) */
            bool now_pressed = (gpio_get_level(key_pins[i]) == 0);

            if (now_pressed && !pressed[i]) {
                /* Just pressed */
                pressed[i] = true;
                press_time[i] = xTaskGetTickCount();
                long_fired[i] = false;
            } else if (now_pressed && pressed[i]) {
                /* Held */
                if (!long_fired[i]) {
                    TickType_t held = xTaskGetTickCount() - press_time[i];
                    if (held >= pdMS_TO_TICKS(BTN_LONG_PRESS_MS)) {
                        app_event_post(key_long_events[i], 0, NULL);
                        long_fired[i] = true;
                    }
                }
            } else if (!now_pressed && pressed[i]) {
                /* Released */
                pressed[i] = false;
                if (!long_fired[i]) {
                    app_event_post(key_events[i], 0, NULL);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
    }
}

esp_err_t key_scan_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_PIN_UP) | (1ULL << BTN_PIN_DOWN) |
                        (1ULL << BTN_PIN_OK) | (1ULL << BTN_PIN_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    xTaskCreatePinnedToCore(key_scan_task, "key_scan", 2048, NULL, 3, NULL, 1);
    ESP_LOGI(TAG, "Key scan started (UP:%d DN:%d OK:%d BACK:%d)",
             BTN_PIN_UP, BTN_PIN_DOWN, BTN_PIN_OK, BTN_PIN_BACK);
    return ESP_OK;
}

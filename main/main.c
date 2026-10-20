#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_efuse.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "app_config.h"
#include "app_event.h"
#include "tft_driver.h"
#include "gui_manager.h"
#include "key_scan.h"
#include "modbus_master.h"
#include "nvs_config.h"

static const char *TAG = "main";

/* ========== Global Event Queue ========== */
static QueueHandle_t s_event_queue = NULL;
#define EVENT_QUEUE_SIZE 32

/* Max listeners per event */
#define MAX_LISTENERS 4

static app_event_cb_t s_listeners[APP_EVT_MAX][MAX_LISTENERS] = {0};

void app_event_register(app_event_id_t event, app_event_cb_t cb)
{
    if (event >= APP_EVT_MAX || !cb) return;
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (s_listeners[event][i] == NULL) {
            s_listeners[event][i] = cb;
            return;
        }
    }
}

void app_event_unregister(app_event_id_t event, app_event_cb_t cb)
{
    if (event >= APP_EVT_MAX) return;
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (s_listeners[event][i] == cb) {
            s_listeners[event][i] = NULL;
            return;
        }
    }
}

void app_event_post(app_event_id_t event, uint32_t arg, void *data)
{
    if (!s_event_queue) return;
    app_event_t evt = { .id = event, .arg = arg, .data = data };
    xQueueSend(s_event_queue, &evt, pdMS_TO_TICKS(10));
}

void app_event_post_from_isr(app_event_id_t event, uint32_t arg, void *data)
{
    if (!s_event_queue) return;
    app_event_t evt = { .id = event, .arg = arg, .data = data };
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_event_queue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void app_event_process(void)
{
    app_event_t evt;
    while (xQueueReceive(s_event_queue, &evt, 0) == pdTRUE) {
        if (evt.id < APP_EVT_MAX) {
            for (int i = 0; i < MAX_LISTENERS; i++) {
                if (s_listeners[evt.id][i]) {
                    s_listeners[evt.id][i](&evt);
                }
            }
        }
    }
}

/* ========== NVS Init ========== */
static esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/* ========== SPIFFS Init ========== */
static esp_err_t spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ========== UI Task ========== */
static void ui_task(void *arg)
{
    ESP_LOGI(TAG, "UI task started");
    gui_manager_init();

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        app_event_process();
        gui_manager_update();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20)); /* 50fps */
    }
}

/* ========== Modbus Task ========== */
static void modbus_task(void *arg)
{
    ESP_LOGI(TAG, "Modbus task started");
    modbus_master_init();

    /* 载入上次保存的串口参数。原来只在设置页里 set 进 NVS，开机从来不读回来 ——
     * 用户把波特率改成 115200，重启就退回默认值，等于「设置没保存」。
     * 改一个字段前先 modbus_master_get_config() 取整份，别用指定初始化（其余字段会清零）。 */
    modbus_config_t cfg;
    uint32_t v;
    modbus_master_get_config(&cfg);
    nvs_config_get_u32("baud", &v, cfg.baud_rate);
    cfg.baud_rate = v;
    nvs_config_get_u32("parity", &v, (uint32_t)cfg.parity);
    cfg.parity = (uint8_t)v;
    nvs_config_get_u32("timeout_ms", &v, cfg.timeout_ms);
    cfg.timeout_ms = v;
    modbus_master_configure(&cfg);
    ESP_LOGI(TAG, "RS-485 restored: %lu baud, parity %u, timeout %lu ms",
             (unsigned long)cfg.baud_rate, (unsigned)cfg.parity,
             (unsigned long)cfg.timeout_ms);

    while (1) {
        modbus_master_process();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ========== Main Entry ========== */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Modbus Diagnostic Tool ===");
    ESP_LOGI(TAG, "ESP32-S3 Rev %lu", (unsigned long)esp_efuse_get_pkg_ver());

    /* Init event queue */
    s_event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(app_event_t));
    assert(s_event_queue);

    /* Init NVS */
    ESP_ERROR_CHECK(nvs_init());

    /* Init SPIFFS */
    spiffs_init();

    /* Init TFT */
    ESP_ERROR_CHECK(tft_driver_init());

    /* Init key scan */
    ESP_ERROR_CHECK(key_scan_init());

    /* Show splash screen */
    tft_fill_screen(TFT_COLOR_BLACK);
    tft_draw_string(60, 100, "Modbus Diag", TFT_COLOR_WHITE, TFT_COLOR_BLACK, 2);
    tft_draw_string(80, 130, "v1.0.0", TFT_COLOR_CYAN, TFT_COLOR_BLACK, 1);

    /* Start tasks */
    xTaskCreatePinnedToCore(ui_task, "ui_task", TASK_STACK_UI, NULL,
                            TASK_PRIO_UI, NULL, 1);

    xTaskCreatePinnedToCore(modbus_task, "modbus_task", TASK_STACK_MODBUS, NULL,
                            1, NULL, 0);  /* Low priority: placeholder for future request queue */

    ESP_LOGI(TAG, "System initialized");
}

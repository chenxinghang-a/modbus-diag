#include "packet_capture.h"
#include "modbus_rtu.h"
#include "modbus_master.h"
#include "app_config.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "capture";

static QueueHandle_t s_frame_queue = NULL;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;

static void capture_task(void *arg)
{
    uint8_t buf[256];
    captured_frame_t frame;
    ESP_LOGI(TAG, "Capture task started");

    while (s_running) {
        /* Read raw bytes from UART with short timeout */
        int len = uart_read_bytes(RS485_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (len > 0) {
            /* Check CRC */
            bool crc_ok = modbus_rtu_check_crc(buf, len);

            frame.len = len > 256 ? 256 : len;
            memcpy(frame.data, buf, frame.len);
            frame.crc_valid = crc_ok;
            /* 时间戳用开机毫秒。原先是 tv_sec*1000000 塞进 uint32 → 溢出成垃圾值，
             * 所以这个字段一直没人读；现在页面能直接显示「第几毫秒收到的帧」。
             * 用 FreeRTOS tick 换算而不是 esp_timer：本组件没有 esp_timer 依赖，
             * 为一行时间戳去改 CMakeLists 的 REQUIRES 不划算。 */
            frame.uptime_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());

            /* Send to queue (non-blocking, drop if full) */
            xQueueSend(s_frame_queue, &frame, 0);
        }
    }

    ESP_LOGI(TAG, "Capture task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t packet_capture_init(void)
{
    if (s_frame_queue) {
        vQueueDelete(s_frame_queue);
    }
    s_frame_queue = xQueueCreate(CAPTURE_MAX_FRAMES, sizeof(captured_frame_t));
    if (!s_frame_queue) {
        ESP_LOGE(TAG, "Queue create failed");
        return ESP_ERR_NO_MEM;
    }
    s_running = false;
    return ESP_OK;
}

esp_err_t packet_capture_start(void)
{
    if (s_running) return ESP_OK;

    /* 原子占用：原来是「先查再设」两步，扫描任务跑在另一个核上，完全可能在这两步
     * 之间把 UART 抢走 —— 结果是抓包和扫描同时操作同一路串口，帧互相踩、数据全乱。
     * 现在 acquire 内部用自旋锁把读-判-写合成一步。 */
    if (!modbus_master_acquire_uart(APP_UART_MODE_CAPTURE)) {
        ESP_LOGW(TAG, "UART busy with Modbus, cannot start capture");
        return ESP_ERR_INVALID_STATE;
    }

    packet_capture_clear();
    s_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(capture_task, "capture", TASK_STACK_CAPTURE,
                                             NULL, TASK_PRIO_CAPTURE, &s_task_handle, 0);
    if (ret != pdPASS) {
        s_running = false;
        /* Hand the UART back, otherwise Modbus stays refused for good */
        modbus_master_release_uart();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Capture started");
    return ESP_OK;
}

void packet_capture_stop(void)
{
    s_running = false;
    /* Task will clean itself up */
    vTaskDelay(pdMS_TO_TICKS(50));
    /* Release UART back to idle */
    modbus_master_release_uart();
}

bool packet_capture_is_running(void)
{
    return s_running;
}

bool packet_capture_get_frame(captured_frame_t *frame)
{
    if (!s_frame_queue || !frame) return false;
    return xQueueReceive(s_frame_queue, frame, 0) == pdTRUE;
}

int packet_capture_frame_count(void)
{
    if (!s_frame_queue) return 0;
    return uxQueueMessagesWaiting(s_frame_queue);
}

void packet_capture_clear(void)
{
    if (s_frame_queue) {
        xQueueReset(s_frame_queue);
    }
}

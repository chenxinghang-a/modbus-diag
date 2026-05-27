#include "packet_capture.h"
#include "modbus_rtu.h"
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
            frame.is_request = false;  /* can't easily distinguish in passive mode */

            struct timeval tv;
            gettimeofday(&tv, NULL);
            frame.timestamp_us = tv.tv_sec * 1000000 + tv.tv_usec;

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

    /* Install a second UART driver on the same UART for passive listening */
    /* Note: We share the UART with modbus_master. In capture mode,
       we read passively while modbus is not actively sending. */
    packet_capture_clear();
    s_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(capture_task, "capture", TASK_STACK_CAPTURE,
                                             NULL, TASK_PRIO_CAPTURE, &s_task_handle, 0);
    if (ret != pdPASS) {
        s_running = false;
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

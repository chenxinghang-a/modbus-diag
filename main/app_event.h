#pragma once

#include <stdint.h>

/* Application-wide event IDs */
typedef enum {
    APP_EVT_NONE = 0,

    /* Input events */
    APP_EVT_KEY_UP,
    APP_EVT_KEY_DOWN,
    APP_EVT_KEY_OK,
    APP_EVT_KEY_BACK,
    APP_EVT_KEY_OK_LONG,     /* long press on OK */

    /* Modbus events */
    APP_EVT_MODBUS_RX_DONE,
    APP_EVT_MODBUS_TX_DONE,
    APP_EVT_MODBUS_ERROR,
    APP_EVT_MODBUS_TIMEOUT,

    /* Scan events */
    APP_EVT_SCAN_PROGRESS,   /* arg = current address */
    APP_EVT_SCAN_DONE,
    APP_EVT_SCAN_DEVICE_FOUND,

    /* WiFi events */
    APP_EVT_WIFI_CONNECTED,
    APP_EVT_WIFI_DISCONNECTED,
    APP_EVT_WIFI_STA_GOT_IP,

    /* UI events */
    APP_EVT_PAGE_ENTER,
    APP_EVT_PAGE_EXIT,
    APP_EVT_UI_REFRESH,

    /* Capture events */
    APP_EVT_CAPTURE_FRAME,
    APP_EVT_CAPTURE_OVERFLOW,

    APP_EVT_MAX
} app_event_id_t;

/* Event data structure */
typedef struct {
    app_event_id_t id;
    uint32_t       arg;
    void          *data;
} app_event_t;

/* Event callback type */
typedef void (*app_event_cb_t)(const app_event_t *event);

/* Register/unregister event listener */
void app_event_register(app_event_id_t event, app_event_cb_t cb);
void app_event_unregister(app_event_id_t event, app_event_cb_t cb);

/* Post event (ISR safe) */
void app_event_post(app_event_id_t event, uint32_t arg, void *data);
void app_event_post_from_isr(app_event_id_t event, uint32_t arg, void *data);

/* Process events in caller's context */
void app_event_process(void);

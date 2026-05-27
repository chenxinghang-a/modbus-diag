#pragma once

#include "scan_result.h"
#include "esp_err.h"

/* Scan callback: called when a device is found or scan progress updates */
typedef void (*scan_progress_cb_t)(int current_addr, int total, void *user_data);
typedef void (*scan_device_found_cb_t)(const device_info_t *dev, void *user_data);

/* Scan configuration */
typedef struct {
    uint32_t *baud_rates;       /* array of baud rates to try */
    int       baud_count;
    uint8_t   addr_start;       /* start address (1-247) */
    uint8_t   addr_end;         /* end address (1-247) */
    uint32_t  timeout_ms;       /* per-request timeout */
    int       retry_count;      /* retries per address */
    scan_progress_cb_t      on_progress;
    scan_device_found_cb_t  on_device_found;
    void                   *user_data;
} scan_config_t;

/* Start a scan (blocking). Returns scan result. */
esp_err_t device_scan_start(const scan_config_t *cfg, scan_result_t *result);

/* Quick scan: only scan specific baud and address range */
esp_err_t device_scan_quick(uint32_t baud, uint8_t addr_start, uint8_t addr_end,
                            scan_result_t *result);

/* Try to read device ID via MEI (FC43) */
esp_err_t device_read_mei(uint8_t slave, char *id_buf, size_t buf_size);

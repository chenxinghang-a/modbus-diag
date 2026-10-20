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

/* 页面之间的传参：最近一次 scan_result_t（由 device_scan_start 记下指针）与当前选中项。
 * 详情页必须显示「用户真正选的那台」，不能像原来那样把 addr=1/9600 硬编码进去。 */
const scan_result_t *device_scan_last_result(void);
const device_info_t *device_scan_selected_device(void);
void device_scan_select_device(int index);

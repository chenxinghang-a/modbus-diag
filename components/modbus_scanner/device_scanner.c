#include "device_scanner.h"
#include "modbus_master.h"
#include "modbus_common.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "scanner";

static const uint32_t default_baud_rates[] = {9600, 19200, 38400, 57600, 115200};

esp_err_t device_read_mei(uint8_t slave, char *id_buf, size_t buf_size)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_MEI,
        .start_addr = 0x0000,  /* Read Device ID, basic */
        .quantity = 0x03,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return ESP_FAIL;

    /* MEI response: skip first bytes, extract string */
    if (resp.data_len > 6) {
        int str_len = resp.data_len - 6;
        if (str_len >= (int)buf_size) str_len = buf_size - 1;
        memcpy(id_buf, &resp.data[6], str_len);
        id_buf[str_len] = '\0';
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t device_scan_start(const scan_config_t *cfg, scan_result_t *result)
{
    if (!cfg || !result) return ESP_ERR_INVALID_ARG;

    memset(result, 0, sizeof(scan_result_t));
    uint32_t *bauds = (uint32_t *)cfg->baud_rates;
    int baud_cnt = cfg->baud_count;

    if (!bauds || baud_cnt == 0) {
        bauds = (uint32_t *)default_baud_rates;
        baud_cnt = sizeof(default_baud_rates) / sizeof(default_baud_rates[0]);
    }

    int total_addrs = cfg->addr_end - cfg->addr_start + 1;
    int total_steps = baud_cnt * total_addrs;
    int step = 0;

    ESP_LOGI(TAG, "Scanning %d baud rates, addresses %d-%d", baud_cnt, cfg->addr_start, cfg->addr_end);

    for (int b = 0; b < baud_cnt; b++) {
        /* Reconfigure baud rate */
        modbus_config_t mcfg = {
            .baud_rate = bauds[b],
            .timeout_ms = cfg->timeout_ms ? cfg->timeout_ms : 50,
            .retry_count = cfg->retry_count ? cfg->retry_count : 1,
        };
        modbus_master_configure(&mcfg);

        for (uint8_t addr = cfg->addr_start; addr <= cfg->addr_end; addr++) {
            step++;

            if (cfg->on_progress) {
                cfg->on_progress(step, total_steps, cfg->user_data);
            }

            /* Try reading holding register 0, count 1 */
            uint16_t values[1];
            uint16_t reg_count = 0;
            modbus_err_t err = modbus_read_holding_regs(addr, 0, 1, values, &reg_count);

            if (err == MODBUS_OK) {
                if (result->device_count < SCANNER_MAX_DEVICES) {
                    device_info_t *dev = &result->devices[result->device_count];
                    dev->slave_addr = addr;
                    dev->baud_rate = bauds[b];
                    dev->online = true;
                    dev->response_time_us = modbus_master_last_response_time();

                    /* Try MEI */
                    device_read_mei(addr, dev->device_id, sizeof(dev->device_id));

                    result->device_count++;
                    ESP_LOGI(TAG, "Found device: addr=%d baud=%d resp=%ldus id='%s'",
                             addr, (int)bauds[b], (long)dev->response_time_us, dev->device_id);

                    if (cfg->on_device_found) {
                        cfg->on_device_found(dev, cfg->user_data);
                    }
                }
            }
        }
    }

    result->baud_rates_tested = baud_cnt;
    result->addresses_tested = total_addrs;

    /* Restore default baud */
    modbus_config_t def = { .baud_rate = RS485_DEFAULT_BAUD };
    modbus_master_configure(&def);

    ESP_LOGI(TAG, "Scan complete: %d devices found", result->device_count);
    return ESP_OK;
}

esp_err_t device_scan_quick(uint32_t baud, uint8_t addr_start, uint8_t addr_end,
                            scan_result_t *result)
{
    scan_config_t cfg = {
        .baud_rates = &baud,
        .baud_count = 1,
        .addr_start = addr_start,
        .addr_end = addr_end,
        .timeout_ms = 50,
        .retry_count = 1,
    };
    return device_scan_start(&cfg, result);
}

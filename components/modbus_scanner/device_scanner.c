#include "device_scanner.h"
#include "modbus_master.h"
#include "modbus_common.h"
#include "modbus_tcp.h"
#include "app_config.h"
#include "app_event.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "scanner";

static const uint32_t default_baud_rates[] = {9600, 19200, 38400, 57600, 115200};

/* 最近一次扫描结果（指向调用方传进来的 scan_result_t，生命周期由页面持有的静态量保证） */
static const scan_result_t *s_last_result = NULL;
static int s_selected = 0;

const scan_result_t *device_scan_last_result(void)
{
    return s_last_result;
}

const device_info_t *device_scan_selected_device(void)
{
    if (!s_last_result || s_selected < 0 || s_selected >= s_last_result->device_count) {
        return NULL;
    }
    return &s_last_result->devices[s_selected];
}

void device_scan_select_device(int index)
{
    if (!s_last_result) return;
    if (index < 0 || index >= s_last_result->device_count) return;
    s_selected = index;
}

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

/* 扫描取消：现场扫描 247 个地址 × 5 档波特率可能要几分钟，
 * 用户必须能中途退出（否则界面在整个过程中都动不了）。 */
static volatile bool s_cancel = false;

void device_scan_cancel(void)
{
    s_cancel = true;
}

esp_err_t device_scan_start(const scan_config_t *cfg, scan_result_t *result)
{
    if (!cfg || !result) return ESP_ERR_INVALID_ARG;

    memset(result, 0, sizeof(scan_result_t));
    /* 记下「当前这轮结果」给详情页用；选中项复位到第一台 */
    s_last_result = result;
    s_selected = 0;
    uint32_t *bauds = (uint32_t *)cfg->baud_rates;
    int baud_cnt = cfg->baud_count;

    if (!bauds || baud_cnt == 0) {
        bauds = (uint32_t *)default_baud_rates;
        baud_cnt = sizeof(default_baud_rates) / sizeof(default_baud_rates[0]);
    }

    int total_addrs = cfg->addr_end - cfg->addr_start + 1;
    int total_steps = baud_cnt * total_addrs;
    int step = 0;

    /* 走 TCP 时波特率没有意义：只扫一遍地址，别把 5 档波特率各跑一遍（白等 5 倍时间） */
    if (modbus_tcp_is_connected()) {
        baud_cnt = 1;
        total_steps = total_addrs;
    }

    /* 扫描会反复改波特率，扫完必须把用户原本的配置**原样**还回去。
     * 原来这里 restore 成 `{ .baud_rate = RS485_DEFAULT_BAUD }`：
     * ① 用户的波特率丢了；② 指定初始化把 timeout_ms 清零 → 之后的每个请求都立刻超时。 */
    modbus_config_t saved;
    modbus_master_get_config(&saved);

    ESP_LOGI(TAG, "Scanning %d baud rates, addresses %d-%d", baud_cnt, cfg->addr_start, cfg->addr_end);

    s_cancel = false;
    bool aborted = false;

    for (int b = 0; b < baud_cnt && !aborted; b++) {
        /* Reconfigure baud rate（在现有配置上只改三个字段，别把 data/stop/parity 清零） */
        modbus_config_t mcfg = saved;
        mcfg.baud_rate = bauds[b];
        mcfg.timeout_ms = cfg->timeout_ms ? cfg->timeout_ms : 50;
        mcfg.retry_count = cfg->retry_count ? cfg->retry_count : 1;
        modbus_master_configure(&mcfg);

        for (uint8_t addr = cfg->addr_start; addr <= cfg->addr_end; addr++) {
            if (s_cancel) { aborted = true; break; }
            step++;

            if (cfg->on_progress) {
                cfg->on_progress(step, total_steps, cfg->user_data);
            }
            /* 这里原来每扫一个地址就 post 一次 APP_EVT_SCAN_PROGRESS —— 两个问题：
             * ① 全工程没有订阅者，纯属往队列里灌垃圾；
             * ② 事件队列只有 32 格，扫描 247 个地址会把队列瞬间打满，
             *    按键事件被挤掉，扫描期间按 BACK 取消会失灵。
             * 进度本来就靠 on_progress 回调直接写变量给 UI 轮询（要的是最新值，
             * 不是一串积压的历史），所以这里不发事件。 */

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
                    /* 同上：发现设备的通知走 on_device_found 回调，不进事件队列 */
                }
            }
        }
    }

    result->baud_rates_tested = baud_cnt;
    result->addresses_tested = total_addrs;

    app_event_post(APP_EVT_SCAN_DONE, (uint32_t)result->device_count, NULL);

    /* 还原用户原本的串口参数（波特率/超时/重试/校验位原样还回去） */
    modbus_master_configure(&saved);

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

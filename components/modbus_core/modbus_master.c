#include "modbus_master.h"
#include "modbus_rtu.h"
#include "modbus_tcp.h"
#include "app_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "modbus_master";

static modbus_config_t s_cfg = {
    .baud_rate = RS485_DEFAULT_BAUD,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = 0,
    .timeout_ms = MODBUS_TIMEOUT_MS,
    .retry_count = MODBUS_RETRY_COUNT,
};

static int32_t s_last_response_time_us = 0;
static volatile app_uart_mode_t s_uart_mode = APP_UART_MODE_IDLE;
/* 跨核自旋锁：s_uart_mode 会被 Core0 的扫描/抓包任务与 Core1 的 UI 任务同时读写，
 * 普通 volatile 只保证单次读写原子，「读-判-写」组合必须加锁。 */
static portMUX_TYPE s_uart_mux = portMUX_INITIALIZER_UNLOCKED;

/* RS-485 direction control */
static void rs485_tx_enable(void)
{
    gpio_set_level(RS485_PIN_DE, 1);
}

static void rs485_rx_enable(void)
{
    gpio_set_level(RS485_PIN_DE, 0);
}

void modbus_master_get_config(modbus_config_t *out)
{
    if (out) *out = s_cfg;
}

esp_err_t modbus_master_configure(const modbus_config_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
    }

    /* Reconfigure UART with new baud rate */
    uart_config_t uart_cfg = {
        .baud_rate = s_cfg.baud_rate,
        .data_bits = s_cfg.data_bits == 7 ? UART_DATA_7_BITS : UART_DATA_8_BITS,
        .parity = s_cfg.parity == 1 ? UART_PARITY_ODD :
                  s_cfg.parity == 2 ? UART_PARITY_EVEN : UART_PARITY_DISABLE,
        .stop_bits = s_cfg.stop_bits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(RS485_UART_NUM, &uart_cfg);
    if (ret != ESP_OK) return ret;

    /* 同一套超时也要喂给 TCP 路径：否则 TCP 模式一直用连接时写死的 2 秒，
     * 扫描时每个空地址都要等满 2 秒。 */
    modbus_tcp_set_timeout(s_cfg.timeout_ms);

    ESP_LOGI(TAG, "Configured: %lu baud, %d%c%d",
             (unsigned long)s_cfg.baud_rate, s_cfg.data_bits,
             s_cfg.parity == 1 ? 'O' : s_cfg.parity == 2 ? 'E' : 'N',
             s_cfg.stop_bits);
    return ESP_OK;
}

esp_err_t modbus_master_init(void)
{
    /* Configure DE/RE GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RS485_PIN_DE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    rs485_rx_enable();  /* Default to receive */

    /* Install UART driver */
    esp_err_t ret = uart_driver_install(RS485_UART_NUM, RS485_BUF_SIZE, RS485_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set UART pins */
    ret = uart_set_pin(RS485_UART_NUM, RS485_PIN_TX, RS485_PIN_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Apply default config */
    return modbus_master_configure(&s_cfg);
}

void modbus_master_process(void)
{
    /* Placeholder for future request queue processing.
       Currently, Modbus operations are invoked directly via
       modbus_master_send_recv() from the UI task. A proper
       request queue would serialize all I/O on core 0. */
    vTaskDelay(pdMS_TO_TICKS(100));
}

app_uart_mode_t modbus_master_get_uart_mode(void)
{
    return s_uart_mode;
}

void modbus_master_set_uart_mode(app_uart_mode_t mode)
{
    taskENTER_CRITICAL(&s_uart_mux);
    s_uart_mode = mode;
    taskEXIT_CRITICAL(&s_uart_mux);
}

bool modbus_master_acquire_uart(app_uart_mode_t mode)
{
    if (mode != APP_UART_MODE_MODBUS && mode != APP_UART_MODE_CAPTURE) return false;

    taskENTER_CRITICAL(&s_uart_mux);
    bool ok = (s_uart_mode == APP_UART_MODE_IDLE);
    if (ok) s_uart_mode = mode;
    taskEXIT_CRITICAL(&s_uart_mux);
    return ok;
}

void modbus_master_release_uart(void)
{
    taskENTER_CRITICAL(&s_uart_mux);
    if (s_uart_mode != APP_UART_MODE_IDLE) s_uart_mode = APP_UART_MODE_IDLE;
    taskEXIT_CRITICAL(&s_uart_mux);
}

/* 一次 UART 事务（建帧→发送→等响应→解析）。不含重试与 UART 模式管理，
 * 由 modbus_master_send_recv() 在外层做。 */
static modbus_err_t uart_transaction(const modbus_request_t *req, modbus_response_t *resp)
{
    uint8_t tx_buf[256];
    uint8_t rx_buf[256];

    int tx_len = modbus_rtu_build_request(req, tx_buf, sizeof(tx_buf));
    if (tx_len < 0) {
        ESP_LOGE(TAG, "Build request failed");
        return MODBUS_ERR_INVALID_PARAM;
    }

    /* Flush RX buffer */
    uart_flush_input(RS485_UART_NUM);

    /* Enable TX */
    rs485_tx_enable();
    esp_rom_delay_us(10);  /* Small delay for direction switch */

    /* Send frame */
    int written = uart_write_bytes(RS485_UART_NUM, tx_buf, tx_len);
    if (written != tx_len) {
        rs485_rx_enable();
        return MODBUS_ERR_IO;
    }

    /* Wait for TX complete */
    uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(100));

    /* Switch to RX */
    rs485_rx_enable();

    /* Record send time */
    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);

    /* Wait for response */
    int rx_len = uart_read_bytes(RS485_UART_NUM, rx_buf, sizeof(rx_buf),
                                 pdMS_TO_TICKS(s_cfg.timeout_ms));

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);
    s_last_response_time_us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 +
                               (tv_end.tv_usec - tv_start.tv_usec);
    resp->response_time_us = s_last_response_time_us;

    if (rx_len <= 0) {
        ESP_LOGD(TAG, "Timeout (addr=%d fc=0x%02X)", req->slave_addr, req->function_code);
        return MODBUS_ERR_TIMEOUT;
    }

    return modbus_rtu_parse_response(rx_buf, rx_len, resp);
}

modbus_err_t modbus_master_send_recv(const modbus_request_t *req, modbus_response_t *resp)
{
    if (!req || !resp) return MODBUS_ERR_INVALID_PARAM;

    /* TCP 已连接时优先走网络：这样设备扫描、寄存器分类、读写全都自动支持 TCP，
     * 而不是只有 TCP 页那一次「测试读」能用。两条路径的请求/响应结构体是同一套，
     * 所以这里只是换传输层，上层代码一行都不用改。 */
    if (modbus_tcp_is_connected()) {
        modbus_err_t err = modbus_tcp_send_recv(req, resp);
        if (resp->response_time_us > 0) s_last_response_time_us = resp->response_time_us;
        return err;
    }

    /* 占用 UART：失败说明抓包任务正在用（或上一次没释放干净） */
    if (!modbus_master_acquire_uart(APP_UART_MODE_MODBUS)) {
        ESP_LOGW(TAG, "UART busy (capture mode)");
        return MODBUS_ERR_IO;
    }

    /* 重试：配置里的 retry_count 以前是个死字段（只在默认值里出现，没人读），
     * RS-485 现场干扰大，超时/CRC 错必须能重试。只重试这两类 ——
     * 异常响应是从站正常回答，重试没有意义。 */
    int attempts = s_cfg.retry_count + 1;
    if (attempts < 1) attempts = 1;
    if (attempts > 5) attempts = 5;      /* 配置写坏了别把自己卡死 */

    modbus_err_t err = MODBUS_ERR_TIMEOUT;
    for (int i = 0; i < attempts; i++) {
        err = uart_transaction(req, resp);
        if (err != MODBUS_ERR_TIMEOUT && err != MODBUS_ERR_CRC) break;
        if (i + 1 < attempts) {
            ESP_LOGD(TAG, "retry %d/%d (addr=%d, err=%d)",
                     i + 1, attempts - 1, req->slave_addr, (int)err);
        }
    }

    modbus_master_release_uart();
    return err;
}

/* ========== High-level API ========== */

modbus_err_t modbus_read_coils(uint8_t slave, uint16_t addr, uint16_t count,
                               uint8_t *coil_data, uint16_t *coil_count)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_READ_COILS,
        .start_addr = addr,
        .quantity = count,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    /* 只要给了输出缓冲就填，count 是可选的回填 —— 原来写成 `if (coil_data && coil_count)`，
     * 调用方传 NULL 想「只要数据不要计数」时会静默拿不到任何数据。 */
    if (coil_data) {
        if (coil_count) *coil_count = count;
        int bytes = (count + 7) / 8;
        if (bytes > resp.data_len) bytes = resp.data_len;
        memcpy(coil_data, resp.data, bytes);
    }
    return MODBUS_OK;
}

modbus_err_t modbus_read_discrete_inputs(uint8_t slave, uint16_t addr, uint16_t count,
                                         uint8_t *di_data, uint16_t *di_count)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_READ_DISC_INPUTS,
        .start_addr = addr,
        .quantity = count,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    if (di_data && di_count) {
        *di_count = count;
        int bytes = (count + 7) / 8;
        if (bytes > resp.data_len) bytes = resp.data_len;
        memcpy(di_data, resp.data, bytes);
    }
    return MODBUS_OK;
}

modbus_err_t modbus_read_holding_regs(uint8_t slave, uint16_t addr, uint16_t count,
                                      uint16_t *values, uint16_t *reg_count)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_READ_HOLD_REGS,
        .start_addr = addr,
        .quantity = count,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    /* 同 read_coils 的坑：给了 values 就填，reg_count 只做可选回填 */
    if (values) {
        if (reg_count) *reg_count = count;
        int n = resp.data_len / 2;
        if (n > count) n = count;
        for (int i = 0; i < n; i++) {
            values[i] = (resp.data[i * 2] << 8) | resp.data[i * 2 + 1];
        }
    }
    return MODBUS_OK;
}

modbus_err_t modbus_read_input_regs(uint8_t slave, uint16_t addr, uint16_t count,
                                    uint16_t *values, uint16_t *reg_count)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_READ_INPUT_REGS,
        .start_addr = addr,
        .quantity = count,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    /* 同 read_coils 的坑：给了 values 就填，reg_count 只做可选回填 */
    if (values) {
        if (reg_count) *reg_count = count;
        int n = resp.data_len / 2;
        if (n > count) n = count;
        for (int i = 0; i < n; i++) {
            values[i] = (resp.data[i * 2] << 8) | resp.data[i * 2 + 1];
        }
    }
    return MODBUS_OK;
}

modbus_err_t modbus_write_single_coil(uint8_t slave, uint16_t addr, bool value)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_WRITE_SINGLE_COIL,
        .start_addr = addr,
        .write_value = value ? 0xFF00 : 0x0000,
    };
    modbus_response_t resp = {0};
    return modbus_master_send_recv(&req, &resp);
}

modbus_err_t modbus_write_single_reg(uint8_t slave, uint16_t addr, uint16_t value)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_WRITE_SINGLE_REG,
        .start_addr = addr,
        .write_value = value,
    };
    modbus_response_t resp = {0};
    return modbus_master_send_recv(&req, &resp);
}

/* FC16：连续写多个保持寄存器。
 * 帧构造在 modbus_rtu_build_request 里早就实现了（write_values），但一直没有高层封装，
 * 所以界面上只能一个寄存器一个寄存器写 —— 论文 FR1 要求的 0x10 实际上用不上。
 * 响应需回显「起始地址 + 数量」，这里校验一次，防止从站回了别的东西。 */
modbus_err_t modbus_write_multiple_regs(uint8_t slave, uint16_t addr,
                                        const uint16_t *values, uint16_t count)
{
    if (!values || count == 0 || count > 16) return MODBUS_ERR_INVALID_PARAM;

    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_WRITE_MULTI_REGS,
        .start_addr = addr,
        .quantity = count,
        .write_values = (uint16_t *)values,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    /* 响应 PDU：FC(1) + 起始地址(2) + 数量(2) */
    if (resp.data_len < 4) return MODBUS_ERR_INVALID_RESPONSE;
    uint16_t r_addr = (uint16_t)((resp.data[0] << 8) | resp.data[1]);
    uint16_t r_qty  = (uint16_t)((resp.data[2] << 8) | resp.data[3]);
    if (r_addr != addr || r_qty != count) {
        ESP_LOGW(TAG, "FC16 echo mismatch: expect %u/%u, got %u/%u",
                 (unsigned)addr, (unsigned)count, (unsigned)r_addr, (unsigned)r_qty);
        return MODBUS_ERR_INVALID_RESPONSE;
    }
    return MODBUS_OK;
}

/* FC15：连续写多个线圈。coil_bytes 每字节 8 个线圈，低位在前。 */
modbus_err_t modbus_write_multiple_coils(uint8_t slave, uint16_t addr,
                                         const uint8_t *coil_bytes, uint16_t count)
{
    if (!coil_bytes || count == 0 || count > 64) return MODBUS_ERR_INVALID_PARAM;

    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_WRITE_MULTI_COILS,
        .start_addr = addr,
        .quantity = count,
        .write_coils = (uint8_t *)coil_bytes,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    if (resp.data_len < 4) return MODBUS_ERR_INVALID_RESPONSE;
    uint16_t r_addr = (uint16_t)((resp.data[0] << 8) | resp.data[1]);
    uint16_t r_qty  = (uint16_t)((resp.data[2] << 8) | resp.data[3]);
    if (r_addr != addr || r_qty != count) return MODBUS_ERR_INVALID_RESPONSE;
    return MODBUS_OK;
}

modbus_err_t modbus_write_multi_regs(uint8_t slave, uint16_t addr, uint16_t count,
                                     const uint16_t *values)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_WRITE_MULTI_REGS,
        .start_addr = addr,
        .quantity = count,
        .write_values = (uint16_t *)values,
    };
    modbus_response_t resp = {0};
    return modbus_master_send_recv(&req, &resp);
}

modbus_err_t modbus_write_multi_coils(uint8_t slave, uint16_t addr, uint16_t count,
                                      const uint8_t *coil_data)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_WRITE_MULTI_COILS,
        .start_addr = addr,
        .quantity = count,
        .write_coils = (uint8_t *)coil_data,
    };
    modbus_response_t resp = {0};
    return modbus_master_send_recv(&req, &resp);
}

modbus_err_t modbus_diagnostics(uint8_t slave, uint16_t sub_func, uint16_t data,
                                uint16_t *response)
{
    modbus_request_t req = {
        .slave_addr = slave,
        .function_code = MODBUS_FC_DIAGNOSTICS,
        .start_addr = sub_func,
        .write_value = data,
    };
    modbus_response_t resp = {0};
    modbus_err_t err = modbus_master_send_recv(&req, &resp);
    if (err != MODBUS_OK) return err;

    if (response && resp.data_len >= 2) {
        *response = (resp.data[0] << 8) | resp.data[1];
    }
    return MODBUS_OK;
}

int32_t modbus_master_last_response_time(void)
{
    return s_last_response_time_us;
}

#pragma once

#include "modbus_common.h"
#include "app_config.h"
#include "esp_err.h"

esp_err_t modbus_master_init(void);
void modbus_master_process(void);

/* UART mode management (shared between Modbus and Capture) */
app_uart_mode_t modbus_master_get_uart_mode(void);
void modbus_master_set_uart_mode(app_uart_mode_t mode);

/* Configure RS-485 parameters */
esp_err_t modbus_master_configure(const modbus_config_t *cfg);

/* Read back the configuration currently in effect.
 * 用途：只想改一个字段时先取回整份再改，别用 `{ .baud_rate = x }` 初始化 ——
 * C 的指定初始化会把其余字段**全部清零**，timeout_ms 变 0 就等于「立刻超时」。 */
void modbus_master_get_config(modbus_config_t *out);

/* Send request and wait for response (blocking) */
modbus_err_t modbus_master_send_recv(const modbus_request_t *req, modbus_response_t *resp);

/* High-level read functions */
modbus_err_t modbus_read_coils(uint8_t slave, uint16_t addr, uint16_t count,
                               uint8_t *coil_data, uint16_t *coil_count);
modbus_err_t modbus_read_discrete_inputs(uint8_t slave, uint16_t addr, uint16_t count,
                                         uint8_t *di_data, uint16_t *di_count);
modbus_err_t modbus_read_holding_regs(uint8_t slave, uint16_t addr, uint16_t count,
                                      uint16_t *values, uint16_t *reg_count);
modbus_err_t modbus_read_input_regs(uint8_t slave, uint16_t addr, uint16_t count,
                                    uint16_t *values, uint16_t *reg_count);

/* High-level write functions */
modbus_err_t modbus_write_single_coil(uint8_t slave, uint16_t addr, bool value);
modbus_err_t modbus_write_single_reg(uint8_t slave, uint16_t addr, uint16_t value);
modbus_err_t modbus_write_multi_regs(uint8_t slave, uint16_t addr, uint16_t count,
                                     const uint16_t *values);
modbus_err_t modbus_write_multi_coils(uint8_t slave, uint16_t addr, uint16_t count,
                                      const uint8_t *coil_data);

/* Diagnostics */
modbus_err_t modbus_diagnostics(uint8_t slave, uint16_t sub_func, uint16_t data,
                                uint16_t *response);

/* Get last response time in microseconds */
int32_t modbus_master_last_response_time(void);

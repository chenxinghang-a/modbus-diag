#pragma once

#include "modbus_common.h"
#include "esp_err.h"

esp_err_t modbus_master_init(void);
void modbus_master_process(void);

/* Configure RS-485 parameters */
esp_err_t modbus_master_configure(const modbus_config_t *cfg);

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

#pragma once

#include "modbus_common.h"

/* CRC16/Modbus calculation */
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);

/* Build RTU request frame. Returns frame length, or <0 on error. */
int modbus_rtu_build_request(const modbus_request_t *req, uint8_t *buf, size_t buf_size);

/* Parse RTU response. Returns MODBUS_OK or error code. */
modbus_err_t modbus_rtu_parse_response(const uint8_t *frame, uint16_t frame_len,
                                       modbus_response_t *resp);

/* Validate RTU frame CRC */
bool modbus_rtu_check_crc(const uint8_t *frame, uint16_t frame_len);

/* Get T3.5 silence time in microseconds for given baud rate */
uint32_t modbus_rtu_t35_us(uint32_t baud_rate);

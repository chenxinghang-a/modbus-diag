#pragma once

#include "modbus_common.h"
#include "esp_err.h"

/* Initialize Modbus TCP client */
esp_err_t modbus_tcp_init(void);

/* Connect to Modbus TCP server */
esp_err_t modbus_tcp_connect(const char *ip, uint16_t port);

/* Disconnect */
void modbus_tcp_disconnect(void);

/* Check if connected */
bool modbus_tcp_is_connected(void);

/* Send request via TCP and get response */
modbus_err_t modbus_tcp_send_recv(const modbus_request_t *req, modbus_response_t *resp);

/* Build MBAP+PDU frame. Returns frame length. */
int modbus_tcp_build_frame(uint16_t trans_id, const modbus_request_t *req,
                           uint8_t *buf, size_t buf_size);

/* Parse TCP response frame */
modbus_err_t modbus_tcp_parse_frame(const uint8_t *frame, uint16_t frame_len,
                                    modbus_response_t *resp);

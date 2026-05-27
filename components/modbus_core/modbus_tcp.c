#include "modbus_tcp.h"
#include "modbus_rtu.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "modbus_tcp";

#define MBAP_HEADER_LEN 7

static int s_sock = -1;
static uint16_t s_trans_id = 0;

esp_err_t modbus_tcp_init(void)
{
    s_sock = -1;
    s_trans_id = 0;
    return ESP_OK;
}

esp_err_t modbus_tcp_connect(const char *ip, uint16_t port)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    s_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Socket create failed");
        return ESP_FAIL;
    }

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1) {
        close(s_sock);
        s_sock = -1;
        return ESP_ERR_INVALID_ARG;
    }

    /* Set timeout */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(s_sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGE(TAG, "Connect to %s:%d failed", ip, port);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connected to %s:%d", ip, port);
    return ESP_OK;
}

void modbus_tcp_disconnect(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}

bool modbus_tcp_is_connected(void)
{
    return s_sock >= 0;
}

int modbus_tcp_build_frame(uint16_t trans_id, const modbus_request_t *req,
                           uint8_t *buf, size_t buf_size)
{
    if (!req || !buf || buf_size < MBAP_HEADER_LEN + 5) return -1;

    int pdu_len = 0;
    uint8_t pdu[256];

    pdu[pdu_len++] = req->slave_addr;  /* Unit ID */
    pdu[pdu_len++] = req->function_code;

    switch (req->function_code) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISC_INPUTS:
        case MODBUS_FC_READ_HOLD_REGS:
        case MODBUS_FC_READ_INPUT_REGS:
            pdu[pdu_len++] = (req->start_addr >> 8) & 0xFF;
            pdu[pdu_len++] = req->start_addr & 0xFF;
            pdu[pdu_len++] = (req->quantity >> 8) & 0xFF;
            pdu[pdu_len++] = req->quantity & 0xFF;
            break;

        case MODBUS_FC_WRITE_SINGLE_COIL:
            pdu[pdu_len++] = (req->start_addr >> 8) & 0xFF;
            pdu[pdu_len++] = req->start_addr & 0xFF;
            pdu[pdu_len++] = req->write_value ? 0xFF : 0x00;
            pdu[pdu_len++] = 0x00;
            break;

        case MODBUS_FC_WRITE_SINGLE_REG:
            pdu[pdu_len++] = (req->start_addr >> 8) & 0xFF;
            pdu[pdu_len++] = req->start_addr & 0xFF;
            pdu[pdu_len++] = (req->write_value >> 8) & 0xFF;
            pdu[pdu_len++] = req->write_value & 0xFF;
            break;

        case MODBUS_FC_WRITE_MULTI_REGS:
            if (!req->write_values) return -1;
            pdu[pdu_len++] = (req->start_addr >> 8) & 0xFF;
            pdu[pdu_len++] = req->start_addr & 0xFF;
            pdu[pdu_len++] = (req->quantity >> 8) & 0xFF;
            pdu[pdu_len++] = req->quantity & 0xFF;
            pdu[pdu_len++] = req->quantity * 2;
            for (int i = 0; i < req->quantity; i++) {
                pdu[pdu_len++] = (req->write_values[i] >> 8) & 0xFF;
                pdu[pdu_len++] = req->write_values[i] & 0xFF;
            }
            break;

        default:
            return -1;
    }

    /* MBAP Header */
    int pos = 0;
    buf[pos++] = (trans_id >> 8) & 0xFF;
    buf[pos++] = trans_id & 0xFF;
    buf[pos++] = 0x00;  /* Protocol ID: Modbus */
    buf[pos++] = 0x00;
    buf[pos++] = (pdu_len >> 8) & 0xFF;
    buf[pos++] = pdu_len & 0xFF;

    /* PDU (Unit ID is included in pdu[0]) */
    memcpy(&buf[pos], pdu, pdu_len);
    pos += pdu_len;

    return pos;
}

modbus_err_t modbus_tcp_parse_frame(const uint8_t *frame, uint16_t frame_len,
                                    modbus_response_t *resp)
{
    if (!frame || !resp || frame_len < MBAP_HEADER_LEN + 1) {
        return MODBUS_ERR_INVALID_RESPONSE;
    }

    /* Parse MBAP header */
    uint16_t trans_id = (frame[0] << 8) | frame[1];
    uint16_t proto_id = (frame[2] << 8) | frame[3];
    uint16_t pdu_len = (frame[4] << 8) | frame[5];

    if (proto_id != 0x0000) return MODBUS_ERR_INVALID_RESPONSE;
    if (pdu_len > frame_len - 6) return MODBUS_ERR_INVALID_RESPONSE;

    const uint8_t *pdu = &frame[6];
    resp->slave_addr = pdu[0];
    resp->function_code = pdu[1];

    if (modbus_is_exception(resp->function_code)) {
        resp->exception_code = pdu[2];
        return MODBUS_ERR_EXCEPTION;
    }

    /* Same PDU parsing as RTU (without CRC) */
    switch (resp->function_code) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISC_INPUTS:
        case MODBUS_FC_READ_HOLD_REGS:
        case MODBUS_FC_READ_INPUT_REGS:
            resp->byte_count = pdu[2];
            resp->data_len = resp->byte_count;
            if (resp->data_len > sizeof(resp->data)) resp->data_len = sizeof(resp->data);
            memcpy(resp->data, &pdu[3], resp->data_len);
            break;

        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REG:
        case MODBUS_FC_WRITE_MULTI_COILS:
        case MODBUS_FC_WRITE_MULTI_REGS:
            memcpy(resp->data, &pdu[2], 4);
            resp->data_len = 4;
            break;

        default:
            resp->data_len = pdu_len - 2;
            if (resp->data_len > sizeof(resp->data)) resp->data_len = sizeof(resp->data);
            memcpy(resp->data, &pdu[2], resp->data_len);
            break;
    }

    return MODBUS_OK;
}

modbus_err_t modbus_tcp_send_recv(const modbus_request_t *req, modbus_response_t *resp)
{
    if (s_sock < 0) return MODBUS_ERR_IO;
    if (!req || !resp) return MODBUS_ERR_INVALID_PARAM;

    uint8_t tx_buf[256], rx_buf[256];
    uint16_t tid = s_trans_id++;

    int tx_len = modbus_tcp_build_frame(tid, req, tx_buf, sizeof(tx_buf));
    if (tx_len < 0) return MODBUS_ERR_INVALID_PARAM;

    struct timeval tv_start, tv_end;
    gettimeofday(&tv_start, NULL);

    int sent = send(s_sock, tx_buf, tx_len, 0);
    if (sent != tx_len) return MODBUS_ERR_IO;

    int rx_len = recv(s_sock, rx_buf, sizeof(rx_buf), 0);
    gettimeofday(&tv_end, NULL);

    resp->response_time_us = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 +
                              (tv_end.tv_usec - tv_start.tv_usec);

    if (rx_len <= 0) return MODBUS_ERR_TIMEOUT;

    return modbus_tcp_parse_frame(rx_buf, rx_len, resp);
}

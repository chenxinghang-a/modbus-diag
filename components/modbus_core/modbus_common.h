#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========== Modbus Function Codes ========== */
#define MODBUS_FC_READ_COILS            0x01
#define MODBUS_FC_READ_DISC_INPUTS      0x02
#define MODBUS_FC_READ_HOLD_REGS        0x03
#define MODBUS_FC_READ_INPUT_REGS       0x04
#define MODBUS_FC_WRITE_SINGLE_COIL     0x05
#define MODBUS_FC_WRITE_SINGLE_REG      0x06
#define MODBUS_FC_READ_EXCEPTION        0x07
#define MODBUS_FC_DIAGNOSTICS           0x08
#define MODBUS_FC_WRITE_MULTI_COILS     0x0F
#define MODBUS_FC_WRITE_MULTI_REGS      0x10
#define MODBUS_FC_REPORT_SERVER_ID      0x11
#define MODBUS_FC_READ_FILE_RECORD      0x14
#define MODBUS_FC_WRITE_FILE_RECORD     0x15
#define MODBUS_FC_MASK_WRITE_REG        0x16
#define MODBUS_FC_READ_WRITE_MULTI      0x17
#define MODBUS_FC_READ_FIFO             0x18
#define MODBUS_FC_MEI                   0x2B  /* MEI Read Device ID */

/* ========== Modbus Exception Codes ========== */
#define MODBUS_EX_NONE                  0x00
#define MODBUS_EX_ILLEGAL_FUNCTION      0x01
#define MODBUS_EX_ILLEGAL_DATA_ADDR     0x02
#define MODBUS_EX_ILLEGAL_DATA_VALUE    0x03
#define MODBUS_EX_SLAVE_DEVICE_FAILURE  0x04
#define MODBUS_EX_ACK                   0x05
#define MODBUS_EX_SLAVE_BUSY            0x06
#define MODBUS_EX_MEMORY_PARITY         0x08
#define MODBUS_EX_GATEWAY_PATH          0x0A
#define MODBUS_EX_GATEWAY_TARGET        0x0B

/* ========== Error Codes (internal) ========== */
typedef enum {
    MODBUS_OK = 0,
    MODBUS_ERR_TIMEOUT,
    MODBUS_ERR_CRC,
    MODBUS_ERR_EXCEPTION,
    MODBUS_ERR_INVALID_RESPONSE,
    MODBUS_ERR_BUFFER_TOO_SMALL,
    MODBUS_ERR_IO,
    MODBUS_ERR_INVALID_PARAM,
} modbus_err_t;

/* ========== Frame Type ========== */
typedef enum {
    MODBUS_FRAME_RTU = 0,
    MODBUS_FRAME_TCP,
    MODBUS_FRAME_ASCII,
} modbus_frame_type_t;

/* ========== Register Type ========== */
typedef enum {
    REG_TYPE_COIL = 0,
    REG_TYPE_DISC_INPUT,
    REG_TYPE_HOLDING,
    REG_TYPE_INPUT,
} reg_type_t;

/* ========== Request/Response Structure ========== */
typedef struct {
    uint8_t  slave_addr;
    uint8_t  function_code;
    uint16_t start_addr;
    uint16_t quantity;
    uint16_t write_value;        /* single write */
    uint16_t *write_values;      /* multi write */
    uint8_t  *write_coils;       /* coil bit array */
    uint8_t  byte_count;
} modbus_request_t;

typedef struct {
    uint8_t  slave_addr;
    uint8_t  function_code;
    uint8_t  byte_count;
    uint8_t  data[256];          /* response data */
    uint16_t data_len;
    uint8_t  exception_code;     /* 0 = no exception */
    modbus_err_t error;
    int32_t  response_time_us;   /* response time in microseconds */
} modbus_response_t;

/* ========== Configuration ========== */
typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;     /* 8 */
    uint8_t  stop_bits;     /* 1 or 2 */
    uint8_t  parity;        /* 0=none, 1=odd, 2=even */
    uint32_t timeout_ms;
    uint8_t  retry_count;
} modbus_config_t;

/* ========== Utility ========== */
static inline bool modbus_is_exception(uint8_t fc)
{
    return (fc & 0x80) != 0;
}

static inline const char *modbus_fc_name(uint8_t fc)
{
    switch (fc) {
        case MODBUS_FC_READ_COILS:        return "RdCoils";
        case MODBUS_FC_READ_DISC_INPUTS:  return "RdDiscIn";
        case MODBUS_FC_READ_HOLD_REGS:    return "RdHoldReg";
        case MODBUS_FC_READ_INPUT_REGS:   return "RdInputReg";
        case MODBUS_FC_WRITE_SINGLE_COIL: return "WrCoil";
        case MODBUS_FC_WRITE_SINGLE_REG:  return "WrReg";
        case MODBUS_FC_WRITE_MULTI_COILS: return "WrMultiCoil";
        case MODBUS_FC_WRITE_MULTI_REGS:  return "WrMultiReg";
        case MODBUS_FC_DIAGNOSTICS:       return "Diag";
        case MODBUS_FC_MEI:               return "MEI";
        default: return "Unknown";
    }
}

static inline const char *modbus_exception_name(uint8_t ex)
{
    switch (ex) {
        case MODBUS_EX_ILLEGAL_FUNCTION:    return "Illegal Func";
        case MODBUS_EX_ILLEGAL_DATA_ADDR:   return "Illegal Addr";
        case MODBUS_EX_ILLEGAL_DATA_VALUE:  return "Illegal Value";
        case MODBUS_EX_SLAVE_DEVICE_FAILURE:return "Device Fail";
        case MODBUS_EX_SLAVE_BUSY:          return "Slave Busy";
        case MODBUS_EX_MEMORY_PARITY:       return "Mem Parity";
        case MODBUS_EX_GATEWAY_PATH:        return "GW Path";
        case MODBUS_EX_GATEWAY_TARGET:      return "GW Target";
        default: return "Unknown";
    }
}

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "modbus_common.h"

#define SCANNER_MAX_DEVICES     32
#define SCANNER_MAX_REG_GROUPS  8

/* Register group info */
typedef struct {
    uint16_t start_addr;
    uint16_t end_addr;
    uint8_t  reg_type;       /* reg_type_t */
    bool     writable;
} reg_group_t;

/* Device info */
typedef struct {
    uint8_t  slave_addr;
    uint32_t baud_rate;
    bool     online;
    int32_t  response_time_us;
    int      reg_group_count;
    reg_group_t reg_groups[SCANNER_MAX_REG_GROUPS];
    char     device_id[64];  /* from MEI if available */
} device_info_t;

/* Scan result */
typedef struct {
    int device_count;
    device_info_t devices[SCANNER_MAX_DEVICES];
    uint32_t scan_duration_ms;
    int baud_rates_tested;
    int addresses_tested;
} scan_result_t;

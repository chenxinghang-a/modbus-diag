#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Parsed frame info */
typedef struct {
    uint8_t  slave_addr;
    uint8_t  function_code;
    uint16_t start_addr;
    uint16_t quantity_or_value;
    bool     is_exception;
    uint8_t  exception_code;
    bool     crc_valid;
    const char *fc_name;
} parsed_frame_t;

/* Parse a raw RTU frame into structured info */
bool frame_parser_parse(const uint8_t *data, uint16_t len, parsed_frame_t *out);

/* Get human-readable description of a parsed frame */
int frame_parser_to_string(const parsed_frame_t *frame, char *buf, size_t buf_size);

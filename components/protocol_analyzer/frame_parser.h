#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Parsed frame info */
typedef struct {
    uint8_t  slave_addr;
    uint8_t  function_code;
    uint16_t start_addr;          /* 仅请求帧有意义 */
    uint16_t quantity_or_value;   /* 请求=数量/写入值；响应=字节数 */
    bool     is_exception;
    bool     is_response;         /* 帧长启发式判定；FC05/06 的请求响应同形，无法区分 */
    uint8_t  exception_code;
    bool     crc_valid;
    const char *fc_name;
} parsed_frame_t;

/* Parse a raw RTU frame into structured info */
bool frame_parser_parse(const uint8_t *data, uint16_t len, parsed_frame_t *out);

/* Get human-readable description of a parsed frame */
int frame_parser_to_string(const parsed_frame_t *frame, char *buf, size_t buf_size);

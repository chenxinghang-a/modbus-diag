#!/usr/bin/env bash
# 协议层主机侧回归：不需要 ESP-IDF、不需要板子
# 用法： bash host_tests/run.sh
set -e
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
OUT_DIR=host_tests/.build
mkdir -p "$OUT_DIR"

"$CC" -std=c99 -Wall -Wextra -O1 \
    -I components/modbus_core \
    -I components/protocol_analyzer \
    host_tests/test_protocol.c \
    components/modbus_core/modbus_rtu.c \
    components/protocol_analyzer/frame_parser.c \
    -o "$OUT_DIR/test_protocol"

"$OUT_DIR/test_protocol"

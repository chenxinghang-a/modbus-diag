#!/usr/bin/env bash
# 主机侧回归：不需要 ESP-IDF、不需要板子
# 用法： bash host_tests/run.sh
set -e
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
OUT_DIR=host_tests/.build
CFLAGS="-std=c99 -Wall -Wextra -O1"
mkdir -p "$OUT_DIR"

fail=0

echo "=== 协议层（RTU 建帧/解析 + 抓包帧解析） ==="
"$CC" $CFLAGS \
    -I components/modbus_core \
    -I components/protocol_analyzer \
    host_tests/test_protocol.c \
    components/modbus_core/modbus_rtu.c \
    components/protocol_analyzer/frame_parser.c \
    -o "$OUT_DIR/test_protocol"
"$OUT_DIR/test_protocol" || fail=1

echo
echo "=== 波形信号处理（SMA / 幅值 / 触发） ==="
"$CC" $CFLAGS \
    -I components/waveform \
    host_tests/test_signal.c \
    components/waveform/signal_process.c \
    -o "$OUT_DIR/test_signal"
"$OUT_DIR/test_signal" || fail=1

echo
echo "=== 诊断报告生成（JSON / HTML / 诊断建议） ==="
"$CC" $CFLAGS \
    -I host_tests/stubs \
    -I components/diag_report \
    -I components/modbus_core \
    host_tests/test_report.c \
    components/diag_report/report_generator.c \
    -o "$OUT_DIR/test_report"
"$OUT_DIR/test_report" || fail=1

echo
if [ "$fail" -ne 0 ]; then
    echo "!!! 有用例失败"
    exit 1
fi
echo "全部通过"

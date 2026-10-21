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
    components/diag_report/health_metrics.c \
    -o "$OUT_DIR/test_report"
"$OUT_DIR/test_report" || fail=1

echo
echo "=== 健康评分模型（权重 / 分段线性 / 等级 / 统计） ==="
"$CC" $CFLAGS \
    -I host_tests/stubs \
    -I components/diag_report \
    host_tests/test_health.c \
    components/diag_report/health_metrics.c \
    -lm -o "$OUT_DIR/test_health"
"$OUT_DIR/test_health" || fail=1

echo
echo "=== 流量统计（错误率 / 滑动平均 / 每秒速率） ==="
"$CC" $CFLAGS \
    -I host_tests/stubs \
    -I components/protocol_analyzer \
    host_tests/test_traffic.c \
    components/protocol_analyzer/traffic_stats.c \
    -lm -o "$OUT_DIR/test_traffic"
"$OUT_DIR/test_traffic" || fail=1

echo
echo "=== Modbus TCP（MBAP 组帧 / 解帧 / 事务号校验） ==="
"$CC" $CFLAGS \
    -I host_tests/stubs \
    -I components/modbus_core \
    host_tests/test_tcp.c \
    components/modbus_core/modbus_tcp.c \
    -lm -o "$OUT_DIR/test_tcp"
"$OUT_DIR/test_tcp" || fail=1

echo
echo "=== 头文件自包含检查 ==="
# 每个头文件单独编译一次：只 include 它，不先包含 stdio/string。
# 这一项是 CI 上真炸过才加的：report_generator.h 用了 size_t 却没包含 stddef.h，
# 本地因为测试文件先 include 了 stdio.h 而侥幸编过，CI 的 glibc 直接报 unknown type name。
# 注意：MinGW 的 stdint.h 会顺带带出 size_t，本地可能复现不出来 —— 真正的把关在 CI。
INCS="-I . -I host_tests/stubs"
for d in components/*/; do INCS="$INCS -I $d"; done
for h in components/modbus_core/modbus_common.h \
         components/modbus_core/modbus_rtu.h \
         components/modbus_core/modbus_tcp.h \
         components/modbus_core/modbus_master.h \
         components/protocol_analyzer/frame_parser.h \
         components/waveform/signal_process.h \
         components/diag_report/health_metrics.h \
         components/diag_report/report_generator.h \
         components/storage/file_export.h \
         components/storage/log_storage.h \
         components/storage/nvs_config.h \
         components/modbus_scanner/scan_result.h \
         components/modbus_scanner/device_scanner.h \
         components/modbus_scanner/register_classifier.h ; do
    if ! echo "#include \"$h\"" | "$CC" $CFLAGS $INCS -fsyntax-only -x c - 2>"$OUT_DIR/hdr.err"; then
        echo "  [FAIL] $h 不能单独编译："
        sed 's/^/         /' "$OUT_DIR/hdr.err" | head -5
        fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "  [ OK ] 14 个头文件均可自包含"

echo
if [ "$fail" -ne 0 ]; then
    echo "!!! 有用例失败"
    exit 1
fi
echo "全部通过"

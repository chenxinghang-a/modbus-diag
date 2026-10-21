#!/usr/bin/env bash
# 端到端互通：起 Python 模拟器 → 用固件自己的帧层发真实请求 → 校验响应。
# 需要 .venv（pymodbus）。用法： bash host_tests/run_e2e.sh
set -e
cd "$(dirname "$0")/.."

if [ -z "$PY" ]; then
    if [ -x .venv/Scripts/python.exe ]; then PY=.venv/Scripts/python.exe
    elif command -v python3 >/dev/null 2>&1; then PY=python3
    else PY=python; fi
fi

CC=${CC:-gcc}
OUT=host_tests/.build
mkdir -p "$OUT"
PORT=15040

# Windows 下 socket 在 ws2_32 里；Linux 直接链 libc
LIBS=""
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) LIBS="-lws2_32" ;;
esac

echo "=== 编译 e2e（链接固件的 modbus_rtu.c） ==="
"$CC" -std=c99 -Wall -Wextra -O1 \
    -I components/modbus_core \
    host_tests/e2e_rtu.c \
    components/modbus_core/modbus_rtu.c \
    -o "$OUT/e2e_rtu" $LIBS

echo "=== 启动模拟器（RTU over TCP :$PORT） ==="
"$PY" sim/modbus_sim.py --rtu "socket://127.0.0.1:$PORT" >"$OUT/sim_e2e.log" 2>&1 &
SIM_PID=$!
cleanup() {
    kill "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== 跑互通用例 ==="
"$OUT/e2e_rtu"

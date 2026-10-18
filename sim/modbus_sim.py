#!/usr/bin/env python3
"""Modbus 从站模拟器 —— 让「模拟模式」不依赖真实硬件。

设计约束（重要）
----------------
* 本目录 `sim/` **只用于开发与演示**，不参与固件构建，也不进生产 BOM / 固件镜像。
* 真实硬件走 `components/` + `main/`；模拟走 `sim/`。两者通过「Modbus 协议」这一层解耦，
  上位机/诊断工具不需要知道自己连的是模拟器还是真板子。
* 只用标准库 + pymodbus，不引入 Web 框架，避免为了演示再装一堆依赖。

用法
----
    python sim/modbus_sim.py                     # 默认读 sim/devices.json
    python sim/modbus_sim.py --config other.json --port 15020

启动后：
    Modbus TCP      : <host>:15020   （unit id 由 devices.json 决定，支持多从站）
    控制接口(HTTP)  : <host>:15021   （读写寄存器 / 看拓扑，方便前端或脚本驱动）

控制接口
--------
    GET  /health
    GET  /read?unit=1&kind=hr&addr=0&count=4
    POST /write?unit=1&kind=hr&addr=0&value=123
    kind: hr=保持寄存器 co=线圈 ir=输入寄存器 di=离散输入
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

try:
    from pymodbus.datastore import (
        ModbusSequentialDataBlock,
        ModbusServerContext,
        ModbusSlaveContext,
    )
    from pymodbus.server import StartTcpServer
except ImportError:  # pragma: no cover
    sys.exit("缺少依赖：pip install -r sim/requirements.txt")

# 说明：锁 pymodbus 3.8.x。3.13+ 把 datastore 改成了 async/SimData 体系
# （ModbusDeviceContext 没有同步 getValues），4.x 还会移除旧接口。
# 这里用 3.8 的稳定经典 API，版本已写进 sim/requirements.txt。

KIND = {"co": 1, "di": 2, "hr": 3, "ir": 4}  # 功能码：线圈/离散输入/保持寄存器/输入寄存器
MAX_ADDR = 65536
ADDR_OFFSET = 1  # pymodbus 3.x datastore 为 1 基，对外仍用 0 基地址


def _block(regs: dict) -> ModbusSequentialDataBlock:
    """把 {addr: value} 铺成连续数据块（0 号地址必须存在，pymodbus 用 0 做占位）。"""
    values = [0] * (max([int(k) for k in regs] + [0]) + 1 + ADDR_OFFSET)
    for k, v in regs.items():
        values[int(k) + ADDR_OFFSET] = int(v)
    return ModbusSequentialDataBlock(0, values)


def build_context(cfg: dict) -> tuple[ModbusServerContext, dict]:
    devices = {}
    meta = {}
    for dev in cfg.get("devices", []):
        unit = int(dev["unit"])
        # 地址约定（实测校准，别改）：datastore 里 values[0] 是占位，配置的 0 号放在 values[1]；
        # 服务端对「客户端读 0 号」会调 getValues(fc, 0, n)，其内部再 +1 取到 values[1]。
        # 所以 HTTP 控制接口传 0 基地址即可，不要再加偏移。
        devices[unit] = ModbusSlaveContext(
            co=_block(dev.get("coils", {})),
            di=_block(dev.get("discrete", {})),
            hr=_block(dev.get("holding", {})),
            ir=_block(dev.get("input", {})),
        )
        meta[unit] = dev.get("name", f"unit-{unit}")
    if not devices:
        raise SystemExit("devices.json 里没有 devices")
    return ModbusServerContext(slaves=devices, single=False), meta


def make_handler(context: ModbusServerContext, meta: dict):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):  # 静音
            pass

        def _json(self, code: int, payload: dict) -> None:
            body = json.dumps(payload, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _ctx(self, qs: dict) -> tuple[int, object]:
            unit = int(qs.get("unit", ["1"])[0])
            if unit not in meta:
                raise KeyError(f"没有 unit={unit}，现有：{sorted(meta)}")
            return unit, context[unit]

        def do_GET(self):  # noqa: N802
            url = urlparse(self.path)
            qs = parse_qs(url.query)
            try:
                if url.path == "/health":
                    return self._json(200, {"ok": True, "units": sorted(meta), "names": meta})
                if url.path == "/read":
                    unit, ctx = self._ctx(qs)
                    kind = KIND[qs.get("kind", ["hr"])[0]]
                    addr = int(qs.get("addr", ["0"])[0])
                    count = int(qs.get("count", ["1"])[0])
                    values = ctx.getValues(kind, addr, count)
                    return self._json(200, {"unit": unit, "kind": kind, "addr": addr, "values": list(values)})
                return self._json(404, {"error": "unknown path"})
            except Exception as exc:  # 控制接口不应把模拟器搞挂
                return self._json(400, {"error": str(exc)})

        def do_POST(self):  # noqa: N802
            url = urlparse(self.path)
            qs = parse_qs(url.query)
            try:
                if url.path == "/write":
                    unit, ctx = self._ctx(qs)
                    kind = KIND[qs.get("kind", ["hr"])[0]]
                    addr = int(qs.get("addr", ["0"])[0])
                    value = int(qs.get("value", ["0"])[0])
                    ctx.setValues(kind, addr, [value])
                    return self._json(200, {"ok": True, "unit": unit, "kind": kind, "addr": addr, "value": value})
                return self._json(404, {"error": "unknown path"})
            except Exception as exc:
                return self._json(400, {"error": str(exc)})

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser(description="Modbus 从站模拟器（开发/演示用）")
    ap.add_argument("--config", default=None, help="设备表 JSON，默认 sim/devices.json")
    ap.add_argument("--host", default=None)
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--control-port", type=int, default=None)
    args = ap.parse_args()

    cfg_path = args.config or str(__file__.rsplit("\\", 1)[0]) + "\\devices.json"
    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    host = args.host or cfg.get("host", "0.0.0.0")
    port = args.port or int(cfg.get("port", 15020))
    control_port = args.control_port or int(cfg.get("control_port", 15021))

    context, meta = build_context(cfg)

    httpd = ThreadingHTTPServer((host, control_port), make_handler(context, meta))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    print(f"[sim] 设备 {sorted(meta)} -> {meta}")
    print(f"[sim] Modbus TCP  {host}:{port}")
    print(f"[sim] 控制接口    http://{host}:{control_port}/health")
    print("[sim] Ctrl+C 结束")
    StartTcpServer(context=context, address=(host, port))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

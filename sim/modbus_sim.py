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
    python sim/modbus_sim.py                          # Modbus TCP（默认 15020）+ 控制接口 15021
    python sim/modbus_sim.py --port 15020 --host 127.0.0.1
    python sim/modbus_sim.py --rtu COM3 --baud 115200  # Modbus RTU（真实串口/虚拟串口对）
    python sim/modbus_sim.py --rtu "socket://127.0.0.1:15040"   # RTU over TCP，免装 com0com

三种传输：
    Modbus TCP : <host>:15020   （unit id 由 devices.json 决定，支持多从站）
    Modbus RTU : --rtu <COMx>   （8N1，波特率/校验/停止位可调）
                 --rtu "socket://host:port" 时不开物理串口，改成在该端口**监听**，
                 客户端用 ``ModbusSerialClient(port="socket://...")`` 连上来 —— 帧仍然是
                 标准 RTU（带 CRC），只是把串口线换成了 TCP 连接。没有 com0com / 没有第二块板子
                 也能把 RTU 链路端到端跑通。
    控制接口   : <host>:15021   （读写寄存器 / 看拓扑 / **注入故障**）

控制接口
--------
    GET  /health
    GET  /read?unit=1&kind=hr&addr=0&count=4          （加 &raw=1 绕过故障注入看底层值）
    POST /write?unit=1&kind=hr&addr=0&value=123       （同样支持 &raw=1）
    kind: hr=保持寄存器 co=线圈 ir=输入寄存器 di=离散输入

    —— 故障注入（诊断工具的题眼）——
    GET  /faults
    POST /fault?unit=1&kind=hr&addr=1&action=exception            地址 1 回异常（默认码 2）
    POST /fault?unit=1&kind=hr&addr=1&action=exception&code=4     指定异常码 4（从站设备故障）
    POST /fault?unit=1&kind=hr&addr=2&action=delay&ms=4000        地址 2 拖 4 秒（触发客户端超时）
    POST /fault/clear?unit=1                                      清掉该 unit
    POST /fault/clear                                             清空全部

两种故障动作
------------
* ``exception`` —— 返回标准 Modbus 异常响应，异常码可选 ``1``~``255``（标准码见下表），默认 ``2``。
  ``1`` 非法功能 / ``2`` 非法数据地址 / ``3`` 非法数据值 / ``4`` 从站设备故障 /
  ``5`` 确认 / ``6`` 从站忙 / ``8`` 存储奇偶性差错 / ``10``(0x0A) 网关路径不可用 /
  ``11``(0x0B) 网关目标设备无响应；其余按厂商自定义处理。
* ``delay`` —— 先等 ``ms`` 毫秒再正常返回。把 ``ms`` 调到大于客户端超时阈值
  （常见 1~3 秒）即可模拟「从站不响应」。

广播（unit 0）
--------------
``broadcast_enable=True``：``dev_id=0`` 的请求会在**所有**从站上下文上执行，且按规范
**不回包**（见 pymodbus ``server/requesthandler.py`` 的 broadcast 分支）。
所以广播测试的现象就是「客户端超时」——先广播写，再单播读回确认写进去了。
unit 0 在 datastore 里挂了一个空上下文（``NoopSlaveContext``），只吞不存，
这样广播遍历所有从站时不会因为 0 号不存在而中途抛错。

> **为什么注入点是 PDU 的 ``update_datastore``**：pymodbus 3.8 里
> ``ExceptionResponse`` 是 PDU 类**不是异常类**（``raise`` 它会 TypeError），
> 而服务端两条兜底路径的异常码都写死：``validate()`` 失败固定为 2，
> 服务端 ``except`` 分支固定为 ``ExceptionResponse(0x00, 4)`` ——
> 后者 FC 是 0，客户端直接解不出帧（实测踩过）。
> 想指定任意异常码，只能包一层 ``update_datastore`` 并**返回**自己构造的
> ``ExceptionResponse``。只包这一层，不碰 framer、不改 datastore 语义。
>
> 也没有「按地址丢弃单个回包」：那要改 framer 层，等于改协议栈本身。
> 诊断意义上 ``delay`` 已能覆盖「超时」现象，且行为可控、可复现。
> （广播的「不回包」是协议规范行为，由 pymodbus 自带的 ``broadcast_enable`` 负责，
> 与这里的故障注入无关。）

地址约定
--------
pymodbus 3.x 的 datastore 是 1 基，``values[0]`` 是占位。配置与 HTTP 接口一律用 **0 基地址**，
代码里统一加 ``ADDR_OFFSET``。别改这个偏移，改了对外语义就变。
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

try:
    from pymodbus.datastore import (
        ModbusSequentialDataBlock,
        ModbusServerContext,
        ModbusSlaveContext,
    )
    from pymodbus.framer import FramerType
    from pymodbus.server import StartSerialServer, StartTcpServer
except ImportError:  # pragma: no cover
    raise SystemExit("缺少依赖：pip install -r sim/requirements.txt")

# 说明：锁 pymodbus 3.8.x。3.13+ 把 datastore 改成了 async/SimData 体系
# （ModbusDeviceContext 没有同步 getValues），4.x 还会移除旧接口。
# 这里用 3.8 的稳定经典 API，版本已写进 sim/requirements.txt。

KIND = {"co": 1, "di": 2, "hr": 3, "ir": 4}  # 功能码：线圈/离散输入/保持寄存器/输入寄存器
# 写操作的功能码也要能映射回 kind（5/15 写线圈，6/16 写保持寄存器）
FC2KIND = {1: "co", 2: "di", 3: "hr", 4: "ir", 5: "co", 6: "hr", 15: "co", 16: "hr"}
MAX_ADDR = 65536
ADDR_OFFSET = 1  # pymodbus 3.x datastore 为 1 基，对外仍用 0 基地址

#: 故障动作 -> 对外表现
ACTIONS = {
    "exception": "返回 Modbus 异常响应（code 可选 1~255，默认 2 非法数据地址）",
    "delay": "延迟响应（毫秒级），用于模拟超时",
}

#: 标准异常码（Modbus Application Protocol V1.1b3 §7）。7 / 9 为保留码，未列。
FAULT_CODES = {
    1: "非法功能",
    2: "非法数据地址",
    3: "非法数据值",
    4: "从站设备故障",
    5: "确认",
    6: "从站忙",
    8: "存储奇偶性差错",
    10: "网关路径不可用",      # 0x0A：网关后级链路不通（不是从站本身的问题）
    11: "网关目标设备无响应",  # 0x0B：网关转发出去，目标设备没回
}


def fault_code_name(code: int) -> str:
    """异常码 -> 中文名。非标准码按「保留 / 厂商自定义」处理，不再拒绝。"""
    if code in FAULT_CODES:
        return FAULT_CODES[code]
    if code in (7, 9):
        return "保留码"
    return "厂商自定义"


class FaultRules:
    """线程安全的故障规则表。控制接口改它，数据层读它。"""

    def __init__(self) -> None:
        self._rules: list[dict] = []
        self._lock = threading.Lock()

    # ---- 写 ----
    def set(self, rule: dict) -> dict:
        """新增一条规则。同 unit+kind+addr 的旧规则先删掉，避免叠加出意外行为。"""
        unit = int(rule["unit"])
        kind = rule.get("kind", "hr")
        addr = int(rule.get("addr", 0))
        action = rule.get("action", "exception")
        if kind not in KIND and kind != "*":
            raise ValueError(f"kind 只能是 {list(KIND)} 或 *，收到 {kind!r}")
        if action not in ACTIONS:
            raise ValueError(f"action 只能是 {list(ACTIONS)}，收到 {action!r}")
        code = None
        if action == "exception":
            code = int(rule.get("code", 2))
            # 异常码是 1 字节字段：0 表示「无异常」，不能注入；255 是上限。
            if not 1 <= code <= 255:
                raise ValueError(f"code 必须在 1~255，收到 {code}")
        item = {
            "unit": unit,
            "kind": kind,
            "addr": addr,
            "count": max(1, int(rule.get("count", 1))),
            "action": action,
            "code": code,
            "ms": max(0, int(rule.get("ms", 3000))) if action == "delay" else None,
        }
        with self._lock:
            self._rules = [r for r in self._rules
                           if not (r["unit"] == unit and r["kind"] == kind and r["addr"] == addr)]
            self._rules.append(item)
        return item

    def clear(self, unit: int | None = None) -> int:
        with self._lock:
            before = len(self._rules)
            if unit is None:
                self._rules = []
            else:
                self._rules = [r for r in self._rules if r["unit"] != unit]
            return before - len(self._rules)

    # ---- 读 ----
    def match(self, unit: int, kind: str, address: int, count: int) -> dict | None:
        """请求区间与规则区间有交集即命中。返回第一条命中的规则。"""
        with self._lock:
            for r in self._rules:
                if r["unit"] != unit:
                    continue
                if r["kind"] != "*" and r["kind"] != kind:
                    continue
                if address < r["addr"] + r["count"] and r["addr"] < address + count:
                    return dict(r)
        return None

    def snapshot(self) -> list[dict]:
        with self._lock:
            return [dict(r) for r in self._rules]


def _block(regs: dict) -> ModbusSequentialDataBlock:
    """把 {addr: value} 铺成连续数据块（0 号地址必须存在，pymodbus 用 0 做占位）。"""
    values = [0] * (max([int(k) for k in regs] + [0]) + 1 + ADDR_OFFSET)
    for k, v in regs.items():
        values[int(k) + ADDR_OFFSET] = int(v)
    return ModbusSequentialDataBlock(0, values)


class FaultedSlaveContext(ModbusSlaveContext):
    """在标准 datastore 外面套一层故障注入。

    两个注入点，都是 pymodbus 的公开面，不打补丁：

    * ``validate()`` —— 返回 False 即「地址非法」，服务端会包成 ExceptionResponse(fc, 2)
    * ``getValues()/setValues()`` —— 抛异常走服务端兜底分支，包成 ExceptionResponse(fc, 4)；
      或在里面 sleep 实现延迟响应

    为什么不用 ``raise ExceptionResponse(...)``：3.8 的 ExceptionResponse 是 PDU 类不是异常类，
    raise 它会 TypeError，服务端兜底再包一层，线上就变成 0x80/0x04（实测踩过）。
    """

    def __init__(self, unit: int, faults: FaultRules, **kwargs) -> None:
        super().__init__(**kwargs)
        self.unit = unit
        self.faults = faults

    def _rule(self, fc: int, address: int, count: int) -> dict | None:
        kind = FC2KIND.get(fc)
        if kind is None:  # 未知功能码交给 pymodbus 自己判
            return None
        return self.faults.match(self.unit, kind, address, max(1, count))

    def _apply(self, fc: int, address: int, count: int) -> None:
        """同步 getValues/setValues 上的注入（HTTP 控制接口直接走这条路）。"""
        rule = self._rule(fc, address, count)
        if not rule:
            return
        if rule["action"] == "delay":
            time.sleep(rule["ms"] / 1000.0)
        elif rule["action"] == "exception":
            raise ValueError(f"注入故障：unit={self.unit} 返回异常码 "
                             f"{rule['code']}（{fault_code_name(rule['code'])}）")

    def getValues(self, fc_as_hex, address, count=1):
        self._apply(fc_as_hex, address, count)
        return super().getValues(fc_as_hex, address, count)

    def setValues(self, fc_as_hex, address, values):
        self._apply(fc_as_hex, address, len(values))
        return super().setValues(fc_as_hex, address, values)


# ---------------------------------------------------------------- PDU 层注入
#
# 为什么要在这一层拦：pymodbus 3.8 的 ExceptionResponse 是 PDU 类**不是异常类**
# （raise 它会 TypeError），而服务端只有两个兜底分支，异常码都被写死：
#   update_datastore 里 validate 失败 -> ExceptionResponse(fc, 2)
#   服务端 except 分支            -> ExceptionResponse(0x00, 4)   ← FC 是 0，客户端解不出帧
# 所以「指定异常码」只能包一层 update_datastore，返回**自己构造的合法 ExceptionResponse**。
# 只包它，不碰 framer、不碰 datastore 语义；pymodbus 版本由 requirements.txt 钉死。

def _pdu_classes() -> list:
    from pymodbus.pdu.bit_message import (
        ReadCoilsRequest,
        ReadDiscreteInputsRequest,
        WriteMultipleCoilsRequest,
        WriteSingleCoilRequest,
    )
    from pymodbus.pdu.register_message import (
        ReadHoldingRegistersRequest,
        ReadInputRegistersRequest,
        WriteMultipleRegistersRequest,
        WriteSingleRegisterRequest,
    )
    return [ReadCoilsRequest, ReadDiscreteInputsRequest,
            ReadHoldingRegistersRequest, ReadInputRegistersRequest,
            WriteSingleCoilRequest, WriteSingleRegisterRequest,
            WriteMultipleCoilsRequest, WriteMultipleRegistersRequest]


def install_fault_hook() -> list:
    """把故障注入装到 PDU 的 update_datastore 上。返回被包过的类名，便于自检。"""
    import asyncio
    from pymodbus.pdu import ExceptionResponse

    patched = []
    for cls in _pdu_classes():
        if getattr(cls, "_scada_fault_hooked", False):
            patched.append(cls.__name__)
            continue
        orig = cls.update_datastore

        async def wrapper(self, context, _orig=orig):
            faults = getattr(context, "faults", None)
            unit = getattr(context, "unit", None)
            if faults is not None and unit is not None:
                fc = self.function_code
                kind = FC2KIND.get(fc)
                if kind:
                    addr = getattr(self, "address", 0)
                    cnt = getattr(self, "count", None)
                    if cnt is None:
                        vals = getattr(self, "values", None)
                        cnt = len(vals) if isinstance(vals, (list, tuple)) else 1
                    rule = faults.match(unit, kind, addr, max(1, cnt))
                    if rule:
                        if rule["action"] == "exception":
                            return ExceptionResponse(fc, rule["code"],
                                                     slave=self.dev_id,
                                                     transaction=self.transaction_id)
                        if rule["action"] == "delay":
                            await asyncio.sleep(rule["ms"] / 1000.0)
            return await _orig(self, context)

        cls.update_datastore = wrapper
        cls._scada_fault_hooked = True
        patched.append(cls.__name__)
    return patched


class NoopSlaveContext(ModbusSlaveContext):
    """unit 0（广播地址）的占位上下文：只吞不存。

    广播请求会遍历 ``context.slaves()`` 并在**每个**上下文上执行一次 ``update_datastore``。
    如果 0 号不在 datastore 里，遍历到最后一步会拿到 ``None`` 直接抛错，
    服务端兜底记一条 error（回包反正要丢弃，但日志会很吵）。
    挂个空上下文就把这条路走平了：读返回全 0，写直接丢弃。
    """

    def getValues(self, fc_as_hex, address, count=1):
        return [0] * max(1, count)

    def setValues(self, fc_as_hex, address, values):
        return None


def build_context(cfg: dict) -> tuple[ModbusServerContext, dict, FaultRules]:
    faults = FaultRules()
    devices = {}
    meta = {}
    for dev in cfg.get("devices", []):
        unit = int(dev["unit"])
        # 地址约定（实测校准，别改）：datastore 里 values[0] 是占位，配置的 0 号放在 values[1]；
        # 服务端对「客户端读 0 号」会调 getValues(fc, 0, n)，其内部再 +1 取到 values[1]。
        # 所以 HTTP 控制接口传 0 基地址即可，不要再加偏移。
        devices[unit] = FaultedSlaveContext(
            unit=unit,
            faults=faults,
            co=_block(dev.get("coils", {})),
            di=_block(dev.get("discrete", {})),
            hr=_block(dev.get("holding", {})),
            ir=_block(dev.get("input", {})),
        )
        meta[unit] = dev.get("name", f"unit-{unit}")

        # 配置里可以预置故障：devices[].faults = [{kind,addr,action,ms,count}, ...]
        for rule in dev.get("faults", []) or []:
            faults.set(dict(rule, unit=unit))

    if not devices:
        raise SystemExit("devices.json 里没有 devices")

    # unit 0 = 广播地址。meta 里**不登记**它，所以 HTTP 控制接口不会被 0 号污染。
    devices[0] = NoopSlaveContext(
        co=_block({}), di=_block({}), hr=_block({}), ir=_block({}),
    )

    # 顶层也可以放一份 faults（便于共享规则的配置）
    for rule in cfg.get("faults", []) or []:
        faults.set(rule)

    return ModbusServerContext(slaves=devices, single=False), meta, faults


def make_handler(context: ModbusServerContext, meta: dict, faults: FaultRules):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):  # 静音
            pass

        def _json(self, code: int, payload) -> None:
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
                    return self._json(200, {"ok": True, "units": sorted(meta), "names": meta,
                                            "faults": len(faults.snapshot()),
                                            "actions": ACTIONS})
                if url.path == "/faults":
                    return self._json(200, {"rules": faults.snapshot()})
                if url.path == "/read":
                    unit, ctx = self._ctx(qs)
                    kind = KIND[qs.get("kind", ["hr"])[0]]
                    addr = int(qs.get("addr", ["0"])[0])
                    count = int(qs.get("count", ["1"])[0])
                    # raw=1 绕过故障注入，直接看底层寄存器 —— 排查「注入了什么」时用
                    if qs.get("raw", ["0"])[0] == "1":
                        values = ModbusSlaveContext.getValues(ctx, kind, addr, count)
                        raw = True
                    else:
                        values = ctx.getValues(kind, addr, count)
                        raw = False
                    return self._json(200, {"unit": unit, "kind": kind, "addr": addr,
                                            "values": list(values), "raw": raw})
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
                    if qs.get("raw", ["0"])[0] == "1":
                        ModbusSlaveContext.setValues(ctx, kind, addr, [value])
                    else:
                        ctx.setValues(kind, addr, [value])
                    return self._json(200, {"ok": True, "unit": unit, "kind": kind,
                                            "addr": addr, "value": value})
                if url.path == "/fault":
                    rule = {
                        "unit": int(qs.get("unit", ["1"])[0]),
                        "kind": qs.get("kind", ["hr"])[0],
                        "addr": int(qs.get("addr", ["0"])[0]),
                        "count": int(qs.get("count", ["1"])[0]),
                        "action": qs.get("action", ["exception"])[0],
                    }
                    if rule["action"] == "delay":
                        rule["ms"] = int(qs.get("ms", ["3000"])[0])
                    elif rule["action"] == "exception":
                        rule["code"] = int(qs.get("code", ["2"])[0])
                    if rule["unit"] not in meta:
                        raise KeyError(f"没有 unit={rule['unit']}，现有：{sorted(meta)}")
                    return self._json(200, {"ok": True, "rule": faults.set(rule)})
                if url.path == "/fault/clear":
                    unit = qs.get("unit", [None])[0]
                    n = faults.clear(int(unit) if unit is not None else None)
                    return self._json(200, {"ok": True, "cleared": n})
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
    # —— RTU ——
    ap.add_argument("--rtu", default=None, metavar="COMx",
                    help="改走 Modbus RTU，指定串口（如 COM3）。给了它就不开 TCP。"
                         "也可给 socket://host:port 走「RTU over TCP」，免装 com0com。")
    ap.add_argument("--baud", type=int, default=19200, help="RTU 波特率，默认 19200")
    ap.add_argument("--parity", default="N", choices=["N", "E", "O"], help="RTU 校验位，默认 N")
    ap.add_argument("--stopbits", type=int, default=1, choices=[1, 2], help="RTU 停止位，默认 1")
    ap.add_argument("--bytesize", type=int, default=8, choices=[7, 8], help="RTU 数据位，默认 8")
    args = ap.parse_args()

    # 不能写死 "\\"：Linux/CI 上 __file__ 里没有反斜杠，拼出来会变成
    # ".../modbus_sim.py\\devices.json" 这种文件，启动直接 FileNotFoundError。
    cfg_path = args.config or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "devices.json")
    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    host = args.host or cfg.get("host", "0.0.0.0")
    port = args.port or int(cfg.get("port", 15020))
    control_port = args.control_port or int(cfg.get("control_port", 15021))

    context, meta, faults = build_context(cfg)
    hooked = install_fault_hook()

    httpd = ThreadingHTTPServer((host, control_port), make_handler(context, meta, faults))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    print(f"[sim] 设备 {sorted(meta)} -> {meta}")
    if args.rtu:
        print(f"[sim] Modbus RTU  {args.rtu} @ {args.baud} "
              f"{args.bytesize}{args.parity}{args.stopbits}")
    else:
        print(f"[sim] Modbus TCP  {host}:{port}")
    print(f"[sim] 控制接口    http://{host}:{control_port}/health")
    print(f"[sim] 故障注入    已挂 {len(hooked)} 个 PDU 类，预置 {len(faults.snapshot())} 条规则")
    print("[sim] Ctrl+C 结束")

    if args.rtu:
        # 预检：先自己开一下串口。串口不存在时 pymodbus 会一直重连、不退出，
        # 那样命令行看着像卡死；这里提前拦住并列出可用端口。
        # socket:// 是「RTU over TCP」测试通道（服务端在该端口监听），不是物理串口，跳过预检。
        if str(args.rtu).startswith("socket://"):
            print(f"[sim] RTU over TCP：监听 {args.rtu}（客户端连这里，帧仍是标准 RTU）")
        else:
            try:
                import serial
            except ImportError:
                print("[sim] 走 RTU 需要 pyserial：pip install pyserial", file=sys.stderr)
                return 2
            try:
                probe = serial.Serial(port=args.rtu, baudrate=args.baud, timeout=0.2)
                probe.close()
            except Exception as exc:
                from serial.tools import list_ports
                ports = [p.device for p in list_ports.comports()]
                print(f"[sim] 打不开串口 {args.rtu!r}：{exc}", file=sys.stderr)
                print(f"[sim] 本机可用串口：{ports or '（无）'}", file=sys.stderr)
                return 2

        StartSerialServer(
            context=context,
            framer=FramerType.RTU,
            port=args.rtu,
            baudrate=args.baud,
            parity=args.parity,
            stopbits=args.stopbits,
            bytesize=args.bytesize,
            timeout=1,
            broadcast_enable=True,
        )
    else:
        StartTcpServer(context=context, address=(host, port), broadcast_enable=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

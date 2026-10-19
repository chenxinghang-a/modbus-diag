"""sim/modbus_sim.py 的验收测试（TCP + 故障注入 + 广播 + RTU over TCP 端到端）。

RTU 不需要 com0com、也不需要第二块板子：``--rtu socket://127.0.0.1:15040`` 让服务端
在该端口监听，客户端用 ``ModbusSerialClient(port="socket://...")`` 连上去，
两端走的仍是标准 RTU 帧（带 CRC），只是把串口线换成了 TCP 连接。

跑法（必须在项目 venv 里，pymodbus 3.8.x）：
    .venv/Scripts/python.exe sim/test_sim.py
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
CTRL = "http://127.0.0.1:15031"
TCP_PORT = 15030

ok = 0
bad = 0


def check(label, cond, detail=""):
    global ok, bad
    if cond:
        ok += 1
        print(f"  [ OK ] {label}" + (f"  {detail}" if detail else ""))
    else:
        bad += 1
        print(f"  [FAIL] {label}" + (f"  {detail}" if detail else ""))


def http(path, method="GET", base=CTRL):
    req = urllib.request.Request(base + path, method=method)
    try:
        return json.loads(urllib.request.urlopen(req, timeout=10).read())
    except urllib.error.HTTPError as e:
        return {"_http_error": e.code, "body": e.read().decode("utf-8", "replace")}


def main():
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    proc = subprocess.Popen(
        [PY, os.path.join(HERE, "modbus_sim.py"),
         "--host", "127.0.0.1", "--port", str(TCP_PORT), "--control-port", "15031"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace", env=env)
    try:
        for _ in range(40):
            time.sleep(0.25)
            try:
                if http("/health").get("ok"):
                    break
            except Exception:
                pass
        else:
            print(proc.stdout.read()[-2000:] if proc.stdout else "")
            raise SystemExit("模拟器没起来")

        from pymodbus.client import ModbusTcpClient
        client = ModbusTcpClient("127.0.0.1", port=TCP_PORT, timeout=2)
        check("TCP 连接", client.connect())

        # ---------- 基线 ----------
        r = client.read_holding_registers(0, count=4, slave=1)
        check("基线 u1 hr0..3 = [230,500,0,1]",
              (not r.isError()) and r.registers == [230, 500, 0, 1],
              str(r.registers if not r.isError() else r))

        # ---------- exception -> 0x02 非法数据地址 ----------
        print("  -- 注入 exception 到 u1 hr addr=1 --")
        check("POST /fault", http("/fault?unit=1&kind=hr&addr=1&action=exception",
                                  "POST").get("ok") is True)
        r = client.read_holding_registers(0, count=4, slave=1)
        check("命中区间 -> 异常响应", r.isError(),
              f"exception_code={getattr(r, 'exception_code', '?')}")
        check("异常码 = 2（非法数据地址）", getattr(r, "exception_code", None) == 2)

        r = client.read_holding_registers(0, count=1, slave=1)
        check("未命中区间仍正常", (not r.isError()) and r.registers == [230],
              str(r.registers if not r.isError() else r))

        d = http("/read?unit=1&kind=hr&addr=0&count=4&raw=1")
        check("/read?raw=1 绕过注入", d.get("values") == [230, 500, 0, 1], str(d.get("values")))
        d = http("/faults")
        check("/faults 有 1 条规则", len(d.get("rules", [])) == 1, str(d.get("rules")))
        d = http("/fault/clear?unit=1", "POST")
        check("/fault/clear 清掉 1 条", d.get("cleared") == 1, str(d))
        r = client.read_holding_registers(0, count=4, slave=1)
        check("清除后恢复", (not r.isError()) and r.registers == [230, 500, 0, 1],
              str(r.registers if not r.isError() else r))

        # ---------- 指定异常码 ----------
        for code, name in [(4, "从站设备故障"), (6, "从站忙"), (1, "非法功能"),
                           (10, "网关路径不可用"), (11, "网关目标设备无响应")]:
            print(f"  -- 注入 exception code={code}（{name}）到 u1 hr addr=1 --")
            http(f"/fault?unit=1&kind=hr&addr=1&action=exception&code={code}", "POST")
            r = client.read_holding_registers(1, count=1, slave=1)
            check(f"code={code} -> 异常响应", r.isError(),
                  f"exception_code={getattr(r, 'exception_code', '?')}")
            check(f"异常码 = {code}（{name}）", getattr(r, "exception_code", None) == code)
            http("/fault/clear", "POST")

        # ---------- 厂商自定义码（1~255 都该放行）----------
        print("  -- 注入 exception code=200（厂商自定义）--")
        check("非标准码可注入", http("/fault?unit=1&kind=hr&addr=1&action=exception&code=200",
                                     "POST").get("ok") is True)
        r = client.read_holding_registers(1, count=1, slave=1)
        check("非标准码原样透传", getattr(r, "exception_code", None) == 200,
              f"exception_code={getattr(r, 'exception_code', '?')}")
        http("/fault/clear", "POST")

        # ---------- delay -> 超时（用独立客户端：timeout=1, retries=0，避免重试掩盖超时）----------
        print("  -- 注入 delay 4000ms 到 u1 hr addr=2 --")
        http("/fault?unit=1&kind=hr&addr=2&action=delay&ms=4000", "POST")
        slow = ModbusTcpClient("127.0.0.1", port=TCP_PORT, timeout=1, retries=0)
        slow.connect()
        t0 = time.time()
        timed_out = False
        r = None
        try:
            r = slow.read_holding_registers(2, count=1, slave=1)
            timed_out = r.isError()
        except Exception:  # retries=0 时 pymodbus 直接抛 ModbusIOException —— 正是「超时」
            timed_out = True
        dt = time.time() - t0
        slow.close()
        check("delay -> 客户端超时", timed_out, f"耗时 {dt:.1f}s")
        check("确实等到了注入的延迟（>1s）", dt >= 0.9, f"{dt:.1f}s")
        http("/fault/clear", "POST")
        time.sleep(0.5)

        # ---------- 写操作也被拦（换新客户端，避免上一步的迟到响应污染事务表）----------
        print("  -- exception code=2 作用于写 hr0 --")
        client.close()
        client = ModbusTcpClient("127.0.0.1", port=TCP_PORT, timeout=2, retries=0)
        client.connect()
        http("/fault?unit=1&kind=hr&addr=0&action=exception&code=2", "POST")
        w = client.write_register(0, 999, slave=1)
        check("写 hr0 返回异常", w.isError(), f"exception_code={getattr(w, 'exception_code', '?')}")
        check("写异常码 = 2", getattr(w, "exception_code", None) == 2)
        http("/fault/clear", "POST")
        w = client.write_register(0, 230, slave=1)
        check("清除后可正常写入", not w.isError(), str(w))
        r = client.read_holding_registers(0, count=1, slave=1)
        check("写入生效（hr0=230）", (not r.isError()) and r.registers == [230],
              str(r.registers if not r.isError() else r))

        # ---------- 参数校验 ----------
        check("非法 kind 被拒", "_http_error" in http("/fault?unit=1&kind=zz&addr=0", "POST"))
        check("非法 action 被拒", "_http_error" in http("/fault?unit=1&kind=hr&addr=0&action=bogus", "POST"))
        check("code=0 被拒（0 表示无异常）",
              "_http_error" in http("/fault?unit=1&kind=hr&addr=0&action=exception&code=0", "POST"))
        check("code=256 被拒（超 1 字节字段）",
              "_http_error" in http("/fault?unit=1&kind=hr&addr=0&action=exception&code=256", "POST"))
        check("不存在的 unit 被拒", "_http_error" in http("/fault?unit=99&kind=hr&addr=0", "POST"))

        # ---------- 广播（unit 0）：执行到所有从站 + 不回包 ----------
        print("  -- 广播写 u0 hr0=777：预期无响应，但三个从站都写入 --")
        bcast = ModbusTcpClient("127.0.0.1", port=TCP_PORT, timeout=1, retries=0)
        bcast.connect()
        no_reply = False
        try:
            w = bcast.write_register(0, 777, slave=0)
            no_reply = w.isError()
        except Exception:  # retries=0 + 无回包 -> pymodbus 直接抛 ModbusIOException
            no_reply = True
        check("广播不回包（客户端超时）", no_reply)
        time.sleep(0.5)
        vals = {u: http(f"/read?unit={u}&kind=hr&addr=0&count=1&raw=1").get("values")
                for u in (1, 2, 3)}
        check("广播写落到全部从站（u1/u2/u3 hr0 都是 777）",
              all(v == [777] for v in vals.values()), str(vals))
        r = client.read_holding_registers(0, count=1, slave=1)
        check("广播后单播读正常（u1 hr0=777）",
              (not r.isError()) and r.registers == [777],
              str(r.registers if not r.isError() else r))
        bcast.close()
        d = http("/health")
        check("广播地址 0 不混进控制接口的 unit 列表", 0 not in d.get("units", []),
              str(d.get("units")))

        client.close()

        # ---------- RTU 参数路径 ----------
        print("  -- RTU 参数路径（本机无串口，预期干净报错而非崩在参数解析）--")
        p2 = subprocess.run([PY, os.path.join(HERE, "modbus_sim.py"),
                             "--rtu", "COM_DOES_NOT_EXIST", "--control-port", "15032"],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=30, env=env)
        blob = (p2.stdout or "") + (p2.stderr or "")
        check("RTU 分支被走到（日志里打印了 Modbus RTU）", "Modbus RTU" in blob,
              (blob.strip().splitlines()[-1][:90] if blob.strip() else ""))
        check("无串口时非零退出", p2.returncode != 0, f"rc={p2.returncode}")

        # ---------- RTU 端到端：RTU over TCP（等效虚拟串口对，免装 com0com / 免两块板）----------
        #
        # pymodbus 的串口传输层认 socket:// —— 服务端当它是「监听」，客户端当它是「连接」，
        # 两端仍走标准 RTU 帧（带 CRC）。等于把串口线换成了一根 TCP 线。
        print("  -- RTU 端到端：--rtu socket://127.0.0.1:15040 --")
        RTU_TCP, RTU_BASE = 15040, "http://127.0.0.1:15033"
        p3 = subprocess.Popen(
            [PY, os.path.join(HERE, "modbus_sim.py"),
             "--rtu", f"socket://127.0.0.1:{RTU_TCP}", "--control-port", "15033",
             "--baud", "115200"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            encoding="utf-8", errors="replace", env=env)
        rtu_up = False
        try:
            for _ in range(40):
                time.sleep(0.25)
                try:
                    if http("/health", base=RTU_BASE).get("ok"):
                        rtu_up = True
                        break
                except Exception:
                    pass
            check("RTU 模拟器起来了（socket:// 监听）", rtu_up)

            if rtu_up:
                from pymodbus.client import ModbusSerialClient
                from pymodbus.framer import FramerType
                rc = ModbusSerialClient(port=f"socket://127.0.0.1:{RTU_TCP}",
                                        framer=FramerType.RTU, timeout=2, retries=0)
                check("RTU 客户端连上", rc.connect())

                r = rc.read_holding_registers(0, count=4, slave=1)
                check("RTU 读 u1 hr0..3 = [230,500,0,1]（CRC 帧往返正常）",
                      (not r.isError()) and r.registers == [230, 500, 0, 1],
                      str(r.registers if not r.isError() else r))

                w = rc.write_register(0, 321, slave=2)
                check("RTU 写 u2 hr0 = 321", not w.isError(), str(w))
                r = rc.read_holding_registers(0, count=1, slave=2)
                check("RTU 回读 u2 hr0 = 321", (not r.isError()) and r.registers == [321],
                      str(r.registers if not r.isError() else r))

                http("/fault?unit=1&kind=hr&addr=1&action=exception&code=11", "POST",
                     base=RTU_BASE)
                r = rc.read_holding_registers(1, count=1, slave=1)
                check("RTU 上异常帧也透传（code=11 网关目标无响应）",
                      getattr(r, "exception_code", None) == 11,
                      f"exception_code={getattr(r, 'exception_code', '?')}")
                http("/fault/clear", "POST", base=RTU_BASE)
                rc.close()
        finally:
            p3.terminate()
            try:
                p3.wait(timeout=5)
            except Exception:
                p3.kill()

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print()
    print(f"合计: OK {ok} / FAIL {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())

# sim/ —— Modbus 模拟模式（开发与演示用）

**这个目录不参与固件构建。** 生产链路是 `components/` + `main/` → `build/`；
模拟链路是这里的 Python 服务。两者只通过「Modbus 协议」这一层打交道，
所以上位机 / 诊断工具不需要知道自己连的是模拟器还是真板子。

## 为什么单独一个目录

| | 生产（真实硬件） | 模拟（本目录） |
|---|---|---|
| 代码 | `components/` `main/`（C） | `sim/*.py`（Python） |
| 构建 | `idf.py build` | 不需要，直接跑 |
| 依赖 | ESP-IDF | `pymodbus==3.8.6` + `pyserial`（见 `requirements.txt`） |
| 产物 | `build/modbus-diag.bin` | 无 |
| 用途 | 交付 | 没有板子时开发 / 答辩演示 / 自动化测试 |

仿真件永远不进 `build/`，生产件永远不进 `sim/`。这条边界比"记得排除"可靠。

## 跑起来

建议用独立 venv（`pymodbus` 必须 3.8.x，系统 Python 里若是 3.13+ 会 import 失败）：

```bash
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r sim/requirements.txt      # Windows
# source .venv/bin/activate && pip install -r sim/requirements.txt   # Linux/macOS

.venv/Scripts/python.exe sim/modbus_sim.py                           # TCP 0.0.0.0:15020 + 控制接口 15021
.venv/Scripts/python.exe sim/modbus_sim.py --host 127.0.0.1 --port 15020
```

启动后：

- **Modbus TCP** `15020`：unit id 由 `devices.json` 决定，支持多从站（示例给了 1/2/3）
- **控制接口** `15021`：HTTP，用来在不重启服务的情况下改寄存器、看拓扑、**注入故障**

```bash
curl http://127.0.0.1:15021/health
curl "http://127.0.0.1:15021/read?unit=1&kind=hr&addr=0&count=4"
curl -X POST "http://127.0.0.1:15021/write?unit=2&kind=hr&addr=0&value=999"
```

`kind`：`hr` 保持寄存器 / `ir` 输入寄存器 / `co` 线圈 / `di` 离散输入。

## RTU 串口模式

```bash
.venv/Scripts/python.exe sim/modbus_sim.py --rtu COM3 --baud 115200
.venv/Scripts/python.exe sim/modbus_sim.py --rtu COM3 --baud 19200 --parity E --stopbits 2
```

给了 `--rtu` 就不开 TCP（控制接口照开，用来注故障 / 看寄存器）。
串口打不开时会**立刻**报错并列出本机可用串口，不会卡住。

> 只用一块板子、没有第二个串口时，可以装 [com0com](https://sourceforge.net/projects/com0com/)
> 建一对虚拟串口（COM10↔COM11），模拟器接一头，诊断工具接另一头。

## 故障注入（诊断工具的题眼）

模拟器能按「unit + 寄存器类型 + 地址」精确制造三类异常，用来验证诊断工具能不能认出来：

```bash
# 0x02 非法数据地址（默认码）
curl -X POST "http://127.0.0.1:15021/fault?unit=1&kind=hr&addr=1&action=exception"

# 指定异常码：1 非法功能 / 2 非法数据地址 / 3 非法数据值 / 4 从站设备故障 / 5 确认 / 6 从站忙
curl -X POST "http://127.0.0.1:15021/fault?unit=1&kind=hr&addr=1&action=exception&code=4"

# 超时：拖 4000ms 再回，客户端 1~3 秒超时就会判「设备不响应」
curl -X POST "http://127.0.0.1:15021/fault?unit=1&kind=hr&addr=2&action=delay&ms=4000"

# 看 / 清
curl http://127.0.0.1:15021/faults
curl -X POST "http://127.0.0.1:15021/fault/clear?unit=1"
curl -X POST "http://127.0.0.1:15021/fault/clear"
```

要读底层真实值（不想被注入干扰）时加 `&raw=1`：

```bash
curl "http://127.0.0.1:15021/read?unit=1&kind=hr&addr=0&count=4&raw=1"
```

也可以写死在 `devices.json` 里，启动即生效：

```json
{ "unit": 4, "name": "故障演示设备",
  "holding": { "0": 1 },
  "faults": [ { "kind": "hr", "addr": 0, "action": "exception", "code": 6 } ] }
```

## 改设备表

`devices.json` 里加一段就是多一个从站，不用改代码：

```json
{ "unit": 7, "name": "自定义设备",
  "holding": { "0": 100, "1": 200 },
  "input": {}, "coils": {}, "discrete": {} }
```

## 验证

```bash
.venv/Scripts/python.exe sim/test_sim.py     # 28 项断言，当前全绿
```

覆盖：基线读写 / 4 种异常码（1、2、4、6）/ 命中区间与未命中区间 / `raw=1` 绕过 /
延迟到超时 / 写操作被拦 / 参数校验 / RTU 分支报错路径。

## 三个坑（别踩）

1. **必须锁 pymodbus 3.8.6**。3.13+ 把 datastore 换成 async / `SimData` 体系，
   `ModbusDeviceContext` 没有同步 `getValues`；4.x 会移除旧接口。换版本前先重跑 `test_sim.py`。
2. **地址偏移**：datastore 里 `values[0]` 是占位，配置的 0 号放在 `values[1]`。
   服务端对「客户端读 0 号」会调 `getValues(fc, 0, n)` 内部再 +1。
   所以 HTTP 控制接口传 0 基地址，**不要自己再加偏移**（加过一版，结果整体错位一位）。
3. **`ExceptionResponse` 是 PDU 类，不是异常类**。`raise ExceptionResponse(...)` 会
   `TypeError: exceptions must derive from BaseException`，服务端兜底再包一层，
   线上变成 `0x80/0x04`（FC 是 0，客户端解不出帧）。要在 PDU 的 `update_datastore`
   里**返回**它，这也是本模拟器给故障注入包了一层 `update_datastore` 的原因。

## 还没做

- `hardware/` 原理图与 PCB（要在 GUI 里画，代码替代不了）
- RTU 只在参数/报错路径验证过 —— **没接过真实串口或 com0com 虚拟串口对**
- 广播（unit 0）、网关异常码（0x0A/0x0B）未支持

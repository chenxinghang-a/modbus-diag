# sim/ —— Modbus 模拟模式（开发与演示用）

**这个目录不参与固件构建。** 生产链路是 `components/` + `main/` → `build/`；
模拟链路是这里的 Python 服务。两者只通过「Modbus 协议」这一层打交道，
所以上位机 / 诊断工具不需要知道自己连的是模拟器还是真板子。

## 为什么单独一个目录

| | 生产（真实硬件） | 模拟（本目录） |
|---|---|---|
| 代码 | `components/` `main/`（C） | `sim/*.py`（Python） |
| 构建 | `idf.py build` | 不需要，直接跑 |
| 依赖 | ESP-IDF | `pymodbus`（见 `requirements.txt`） |
| 产物 | `build/modbus-diag.bin` | 无 |
| 用途 | 交付 | 没有板子时开发 / 答辩演示 / 自动化测试 |

仿真件永远不进 `build/`，生产件永远不进 `sim/`。这条边界比"记得排除"可靠。

## 跑起来

```bash
pip install -r sim/requirements.txt
python sim/modbus_sim.py                 # 默认 0.0.0.0:15020（Modbus TCP）+ 15021（控制接口）
python sim/modbus_sim.py --host 127.0.0.1 --port 15020
```

启动后：

- **Modbus TCP** `15020`：unit id 由 `devices.json` 决定，支持多从站（示例给了 1/2/3）
- **控制接口** `15021`：HTTP，用来在不重启服务的情况下改寄存器、看拓扑

```bash
curl http://127.0.0.1:15021/health
curl "http://127.0.0.1:15021/read?unit=1&kind=hr&addr=0&count=4"
curl -X POST "http://127.0.0.1:15021/write?unit=2&kind=hr&addr=0&value=999"
```

`kind`：`hr` 保持寄存器 / `ir` 输入寄存器 / `co` 线圈 / `di` 离散输入。

## 改设备表

`devices.json` 里加一段就是多一个从站，不用改代码：

```json
{ "unit": 7, "name": "自定义设备",
  "holding": { "0": 100, "1": 200 },
  "input": {}, "coils": {}, "discrete": {} }
```

## 已验证（2026-10-18，本机）

```
pymodbus 客户端读 unit1 hr0..3  -> [230, 500, 0, 1]   （与 devices.json 一致）
pymodbus 客户端读 unit1 ir0..1  -> [251, 250]
HTTP 写 unit2 hr0=999 后客户端读 -> [999, 51]          （写入对客户端可见）
越界读（unit2 只要 3 个寄存器却读 4 个）-> 异常响应 131（非法数据地址），符合协议
```

## 两个坑（别踩）

1. **必须锁 pymodbus 3.8.6**。3.13+ 把 datastore 换成 async / `SimData` 体系，
   `ModbusDeviceContext` 没有同步 `getValues`；4.x 会移除旧接口。换版本前先重跑上面的验证。
2. **地址偏移**：datastore 里 `values[0]` 是占位，配置的 0 号放在 `values[1]`。
   服务端对「客户端读 0 号」会调 `getValues(fc, 0, n)` 内部再 +1。
   所以 HTTP 控制接口传 0 基地址，**不要自己再加偏移**（加过一版，结果整体错位一位）。

## 还没做（下一步）

- RTU 串口模式（需要 `pyserial` + `com0com` 虚拟串口对）
- 异常注入（按 unit 返回 0x02/0x04，演"设备故障"）
- 超时注入（演"设备掉线"）

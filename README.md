# 便携式 Modbus 通信诊断工具（ESP32-S3）

面向工厂维护人员的**手持式 Modbus 诊断设备**：一根 RS-485 线接上总线，屏幕直接给出「通信健不健康、问题出在哪」。

与常见的「Modbus 调试助手」不同，本工具把三层信息串成一条链：

```
物理层（ADC 采 RS-485 波形） → 协议层（抓包 / 帧解析 / 异常码） → 结论层（健康评分 + 处置建议）
```

维护人员不需要看懂报文，只需要看「Fix: Check gateway link」这类能照着做的建议。

> **当前状态**：固件功能完整、可编译、有主机侧单元测试；`hardware/` 仅为 KiCad 工程骨架，
> **PCB 未打样、整机未焊接**。所有功能验证均未在实物上进行。

## 功能

| 功能 | 说明 | 主要代码 |
|---|---|---|
| Modbus RTU 主站 | RS-485，1200~115200bps，超时/重试可配并持久化 | `modbus_core/modbus_master.c`、`modbus_rtu.c` |
| Modbus TCP 客户端 | MBAP+PDU，事务号校验，断线自动回退串口 | `modbus_core/modbus_tcp.c` |
| 设备扫描 | 1~247 地址，五档波特率全档遍历或单档快速扫描 | `modbus_scanner/device_scanner.c` |
| 寄存器分类 | 探测保持/输入寄存器、线圈、离散输入，并判断可写性 | `modbus_scanner/register_classifier.c` |
| 寄存器读写 | 单点写（FC05/06）+ 读回确认，逐位输入 | `display/pages/page_device_info.c` |
| 被动抓包 | UART 旁路监听，CRC 校验、流量统计、帧列表 | `protocol_analyzer/` |
| 波形采集 | ADC 100KSPS DMA 连续采样，双缓冲防撕裂，Vpp/DC/触发 | `waveform/` |
| 健康评分 | 响应时间 0.3 / 错误率 0.4 / 抖动 0.3 加权，0~100 分 | `diag_report/health_metrics.c` |
| 诊断报告 | JSON + HTML（含诊断建议），导出到 SPIFFS | `diag_report/report_generator.c` |
| 操作日志 | `/spiffs/oplog.txt`，8KB 轮转，Web 可看 | `storage/log_storage.c` |
| Web 服务 | `GET /`、`/report`（优先 HTML）、`/log` | `wifi_service/web_server.c` |

## 目录结构

```
main/                    启动流程、任务创建、事件总线消费者
components/
  common_config/         引脚与全局参数（唯一配置源）
  modbus_core/           RTU/TCP 协议栈 + 主站收发（含重试与传输层分发）
  modbus_scanner/        设备扫描、寄存器类型分类
  protocol_analyzer/     抓包、帧解析、流量统计
  waveform/              ADC 采样、滤波与测量、波形绘制
  display/               TFT 驱动、GUI 框架、8 个页面
  input/                 按键扫描（含 800ms 长按）
  diag_report/           健康评分、报告生成与导出
  storage/               NVS 配置、SPIFFS 文件与操作日志
  wifi_service/          WiFi AP/STA、HTTP 服务、mDNS
host_tests/              主机侧单元测试（纯 C，gcc 直接跑）
sim/                     Python 从站模拟器（开发/演示用，不进固件）
web/                     本地调试用的网页原型
hardware/                KiCad 工程骨架（原理图/PCB 未完成）
论文/                    毕业论文 Markdown 与 docx 生成脚本
```

## 构建

需要 ESP-IDF **v5.4**：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

> 若 `idf.py` 报找不到 python 环境，先确认 `IDF_PATH` 与本机 python 版本匹配；
> 本仓库在 Windows 上的经验是：手工设 `IDF_PATH` 并把
> `xtensa-esp-elf/*/xtensa-esp-elf/bin`、`ninja`、`ccache` 加进 `PATH` 后，
> 直接 `cmake --build build --parallel 2` 也能完整构建。

## 测试

两层验收，**都不需要开发板**：

```bash
# 1) 主机侧单元测试：协议 / UI 编辑 / TCP / 信号 / 报告 / 健康评分 / 流量统计，206 项断言
bash host_tests/run.sh

# 2) 从站模拟器自检：TCP/RTU 帧、故障注入、广播、异常码，45 项断言
cd sim && python test_sim.py

# 3) 端到端互通：用固件自己的帧层和模拟器对话（读/写/异常/超时，11 项）
bash host_tests/run_e2e.sh
```

三层测试都在 GitHub Actions 里自动执行（`.github/workflows/host-tests.yml`），每次 push 都会跑。
固件构建需要完整 ESP-IDF 环境（下载数百 MB、耗时数分钟），不放进 CI。

主机测试覆盖的是「编译通过也发现不了」的错误：响应帧错位、`len=0` 除零、
`INT32_MAX` 哨兵值写进报告、CRC 查表与逐位算法不一致等。

## 从站模拟器

没有板子也没有从站时，用它代替真实设备：
```bash
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r sim/requirements.txt
.venv/Scripts/python.exe sim/modbus_sim.py            # TCP 0.0.0.0:15020 + 控制接口 15021
.venv/Scripts/python.exe sim/modbus_sim.py --rtu "socket://127.0.0.1:15040"   # RTU over TCP，免 com0com
```

支持按 `unit + 寄存器类型 + 地址` 精确注入异常码、延迟与网关类异常（0x0A/0x0B），
用于验证诊断逻辑。详见 `sim/README.md`。

## 上板验证

主机测试和模拟器覆盖不到的部分 —— UI 布局、长按手感、RS-485 电气层、TCP 断线回退、
波形测量精度 —— 需要实物板。首次上板请按 **[`docs/上板验证清单.md`](docs/上板验证清单.md)**
逐项走一遍：每项都写了预期结果和「不符时先查什么」，最后一节是最容易出问题的五个点。

## 硬件设计包

`hardware/` 提供可核对的设计数据（[详见 hardware/README.md](hardware/README.md)）：

- `netlist.json` —— 单一数据源：43 个元件的网表
- `BOM.csv` —— 由网表生成，每个元件带 MPN 与封装
- `tools/check_hardware.py` —— **固件引脚 ↔ 硬件网表**一致性自检（32 项，已接入 CI）

引脚改动若忘了同步硬件，CI 会直接报错。PCB 布局布线尚未进行（KiCad 工程为骨架）。

## 已知限制

- **硬件未完成**：`hardware/` 只有 KiCad 工程骨架，PCB 未打样、未焊接；第 7 章按「设计 + 模块级验证」口径撰写。
- **TCP 单连接**：同一时刻只连一台从站（便携工具的使用场景），不支持并发连接。
- **批量写无界面**：FC0F/FC10 在协议层已实现，四键机身无法选区间，未做 UI。
- **无 RTC**：操作日志时间戳为开机计时 `[T+12.345s]`。
- **波形为单端测量**：A 线对地，经 1/4 分压；不含隔离放大，共模范围受限。
- **无文本输入**：四键 + 320×240 屏，Wi-Fi 密码等需在 `app_config.h` 中配置。

## 论文

`论文/毕业论文_完整版.md` 为源文件，`generate_docx.py` 生成排版件：

```bash
python 论文/generate_docx.py     # 输出 论文/毕业论文_排版版.docx
```

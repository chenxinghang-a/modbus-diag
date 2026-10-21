#!/usr/bin/env python3
"""硬件网表自检：把「固件引脚」和「硬件网表」强制绑在一起。

查四类问题：
  1. 网表内部一致性     —— 元件是否存在、同一 pin 是否被接到两个网、有没有悬空元件
  2. 固件 ↔ 硬件一致性   —— app_config.h 里的引脚号必须与 netlist.json 的 U1.IOxx 对得上
  3. 历史踩过的坑       —— ADC 通道与 RS-485 方向脚不能撞同一个 GPIO
  4. BOM 是否最新       —— BOM.csv 必须与 netlist.json 的元件表一致

用法： python hardware/tools/check_hardware.py
返回非零表示有问题，可直接接 CI。
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
REPO = os.path.dirname(HW)
CFG = os.path.join(REPO, "components", "common_config", "app_config.h")
NETLIST = os.path.join(HW, "netlist.json")
BOM = os.path.join(HW, "BOM.csv")

# 固件宏 → 期望的网络名。GPIO 号本身从 app_config.h 读，不写死。
FIRMWARE_MAP = [
    ("TFT_PIN_MOSI", "SPI_MOSI"),
    ("TFT_PIN_SCK", "SPI_SCK"),
    ("TFT_PIN_CS", "TFT_CS"),
    ("TFT_PIN_DC", "TFT_DC"),
    ("TFT_PIN_RST", "TFT_RST"),
    ("TFT_PIN_BL", "TFT_BL"),
    ("TOUCH_PIN_CS", "TOUCH_CS"),
    ("TOUCH_PIN_IRQ", "TOUCH_IRQ"),
    ("RS485_PIN_TX", "RS485_TX"),
    ("RS485_PIN_RX", "RS485_RX"),
    ("RS485_PIN_DE", "RS485_DE"),
    ("BTN_PIN_UP", "KEY_UP"),
    ("BTN_PIN_DOWN", "KEY_DOWN"),
    ("BTN_PIN_OK", "KEY_OK"),
    ("BTN_PIN_BACK", "KEY_BACK"),
    ("LED_PIN", "LED_STATUS"),
]

# ESP32-S3 ADC1 通道 → GPIO（用于 WAVE_ADC_CHANNEL 校验）
ADC1_CH_TO_GPIO = {0: 1, 1: 2, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 10}

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


def parse_defines(path):
    text = open(path, encoding="utf-8").read()
    return {m.group(1): m.group(2)
            for m in re.finditer(r"#define\s+([A-Z0-9_]+)\s+([^\s/]+)", text)}


def main():
    nl = json.load(open(NETLIST, encoding="utf-8"))
    comps, nets = nl["components"], nl["nets"]

    print("=== 网表内部一致性 ===")
    pin_owner = {}
    conflict = []
    for net, members in nets.items():
        for ref, pin in members:
            if ref not in comps:
                conflict.append(f"{net}: 元件 {ref} 不在元件表中")
                continue
            key = (ref, pin)
            if key in pin_owner and pin_owner[key] != net:
                conflict.append(f"{ref}.{pin} 同时出现在 {pin_owner[key]} 和 {net}")
            pin_owner[key] = net
    check("所有网表端点都能找到元件", not any("不在元件表" in c for c in conflict))
    check("同一 pin 没有接到两个网", not any("同时出现在" in c for c in conflict),
          "; ".join(conflict[:3]))

    used = {ref for m in nets.values() for ref, _ in m}
    floating = sorted(set(comps) - used)
    check("没有悬空元件", not floating, ", ".join(floating))

    check("GND 网存在且挂了 U1", any(r == "U1" for r, _ in nets.get("GND", [])),
          f"{len(nets.get('GND', []))} 个节点")
    check("3V3 网存在且连到 U4.VO",
          ["U4", "VO"] in nets.get("3V3", []), f"{len(nets.get('3V3', []))} 个节点")

    print("=== 固件 ↔ 硬件引脚一致性 ===")
    d = parse_defines(CFG)
    # 先建 IO 号 → 所在网络 的反查表
    io_net = {}
    for net, members in nets.items():
        for ref, pin in members:
            if ref == "U1" and pin.startswith("IO"):
                io_net[int(pin[2:])] = net

    for macro, expect_net in FIRMWARE_MAP:
        if macro not in d:
            check(f"{macro} 在 app_config.h 中定义", False)
            continue
        try:
            gpio = int(d[macro], 0)
        except ValueError:
            check(f"{macro} 的值可解析为整数", False, d[macro])
            continue
        actual = io_net.get(gpio, "<未连接>")
        check(f"{macro}=GPIO{gpio} → {expect_net}", actual == expect_net,
              "" if actual == expect_net else f"网表中该脚在 {actual}")

    # ADC 通道：WAVE_ADC_CHANNEL = ADC_CHANNEL_4 → GPIO5
    chan_raw = d.get("WAVE_ADC_CHANNEL", "")
    m = re.search(r"(\d+)", chan_raw)
    if m:
        gpio = ADC1_CH_TO_GPIO.get(int(m.group(1)))
        check(f"WAVE_ADC_CHANNEL={chan_raw} → GPIO{gpio} 接在 ADC_IN",
              io_net.get(gpio) == "ADC_IN", f"实际在 {io_net.get(gpio)}")
        # 论文 4.3.1 记录过的历史冲突：ADC 与 RS-485 方向脚撞同一个 GPIO
        check("ADC 脚与 RS-485 DE 脚不是同一个 GPIO",
              gpio != int(d.get("RS485_PIN_DE", "-1"), 0),
              f"ADC=GPIO{gpio}, DE=GPIO{d.get('RS485_PIN_DE')}")

    print("=== 关键器件的必要连接 ===")
    for ref, pin, net in [
        ("U2", "RO", "RS485_RX"), ("U2", "DI", "RS485_TX"),
        ("U2", "DE", "RS485_DE"), ("U2", "RE", "RS485_DE"),
        ("U3", "UD+", "USB_DP"), ("U3", "UD-", "USB_DM"),
        ("U1", "IO19", None),  # 原生 USB 预留，不接 USB 连接器
    ]:
        if net is None:
            check(f"{ref}.{pin} 未接入 USB 连接器（避免与 CH340 抢总线）",
                  io_net.get(19) != "USB_DP")
            continue
        check(f"{ref}.{pin} 在 {net}", [ref, pin] in nets.get(net, []))

    print("=== BOM 与网表同步 ===")
    if os.path.exists(BOM):
        import csv
        rows = list(csv.DictReader(open(BOM, encoding="utf-8-sig")))
        bom_refs = {r["Ref"] for r in rows}
        check("BOM 元件与网表元件完全一致", bom_refs == set(comps),
              f"BOM {len(bom_refs)} 个 / 网表 {len(comps)} 个"
              if bom_refs != set(comps) else f"{len(bom_refs)} 个")
        check("BOM 每个元件都有 MPN",
              all(r["MPN"].strip() for r in rows))
    else:
        check("BOM.csv 存在（跑 gen_bom.py 生成）", False)

    print(f"\n合计: OK {ok} / FAIL {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

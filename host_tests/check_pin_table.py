#!/usr/bin/env python3
"""硬件接线表（hardware/README.md）与固件引脚宏（app_config.h）的一致性检查。

为什么需要：`PROTOTYPE.md` 让用户照这张表接线，表一旦与固件不符，
接出来的板子就是错的，而且现象千奇百怪（屏不亮、按键乱跳、485 不通）。
文档漂移比代码 bug 更难发现 —— 代码有测试，文档没有。

做法：解析 README 表格里「一行一个 GPIO」的行，按网络名映射到 app_config 宏，
逐个比对数字。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 网络名 → app_config.h 宏名
NET2MACRO = {
    "SPI_SCK": "TFT_PIN_SCK",
    "SPI_MOSI": "TFT_PIN_MOSI",
    "TFT_CS": "TFT_PIN_CS",
    "TFT_DC": "TFT_PIN_DC",
    "TFT_RST": "TFT_PIN_RST",
    "TFT_BL": "TFT_PIN_BL",
    "TOUCH_CS": "TOUCH_PIN_CS",
    "TOUCH_IRQ": "TOUCH_PIN_IRQ",
    "RS485_TX": "RS485_PIN_TX",
    "RS485_RX": "RS485_PIN_RX",
    "RS485_DE": "RS485_PIN_DE",
    "LED_STATUS": "LED_PIN",
}


def load_macros():
    path = os.path.join(ROOT, "components/common_config/app_config.h")
    src = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"#define\s+(\w+)\s+(\d+)", src):
        out[m.group(1)] = int(m.group(2))
    return out


def load_table():
    """从 README 表格里抽「单 GPIO」行：net → gpio。"""
    path = os.path.join(ROOT, "hardware/README.md")
    out = {}
    for line in open(path, encoding="utf-8"):
        if not line.startswith("|"):
            continue
        cols = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cols) < 3:
            continue
        gpio_cell, net_cell = cols[1], cols[2]
        gpios = re.findall(r"\d+", gpio_cell)
        # 只保留已知的固件网络名：说明列里常混着 "2N7002" 这类型号，不该参与配对
        nets = [n for n in re.findall(r"[A-Z][A-Z0-9_]{2,}", net_cell) if n in NET2MACRO]
        if not gpios or len(gpios) != len(nets):
            continue                                # 数量对不上就跳过，不猜
        for gpio, net in zip(gpios, nets):
            if net in NET2MACRO:
                out[net] = int(gpio)
    return out


def main():
    macros = load_macros()
    table = load_table()
    ok = bad = 0
    print("=== 接线表 ↔ 固件引脚宏 ===")
    for net, gpio in sorted(table.items()):
        macro = NET2MACRO[net]
        if macro not in macros:
            print(f"  [FAIL] {net}: 表里有，但 app_config.h 没有 {macro}")
            bad += 1
            continue
        if macros[macro] != gpio:
            print(f"  [FAIL] {net}: 表写 GPIO{gpio}，固件是 GPIO{macros[macro]}（{macro}）")
            bad += 1
        else:
            print(f"  [ OK ] {net} = GPIO{gpio}")
            ok += 1

    # 表里没提到的宏也要点出来（可能漏写进接线表）
    missing = [m for n, m in NET2MACRO.items() if n not in table]
    if missing:
        print(f"  [ -- ] 接线上未列出（多引脚或未收录）：{', '.join(missing)}")

    print(f"\n合计: OK {ok} / FAIL {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

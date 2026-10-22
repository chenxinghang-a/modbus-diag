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
        # 背光走 2N7002 低边开关（论文 4.4.1），不是 GPIO 直驱
        ("Q1", "G", "TFT_BL"), ("Q1", "D", "BLK_LED"), ("J3", "LED", "BLK_LED"),
        ("U1", "IO19", None),  # 原生 USB 预留，不接 USB 连接器
    ]:
        if net is None:
            check(f"{ref}.{pin} 未接入 USB 连接器（避免与 CH340 抢总线）",
                  io_net.get(19) != "USB_DP")
            continue
        check(f"{ref}.{pin} 在 {net}", [ref, pin] in nets.get(net, []))

    # ---------- GPIO 冲突检测（开发板接线时最容易踩的坑） ----------
    # 两个功能抢同一个 GPIO：编译不报错、上板后两个功能一起抽。
    # 典型场景：照抄设计文件的引脚到开发板，发现 TFT 的 CS 和 485 的 DE 是同一个脚。
    seen_gpio = {}
    for macro, _ in FIRMWARE_MAP:
        if macro not in d:
            continue
        try:
            g = int(d[macro], 0)
        except ValueError:
            continue
        if g in seen_gpio:
            check(f"GPIO{g} 只被一个功能占用", False,
                  f"{seen_gpio[g]} 与 {macro} 冲突")
        else:
            seen_gpio[g] = macro
            check(f"GPIO{g} → {macro} 无冲突", True)

    # ESP32-S3 的 GPIO19/20 是 USB D-/D+，接了 USB 或 JTAG 就不能挪作他用
    for macro, _ in FIRMWARE_MAP:
        if macro in d:
            try:
                g = int(d[macro], 0)
            except ValueError:
                continue
            if g in (19, 20):
                check(f"{macro} 未占用 USB D-/D+ (GPIO{g})", False,
                      "GPIO19/20 是 USB 与 JTAG 专用")

    # ---------- 悬空引脚与 USB-C 供电 ----------
    # 两端器件只有一端进网络 = 另一端悬空，这是最典型的手误（漏画一根线）。
    # 曾经的真实案例：R1/R2（5.1k CC 下拉）只接了 GND 一端，
    # 结果 J1 的 CC1/CC2 根本没接 —— 标准 C-to-C 线插上去主机不给电。
    import collections
    used = collections.defaultdict(set)
    for net, members in nets.items():
        for ref, pin in members:
            used[ref].add(pin)

    dangling = []
    for ref in comps:
        pkg = comps[ref].get("package", "")
        if ref.startswith(("R", "C", "D", "F")):
            if len(used.get(ref, ())) < 2:
                dangling.append(f"{ref}({sorted(used.get(ref, []))})")
    check("两端器件没有悬空引脚", not dangling, ", ".join(dangling))

    # J1 的 CC1/CC2 必须各经 5.1k 下拉到 GND（UFP 请求 5V 的必要条件）
    for cc, res in (("CC1", "R1"), ("CC2", "R2")):
        net = [n for n, m in nets.items() if ["J1", cc] in m]
        # 注意别用 ok 当局部变量：它会覆盖模块级的计数器，导致汇总行变成 "OK True"
        cc_ok = bool(net) and [res, "1"] in nets[net[0]]                 and [res, "2"] in nets.get("GND", [])
        check(f"J1.{cc} 经 {res} 下拉到 GND", cc_ok,
              f"{res}={comps.get(res, {}).get('value', '?')}")

    # ---------- strapping 脚必须有确定的启动电平 ----------
    # ESP32-S3 的 GPIO0/3/45/46 在复位瞬间被采样，电平决定启动模式。
    # 悬空 = 电平不确定，可能出现「上电偶尔不启动」这种最难查的故障。
    # 规则：用到 strapping 脚时，该网络上必须有一个电阻接到 3V3 或 GND。
    STRAPPING = {"IO0": "GPIO0", "IO3": "GPIO3", "IO45": "GPIO45", "IO46": "GPIO46"}
    for pin, name in STRAPPING.items():
        net = io_net.get(int(pin[2:]))
        if net is None:
            check(f"{name} 未使用（无需固定电平）", True)
            continue
        members = nets.get(net, [])
        has_r = any(r.startswith("R") for r, _ in members)
        check(f"{name} 有确定启动电平（{net} 上有电阻）", has_r,
              net if has_r else f"{net} 只有 {[f'{a}.{b}' for a, b in members]}")

    # ---------- 电源与 485 偏置 ----------
    # LDO 的输入/输出都必须在**芯片引脚附近**有电容。10uF 在 PTC 之前不算数：
    # 负载跳变时靠的是 VIN 脚旁边那颗，缺了会掉压甚至振荡。
    def net_has_cap(net_name):
        return any(r.startswith("C") for r, _ in nets.get(net_name, []))

    vin_net = next((n for n, m in nets.items() if ["U4", "VI"] in m), None)
    vout_net = next((n for n, m in nets.items() if ["U4", "VO"] in m), None)
    check("LDO 输入端有电容", bool(vin_net) and net_has_cap(vin_net),
          vin_net or "VI 未接")
    check("LDO 输出端有电容", bool(vout_net) and net_has_cap(vout_net),
          vout_net or "VO 未接")

    # RS-485 空闲时若 A/B 无偏置，差分电压不定，接收器会输出随机跳变 ——
    # 抓包页表现为「总线上没设备却一直有 0x00/0xFF」。
    def pin_net(ref, pin):
        return next((n for n, m in nets.items() if [ref, pin] in m), None)

    def res_to(net_name, target):
        """该网络上是否有电阻，其**另一端**接在 target 上（不是随便一个电阻就算）。"""
        for ref, pin in nets.get(net_name, []):
            if not ref.startswith("R"):
                continue
            other = "2" if pin == "1" else "1"
            if pin_net(ref, other) == target:
                return ref
        return None

    a_net = pin_net("U2", "A")
    b_net = pin_net("U2", "B")
    ra = res_to(a_net, "3V3") if a_net else None
    rb = res_to(b_net, "GND") if b_net else None
    check("485_A 有偏置电阻到 3V3", ra is not None,
          f"{ra} 从 {a_net} 到 3V3" if ra else f"{a_net} 上没有到 3V3 的电阻")
    check("485_B 有偏置电阻到 GND", rb is not None,
          f"{rb} 从 {b_net} 到 GND" if rb else f"{b_net} 上没有到 GND 的电阻")

    # ---------- 电源预算 ----------
    # PTC 的 hold 电流随温度降额（50°C 时约打七折），所以额定值必须留足余量。
    # 曾经的 0.5A 对 435mA 峰值：常温勉强、装机升温后 WiFi 一发射就跳闸，
    # 现象是「连 WiFi 就重启」，很容易误判成固件问题。
    budget_ma = {
        "ESP32-S3 WiFi 发射峰值": 350,
        "TFT 背光": 60,
        "CH340": 15,
        "RS-485": 10,
    }
    peak = sum(budget_ma.values())
    f1 = comps.get("F1", {}).get("value", "")
    import re as _re
    m_amp = _re.search(r"([\d.]+)\s*A", f1)
    rating = float(m_amp.group(1)) * 1000 if m_amp else 0
    check(f"PTC 额定 ≥ 1.5× 峰值（{peak}mA → 需 {int(peak*1.5)}mA）",
          rating >= peak * 1.5, f"F1={f1}, 额定约 {int(rating)}mA")

    # ---------- ADC 输入保护 ----------
    # RS-485 标准允许 -7V 共模；经 1/4 分压后 ADC 会看到 -1.75V，
    # 而 ESP32 的 ADC 绝对最小值是 -0.3V。没有钳位二极管就是在赌现场不出现负共模。
    adc_net = next((n for n, m in nets.items() if ["U1", "IO5"] in m), None)
    has_clamp = False
    if adc_net:
        for ref, pin in nets.get(adc_net, []):
            if not ref.startswith("D"):
                continue
            other = "A" if pin == "K" else "K"
            if next((n for n, m in nets.items() if [ref, other] in m), None) == "GND":
                has_clamp = True
                break
    check("ADC 输入有对地钳位（防 RS-485 负共模）", has_clamp,
          adc_net or "ADC 未接")

    # ---------- 485 偏置的「有效性」而不只是「存在性」 ----------
    # RS-485 接收器要 ≥200mV 差分才算确定电平。
    # 偏置电流 I = 3.3 / (R上 + R终端 + R下)，差分 = I × 120Ω。
    # 10k/10k 在 120Ω 上只有 20mV —— 装上去等于没装，这是我第一版的错误。
    def res_value_ohm(ref):
        v = comps.get(ref, {}).get("value", "")
        m = _re.match(r"^([\d.]+)\s*([kKmM]?)", v)
        if not m:
            return None
        mult = {"": 1, "k": 1e3, "K": 1e3, "m": 1e-3, "M": 1e6}[m.group(2)]
        return float(m.group(1)) * mult

    if ra and rb:
        r1, r2 = res_value_ohm(ra), res_value_ohm(rb)
        if r1 and r2:
            vdiff_mv = 3.3 / (r1 + 120 + r2) * 120 * 1000
            check(f"485 偏置能产生 ≥200mV 差分（实测 {vdiff_mv:.0f}mV）",
                  vdiff_mv >= 200,
                  f"{ra}={r1:.0f}Ω, {rb}={r2:.0f}Ω")

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

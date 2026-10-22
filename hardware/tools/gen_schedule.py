#!/usr/bin/env python3
"""从 netlist.json 生成「原理图绘制工作表」hardware/SCHEDULE.md。

定位：KiCad 装不了（需要管理员），原理图没法在这里画出并 ERC 验证。
但画图所需的信息全在网表里 —— 把网表翻译成「逐元件引脚 → 接哪个网络 → 同网还有谁」
的表格，人工照着连就行，比对着一个 49 元的 JSON 猜要快得多。

用法： python hardware/tools/gen_schedule.py
CI 会校验生成结果与仓库里的文件一致（网表改了不同步会红）。
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)

POWER_NETS = {"3V3", "+5V", "VBUS", "GND", "V5_ISO"}
DIFF_PAIRS = [("RS485_A", "RS485_B")]

DRAW_ORDER = [
    ("U1", "主控：先放 ESP32-S3-WROOM-1，它是所有网络的枢纽"),
    ("U4", "电源：AMS1117 + 输入输出电容（C1/C2/C3/C4/C11）"),
    ("U3", "下载：CH340C + USB 座 J1 + ESD U5 + CC 下拉 R1/R2"),
    ("U2", "RS-485：MAX3485 + 端接 R6/JP1 + 偏置 R14/R15 + TVS D4/D5"),
    ("J3", "显示：14P 排座，注意 VCC 与 T_CLK/T_DIN"),
    ("SW1-SW6", "按键与复位/BOOT：4 个功能键 + BOOT + RST"),
    ("U1_GPIO", "剩余：R5/R10/C10、LED D1/D2/R8/R9、背光 Q1/R11"),
    ("ADC", "波形：R3/R4 分压 + D6 钳位"),
]


def main():
    nl = json.load(open(os.path.join(HW, "netlist.json"), encoding="utf-8"))
    comps, nets = nl["components"], nl["nets"]

    # 反向索引：元件 → [(pin, net)]
    comp_pins = {}
    for net, members in nets.items():
        for ref, pin in members:
            comp_pins.setdefault(ref, []).append((pin, net))

    def others(ref, net):
        return [f"{r}.{p}" for r, p in nets[net] if r != ref]

    out = []
    out.append("# 原理图绘制工作表")
    out.append("")
    out.append("> 本文件由 `hardware/tools/gen_schedule.py` 从 `netlist.json` 自动生成，请勿手改。")
    out.append("> 打开 KiCad → 新建工程 → 按下面的顺序放符号 → 照「同网其它成员」连线。")
    out.append("")
    out.append(f"元件 **{len(comps)}** 个，网络 **{len(nets)}** 条。")
    out.append("")

    out.append("## 1. 绘制顺序")
    out.append("")
    out.append("| 步骤 | 内容 |")
    out.append("|---|---|")
    for i, (_, desc) in enumerate(DRAW_ORDER, 1):
        out.append(f"| {i} | {desc} |")
    out.append("")

    out.append("## 2. 逐元件引脚连接表")
    out.append("")
    out.append("每个元件一颗子表。**引脚名按网表**，KiCad 符号上的引脚名通常一致，按名字连即可。")
    out.append("")
    order = sorted(comps, key=lambda r: (r[0], int("".join(filter(str.isdigit, r)) or 0)))
    for ref in order:
        c = comps[ref]
        pins = comp_pins.get(ref, [])
        out.append(f"### {ref} — {c.get('value','')}（{c.get('package','')}）")
        out.append("")
        if c.get("note"):
            out.append(f"*{c['note']}*")
            out.append("")
        if not pins:
            out.append("**未连接任何网络** ⚠️")
            out.append("")
            continue
        out.append("| 引脚 | 网络 | 同网其它成员 |")
        out.append("|---|---|---|")
        for pin, net in sorted(pins, key=lambda t: t[0]):
            out.append(f"| {pin} | `{net}` | {', '.join(others(ref, net)) or '—'} |")
        out.append("")

    out.append("## 3. 逐网络成员表（画完用它自查）")
    out.append("")
    out.append("| 网络 | 成员 | 说明 |")
    out.append("|---|---|---|")
    for net in sorted(nets, key=lambda n: (n not in POWER_NETS, n)):
        members = ", ".join(f"{r}.{p}" for r, p in nets[net])
        note = ""
        if net in POWER_NETS:
            note = "电源/地"
        for a, b in DIFF_PAIRS:
            if net == a:
                note = f"差分对（与 {b} 走 100Ω 差分）"
            elif net == b:
                note = f"差分对（与 {a} 走 100Ω 差分）"
        if len(nets[net]) < 2:
            note = "⚠️ 只有一个成员，检查是否漏接"
        out.append(f"| `{net}` | {members} | {note} |")
    out.append("")
    out.append("## 4. 画完后的自查清单")
    out.append("")
    out.append("- [ ] 每个元件都在图上，且引脚数与封装一致")
    out.append("- [ ] 电源符号齐全：3V3 / +5V / VBUS / GND")
    out.append("- [ ] 差分对（RS485_A/B）用差分线，长度尽量等长")
    out.append("- [ ] 去耦电容画在对应芯片的电源脚旁边（电气上并联即可，位置在 PCB 决定）")
    out.append("- [ ] 执行 ERC，网络标签重名/未连接引脚全部清零")
    out.append("")
    out.append("> ERC 清零后，用 KiCad 导出网表，与本目录 `netlist.json` 对比元件数与网络数应一致。")
    out.append("")

    text = "\n".join(out)
    dst = os.path.join(HW, "SCHEDULE.md")
    open(dst, "w", encoding="utf-8").write(text)
    print(f"已生成 {dst}：{len(comps)} 元件 / {len(nets)} 网络")


if __name__ == "__main__":
    main()

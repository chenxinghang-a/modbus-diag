#!/usr/bin/env python3
"""板级布局方案：显式给出 43 个元件的坐标，再用几何检查验证「不重叠、在板内、接插件靠边」。

为什么不用自动打包：自动行排会把 ESP32 模组顶出板框、把 USB 座塞到板中间。
布局本来就是工程判断，脚本的价值是**验证**（重叠/越界/边缘朝向），不是替代判断。

尺寸按封装标称值（0402=1.0×0.5mm 等）+ 0.6mm 丝印余量。

用法： python hardware/tools/gen_placement.py
输出： hardware/placement.csv、hardware/placement.svg
"""
import csv
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)

BOARD_W, BOARD_H = 100.0, 70.0
EDGE = 3.0          # 元件距板边最小间隙
GAP = 0.6           # 相邻元件最小间隙

# (x, y, w, h) 单位 mm，原点在板左下角（SVG 里翻转成左上）
PLACE = {
    # ---- 电源链（左上）----
    "U4":  (3.0, 3.0, 6.5, 3.5),   # AMS1117
    "C1":  (10.2, 3.0, 3.2, 1.6),  # 10uF 钽
    "C3":  (10.2, 5.4, 3.2, 1.6),  # 22uF 钽
    "C2":  (14.2, 3.0, 1.0, 0.5),
    "C4":  (14.2, 4.2, 1.0, 0.5),
    "F1":  (16.0, 3.0, 2.0, 1.25), # PTC
    "D3":  (19.0, 3.0, 5.3, 3.6),  # SMBJ5.0A
    "D1":  (3.0, 8.0, 1.6, 0.8),   # 电源灯
    "R8":  (5.6, 8.0, 1.0, 0.5),

    # ---- 主控（中上）----
    "U1":  (34.0, 3.0, 25.5, 18.0),
    "C5":  (60.6, 3.0, 1.0, 0.5),
    "C6":  (60.6, 4.2, 1.0, 0.5),
    "R10": (60.6, 5.4, 1.0, 0.5),
    "C10": (60.6, 6.6, 1.0, 0.5),
    "D2":  (62.6, 3.0, 1.6, 0.8),
    "R9":  (62.6, 4.4, 1.0, 0.5),

    # ---- RS-485（右上）----
    "U2":  (68.0, 3.0, 4.9, 3.9),
    "C7":  (73.6, 3.0, 1.0, 0.5),
    "R7":  (73.6, 4.2, 1.0, 0.5),
    "R3":  (75.4, 3.0, 1.0, 0.5),
    "R4":  (75.4, 4.2, 1.0, 0.5),
    "R6":  (77.4, 3.0, 2.0, 1.25),
    "JP1": (80.2, 3.0, 5.5, 2.6),
    "D4":  (68.0, 8.4, 5.3, 3.6),
    "D5":  (74.4, 8.4, 5.3, 3.6),
    "J2":  (86.8, 3.0, 10.2, 8.0),  # 端子，贴右边缘

    # ---- 下载 / USB（左中）----
    "J1":  (3.0, 26.0, 9.0, 7.5),   # USB-C，贴左边缘
    "U3":  (14.4, 26.0, 9.9, 3.9),  # CH340C
    "U5":  (14.4, 31.2, 3.0, 1.6),  # USBLC6
    "R1":  (18.2, 31.2, 1.0, 0.5),
    "R2":  (19.8, 31.2, 1.0, 0.5),
    "C8":  (14.4, 34.6, 1.0, 0.5),   # CH340 V3 去耦
    "C9":  (16.2, 34.6, 1.0, 0.5),   # CH340 VCC 去耦

    # ---- 显示接口（中下）----
    "J3":  (33.0, 40.0, 35.6, 2.6), # 1x14 排针，横放

    # ---- 按键（下排）----
    "SW1": (5.0, 50.0, 6.0, 6.0),
    "SW2": (14.0, 50.0, 6.0, 6.0),
    "SW3": (23.0, 50.0, 6.0, 6.0),
    "SW4": (32.0, 50.0, 6.0, 6.0),
    "SW5": (41.0, 50.0, 6.0, 6.0),
    "SW6": (50.0, 50.0, 6.0, 6.0),

    # ---- 背光与其余小件（右下）----
    "Q1":  (60.0, 50.0, 2.9, 1.3),
    "R11": (63.6, 50.0, 1.0, 0.5),
    "R5":  (65.4, 50.0, 1.0, 0.5),
    "R12": (67.2, 50.0, 1.0, 0.5),   # 触控 IRQ 上拉
    "R13": (69.0, 50.0, 1.0, 0.5),   # 触控 CS 下拉
    "R14": (70.8, 50.0, 1.0, 0.5),   # 485 A 偏置
    "R15": (72.6, 50.0, 1.0, 0.5),   # 485 B 偏置
    "C11": (74.4, 50.0, 1.6, 0.8),   # LDO 输入去耦
}

EDGE_PARTS = {"J1", "J2"}


def main():
    nl = json.load(open(os.path.join(HW, "netlist.json"), encoding="utf-8"))
    comps = nl["components"]

    missing = sorted(set(comps) - set(PLACE))
    extra = sorted(set(PLACE) - set(comps))
    problems = []
    if missing:
        problems.append("未摆放的元件: " + ", ".join(missing))
    if extra:
        problems.append("坐标表里有网表中不存在的元件: " + ", ".join(extra))

    # 越界
    for comp, (x, y, w, h) in PLACE.items():
        if comp in EDGE_PARTS:
            if not (x <= EDGE + 0.5 or y <= EDGE + 0.5 or
                    x + w >= BOARD_W - EDGE - 0.5 or y + h >= BOARD_H - EDGE - 0.5):
                problems.append(f"{comp}（接插件）没有靠板边")
            continue
        if x < EDGE - 1e-6 or y < EDGE - 1e-6 or \
           x + w > BOARD_W - EDGE + 1e-6 or y + h > BOARD_H - EDGE + 1e-6:
            problems.append(f"{comp} 越界: ({x},{y}) {w}x{h}")

    # 重叠（含 GAP 间距）
    items = sorted(PLACE.items())
    for i in range(len(items)):
        a, (ax, ay, aw, ah) = items[i]
        for j in range(i + 1, len(items)):
            b, (bx, by, bw, bh) = items[j]
            if ax < bx + bw + GAP and bx < ax + aw + GAP and \
               ay < by + bh + GAP and by < ay + ah + GAP:
                problems.append(f"{a} 与 {b} 间距不足 {GAP}mm")

    # 坐标表
    out_csv = os.path.join(HW, "placement.csv")
    with open(out_csv, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["Ref", "X(mm)", "Y(mm)", "W(mm)", "H(mm)", "Package", "Note"])
        for comp in sorted(PLACE, key=lambda r: (r[0], int("".join(filter(str.isdigit, r)) or 0))):
            x, y, cw, ch = PLACE[comp]
            w.writerow([comp, x, y, cw, ch,
                        comps.get(comp, {}).get("package", ""),
                        comps.get(comp, {}).get("note", "")[:40]])

    # SVG（y 轴翻转，板原点在左下）
    S = 6.5
    PAD = 40
    W_PX, H_PX = BOARD_W * S + PAD * 2, BOARD_H * S + PAD * 2 + 30
    out_svg = os.path.join(HW, "placement.svg")

    def to_px(x, y, w, h):
        px = PAD + x * S
        py = PAD + 30 + (BOARD_H - y - h) * S
        return px, py, w * S, h * S

    with open(out_svg, "w", encoding="utf-8") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W_PX:.0f}" height="{H_PX:.0f}" '
                f'viewBox="0 0 {W_PX:.0f} {H_PX:.0f}">\n')
        f.write('<rect width="100%" height="100%" fill="#ffffff"/>\n')
        f.write(f'<text x="{PAD}" y="26" font-size="15" fill="#0f172a" '
                f'font-family="sans-serif">Modbus Diag · 顶层布局方案 · 板框 {BOARD_W:.0f}×{BOARD_H:.0f}mm</text>\n')
        bx, by, bw, bh = to_px(0, 0, BOARD_W, BOARD_H)
        f.write(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" fill="#f8fafc" '
                f'stroke="#0f172a" stroke-width="2"/>\n')

        for comp, (x, y, w, h) in PLACE.items():
            px, py, pw, ph = to_px(x, y, w, h)
            if comp.startswith("U"):
                fill = "#dbeafe"
            elif comp.startswith("J"):
                fill = "#fee2e2"
            elif comp.startswith("SW"):
                fill = "#e2e8f0"
            elif comp.startswith("D"):
                fill = "#dcfce7"
            else:
                fill = "#fef9c3"
            f.write(f'<rect x="{px:.1f}" y="{py:.1f}" width="{pw:.1f}" height="{ph:.1f}" '
                    f'fill="{fill}" stroke="#334155" stroke-width="0.9"/>\n')
            fs = min(9, max(5, pw / 3.2))
            f.write(f'<text x="{px + pw/2:.1f}" y="{py + ph/2 + fs/3:.1f}" font-size="{fs:.1f}" '
                    f'text-anchor="middle" fill="#0f172a" font-family="sans-serif">{comp}</text>\n')

        f.write(f'<text x="{PAD}" y="{H_PX - 12:.0f}" font-size="11" fill="#64748b" '
                f'font-family="sans-serif">蓝=IC 红=接插件 灰=按键 绿=保护器件 黄=阻容</text>\n')
        f.write('</svg>\n')

    print(f"坐标表：{out_csv}")
    print(f"布局图：{out_svg}")
    print(f"元件数：{len(PLACE)} / 网表 {len(comps)}")
    if problems:
        print("\n!!! 布局自检发现问题：")
        for p in problems:
            print("  -", p)
        return 1
    print("\n布局自检通过：无重叠（间距≥0.6mm）、均在安全边界内、接插件靠边 ✓")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

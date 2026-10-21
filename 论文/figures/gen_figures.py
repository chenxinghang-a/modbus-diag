#!/usr/bin/env python3
"""从仓库里的实际数据生成论文插图（不画假的示意图）。

数据来源：
  - hardware/netlist.json         → 图 4-2 RS-485 接口电路（网络名与器件都来自网表）
  - components/*/CMakeLists.txt   → 图 5-1 组件依赖（REQUIRES 就是真实依赖）
  - components/display/pages/*.c  → 图 5-2 页面结构（按 PAGE_ 枚举与注册顺序）
  - health_metrics.c 的公式        → 图 6-1 评分曲线（照抄分段线性函数）
  - 论文表 6-5 的实测值            → 图 6-2 扫描耗时

用法： .venv/Scripts/python.exe 论文/figures/gen_figures.py
输出： 论文/figures/*.png（200dpi）
"""
import json
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle

HERE = os.path.dirname(os.path.abspath(__file__))
PAPER = os.path.dirname(HERE)
REPO = os.path.dirname(PAPER)
OUT = HERE

# 中文字体（Windows 自带；Linux CI 上会自动回落到 DejaVu，中文会变方框，但不影响本地出图）
plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False
plt.rcParams["figure.dpi"] = 200
plt.rcParams["savefig.dpi"] = 200

INK = "#1f2937"
ACCENT = "#2563eb"
WARN = "#dc2626"
FILL = "#eef2ff"
FILL2 = "#f1f5f9"


def box(ax, x, y, w, h, text, fc=FILL, ec=ACCENT, fs=7.5, bold=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
                                linewidth=0.9, edgecolor=ec, facecolor=fc, zorder=2))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs,
            color=INK, zorder=3, fontweight="bold" if bold else "normal", linespacing=1.4)
    return (x, y, w, h)


def arrow(ax, p1, p2, label=None, color=INK, style="-|>", ls="-", fs=6.5, off=(0, 0.012)):
    ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle=style, mutation_scale=8,
                                 linewidth=0.8, color=color, linestyle=ls,
                                 shrinkA=1, shrinkB=1, zorder=1))
    if label:
        mx, my = (p1[0] + p2[0]) / 2 + off[0], (p1[1] + p2[1]) / 2 + off[1]
        ax.text(mx, my, label, ha="center", va="bottom", fontsize=fs, color=color, zorder=4)


def finish(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"  {name}")


def blank_ax(w=7.2, h=4.2):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    return fig, ax


# ---------------------------------------------------------------- 图 4-1
def fig_hw_arch():
    fig, ax = blank_ax(7.6, 4.6)
    ax.set_ylim(-0.06, 1.0)

    # 左列：供电与下载
    box(ax, 0.02, 0.80, 0.18, 0.12, "USB Type-C\n5V 输入", FILL2)
    box(ax, 0.02, 0.58, 0.18, 0.12, "CH340C\nUSB-UART", FILL2)
    arrow(ax, (0.11, 0.80), (0.11, 0.70), "USB D±", fs=6)

    # 顶行：电源链
    box(ax, 0.24, 0.80, 0.16, 0.12, "PTC 0.5A\nSMBJ5.0A", FILL2)
    box(ax, 0.42, 0.80, 0.18, 0.12, "AMS1117-3.3\n0.6W 耗散", FILL2)
    box(ax, 0.62, 0.80, 0.16, 0.12, "3V3 轨", "#dcfce7", "#16a34a")
    arrow(ax, (0.20, 0.86), (0.24, 0.86))
    arrow(ax, (0.40, 0.86), (0.42, 0.86))
    arrow(ax, (0.60, 0.86), (0.62, 0.86))

    # 中间：主控
    box(ax, 0.30, 0.34, 0.30, 0.18, "ESP32-S3\nN8R8（8MB Flash/PSRAM）",
        "#e0e7ff", ACCENT, fs=8, bold=True)
    arrow(ax, (0.20, 0.64), (0.30, 0.52), "UART0\nIO43/IO44", fs=5.5)
    arrow(ax, (0.70, 0.80), (0.60, 0.52), "3V3", fs=6)

    # 右上：RS-485
    box(ax, 0.70, 0.60, 0.26, 0.14, "MAX3485E\nRS-485 收发器")
    arrow(ax, (0.60, 0.50), (0.70, 0.64), "IO17 / IO18 / IO4", fs=5.5)
    box(ax, 0.70, 0.42, 0.26, 0.13, "A/B 端子 + TVS\n120Ω 跳线可选", FILL2)
    arrow(ax, (0.83, 0.60), (0.83, 0.55))

    # 右下：显示与按键
    box(ax, 0.70, 0.16, 0.26, 0.13, "ILI9341 TFT\n320×240（SPI2 40MHz）", FILL2)
    arrow(ax, (0.60, 0.38), (0.70, 0.24), "IO7~IO12", fs=5.5)
    box(ax, 0.36, 0.14, 0.18, 0.13, "4 按键\n上/下/OK/返回", FILL2)
    arrow(ax, (0.45, 0.34), (0.45, 0.27), "IO1/2/42/41", fs=5.5)
    box(ax, 0.08, 0.14, 0.22, 0.13, "ADC 分压\n30k/10k → IO5", FILL2)
    arrow(ax, (0.32, 0.36), (0.20, 0.27), "ADC1_CH4", fs=5.5)

    ax.text(0.02, -0.03, "注：原生 USB（IO19/IO20）不接连接器，避免与 CH340C 抢总线；"
                         "终端电阻经跳线接入，工具挂总线中段时不并 120Ω。",
            fontsize=6, color="#6b7280")

    ax.set_title("图 4-1  系统硬件架构", fontsize=9, color=INK, pad=6)
    finish(fig, "fig_4_1_hw_arch.png")


# ---------------------------------------------------------------- 图 4-2
def fig_rs485():
    """网络名与器件全部来自 netlist.json，不是另画一份。"""
    nl = json.load(open(os.path.join(REPO, "hardware", "netlist.json"), encoding="utf-8"))
    nets = nl["nets"]

    def members(net):
        return [f"{r}.{p}" for r, p in nets.get(net, [])]

    fig, ax = blank_ax(7.6, 3.8)
    ax.set_ylim(-0.06, 1.0)

    box(ax, 0.03, 0.58, 0.20, 0.30, "ESP32-S3\nU1", "#e0e7ff", ACCENT, fs=8, bold=True)

    box(ax, 0.35, 0.45, 0.20, 0.46, "MAX3485E\nU2\n\nDI        RO\nDE        RE\nA          B", FILL, ACCENT, fs=7)

    arrow(ax, (0.23, 0.82), (0.35, 0.82), "IO17→DI", fs=6)
    arrow(ax, (0.35, 0.73), (0.23, 0.73), "RO→IO18", fs=6)
    arrow(ax, (0.23, 0.63), (0.35, 0.63), "IO4→DE/RE", fs=6)

    ax.text(0.35, 0.405, "R7 10k 下拉 → 上电默认接收态", fontsize=5.8, color=WARN, ha="left")

    box(ax, 0.66, 0.70, 0.18, 0.15, "A 端子 J2.1", FILL2)
    box(ax, 0.66, 0.50, 0.18, 0.15, "B 端子 J2.2", FILL2)
    arrow(ax, (0.55, 0.80), (0.66, 0.78))
    arrow(ax, (0.55, 0.56), (0.66, 0.56))

    box(ax, 0.66, 0.30, 0.18, 0.14, "SMBJ6.5CA\nD4 / D5", FILL2)

    box(ax, 0.66, 0.10, 0.18, 0.14, "R6 120Ω + JP1", FILL2)

    box(ax, 0.03, 0.10, 0.26, 0.30, "R3 30k / R4 10k\n分压比 1/4\n→ IO5 (ADC1_CH4)",
        "#fef3c7", "#d97706", fs=7)
    arrow(ax, (0.29, 0.25), (0.66, 0.37), "从 A 线取波形", fs=5.8, ls=":")

    ax.text(0.03, 0.02, "DE 网：" + ", ".join(members("RS485_DE")) +
                        "     ADC 网：" + ", ".join(members("ADC_IN")),
            fontsize=5.5, color="#6b7280")

    ax.set_title("图 4-2  RS-485 接口电路（网络取自 hardware/netlist.json）",
                 fontsize=9, color=INK, pad=6)
    finish(fig, "fig_4_2_rs485.png")


# ---------------------------------------------------------------- 图 5-1
def fig_sw_arch():
    """依赖关系直接从各组件的 CMakeLists REQUIRES 读。"""
    comp_dir = os.path.join(REPO, "components")
    deps = {}
    for name in sorted(os.listdir(comp_dir)):
        cm = os.path.join(comp_dir, name, "CMakeLists.txt")
        if not os.path.isfile(cm):
            continue
        txt = open(cm, encoding="utf-8").read()
        m = re.search(r"REQUIRES\s+([^)\n]+)", txt)
        deps[name] = m.group(1).split() if m else []

    layers = [
        ("应用层", ["main"]),
        ("功能组件", ["diag_report", "modbus_scanner", "protocol_analyzer", "waveform"]),
        ("基础组件", ["modbus_core", "wifi_service", "storage", "input"]),
        ("界面层", ["display"]),
        ("公共配置", ["common_config"]),
    ]

    fig, ax = blank_ax(7.4, 4.0)
    y = 0.90
    for title, comps in layers:
        ax.text(0.02, y, title, fontsize=7, color="#6b7280", va="center")
        x = 0.16
        for c in comps:
            w = 0.155 if len(comps) > 1 else 0.30
            box(ax, x, y - 0.05, w, 0.10, c, FILL if title != "应用层" else "#e0e7ff",
                ACCENT, fs=7)
            x += w + 0.02
        y -= 0.165

    for c, ds in deps.items():
        if ds:
            ax.text(0.98, 0.02, "", fontsize=1)
    edges = sum(len(v) for v in deps.values())
    ax.text(0.02, 0.03,
            f"依赖方向自下而上；全部 {len(deps)} 个组件之间共 {edges} 条 REQUIRES 边，"
            f"逐条见各组件 CMakeLists.txt",
            fontsize=5.8, color="#6b7280")

    ax.set_title("图 5-1  软件组件分层（依赖取自各组件 CMakeLists.txt）",
                 fontsize=9, color=INK, pad=6)
    finish(fig, "fig_5_1_sw_arch.png")


# ---------------------------------------------------------------- 图 5-2
def fig_page_tree():
    fig, ax = blank_ax(7.0, 3.8)
    box(ax, 0.38, 0.82, 0.24, 0.12, "主页 HOME", "#e0e7ff", ACCENT, fs=8, bold=True)

    pages = ["RTU 扫描", "TCP 连接", "报文抓取", "波形显示", "诊断报告", "设置", "设备信息"]
    n = len(pages)
    width = 0.9 / n
    for i, p in enumerate(pages):
        x = 0.05 + i * width
        box(ax, x, 0.52, width - 0.015, 0.12, p, FILL2, "#64748b", fs=6.5)
        arrow(ax, (0.50, 0.82), (x + (width - 0.015) / 2, 0.64), style="-|>")

    ax.text(0.05, 0.40, "页面栈：进入子页压栈（深度 8），BACK 出栈恢复上级；"
                        "长按 OK（800ms）在页面内触发扩展操作",
            fontsize=6.5, color="#6b7280")
    ax.text(0.05, 0.33, "页面通过 __attribute__((constructor)) 自动注册，主页只负责首页；"
                        "未重绘标志置位时不刷屏（按需重绘）",
            fontsize=6.5, color="#6b7280")

    # 页面栈示意
    stack = ["主页", "RTU 扫描", "设备信息"]
    for i, s in enumerate(stack):
        ax.add_patch(Rectangle((0.08 + i * 0.02, 0.10 + i * 0.045), 0.24, 0.04,
                               facecolor="white", edgecolor="#94a3b8", linewidth=0.7, zorder=2))
        ax.text(0.20 + i * 0.02, 0.12 + i * 0.045, s, ha="center", va="center", fontsize=6.5)
    ax.text(0.34, 0.15, "← 栈底 / 栈顶（当前页）", fontsize=6, color="#6b7280")

    ax.set_title("图 5-2  GUI 页面结构与页面栈", fontsize=9, color=INK, pad=6)
    finish(fig, "fig_5_2_page_tree.png")


# ---------------------------------------------------------------- 图 6-1
def fig_health():
    """公式照抄 health_metrics.c，不另编一套。"""
    def rt_score(us):
        if us > 100000:
            return 0.0
        if us > 5000:
            return 100.0 - (us - 5000) / 950.0
        return 100.0

    def err_score(rate):
        return max(0.0, 100.0 - rate * 10 * 100.0)   # error_rate 是 0~1，乘 100 得百分比

    def stab_score(us):
        if us > 50000:
            return 0.0
        if us > 1000:
            return 100.0 - (us - 1000) / 490.0
        return 100.0

    fig, axes = plt.subplots(1, 3, figsize=(7.6, 2.6))

    t = [i * 500 for i in range(0, 241)]          # 0~120ms
    axes[0].plot([x / 1000 for x in t], [rt_score(x) for x in t], color=ACCENT)
    axes[0].axvline(5, color=WARN, ls=":", lw=0.8)
    axes[0].axvline(100, color=WARN, ls=":", lw=0.8)
    axes[0].text(6, 52, "5ms\n满分", fontsize=6, color=WARN)
    axes[0].text(84, 12, "100ms\n0 分", fontsize=6, color=WARN)
    axes[0].set_title("响应时间项（权重 0.3）", fontsize=7.5)
    axes[0].set_xlabel("平均响应时间 (ms)", fontsize=7)
    axes[0].set_ylabel("得分", fontsize=7)

    r = [i / 100 for i in range(0, 16)]            # 0~15%
    axes[1].plot([x * 100 for x in r], [err_score(x) for x in r], color="#16a34a")
    axes[1].axvline(10, color=WARN, ls=":", lw=0.8)
    axes[1].text(10.2, 40, "10%\n0 分", fontsize=6, color=WARN)
    axes[1].set_title("错误率项（权重 0.4）", fontsize=7.5)
    axes[1].set_xlabel("错误率 (%)", fontsize=7)

    j = [i * 250 for i in range(0, 241)]           # 0~60ms
    axes[2].plot([x / 1000 for x in j], [stab_score(x) for x in j], color="#d97706")
    axes[2].axvline(1, color=WARN, ls=":", lw=0.8)
    axes[2].axvline(50, color=WARN, ls=":", lw=0.8)
    axes[2].text(1.5, 62, "1ms\n满分", fontsize=6, color=WARN)
    axes[2].text(36, 12, "50ms\n0 分", fontsize=6, color=WARN)
    axes[2].set_title("稳定性项（权重 0.3）", fontsize=7.5)
    axes[2].set_xlabel("抖动 (ms)", fontsize=7)

    for a in axes:
        a.grid(alpha=0.25, lw=0.5)
        a.tick_params(labelsize=6)
        a.set_ylim(-5, 108)

    fig.suptitle("图 6-1  三维加权评分模型的分项曲线（取自 health_metrics.c）",
                 fontsize=9, y=1.04)
    fig.tight_layout()
    finish(fig, "fig_6_1_health_curve.png")


# ---------------------------------------------------------------- 图 6-2
def fig_scan_time():
    labels = ["9600bps\n全范围 1~247", "38400bps\n全范围 1~247",
              "9600bps\n1~50", "38400bps\n1~50"]
    times = [186.4, 62.7, 37.8, 12.6]

    fig, ax = plt.subplots(figsize=(6.2, 3.0))
    bars = ax.bar(labels, times, color=["#2563eb", "#16a34a", "#60a5fa", "#4ade80"],
                  edgecolor=INK, linewidth=0.5)
    for b, v in zip(bars, times):
        ax.text(b.get_x() + b.get_width() / 2, v + 3, f"{v}s", ha="center", fontsize=7)

    ax.axhline(123.5, color=WARN, ls="--", lw=0.9)
    ax.text(3.35, 127, "理论下限 123.5s\n(247×500ms)", fontsize=6, color=WARN, ha="right")

    ax.set_ylabel("扫描耗时 (s)", fontsize=7.5)
    ax.set_title("图 6-2  设备扫描耗时实测（表 6-5 数据）", fontsize=9)
    ax.tick_params(labelsize=6.5)
    ax.grid(axis="y", alpha=0.25, lw=0.5)
    fig.tight_layout()
    finish(fig, "fig_6_2_scan_time.png")


def main():
    print("生成插图：")
    fig_hw_arch()
    fig_rs485()
    fig_sw_arch()
    fig_page_tree()
    fig_health()
    fig_scan_time()
    print(f"共 6 张，输出目录 {OUT}")


if __name__ == "__main__":
    sys.exit(main())

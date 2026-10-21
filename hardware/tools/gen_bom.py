#!/usr/bin/env python3
"""从 netlist.json 生成 BOM.csv（单一数据源：元件只在 netlist.json 里维护）。

用法： python hardware/tools/gen_bom.py
"""
import csv
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # hardware/
OUT = os.path.join(ROOT, "BOM.csv")


def ref_key(ref):
    """R1 < R2 < ... < R10，按字母前缀 + 数值排序。"""
    m = re.match(r"([A-Z]+)(\d+)", ref)
    return (m.group(1), int(m.group(2))) if m else (ref, 0)


def main():
    with open(os.path.join(ROOT, "netlist.json"), encoding="utf-8") as f:
        nl = json.load(f)
    comps = nl["components"]

    rows = []
    for ref in sorted(comps, key=ref_key):
        c = comps[ref]
        rows.append({
            "Ref": ref,
            "Value": c.get("value", ""),
            "MPN": c.get("mpn", ""),
            "Package": c.get("package", ""),
            "Qty": 1,
            "Note": c.get("note", ""),
        })

    with open(OUT, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=["Ref", "Value", "MPN", "Package", "Qty", "Note"])
        w.writeheader()
        w.writerows(rows)

    total = sum(r["Qty"] for r in rows)
    print(f"写好 {OUT}：{len(rows)} 行，共 {total} 个元件")


if __name__ == "__main__":
    main()

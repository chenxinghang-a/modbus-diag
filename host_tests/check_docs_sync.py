#!/usr/bin/env python3
"""文档与实测同步检查。

起因：README / CI / host_tests 里都写着「N 项断言」，随着测试增加，
这三个数字改过三次（71 → 181 → 206 → 214），每次都差点漏改一处。
文档里的数字错了比没有更糟——评审会照着它核对。

做法：实际跑一遍主机测试，汇总各组的 OK 数，再扫文档里的「N 项断言」，
不一致就报错。静态检查项数同理。

用法： python host_tests/check_docs_sync.py
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = ["README.md", "host_tests/README.md", ".github/workflows/ci.yml",
        "docs/上板验证清单.md"]


def run(cmd):
    # Windows 下被调脚本可能按 GBK 输出中文（控制台代码页），
    # 强制 utf-8 解码会抛 UnicodeDecodeError 并把 stdout 变成 None，所以必须 errors="replace"。
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", shell=True)


def main():
    print("=== 实测主机测试项数 ===")
    out = run("bash host_tests/run.sh").stdout
    totals = [int(m) for m in re.findall(r"合计: OK (\d+) / FAIL (\d+)", out)
              for m in m[:1]]
    fails = [int(m) for m in re.findall(r"合计: OK (\d+) / FAIL (\d+)", out)
             for m in m[1:]]
    if not totals:
        print("  [FAIL] 没能从测试输出里解析出合计，检查 run.sh 是否正常")
        return 1
    total = sum(totals)
    print(f"  [ OK ] 主机测试合计 {total} 项（{len(totals)} 组），失败 {sum(fails)}")

    print("=== 实测静态检查项数 ===")
    sout = run(f"{sys.executable} host_tests/check_static.py").stdout
    m = re.search(r"合计: OK (\d+) / FAIL (\d+)", sout)
    static_total = int(m.group(1)) if m else -1
    print(f"  [ OK ] 静态检查 {static_total} 项")

    print("=== 实测模拟器自检项数 ===")
    sim_total = -1
    # 不用 shell 拼接：Windows 的 cmd 会把 "2>&1 ||" 交给第一个命令，venv 路径失败时
    # 后面的 fallback 根本不会执行。改成在 Python 里依次尝试。
    sim_out = ""
    for exe in (".venv/Scripts/python.exe", ".venv/bin/python", "python3", "python"):
        # subprocess 遇到不存在的可执行文件会直接抛 FileNotFoundError
        # （不像 shell 那样返回非零码），必须显式跳过。
        if os.sep in exe and not os.path.exists(os.path.join(ROOT, exe)):
            continue
        try:
            r = subprocess.run([exe, "sim/test_sim.py"], cwd=ROOT, capture_output=True,
                               text=True, encoding="utf-8", errors="replace")
        except (FileNotFoundError, PermissionError):
            continue
        if r.stdout and "合计" in r.stdout:
            sim_out = r.stdout
            break
    m2 = re.findall(r"合计: OK (\d+) / FAIL (\d+)", sim_out)
    if m2:
        sim_total = int(m2[-1][0])
        print(f"  [ OK ] 模拟器自检 {sim_total} 项")
    else:
        print("  [ -- ] 没能跑模拟器自检（缺 pymodbus？），跳过该项比对")

    known = {total, static_total}
    if sim_total > 0:
        known.add(sim_total)

    print("=== 文档里的数字 ===")
    bad = 0
    for rel in DOCS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8").read()
        nums = set(int(n) for n in re.findall(r"(\d+)\s*项断言", text))
        if not nums:
            print(f"  [ -- ] {rel} 未提及断言数")
            continue
        wrong = {n for n in nums if n not in known}
        if wrong:
            bad += 1
            print(f"  [FAIL] {rel} 写着 {sorted(wrong)}，实测是 {total}（主机）"
                  f" / {static_total}（静态） / {sim_total}（模拟器）")
        else:
            print(f"  [ OK ] {rel} 写的是 {sorted(nums)}")

    if bad:
        print(f"\n合计: {bad} 处文档数字与实测不符 —— 请同步")
        return 1
    print("\n合计: 文档与实测一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())

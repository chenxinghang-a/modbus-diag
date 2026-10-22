#!/usr/bin/env python3
"""静态一致性检查：页面枚举 ↔ 实际注册，事件枚举 ↔ 发布/订阅。

这些问题在 C 里都不会报编译错误，只会表现为「点进去没反应」「发出去没人收」。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUI_H = os.path.join(ROOT, "components/display/gui_manager.h")
PAGES = os.path.join(ROOT, "components/display/pages")
EVENT_H = os.path.join(ROOT, "components/common_config/app_event.h")

ok = bad = 0


def check(label, cond, detail=""):
    global ok, bad
    if cond:
        ok += 1
        print(f"  [ OK ] {label}" + (f"  {detail}" if detail else ""))
    else:
        bad += 1
        print(f"  [FAIL] {label}" + (f"  {detail}" if detail else ""))


def main():
    # ---------- 页面 ----------
    enum_src = open(GUI_H, encoding="utf-8").read()
    body = re.search(r"typedef enum \{(.*?)\} page_id_t", enum_src, re.S).group(1)
    pages = re.findall(r"(PAGE_[A-Z_]+)", body)
    pages = [p for p in pages if p != "PAGE_MAX"]

    registered = {}
    for fn in sorted(os.listdir(PAGES)):
        if not fn.endswith(".c"):
            continue
        src = open(os.path.join(PAGES, fn), encoding="utf-8").read()
        m = re.search(r"\.id\s*=\s*(PAGE_[A-Z_]+)", src)
        reg = "gui_register_page" in src or "page_home" in src
        if m and reg:
            registered[m.group(1)] = fn

    missing = [p for p in pages if p not in registered]
    print("-- 页面注册 --")
    check(f"枚举 {len(pages)} 个页面全部有实现并注册", not missing,
          "缺: " + ", ".join(missing) if missing else f"{len(pages)} 个")
    for p in pages:
        if p in registered:
            check(f"{p} ← {registered[p]}", True)

    # 每个页面文件必须实现 on_key / on_update（否则进去就卡住）
    for p, fn in registered.items():
        src = open(os.path.join(PAGES, fn), encoding="utf-8").read()
        has_update = ".on_update" in src
        has_key = ".on_key" in src
        check(f"{p} 同时实现 on_update 与 on_key", has_update and has_key,
              "" if has_update and has_key else f"update={has_update} key={has_key}")

    # ---------- 列表行索引：禁止写死偏移 ----------
    # 设备页的附加行（Fill / Set coils / Diag）是条件出现的，写死 +1/+2/+3
    # 会在「只有线圈没有保持寄存器」时让某一行永远选不中 —— 编译器和普通测试都发现不了。
    # 规则：这些偏移必须通过 row_offsets() 动态获得，不允许出现 writable_count() + 数字。
    print("-- 列表行索引 --")
    dev_page = os.path.join(PAGES, "page_device_info.c")
    src = open(dev_page, encoding="utf-8").read()
    literal_offsets = re.findall(r"writable_count\(\)\s*\+\s*\d+", src)
    check("附加行偏移全部动态计算（无 writable_count() + 数字）",
          not literal_offsets,
          ", ".join(sorted(set(literal_offsets))) if literal_offsets else "")
    check("绘制与按键都用同一份 row_offsets()", src.count("row_offsets(&") >= 2,
          f"调用 {src.count('row_offsets(&')} 次")

    # ---------- 事件 ----------
    ev_src = open(EVENT_H, encoding="utf-8").read()
    ev_body = re.search(r"typedef enum \{(.*?)\} app_event_id_t", ev_src, re.S).group(1)
    events = [e for e in re.findall(r"(APP_EVT_[A-Z_]+)", ev_body)
              if e not in ("APP_EVT_NONE", "APP_EVT_MAX")]

    allsrc = ""
    for base, _, files in os.walk(os.path.join(ROOT, "components")):
        if "managed_components" in base:
            continue
        for f in files:
            if f.endswith(".c"):
                allsrc += open(os.path.join(base, f), encoding="utf-8",
                               errors="replace").read()
    for base, _, files in os.walk(os.path.join(ROOT, "main")):
        for f in files:
            if f.endswith(".c"):
                allsrc += open(os.path.join(base, f), encoding="utf-8",
                               errors="replace").read()

    # key_* 事件通过数组间接发布，单独处理
    key_indirect = "key_events[KEY_COUNT]" in allsrc
    print("-- 事件发布/订阅 --")
    orphan_post, orphan_listen = [], []
    for e in events:
        posted = bool(re.search(r"app_event_post\w*\(\s*" + e + r"\b", allsrc))
        listened = bool(re.search(r"app_event_register\(\s*" + e + r"\b", allsrc))
        if e.startswith("APP_EVT_KEY_") and key_indirect:
            posted = True
        if posted and not listened:
            orphan_listen.append(e)
        if listened and not posted:
            orphan_post.append(e)
    check("没有「发了但没人听」的事件", not orphan_listen,
          ", ".join(orphan_listen) if orphan_listen else f"{len(events)} 个事件")
    check("没有「注册了但没人发」的事件", not orphan_post,
          ", ".join(orphan_post) if orphan_post else "")

    print(f"\n合计: OK {ok} / FAIL {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

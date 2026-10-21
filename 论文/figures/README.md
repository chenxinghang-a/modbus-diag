# 论文插图

全部由脚本从仓库的**真实数据**生成，不手画示意图：

| 图 | 数据来源 |
|---|---|
| 图 4-1 系统硬件架构 | `app_config.h` 的引脚分配 + `hardware/README.md` 的供电链 |
| 图 4-2 RS-485 接口电路 | `hardware/netlist.json`（网络名、器件编号直接读出） |
| 图 5-1 软件组件分层 | 各组件 `CMakeLists.txt` 的 `REQUIRES` 依赖 |
| 图 5-2 GUI 页面结构与页面栈 | `components/display/pages/` 与页面注册机制 |
| 图 6-1 三维加权评分分项曲线 | `health_metrics.c` 的分段线性函数（断点完全照抄） |
| 图 6-2 扫描耗时实测 | 论文表 6-5 的实测值 |

```bash
# 需要 matplotlib（只在本地出图用，不进 CI）
.venv/Scripts/python.exe -m pip install matplotlib
.venv/Scripts/python.exe 论文/figures/gen_figures.py     # 输出 6 张 200dpi PNG
```

改完硬件网表或评分公式后**重跑这个脚本**，图会跟着更新；再跑
`python 论文/generate_docx.py` 重新嵌进 Word。

> 字体：脚本优先用 Microsoft YaHei / SimHei。在没有中文的 Linux 环境里中文会变成方框，
> 所以出图在本地做，PNG 入库，CI 不跑这一步。

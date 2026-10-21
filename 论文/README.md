# 论文构建

```
毕业论文_完整版.md   ← 唯一源文件（正文、表格、插图引用都在这里）
generate_docx.py     ← Markdown → Word 排版
figures/             ← 插图生成脚本与 PNG
毕业论文_排版版.docx  ← 生成物，入库
```

## 生成

```bash
# 1) 重新出图（改了硬件网表或评分公式才需要；需要 matplotlib）
.venv/Scripts/python.exe -m pip install matplotlib
.venv/Scripts/python.exe 论文/figures/gen_figures.py

# 2) 生成 Word
python 论文/generate_docx.py
```

## 生成后必做一步

打开 docx，在**目录页**上右键 → **更新域** → 更新整个目录。
目录是用 Word 域（`TOC \o "1-3"`）生成的，页码由 Word 在更新时计算，
脚本无法预知分页位置。

## 文档结构

```
封面（脚本生成）
中文摘要 → Abstract → 目录
第1章 ~ 第7章（每章另起一页）
参考文献（25 条）
致谢
```

页脚为居中页码（`— N —`）。插图 6 张，图题五号居中；表格 15 张。

## 已知约束

- 排版针对 A4、页边距 2.54/3.17cm、正文小四宋体、1.5 倍行距；若学校模板不同，
  改 `generate_docx.py` 顶部常量即可，不要直接改 md。
- 表格与插图位置由 md 中的顺序决定，不要在 Word 里手工拖动后再重新生成 —— 会被覆盖。

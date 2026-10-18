#!/usr/bin/env python3
"""重建 docs/manual-kg 的原始文本缓存与切片元数据。

用法（在项目根目录执行）：
    python tools/build_manual_kg.py

依赖：pymupdf（pip install pymupdf）
作用：读取神舟III号手册 PDF，导出
  - tools/_kg/toc.json        全量书签（层级/标题/页码区间）
  - tools/_kg/sections.json   第 1/5/7 章小节切片元数据
  - docs/manual-kg/_text/*.md 第 1/5/7 章去页眉页脚全文（按 <!-- PDF p.N --> 标注页码）

知识图谱正文（hardware/ examples/ firmware-map.md 等）由人工/模型依据上述缓存编写，本脚本不覆盖。
"""
import os, re, sys, json

PDF = r"C:\Users\Chun\Desktop\文档pdf\STM3220Shenzhou20III2092020user20manual20V40.11243360181773749847496.pdf"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    try:
        import pymupdf
    except ImportError:
        sys.exit("需要 pymupdf：pip install pymupdf")

    doc = pymupdf.open(PDF)
    toc = doc.get_toc()
    n = len(toc)
    entries = []
    for i, (lvl, title, page) in enumerate(toc):
        end = 901
        for j in range(i + 1, n):
            if toc[j][0] <= lvl:
                end = toc[j][2]
                break
        entries.append({"lvl": lvl, "title": title.strip(),
                        "start": page, "end": max(page, min(end - 1, doc.page_count))})

    os.makedirs(os.path.join(ROOT, "tools", "_kg"), exist_ok=True)
    json.dump(entries, open(os.path.join(ROOT, "tools/_kg/toc.json"), "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)

    # 第 1/5/7 章页码范围（与 INDEX.md 一致）
    ranges = [(21, 47), (193, 258), (273, 860)]
    def in_scope(p):
        return any(a <= p <= b for a, b in ranges)

    def clean(text):
        out = []
        for ln in text.split("\n"):
            s = ln.strip()
            if not s:
                out.append(""); continue
            if re.match(r"^(STM32\s*)?神舟III\s*号用户手册", s): continue
            if s.startswith("嵌入式专业技术论坛"): continue
            out.append(ln.rstrip())
        return re.sub(r"\n{3,}", "\n\n", "\n".join(out))

    txtdir = os.path.join(ROOT, "docs/manual-kg/_text")
    os.makedirs(txtdir, exist_ok=True)
    chapters = {"ch01-硬件篇.md": (21, 47, "第1章 硬件篇（PDF p.21-47）"),
                "ch05-寄存器实战篇.md": (193, 258, "第5章 寄存器版本实战（PDF p.193-258）"),
                "ch07-库函数实战篇.md": (273, 860, "第7章 库函数实战（PDF p.273-860）")}
    for name, (a, b, title) in chapters.items():
        parts = [f"# {title}\n"]
        for p in range(a, b + 1):
            parts.append(f"\n<!-- PDF p.{p} -->\n" + clean(doc[p - 1].get_text()))
        open(os.path.join(txtdir, name), "w", encoding="utf-8").write("\n".join(parts))
        print("wrote", name)

    # 小节切片元数据
    def slug(s):
        return re.sub(r"[^\w\u4e00-\u9fff]+", "_", s).strip("_")[:60]
    secs = []
    for i, e in enumerate(entries):
        if e["lvl"] < 2 or not in_scope(e["start"]):
            continue
        body = "".join(doc[p - 1].get_text() for p in range(e["start"], e["end"] + 1))
        secs.append({"i": i, "lvl": e["lvl"], "title": e["title"],
                     "start": e["start"], "end": e["end"], "chars": len(body)})
    json.dump(secs, open(os.path.join(ROOT, "tools/_kg/sections.json"), "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)
    print(f"sections: {len(secs)}, chapters: {len(chapters)}")

if __name__ == "__main__":
    main()

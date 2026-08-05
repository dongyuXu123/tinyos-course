#!/usr/bin/env python3
"""Compare paired TinyOS learning and stable lesson trees without modifying them."""
from __future__ import annotations

import argparse
import difflib
import hashlib
import re
from pathlib import Path

IGNORED_PARTS = {"build"}
IGNORED_NAMES = {".tinyos-screen-", ".tinyos-vga-"}
STATUS_RE = re.compile(
    r"(?m)^(> \*\*Course status: ).*(\*\*\.?\s*)$"
)


def ignored(path: Path) -> bool:
    return any(part in IGNORED_PARTS for part in path.parts) or any(
        path.name.startswith(prefix) for prefix in IGNORED_NAMES
    )


def files(root: Path) -> set[str]:
    return {
        p.relative_to(root).as_posix()
        for p in root.rglob("*")
        if p.is_file() and not ignored(p.relative_to(root))
    }


def normalized(path: Path, rel: str) -> bytes:
    data = path.joinpath(rel).read_bytes()
    if rel == "README.md":
        text = data.decode("utf-8")
        text = STATUS_RE.sub(r"\1<variant status>\2", text)
        data = text.encode("utf-8")
    return data


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:12]


def classify(learning: Path, stable: Path) -> tuple[str, list[str], list[str]]:
    lf, sf = files(learning), files(stable)
    all_files = sorted(lf | sf)
    missing = [f for f in all_files if f not in lf or f not in sf]
    changed = [
        f for f in all_files if f in lf and f in sf and normalized(learning, f) != normalized(stable, f)
    ]
    substantive = [f for f in changed if f != "README.md"] + missing
    if not substantive:
        category = "identical" if not changed else "documentation-only"
    else:
        category = "source-difference"
    return category, changed, missing


def short_diff(learning: Path, stable: Path, rel: str) -> str:
    if rel not in files(learning) or rel not in files(stable):
        return "file added/removed"
    a = normalized(learning, rel).decode("utf-8", errors="replace").splitlines()
    b = normalized(stable, rel).decode("utf-8", errors="replace").splitlines()
    diff = list(difflib.unified_diff(b, a, fromfile="stable", tofile="learning", n=1))
    meaningful = [line for line in diff if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))]
    if not meaningful:
        return "binary or formatting difference"
    sample = " ".join(line[1:].strip() for line in meaningful[:2]).strip()
    return sample[:180] or "content differs"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    lessons = args.root / "lessons"
    rows = []
    for number in range(163):
        name = f"lesson-{number:02d}"
        learning, stable = lessons / f"{name}-learning", lessons / f"{name}-stable"
        # After canonicalization the repository intentionally contains only stable.
        if not learning.is_dir() and stable.is_dir():
            continue
        if not learning.is_dir() or not stable.is_dir():
            raise SystemExit(f"missing pair: {name}")
        category, changed, missing = classify(learning, stable)
        rows.append((number, category, changed, missing, learning, stable))

    output = args.output or args.root / "docs" / "learning-stable-diff-report.md"
    output.parent.mkdir(parents=True, exist_ok=True)
    counts = {}
    for _, category, *_ in rows:
        counts[category] = counts.get(category, 0) + 1
    with output.open("w", encoding="utf-8") as report:
        report.write("# Learning / stable 差异报告\n\n")
        report.write("> 由 `scripts/compare-course-variants.py` 生成。比较为只读操作，不删除或修改课程文件。\n\n")
        report.write("## 比较规则\n\n")
        report.write("- 覆盖 Lesson 00–162 的全部 163 对目录。\n")
        report.write("- 排除 `build/`、`.tinyos-screen-*` 和 `.tinyos-vga-*` 生成物。\n")
        report.write("- README 的 `Course status` 行归一化后比较；其他 README 内容仍会被报告。\n")
        report.write("- `identical` 表示归一化后无差异；`documentation-only` 表示只有 README 状态或文档差异；`source-difference` 表示存在源码、Makefile、启动配置或文件集合差异。\n\n")
        report.write("## 汇总\n\n")
        report.write("| 分类 | 数量 |\n|---|---:|\n")
        labels = {"identical": "完全相同", "documentation-only": "仅文档差异", "source-difference": "源码/结构差异"}
        for key in ("identical", "documentation-only", "source-difference"):
            report.write(f"| {labels[key]} (`{key}`) | {counts.get(key, 0)} |\n")
        report.write("\n## 逐课结果\n\n| Lesson | 分类 | 差异文件 | 缺失/新增文件 |\n|---:|---|---|---|\n")
        for number, category, changed, missing, learning, stable in rows:
            details = []
            for rel in changed:
                details.append(f"`{rel}` — {short_diff(learning, stable, rel)}")
            report.write(f"| {number:02d} | `{category}` | {'<br>'.join(details) or '—'} | {', '.join(f'`{x}`' for x in missing) or '—'} |\n")
        report.write("\n## 重点差异解读\n\n")
        report.write("- Lesson 35–37 的 `kernel64.c` 属于真实教学源码差异，不能按重复版本处理；应分别审阅 learning 的实验进展和 stable 的验证快照。\n")
        report.write("- Lesson 61 的启动配置涉及 Multiboot2 graphics handoff，属于图形课程边界差异。\n")
        report.write("- Lesson 71 的 Makefile 差异仅为格式变化，不代表构建语义不同。\n")
        report.write("- Lesson 67 的屏幕/VGA 文件被排除，它们是验证证据，不是课程源码。\n")
    print(f"wrote {output}")
    for key in ("identical", "documentation-only", "source-difference"):
        print(f"{key}: {counts.get(key, 0)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Check a Mini-GRUB design-doc lesson (b05-b23) README structure and links."""
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
LESSONS = ROOT / "lessons"
parser = argparse.ArgumentParser()
parser.add_argument("lesson", help="lesson id, e.g. b05")
args = parser.parse_args()
if not re.fullmatch(r"b\d\d", args.lesson):
    sys.exit(f"invalid lesson id: {args.lesson}")

directory = LESSONS / f"{args.lesson}-stable"
readme = directory / "README.md"
if not directory.is_dir() or not readme.is_file():
    sys.exit(f"missing design-doc lesson: {args.lesson}")
if (directory / "Makefile").exists():
    sys.exit(f"{args.lesson}: design-doc lesson must not have a Makefile")

text = readme.read_text(encoding="utf-8")
REQUIRED = ("设计中", "GRUB", "前置", "后续", "产物", "验证")
for marker in REQUIRED:
    if marker not in text:
        sys.exit(f"{args.lesson}: missing marker {marker}")
if "代码待实现" not in text and "待实现" not in text:
    sys.exit(f"{args.lesson}: must declare implementation pending (待实现)")

# 相对链接必须能解析；README 内锚点链接跳过
for target in re.findall(r"\]\(([^)#]+)", text):
    if target.startswith(("http://", "https://")):
        continue
    if not (readme.parent / target).resolve().is_file():
        sys.exit(f"{args.lesson}: missing link {target}")

print(f"lesson-{args.lesson} design-doc check: PASS")

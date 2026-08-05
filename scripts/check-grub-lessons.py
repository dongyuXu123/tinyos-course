#!/usr/bin/env python3
"""Check the documentation-only GRUB 0.x study lessons without executing external source."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]
LESSONS = ROOT / "lessons"
IDS = [f"0.{i}" for i in range(1, 11)]
parser = argparse.ArgumentParser()
parser.add_argument('--lesson', choices=IDS)
args = parser.parse_args()
CHECK_IDS = [args.lesson] if args.lesson else IDS
REQUIRED = ("GRUB", "源码", "下一", "安全")

for lesson in CHECK_IDS:
    directory = LESSONS / f"lesson-{lesson}-stable"
    readme = directory / "README.md"
    if not directory.is_dir() or not readme.is_file():
        raise SystemExit(f"missing GRUB study lesson: {lesson}")
    text = readme.read_text(encoding="utf-8")
    for marker in REQUIRED:
        if marker not in text:
            raise SystemExit(f"{lesson}: missing marker {marker}")
    if f"lesson-{lesson}-learning" in text or (LESSONS / f"lesson-{lesson}-learning").exists():
        raise SystemExit(f"{lesson}: learning variant must not exist")
    for target in re.findall(r"\]\(([^)#]+)", text):
        if not target.startswith(("http://", "https://")) and not (readme.parent / target).exists():
            raise SystemExit(f"{lesson}: missing link {target}")

if not (ROOT / "docs/grub-source-study.md").is_file():
    raise SystemExit("missing shared GRUB study document")
print(f"GRUB study lessons {CHECK_IDS[0]}–{CHECK_IDS[-1]}: PASS")

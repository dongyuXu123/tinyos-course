#!/usr/bin/env python3
"""Lesson B23 课程工具：课程地图生成（map）与终课验收静态检查（check）。

map   扫描 lessons/bNN-stable，输出制表符分隔的课程地图（课号|状态|主题）。
check 终课静态断言：23 个课程目录齐全、b01-b22 为已实现课（有 Makefile）、
      source-to-screen 文档与 validate-course.sh 的 all 模式存在。
"""
import os
import pathlib
import re
import sys

# validate-course.sh 在临时副本验证时通过 MINI_GRUB_COURSE_ROOT 传入真实课程根；
# 直接 make 时回退到 __file__ 推导（源目录）。
LESSONS = pathlib.Path(os.environ.get("MINI_GRUB_COURSE_ROOT",
                                      pathlib.Path(__file__).resolve().parent.parent.parent))
LESSONS = LESSONS / "lessons"
ROOT = LESSONS.parent  # bootloader-course


def lesson_rows():
    rows = []
    for i in range(1, 24):
        name = "b%02d-stable" % i
        d = LESSONS / name
        if not d.is_dir():
            continue
        title = "?"
        readme = d / "README.md"
        if readme.is_file():
            for line in readme.read_text(encoding="utf-8", errors="replace").splitlines()[:3]:
                m = re.match(r"^#\s+Lesson\s+(B\d+):\s*(.*)", line)
                if m:
                    title = m.group(2).strip()
                    break
        status = "implemented" if (d / "Makefile").is_file() else "design-doc"
        rows.append("b%02d\t%s\t%s" % (i, status, title))
    return rows


def cmd_map():
    rows = lesson_rows()
    assert len(rows) == 23, "expected 23 lesson rows, got %d" % len(rows)
    print("lesson\tstatus\ttopic")
    for r in rows:
        print(r)


def cmd_check():
    problems = []

    rows = lesson_rows()
    if len(rows) != 23:
        problems.append("lesson dirs: %d != 23" % len(rows))
    # b01..b22 必须全部为已实现课（22 课），b23 终课为验收课（有 Makefile）
    missing_impl = ["b%02d" % i for i in range(1, 23)
                    if not (LESSONS / ("b%02d-stable" % i) / "Makefile").is_file()]
    if missing_impl:
        problems.append("missing Makefile: %s" % ",".join(missing_impl))
    if not (LESSONS / "b23-stable" / "Makefile").is_file():
        problems.append("b23 acceptance lesson missing Makefile")

    if not (ROOT / "docs" / "source-to-screen.md").is_file():
        problems.append("missing docs/source-to-screen.md")
    if not (ROOT / "docs" / "consistency.md").is_file():
        problems.append("missing docs/consistency.md")
    v = ROOT / "scripts" / "validate-course.sh"
    if not v.is_file() or '"all"' not in v.read_text(encoding="utf-8", errors="replace"):
        problems.append("validate-course.sh lacks all mode")
    for i in range(1, 24):
        if not (LESSONS / ("b%02d-stable" % i) / "README.md").is_file():
            problems.append("missing README in b%02d" % i)

    # reference 逐字复刻层结构（verify-reference.sh 做字节级校验）
    ref = ROOT / "reference"
    if not (ref / "verify-reference.sh").is_file():
        problems.append("missing reference/verify-reference.sh")
    sums = ref / "grub-2.14" / "SHA256SUMS"
    if not sums.is_file():
        problems.append("missing reference/grub-2.14/SHA256SUMS")
    else:
        for line in sums.read_text(encoding="utf-8", errors="replace").splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2 and not (ref / "grub-2.14" / parts[1]).is_file():
                problems.append("reference file missing: %s" % parts[1])

    if problems:
        for p in problems:
            print("FAIL: %s" % p, file=sys.stderr)
        return 1
    return 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    if cmd == "map":
        cmd_map()
        return 0
    if cmd == "check":
        return cmd_check()
    print("usage: course.py map|check", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())

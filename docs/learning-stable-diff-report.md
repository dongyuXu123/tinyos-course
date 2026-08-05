# Learning / stable 历史差异报告

> 这是单一 stable 版本整理前生成的审计记录。当前仓库只发布 `lesson-XX-stable/`；learning 目录已按 canonical 决策移除。由 `scripts/compare-course-variants.py` 生成，比较本身不删除课程文件。

## 比较规则

- 覆盖 Lesson 00–162 的全部 163 对目录。
- 排除 `build/`、`.tinyos-screen-*` 和 `.tinyos-vga-*` 生成物。
- README 的 `Course status` 行归一化后比较；其他 README 内容仍会被报告。
- `identical` 表示归一化后无差异；`documentation-only` 表示只有 README 状态或文档差异；`source-difference` 表示存在源码、Makefile、启动配置或文件集合差异。

## 汇总

| 分类 | 数量 |
|---|---:|
| 完全相同 (`identical`) | 154 |
| 仅文档差异 (`documentation-only`) | 4 |
| 源码/结构差异 (`source-difference`) | 5 |

## 逐课结果

| Lesson | 分类 | 差异文件 | 缺失/新增文件 |
|---:|---|---|---|
| 00 | `identical` | — | — |
| 01 | `identical` | — | — |
| 02 | `identical` | — | — |
| 03 | `identical` | — | — |
| 04 | `identical` | — | — |
| 05 | `identical` | — | — |
| 06 | `identical` | — | — |
| 07 | `identical` | — | — |
| 08 | `identical` | — | — |
| 09 | `identical` | — | — |
| 10 | `identical` | — | — |
| 11 | `identical` | — | — |
| 12 | `identical` | — | — |
| 13 | `identical` | — | — |
| 14 | `identical` | — | — |
| 15 | `identical` | — | — |
| 16 | `identical` | — | — |
| 17 | `identical` | — | — |
| 18 | `identical` | — | — |
| 19 | `identical` | — | — |
| 20 | `identical` | — | — |
| 21 | `identical` | — | — |
| 22 | `identical` | — | — |
| 23 | `identical` | — | — |
| 24 | `identical` | — | — |
| 25 | `identical` | — | — |
| 26 | `identical` | — | — |
| 27 | `documentation-only` | `README.md` — > **Course status: <variant status>** > **Course status: learning edition.** Derived only from `lesson-26-stable`; this tree starts without inherited build artifacts. | — |
| 28 | `identical` | — | — |
| 29 | `identical` | — | — |
| 30 | `identical` | — | — |
| 31 | `identical` | — | — |
| 32 | `identical` | — | — |
| 33 | `identical` | — | — |
| 34 | `documentation-only` | `README.md` — # Lesson 34: bounded address-space object over the validated kernel-embedded user image # Lesson 34: bounded process/thread object with saved user context | — |
| 35 | `source-difference` | `kernel64.c` — /* Lesson 34: bounded address spaces over the inherited syscall ABI. */ /* Lesson 35: bounded CPL3 PIT preemption over the inherited syscall ABI. */ | — |
| 36 | `source-difference` | `README.md` — Lesson 35 extends the validated embedded user image and bounded process/thread objects from Lesson 34. IRQ0 now distinguishes a CPL3-origin frame by its `CS` selector. For the sing<br>`kernel64.c` — /* Lesson 34 keeps one bounded process object and one user thread.  The /* Lesson 36 keeps two bounded process/address-space/thread objects.  The | — |
| 37 | `source-difference` | `README.md` — Lesson 35 extends the validated embedded user image and bounded process/thread objects from Lesson 34. IRQ0 now distinguishes a CPL3-origin frame by its `CS` selector. For the sing<br>`kernel64.c` — task_table[0].pid=0; task_table[0].tid=0; task_table[0].parent_pid=0; task_table[0].kind=TASK_KIND_KERNEL; task_table[0].state=TASK_RUNNING; task_table[0].valid=1; task_table[1].pi | — |
| 38 | `identical` | — | — |
| 39 | `identical` | — | — |
| 40 | `documentation-only` | `README.md` — > **Course status: <variant status>** | — |
| 41 | `documentation-only` | `README.md` — > **Course status: <variant status>** | — |
| 42 | `identical` | — | — |
| 43 | `identical` | — | — |
| 44 | `identical` | — | — |
| 45 | `identical` | — | — |
| 46 | `identical` | — | — |
| 47 | `identical` | — | — |
| 48 | `identical` | — | — |
| 49 | `identical` | — | — |
| 50 | `identical` | — | — |
| 51 | `identical` | — | — |
| 52 | `identical` | — | — |
| 53 | `identical` | — | — |
| 54 | `identical` | — | — |
| 55 | `identical` | — | — |
| 56 | `identical` | — | — |
| 57 | `identical` | — | — |
| 58 | `identical` | — | — |
| 59 | `identical` | — | — |
| 60 | `identical` | — | — |
| 61 | `source-difference` | `boot.S` — .set MB2_HEADER_TAG_GRAPHICS, 5 .set MB2_TAG_FRAMEBUFFER, 5 | — |
| 62 | `identical` | — | — |
| 63 | `identical` | — | — |
| 64 | `identical` | — | — |
| 65 | `identical` | — | — |
| 66 | `identical` | — | — |
| 67 | `identical` | — | — |
| 68 | `identical` | — | — |
| 69 | `identical` | — | — |
| 70 | `identical` | — | — |
| 71 | `source-difference` | `Makefile` — @grep -q 'l71test' kernel64.c @grep -q 'l71test' kernel64.c | — |
| 72 | `identical` | — | — |
| 73 | `identical` | — | — |
| 74 | `identical` | — | — |
| 75 | `identical` | — | — |
| 76 | `identical` | — | — |
| 77 | `identical` | — | — |
| 78 | `identical` | — | — |
| 79 | `identical` | — | — |
| 80 | `identical` | — | — |
| 81 | `identical` | — | — |
| 82 | `identical` | — | — |
| 83 | `identical` | — | — |
| 84 | `identical` | — | — |
| 85 | `identical` | — | — |
| 86 | `identical` | — | — |
| 87 | `identical` | — | — |
| 88 | `identical` | — | — |
| 89 | `identical` | — | — |
| 90 | `identical` | — | — |
| 91 | `identical` | — | — |
| 92 | `identical` | — | — |
| 93 | `identical` | — | — |
| 94 | `identical` | — | — |
| 95 | `identical` | — | — |
| 96 | `identical` | — | — |
| 97 | `identical` | — | — |
| 98 | `identical` | — | — |
| 99 | `identical` | — | — |
| 100 | `identical` | — | — |
| 101 | `identical` | — | — |
| 102 | `identical` | — | — |
| 103 | `identical` | — | — |
| 104 | `identical` | — | — |
| 105 | `identical` | — | — |
| 106 | `identical` | — | — |
| 107 | `identical` | — | — |
| 108 | `identical` | — | — |
| 109 | `identical` | — | — |
| 110 | `identical` | — | — |
| 111 | `identical` | — | — |
| 112 | `identical` | — | — |
| 113 | `identical` | — | — |
| 114 | `identical` | — | — |
| 115 | `identical` | — | — |
| 116 | `identical` | — | — |
| 117 | `identical` | — | — |
| 118 | `identical` | — | — |
| 119 | `identical` | — | — |
| 120 | `identical` | — | — |
| 121 | `identical` | — | — |
| 122 | `identical` | — | — |
| 123 | `identical` | — | — |
| 124 | `identical` | — | — |
| 125 | `identical` | — | — |
| 126 | `identical` | — | — |
| 127 | `identical` | — | — |
| 128 | `identical` | — | — |
| 129 | `identical` | — | — |
| 130 | `identical` | — | — |
| 131 | `identical` | — | — |
| 132 | `identical` | — | — |
| 133 | `identical` | — | — |
| 134 | `identical` | — | — |
| 135 | `identical` | — | — |
| 136 | `identical` | — | — |
| 137 | `identical` | — | — |
| 138 | `identical` | — | — |
| 139 | `identical` | — | — |
| 140 | `identical` | — | — |
| 141 | `identical` | — | — |
| 142 | `identical` | — | — |
| 143 | `identical` | — | — |
| 144 | `identical` | — | — |
| 145 | `identical` | — | — |
| 146 | `identical` | — | — |
| 147 | `identical` | — | — |
| 148 | `identical` | — | — |
| 149 | `identical` | — | — |
| 150 | `identical` | — | — |
| 151 | `identical` | — | — |
| 152 | `identical` | — | — |
| 153 | `identical` | — | — |
| 154 | `identical` | — | — |
| 155 | `identical` | — | — |
| 156 | `identical` | — | — |
| 157 | `identical` | — | — |
| 158 | `identical` | — | — |
| 159 | `identical` | — | — |
| 160 | `identical` | — | — |
| 161 | `identical` | — | — |
| 162 | `identical` | — | — |

## 重点差异解读

- Lesson 35–37 的 `kernel64.c` 属于真实教学源码差异，不能按重复版本处理；应分别审阅 learning 的实验进展和 stable 的验证快照。
- Lesson 61 的启动配置涉及 Multiboot2 graphics handoff，属于图形课程边界差异。
- Lesson 71 的 Makefile 差异仅为格式变化，不代表构建语义不同。
- Lesson 67 的屏幕/VGA 文件被排除，它们是验证证据，不是课程源码。

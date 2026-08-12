# Lesson B23: 端到端综合验收 — 精讲文档

> **课号**：Lesson B23（Mini-GRUB 从零写 GRUB 课程第 23 课，终课）
> **主题**：source-to-screen 全链路、全课程回归验证、课程地图与扩展方向
> **课程位置**：阶段六「故障与验收」第 2 课（课程收尾）
> **前置课程**：[`b22-stable/README.md`](../b22-stable/README.md)（故障调试与 rescue）；
> B01–B21 全部
> **后续课程**：回归 TinyOS 主线（Lesson 01–162）；扩展方向（UEFI、网络启动）
> **一句话目标**：完整跑通"自写引导器 → 光盘 → 内核 → 图形桌面"链路，用统一
> 脚本验证全部 23 课，输出最终课程地图。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能用一条命令验证 Mini-GRUB 全课程
（`validate-course.sh all check` / `all qemu`），并画出从加电到桌面的一整条
source-to-screen 时序。

- **在课程中的位置**：终课。研读支线 0.10 是"GRUB → TinyOS 端到端"的观察课；
  B23 是"**自己写的** GRUB → TinyOS 端到端"的验收课——把 B01–B22 的能力与
  验证手法串成一次全课程回归。
- **责任边界**：本课**不新增引导器功能**（所有能力已在 B01–B22 交付）；
  B23 交付的是**验收动作本身**：回归入口（`all` 模式）、时序文档
  （`docs/source-to-screen.md`）、课程地图（`build/course-map.txt`）与
  验收报告（`build/acceptance.txt`）。
- **前置知识清单**：
  1. B01–B22 全部能力与验证手法（尤其 B12/B21 的 TinyOS 交接）；
  2. 课程验证分层：结构 → build → check → QEMU 冒烟 → VGA 文本 → 专项 GUI；
  3. 研读支线 0.10（端到端时序图）。
- **本课交付**：`scripts/validate-course.sh all` 模式；`docs/source-to-screen.md`
  时序文档；`lessons/b23-stable/Makefile`（`check` 静态断言 + `acceptance`
  全课程回归）；`build/course-map.txt` / `build/acceptance.txt`。

---

## 2. 核心概念精讲

### 2.1 概念一：回归矩阵 —— 23 课 × 两级验证

验证矩阵按"已实现课 / 设计课"分发：

| 层 | 命令 | 覆盖 | 耗时 |
|---|---|---|---|
| 结构 | `check-lesson-doc.py`（无 Makefile 的设计课） | README 结构 | 秒级 |
| build | `make` | 每课产物可构建 | 秒级 |
| check | `make check` | 静态断言（大小/签名/符号/消息/ISO 记录） | 秒级 |
| QEMU 冒烟 | `validate-course.sh bNN qemu` | 不崩溃 + marker 命中 | 每课 5–30s |
| 专项 GUI | `QEMU_SCREENDUMP=1` 像素探针 | LFB 测试图案锚点（B20） | B20 内 |

B23 的 `all` 模式遍历 b01..b23，逐课调用单课验证，收集 PASS/FAIL 并输出
汇总表——这就是终课的"验收动作"：

```
=== Mini-GRUB all-check summary: 23 passed, 0 failed ===
```

### 2.2 概念二：source-to-screen —— 20 个阶段的证据链

`docs/source-to-screen.md` 把链路分成 20 个阶段，每阶段标注课程、GRUB 源码
与 QEMU marker。整条链路的**骨架**：

```
加电 → SeaBIOS → El Torito → stage1 → stage2 → 保护模式 → ISO9660
→ grub.cfg → ELF/header 校验 → PT_LOAD → MBI(mmap+fb) → 交接 → TinyOS
→ 图形桌面 → 故障降级 → 回归验收
```

关键点：**这条链路的每一跳都有 QEMU 证据**——不是纸面设计，而是
`validate-course.sh bNN qemu` 实际命中的 marker。链路的"心脏"是 B21 的
type-8 fb tag 交接（第 17–18 阶段），它把 loader 的 VBE 模式设置变成内核
可验证的 framebuffer 描述。

### 2.3 概念三：能力追溯与边界声明

课程地图（`build/course-map.txt`）把 23 课映射到"主题 + 状态"，README
主表进一步映射到 GRUB 2.14 源码锚点。边界声明（对照实现指南契约清单）：

- **已验证的替换范围**：L01/L05（B12）、L61（B21）——自写引导器真实启动
  主线内核；
- **明确的非目标**：L08 之后 long mode 下的 MBI 重读、UEFI、网络启动等
  不在本课程范围（见第 6 节扩展方向）。

---

## 3. 对照 GRUB 源码

| B23 交付 | 对照 |
|---|---|
| 全课程回归入口（`all`） | 研读支线 0.10 端到端 checkpoint 的"验证矩阵"思想 |
| source-to-screen 时序 | 研读支线 0.10 时序图；各课 README 的"对照"节汇总 |
| 能力追溯表 | [`docs/grub-implementation-guide.md`](../../docs/grub-implementation-guide.md) 契约清单 |
| TinyOS 交接边界 | TinyOS `lessons/lesson-61-stable`（L61 内核 type-8 tag 校验源码） |

## 4. 实现解读

### 4.1 `validate-course.sh all`（回归入口）

```sh
if [ "$LESSON" = "all" ]; then
  # 遍历 b01..b23，逐课调用本脚本自身（check/qemu）
  for n in 01 .. 23; do
    L="b$n"
    if "$0" "$L" "$MODE" >"$log" 2>&1; then pass=$((pass+1)); ...
    else fail=$((fail+1)); ... tail -5 "$log"; fi
  done
  printf '=== Mini-GRUB all-%s summary: %d passed, %d failed ===\n' ...
  [ "$fail" -eq 0 ]
fi
```

设计要点：

1. **递归复用单课逻辑**：`all` 只是循环调用本脚本自身，单课验证逻辑零重复；
2. **失败可见**：每课日志落盘（`${TMPDIR}/mini-grub-all-$$/bNN.log`），FAIL
   时打印日志尾部；
3. **b23 自身无镜像**：qemu 模式对 b23 做**验收产物完整性检查**（时序文档
   存在、all 模式存在、b01-b22 均为已实现课），`skip` 跳过共享 marker 逻辑。

### 4.2 `b23-stable/course.py`（课程地图 + 静态断言）

```bash
python3 course.py map     # 扫描 lessons/bNN-stable → 课号|状态|主题
python3 course.py check   # 断言 23 目录齐全、22 已实现、文档/入口存在
```

`make check` 的断言刻意**不递归**调用 `validate-course.sh`——否则
`all check` → b23 check → `all check` 会死循环（B23 设计红线）。

### 4.3 `make acceptance`（完整验收动作）

```bash
make -C lessons/b23-stable acceptance
```

执行 `validate-course.sh all check` + `all qemu`（约 10 分钟，在临时副本
运行），把结果写入 `build/acceptance.txt`。这是终课对外承诺的"一条命令验收"。

---

## 5. 产物与验证

### 5.1 构建产物

- `course.py`：课程地图生成器 + 终课静态检查；
- `build/course-map.txt`：23 课地图（`make` 生成，提交）；
- `build/acceptance.txt`：验收报告（`make acceptance` 生成，提交）；
- `docs/source-to-screen.md`：20 阶段时序文档（课程级，提交）。

### 5.2 验证命令与判据

```bash
scripts/validate-course.sh b23 check    # 静态断言：23 目录 + 22 已实现 + 文档
scripts/validate-course.sh b23 qemu     # 验收产物完整性检查（无镜像）
scripts/validate-course.sh all check    # 全课程快速矩阵（~2 分钟）
scripts/validate-course.sh all qemu     # 全课程 QEMU 电池（~10 分钟）
```

关键判据：`all check` 与 `all qemu` 汇总 **23 passed, 0 failed**；B12/B21
依赖的 TinyOS `lessons/lesson-0X-stable/build/kernel.elf` 产物存在（缺失时
对应课 FAIL 并明确提示）。

## 6. 安全边界与扩展方向

- 回归全程在临时副本执行；`build/` 只读提交；不自动 commit；
- 扩展方向（文档标注，不在实现范围）：**UEFI**（x86_64-efi，引导协议与
  BIOS 完全不同）、**网络启动**（PXE）、**安全启动**、**多平台模块**
  （x86_64-efi/aarch64-efi）。这些都会改变 stage1/交接协议，需要新课程线。

## 7. 课程小结

Mini-GRUB 23 课至此闭环：研读支线 0.1–0.10 提供"GRUB 是什么"，本课程提供
"GRUB 怎么做"——从 512 字节 stage1 到 VBE 图形模式、从 ISO9660 到
Multiboot2 交接、从 grub.cfg 脚本到 rescue 降级，最终用全课程回归把 23 课
验证为一条可复现的 source-to-screen 链路。课程地图与完整清单见
[`../../README.md`](../../README.md)。

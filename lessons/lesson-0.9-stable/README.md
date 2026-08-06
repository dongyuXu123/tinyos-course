# Lesson 0.9: GRUB 故障与调试实验 — 精讲文档

> **课号**：Lesson 0.9（GRUB 源码研读支线第 9 课，文档观察课，不生成内核）
> **主题**：启动链上各环节出错时 GRUB 的表现、报错文本，以及如何用只读工具定位根因
> **课程主线位置**：第 1 阶段支线；0.8 讲完「怎么造」，本课讲「坏了怎么查」
> **前置课程**：[`lesson-0.8-stable/README.md`](../lesson-0.8-stable/README.md)
> （GRUB 构建、安装和镜像组成）
> **后续课程**：[`lesson-0.10-stable/README.md`](../lesson-0.10-stable/README.md)
> （GRUB → TinyOS 端到端 checkpoint）
> **一句话目标**：建立「现象 → 最可能原因 → 只读检查方法」的排错索引，理解 GRUB rescue
> 模式的基本操作。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能根据 GRUB 的一句报错，用 `readelf`/`xorriso`/`grub-file`
等只读命令定位到具体环节，并描述 rescue 模式的处置思路。

- **在课程主线中的位置**：排错能力是进入 Lesson 01 写内核前的最后一块拼图——第一课你写的
  代码如果起不来，报错很可能来自 GRUB 而非 TinyOS。本课教你「先分清责任人」。
- **前置知识清单**：
  1. [`lesson-0.2-stable`](../lesson-0.2-stable/README.md) 的命令分发（`command not found` 从哪来）；
  2. [`lesson-0.3-stable`](../lesson-0.3-stable/README.md) 的路径解析（`file not found` 从哪来）；
  3. [`lesson-0.4-stable`](../lesson-0.4-stable/README.md) 与 [`lesson-0.5-stable`](../lesson-0.5-stable/README.md)
     的 ELF 装载与 header 校验（`invalid ELF header` / header 失败从哪来）。
- **本课交付**：故障场景 × 根因 × 只读诊断方法表 + rescue 命令速查表 + 安全实验流程。

---

## 2. 核心概念精讲

### 2.1 概念一：GRUB 的两级错误现场

GRUB 出错后有两个现场：

| 现场 | 触发条件 | 界面 |
|---|---|---|
| normal 提示符 `grub>` | core image 能起来，模块/配置部分缺失 | 命令可用，但脚本/菜单可能没加载 |
| rescue 提示符 `grub rescue>` | core image 都起不全，或 `normal.mod` 加载失败 | 只剩最小内置命令集 |

**为什么会有 rescue？** core image 只保证「最小内核能跑」；一旦它找不到模块或配置，就退到
只能手工敲命令的最低限度状态。这也是 0.8 课 `prefix` 那么重要的原因。

### 2.2 概念二：典型故障的五类根因

| 故障 | 报错特征（典型文本） | 根因层 |
|---|---|---|
| 缺 grub.cfg / 模块 | 进入 `grub rescue>` | prefix 或模块路径不对（0.8） |
| 路径错 | `error: file not found` | 文件系统/路径（0.3） |
| 模块缺失 | `error: command not found` | 命令注册表（0.2） |
| 坏 ELF | `error: invalid ELF header` | ELF 解析（0.4） |
| 坏 Multiboot2 header | `grub-file` 非 0 / 装载器拒绝 | header 校验（0.5） |

**方法论**：报错文本本身就在提示「卡在哪个层」。按 0.2–0.5 课的知识把每一句报错映射到
对应抽象层，就能把排查范围缩小到一层。

### 2.3 概念三：rescue 模式的最小命令集

`grub rescue>` 下仍可用的命令（GRUB 内置）：

| 命令 | 用途 | 典型用法 |
|---|---|---|
| `set` | 查看/设置环境变量 | `set`；`set prefix=(cd0)/boot/grub` |
| `ls` | 列出设备与文件 | `ls`；`ls (cd0)/` |
| `insmod` | 加载模块（若可访问模块文件） | `insmod normal` |
| `normal` | 启动 normal 模式 | `normal` |
| `cat`/`configfile` | 查看/执行配置 | `configfile (cd0)/boot/grub/grub.cfg` |

**处置思路**：先 `set` 看 prefix 对不对 → `ls` 确认设备与路径存在 → 修 prefix → `insmod
normal` → `normal`。**注意**：rescue 时很多命令不可用，能用的只有内置的一小撮。

### 2.4 概念四：只读诊断与故障注入的边界

本课所有故障**注入**（改 cfg、删模块、损坏 ELF）必须在**临时副本**里做，绝不碰 stable 产物；
本课的观察工具（`grub-file`、`readelf`、`xorriso`）只读。规则依据
[`docs/grub-source-study.md`](../../docs/grub-source-study.md) 的证据边界。

---

## 3. 机制精讲与观察方法

### 3.1 故障一：缺 grub.cfg / prefix 不对

**现象**：QEMU 停在实际/交互提示符或 rescue。

**只读诊断**：

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --   # 应存在，102 字节
xorriso -indev "$ISO" -find /boot/grub/i386-pc/normal.mod -exec lsdl --
```

**预期输出解读**：两条都命中说明「配置与模块都在位」，故障更可能来自运行时 prefix 偏差；
若第一条为空，说明 ISO 上根本没有 grub.cfg（构建期问题，查 0.8 课 Makefile）。

### 3.2 故障二：路径错 / 文件缺失

**现象**：`error: file not found`。

**只读诊断**：

```bash
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --   # 应 5352 字节
xorriso -indev "$ISO" -find /boot -exec lsdl --              # 目录链完整性
```

**预期输出解读**：`/boot/kernel.elf` 缺失或大小异常（≠5352）说明「逻辑路径与 ISO 布局不一致」
（0.3 课的排查点）；目录链不完整说明构建期复制少了一步。

### 3.3 故障三：缺失模块导致命令未注册

**现象**：`error: command not found: multiboot2`。

**只读诊断**：

```bash
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -c '\.mod'   # 期望 297
xorriso -indev "$ISO" -find /boot/grub/i386-pc/multiboot2.mod -exec lsdl --       # 15972 字节
```

**预期输出解读**：`multiboot2.mod` 不在模块集里时，`multiboot2` 命令就不会注册（0.2 课机制）；
`grep -c` 数量显著小于 297 说明模块被裁剪过（0.8 课问题）。

### 3.4 故障四：坏 ELF

**现象**：`error: invalid ELF header`。

**只读诊断**：

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
file "$K"                       # ELF 32-bit ... Intel i386
readelf -h -W "$K" | grep -E '类别|入口'   # ELF32 / entry 0x100020
readelf -l -W "$K" | grep LOAD   # 两个 LOAD 段
```

**预期输出解读**：三类检查全绿才说明 ELF 结构完整；若 `file` 显示 "data" 或
`readelf` 报错，说明文件不是合法 ELF（0.4 课装载会直接拒绝）。GRUB 侧报错的根因在
`grub-core/kern/elf.c` 的 `grub_elf32_open` 校验点。

### 3.5 故障五：坏 Multiboot2 header

**现象**：`grub-file --is-x86-multiboot2` 退出非 0；或装载器拒绝。

**只读诊断**：

```bash
grub-file --is-x86-multiboot2 "$K"; echo $?     # 期望 0
readelf -x .multiboot "$K"                       # 对照 0.5 课字节表
readelf -S -W "$K" | grep multiboot             # 地址 0x100000、Al 8
```

**预期输出解读**：magic/checksum/对齐/位置四项（0.5 课 3.3 表）逐一核对；常见失败是
`KEEP()` 丢了 header 或 `ALIGN(8)` 缺失。

### 3.6 故障注入实验（仅临时副本，文字流程）

下列实验**不要**在 stable 目录执行；复制 `lesson-01-stable` 到个人目录后逐一尝试：

1. 删掉 `iso/boot/grub/grub.cfg` 再 `grub-mkrescue` → 观察是否进入 rescue；
2. 改 grub.cfg 路径为不存在文件 → 记录 `file not found` 文本；
3. 在 ISO 目录里删掉 `multiboot2.mod`（连同模块集）→ 记录 `command not found`；
4. 用 `dd` 破坏 `kernel.elf` 中部一个字节 → 观察 `invalid ELF header` 或启动失败；
5. 改 `boot.S` 的 checksum 常量 → `grub-file` 应返回非 0（0.5 课验证过原理）。

每个实验后都回到 3.1–3.5 的只读诊断，把「现象 → 证据」闭环。

---

## 4. 数据流与运行逻辑

```text
故障进入点 → GRUB 报错/现场 → 只读诊断（按层缩小范围）→ 修复（个人副本）
  ├─ 配置/模块层：rescue、command not found   → xorriso 查 ISO 文件树
  ├─ 文件系统层：file not found               → xorriso -find 核对路径
  ├─ ELF 层：invalid ELF header               → file/readelf 查结构
  └─ 协议层：header 校验失败                   → grub-file + readelf -x
```

**排错第一原则**：先分清责任人（固件/GRUB/内核），再用只读工具把证据钉死，最后才改代码。

---

## 5. 观察与验证

### 5.1 依赖

`grub-common`（grub-file）、`binutils`（file/readelf）、`xorriso`。

### 5.2 复现命令清单（全部只读）

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
ISO="lessons/lesson-01-stable/build/kernel.iso"
grub-file --is-x86-multiboot2 "$K"; echo $?     # 0（正常基线）
readelf -x .multiboot "$K"                       # d65052e8 00000000 18000000 12afad17 ...
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --   # 5352 字节
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -c '\.mod'  # 297
```

### 5.3 实测记录（2026-08-06，全部只读）

正常基线全部命中：`grub-file` 退出 0、`.multiboot` 字节与 0.5 课一致、`/boot/kernel.elf`
5352 字节、`i386-pc` 297 个模块。故障注入实验未执行（遵守 stable 只读边界），仅给出流程。

### 5.4 安全边界（本课红线）

任何改动/删除/重建只在个人副本进行；本课观察命令只读；不运行 `make`、`qemu`、`git`；
不下载或执行第三方源码。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 直接进 `grub rescue>` | core image 起不来或 normal 加载失败 | `set` 看 prefix；`ls` 看设备；修复后 `insmod normal` |
| `error: file not found` | grub.cfg 里路径与 ISO 布局不符 | `xorriso -find /boot/kernel.elf` |
| `error: command not found: multiboot2` | 缺 `multiboot2.mod` | 检查 `i386-pc/` 模块清单 |
| `error: invalid ELF header` | kernel.elf 非 ELF 或损坏 | `file`、`readelf -h` |
| `grub-file` 退出非 0 | header 校验失败 | `readelf -x .multiboot` 对照 0.5 表 |
| `error: unknown filesystem` | 文件系统驱动没加载或盘格式未知 | 确认 `iso9660.mod` 在位 |
| 想改任何文件实验 | 会破坏 stable 基线 | 复制个人副本再操作 |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/init/main.c` 的 `early_param`/`panic` 处理：Linux 早期启动也会在无法继续时
  停在有限界面（`emergency`/`initramfs` shell），与 GRUB rescue 是同一设计思想；
- `linux-v6.12/kernel/panic.c`：内核 panic 的「停止 + 报错」模式与 GRUB 的报错+提示符
  同属「受控失败」工程实践。

**边界提醒**：GRUB 报错文本与 rescue 行为以 GRUB 实现为准；Linux 对照仅用于设计意图类比。

---

## 8. 思考题与练习

1. 概念理解：rescue 模式为什么只保留 `set`/`ls`/`insmod`/`normal` 等少数命令？
2. 源码定位：在 `$GRUB_SRC` 中找 `error: file not found` 与 `invalid ELF header` 对应的
   报错源点（`grub_file`/`grub_elf` 相关），记录所在文件。
3. 动手观察：运行 5.2 的四个命令，把每条输出记下来，作为「正常基线」。
4. 实验（个人副本）：按 3.6 的流程 2 制造 `file not found`，用 `xorriso -find` 验证根因。
5. 综合：把 2.2 表扩展成「报错文本 → 课程章节 → 检查命令」三列速查表。

---

## 9. 本课小结与下一课预告

**小结**：GRUB 报错现场分 normal 与 rescue 两级；五类典型故障（缺配置/错路径/缺模块/坏 ELF/
坏 header）分别对应 0.2–0.5 课的一层抽象；只用 `grub-file`、`readelf`、`xorriso` 三个只读
工具就能把证据钉死在某一层；故障注入必须在个人副本进行，stable 目录保持冻结。

**下一课预告**：进入 [`lesson-0.10-stable`](../lesson-0.10-stable/README.md)，把 0.1–0.9 的
全部零件拼成一条完整的 GRUB → TinyOS 时序线，做支线收官的端到端 checkpoint。

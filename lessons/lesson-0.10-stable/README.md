# Lesson 0.10: GRUB → TinyOS 端到端 checkpoint — 精讲文档

> **课号**：Lesson 0.10（GRUB 源码研读支线收官课，文档观察课，不生成内核）
> **主题**：把 0.1–0.9 的每一环拼成一条完整的启动时序线，做进入 Lesson 01 前的总检查
> **课程主线位置**：第 1 阶段支线终点；完成本课后进入
> [`Lesson 01`](../lesson-01-stable/README.md)（第一个可执行内核）
> **前置课程**：[`lesson-0.9-stable/README.md`](../lesson-0.9-stable/README.md)
> （GRUB 故障与调试实验）
> **后续课程**：[`lesson-01-stable/README.md`](../lesson-01-stable/README.md)
> （从 GRUB 启动并在 VGA 屏幕显示 TinyOS）
> **一句话目标**：在写第一行内核代码前，用只读证据把「BIOS → GRUB → MBI → _start」整条链
> 验证一遍，把每个环节的责任人与检查命令背下来。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能不看讲义、只凭工具输出，从头到尾讲出 kernel.iso 从光盘
到 `_start` 的每一跳，并说清每个跳变对应哪个源码目录、哪个规范、哪条检查命令。

- **在课程主线中的位置**：支线 0.1–0.9 分别研究了源码树、配置、文件系统、ELF 装载、header
  校验、MBI、平台分支、构建与故障；本课把它们**串成一条时序**，验证「分工与接口」完整闭环，
  之后 Lesson 01 写内核时所有现象都能在这里找到归属。
- **前置知识清单**：
  1. [`lesson-0.1-stable`](../lesson-0.1-stable/README.md) 到
     [`lesson-0.9-stable`](../lesson-0.9-stable/README.md) 的全部机制（至少能复述每课的「一句话」）；
  2. 会用 `readelf`/`xorriso`/`grub-file` 三个只读工具；
  3. 知道安全边界（只读、个人副本、不覆盖 stable）。
- **本课交付**：端到端时序图 + 逐段证据表 + 「进入 Lesson 01」检查清单。

---

## 2. 核心概念精讲

### 2.1 概念一：checkpoint 是什么

checkpoint 不是新机制，而是「把已学机制当知识清单逐项打勾」。每一项要回答四个问题：
**谁做（责任层）→ 依据什么（规范/源码）→ 产物/状态（证据）→ 怎么验（只读命令）**。
本课把 0.1–0.9 的十课内容压成这张表。

### 2.2 概念二：端到端时序全景

```text
[0.7] SeaBIOS 按 El Torito#1（LBA 3040）执行 eltorito.img
[0.7] i386-pc core image：实模式→保护模式 → grub_main
[0.1] core image 加载模块（normal.mod 等），prefix=/boot/grub
[0.2] normal 读 /boot/grub/grub.cfg → menuentry 登记菜单
[0.2] timeout=0/default=0 → 自动选中 → 执行函数体
[0.3] multiboot2 /boot/kernel.elf：五层抽象打开文件（5352 字节）
[0.5] 前 32768 字节内 8 对齐扫描 → magic/checksum 校验
[0.4] ELF 装载：两个 PT_LOAD → 0x100000（R E）/ 0x101000（RW+清零）
[0.6] boot：生成 MBI（mmap 等 tag）→ EAX/EBX → jmp 0x100020
[0.5] _start：cli → 建栈（.bss）→ kernel_main32 → VGA Hello
```

### 2.3 概念三：责任人与权威来源边界

| 环节 | 责任人 | 权威来源 |
|---|---|---|
| CPU reset、模式切换 | CPU | Intel SDM |
| 固件初始化、El Torito 执行 | SeaBIOS | QEMU/SeaBIOS |
| 读 ISO、解析配置、装载 ELF、生成 MBI | GRUB | GNU GRUB 源码/文档 |
| header/MBI 格式 | 协议 | Multiboot2 规范 |
| 接手后的一切 | TinyOS | 本课程代码 |

排错第一原则（0.9 课）在此收束：**先用本表确定责任人，再用对应只读命令取证**。

---

## 3. 机制精讲与观察方法

### 3.1 段 A：固件 → GRUB 核心（证据来自 0.1/0.7）

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -report_el_torito plain
```

实测（节选）：`El Torito boot img 1: BIOS ... 4 ... 3040`，路径 `/boot/grub/i386-pc/eltorito.img`。
**解读**：SeaBIOS 读这条记录 → 执行引导段 → core image 接管。只读命令即「固件侧检查」。

### 3.2 段 B：配置与命令（证据来自 0.2）

```bash
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --   # 102 字节
wc -c lessons/lesson-01-stable/grub.cfg                          # 102（一致）
```

**解读**：配置在 ISO 上、与源码一致；`menuentry`/`multiboot2`/`boot` 三条命令在
`normal`+`multiboot2.mod` 支持下行得通（命令注册证据见 0.2 课 3.4）。

### 3.3 段 C：路径解析与文件可达（证据来自 0.3）

```bash
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --       # 5352 字节
xorriso -indev "$ISO" -find /boot -exec lsdl --                  # 目录链完整
```

**解读**：五层抽象（设备→磁盘→分区→fs→文件）的终端目标存在；`multiboot2` 能拿到
5352 字节的文件流。

### 3.4 段 D：header 校验与 ELF 装载（证据来自 0.4/0.5）

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
grub-file --is-x86-multiboot2 "$K"; echo $?        # 0 = header 合法
readelf -x .multiboot "$K"                          # d65052e8 00000000 18000000 12afad17 ...
readelf -h -W "$K" | grep 入口                     # 0x100020
readelf -l -W "$K" | grep LOAD                     # R E 0x100000 / RW 0x101000
objdump -d -Mintel --disassemble=_start "$K"       # cli → mov esp → call
```

**解读**：header 四字段通过（0.5 课可手算 checksum）；两个 LOAD 段决定内存布局；`_start`
第一条指令是 `cli`——GRUB 已把 CPU 放在 32 位保护模式，TinyOS 只需接手。

### 3.5 段 E：MBI 与交接（证据来自 0.6）

```bash
grep -n "MB2_BOOT_MAGIC\|MB2_TAG_MMAP" lessons/lesson-05-stable/kernel.c | head -5
readelf -x .multiboot lessons/lesson-05-stable/build/kernel.elf   # 请求 type 6 的 tag
```

**解读**：交接协议在 TinyOS 侧的消费端是 `0x36d76289` magic 与 type-6 mmap walker；MBI 是
运行时产物，静态 ELF 只留下「请求侧」证据（Lesson 05 header 的信息请求 tag）。运行观察须按
Lesson 05 的流程在 QEMU 中进行（个人副本）。

### 3.6 段 F：进入 Lesson 01 检查清单

| # | 检查项 | 命令 | 通过标准 |
|---|---|---|---|
| 1 | 工具版本 | `grub-file --version` | 2.x（实测 2.14-2ubuntu2） |
| 2 | header 合法 | `grub-file --is-x86-multiboot2` | 退出码 0 |
| 3 | 入口正确 | `readelf -h` | `0x100020` |
| 4 | 装载布局 | `readelf -l` | 两个 LOAD、无 RWX |
| 5 | ISO 布局 | `xorriso -find /boot` | `kernel.elf` 5352、`grub.cfg` 102 |
| 6 | El Torito | `xorriso -report_el_torito` | BIOS 记录存在 |
| 7 | 模块在位 | `xorriso -find i386-pc | grep -c .mod` | 约 297 |

全部通过后，Lesson 01 的 `make run` 若还有问题，报错可 100% 归到 TinyOS 侧（或 0.9 课
清单）。

---

## 4. 数据流与运行逻辑

完整时序 + 每段证据命令：

```text
SeaBIOS ──El Torito#1──► eltorito.img ──► core image(i386-pc)
   │                                            │ [0.1/0.7: xorriso -report_el_torito]
   ▼                                            ▼
grub_main ──► normal 读 grub.cfg(102B) ──► menuentry("TinyOS lesson 1")
   │ [0.2: xorriso -find /boot/grub/grub.cfg]     │
   ▼                                              ▼
multiboot2 /boot/kernel.elf ──► 五层文件抽象(5352B)
   │ [0.3: xorriso -find /boot/kernel.elf]         │
   ▼                                              ▼
header 校验(magic/checksum/对齐/32KiB) ──► ELF 装载(两个 LOAD)
   │ [0.5: grub-file + readelf -x]   [0.4: readelf -l]  │
   ▼                                                     ▼
boot ──► 生成 MBI(mmap…) ──► EAX=0x36d76289, EBX=MBI ──► jmp 0x100020
   │ [0.6: 运行时观察(个人副本)]        [0.5: boot.S/_start] │
   ▼                                                        ▼
_start: cli → mov esp,0x105000 → call kernel_main32 ──► VGA "TinyOS lesson 1"
   [Lesson 01 侧；GRUB 不再参与]
```

---

## 5. 观察与验证

### 5.1 依赖

`binutils`、`grub-common`、`xorriso`；源码阅读需 `$GRUB_SRC`。

### 5.2 端到端复现命令（全部只读）

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
ISO="lessons/lesson-01-stable/build/kernel.iso"
grub-file --is-x86-multiboot2 "$K"; echo $?
readelf -x .multiboot "$K"; readelf -h -l -W "$K"
objdump -d -Mintel --disassemble=_start "$K"
xorriso -indev "$ISO" -report_el_torito plain
xorriso -indev "$ISO" -find /boot -exec lsdl --
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -c '\.mod'
```

### 5.3 实测记录（2026-08-06，全部只读）

全部检查项通过：`grub-file` 退出 0；`.multiboot` = `d6 50 52 e8 | 00 00 00 00 |
18 00 00 00 | 12 af ad 17 | 00 00 00 00 | 08 00 00 00`；入口 `0x100020`；两个 LOAD
（R E 0x100000 / RW 0x101000，无 RWX）；`_start` 为 `cli; mov esp,0x105000; xor ebp,ebp;
call kernel_main32`；El Torito BIOS 记录 LBA 3040；`/boot/kernel.elf` 5352 字节；
`i386-pc` 297 个模块。**未运行 make/qemu，未改动任何文件。**

### 5.4 安全边界（本课红线）

本课收官检查全部只读；进入 Lesson 01 后，任何实验仍在个人副本进行；`lesson-01-stable`
及其产物保持冻结作为全课程证据基线（依据
[`docs/grub-source-study.md`](../../docs/grub-source-study.md)）。

---

## 6. 调试地图

| 现象（Lesson 01 阶段） | 归属环节 | 回查课程/命令 |
|---|---|---|
| `make run` 无任何画面 | 固件/El Torito | 0.7：`xorriso -report_el_torito` |
| 菜单没出现直接黑屏 | 配置/命令 | 0.2/0.9：检查 grub.cfg 与模块 |
| `error: file not found` | 路径/文件系统 | 0.3：`xorriso -find /boot` |
| `error: invalid ELF header` | ELF | 0.4：`file`/`readelf -h` |
| `grub-file` 失败 | header | 0.5：`readelf -x .multiboot` |
| 进 `_start` 后立刻崩 | TinyOS 侧 | Lesson 01 的 boot.S 栈/入口 |
| MBI walker 出错 | MBI 格式 | 0.6：Lesson 05 walker 校验 |

---

## 7. 与 Linux 源码对照

- 整个 0.x 支线对照 Linux 的位置：`linux-v6.12/arch/x86/boot/`（早期启动布局）、
  `fs/binfmt_elf.c`（ELF 装载）、`arch/x86/kernel/e820.c`（内存图）、`drivers/firmware/efi/`
  （UEFI 分支）——Linux 每块都有对应实现，但**都不是** GRUB/Multiboot2 的权威来源。

**边界提醒**：Multiboot2 规范、Intel SDM、GNU GRUB 源码、QEMU/SeaBIOS 各自定义自己的事实；
Linux 仅作工程对照。这条证据边界贯穿整个支线，也是进入 Lesson 01 后判断「责任人」的准则。

---

## 8. 思考题与练习

1. 综合：不看讲义，画一遍「SeaBIOS → _start」的时序图，标出每段的责任人与一条检查命令。
2. 概念理解：为什么说 `grub-file` 通过 ≠ 能启动？端到端里还差哪几步？
3. 源码定位：`_start` 的反汇编里哪条指令对应「GRUB 已完成保护模式交接」的隐含前提？
4. 动手观察：把 3.6 清单 7 项全部跑一遍，写一份自己的「通过记录」。
5. 预告：读完 [`lesson-01-stable/README.md`](../lesson-01-stable/README.md)，
   找出 boot.S 里哪 4 个 `.long` 对应 0.5 课的四字段。

---

## 9. 本课小结与下一课预告

**小结**：本课把 0.1–0.9 拼成一条完整时序：SeaBIOS → El Torito → core image → normal →
grub.cfg → multiboot2 → header 校验 → ELF 装载 → MBI → `_start`；每段都有只读证据与责任人；
7 项检查清单全部通过后，GRUB 侧已无可疑，接下来写内核时的报错可以放心归因到 TinyOS。

**下一课预告**：进入 [`lesson-01-stable`](../lesson-01-stable/README.md)，正式开始写内核：
Multiboot2 header + 32 位 `_start` + 直接写 VGA `0xb8000`，在 QEMU 图形窗口显示第一句
"TinyOS lesson 1"——你已知道 GRUB 会带着 EAX/EBX 跳进来，剩下的就是自己搭栈、调 C、写屏。

# Lesson 00: 从加电复位到 GRUB 的 Multiboot2 保护模式交接 — 精讲文档

> **课号**：Lesson 00（文档观察课，不生成内核）
> **主题**：启动链全景：CPU 加电 → SeaBIOS → GRUB → Multiboot2 i386 交接 → TinyOS `_start`
> **课程主线位置**：第 1 阶段（启动链与基础输出）的起点；位于可执行课程之前
> **前置课程**：无（零基础即可开始）
> **后续课程**：[`lesson-0.1-stable/README.md`](../lesson-0.1-stable/README.md)（GRUB 源码研读支线）→
> [`lesson-01-stable/README.md`](../lesson-01-stable/README.md)（第一个 VGA Hello 内核）
> **一句话目标**：在写第一行 TinyOS 内核代码前，准确划清 CPU、QEMU/SeaBIOS、GRUB、Multiboot2
> 和 TinyOS 各自的责任，理解为什么第一课的 `_start` 一开始就是 32 位保护模式代码。

---

## 1. 课程定位（Mission）

**一句话目标**：能画出「按下电源 → 看到 VGA 文字」之间每一层的责任边界，并且能对
已冻结的 Lesson 01 产物（`kernel.elf`、`kernel.iso`）做只读检查，从二进制证据中读出
「GRUB 会把控制权交给谁、按什么协议交」。

- **在课程主线中的位置**：本课是全部课程的第一课，但**没有任何内核代码**。
  它的对象是 `lesson-01-stable/` 的冻结产物。为什么要先上这一课？因为如果不知道
  `_start` 之前发生了什么，第一课的 `boot.S` 里每一行都会显得「凭空出现」。
- **前置知识清单**：
  1. 二进制与十六进制的基本概念（字节、地址、内存）；
  2. 一条汇编指令长什么样（`mov`、`call`）——不需要会写，只需要会认；
  3. 虚拟机是什么（QEMU 模拟一台 x86 PC）。
- **本课交付**：一个可重复的「启动链观察实验」：用 binutils/xorriso 工具
  从 ELF 和 ISO 中读出 Multiboot2 header、ELF 段、入口点、ISO 启动记录，
  并跑一次 Lesson 01 看到真实运行结果。

---

## 2. 核心概念精讲

### 2.1 概念一：启动链的分层责任（最重要的概念）

从按下电源到 TinyOS 运行，每一层只负责一件事，谁也不替谁干活：

```text
CPU 加电复位（Intel SDM 定义 CPU 状态）
  → QEMU 默认 SeaBIOS（本实验的固件：初始化硬件、按启动顺序找 CD-ROM）
  → GRUB（独立 bootloader：读 ISO 文件系统、读 grub.cfg、装载内核）
  → Multiboot2（loader 与内核之间的协议：header + i386 交接状态）
  → TinyOS _start（我们自己的 32 位保护模式入口）
```

**为什么必须分清责任？** 因为排错的第一原则是「找对责任人」：

| 要解释的事实 | 权威来源 | 边界 |
|---|---|---|
| CPU reset、实模式、保护模式、控制寄存器、long mode | **Intel SDM Vol. 3** | Intel 定义 CPU；Linux/GRUB/QEMU 都不是 x86 规范 |
| 本实验的 BIOS 固件 | **QEMU + SeaBIOS**（`/usr/share/seabios/bios.bin`） | 只是本课程 QEMU 的默认路径 |
| `grub-mkrescue`、`grub-file`、loader 行为 | **GNU GRUB 官方文档/源码** | GRUB 负责读 ISO、配置、装载；TinyOS 不实现这些 |
| header 格式、i386 交接状态、MBI | **Multiboot2 官方规范** | 是 loader↔内核协议，不是 Linux boot protocol |
| TinyOS 接手后的工程设计 | **Linux v6.12**（工程对照） | Linux 是参考，不定义 BIOS/GRUB/Multiboot2 |

### 2.2 概念二：保护模式不是 TinyOS 切进去的

第一课的 `boot.S` 里写着 `.code32` 和 `_start:`，但这**不代表** TinyOS 自己完成了
实模式 → 保护模式的切换：

```asm
.code32
_start:
    cli                     /* 关中断 */
    movl $stack_top, %esp   /* 建栈 */
    xorl %ebp, %ebp         /* 帧指针清零 */
    call kernel_main32      /* 进 C */
```

这里的 `.code32` 只是**汇编器指令**——它告诉汇编器「接下来的指令按 32 位编码」。
真正把 CPU 从实模式带到保护模式的是 **GRUB**：GRUB 按 Multiboot2 规范的 i386 交接
约定完成装载后，CPU 已经在 32 位保护模式，`_start` 只是「接手」。

> **记忆钩子**：`.code32` 管「怎么编码」，不管「CPU 处于什么模式」。

### 2.3 概念三：保护模式 ≠ long mode

课程的目标架构是 x86_64，但第一课明显是 32 位（`gcc -m32`、`ld -m elf_i386`）。
这不是矛盾，而是刻意保留的**可见早期阶段**：

```text
实模式（早期固件相关）
   │  GRUB 完成 Multiboot2 i386 交接
   ▼
32 位保护模式（Lesson 01–07：VGA、键盘、shell、内存图、分页）
   │  TinyOS 自己建立 GDT、页表、CR4.PAE、EFER.LME、CR0.PG
   ▼
64 位 long mode（Lesson 08 起：真正的 -m64 内核）
```

所以：Multiboot2 header 里的 `architecture=0`（i386）描述的是**交接架构**，
不是说「课程最终不做 64 位」。long mode 由 TinyOS 自己在 Lesson 08 完成，
它绝不能偷偷藏在 Hello 后面，必须独立成课、独立验证。

### 2.4 概念四：Multiboot2 header 与校验和

GRUB 装载内核前，要验证「这确实是一个 Multiboot2 内核」。验证对象是镜像前
**32 KiB 内、8 字节对齐**的 header。header 前 16 字节是固定格式：

```text
偏移  长度  字段
0     4     magic = 0xe85250d6
4     4     architecture = 0（i386）
8     4     length（header 总长）
12    4     checksum = -(magic + architecture + length)   （低 32 位）
```

校验规则：`magic + architecture + length + checksum ≡ 0 (mod 2^32)`。
读者可在 `lesson-01-stable/boot.S` 中看到这些常量与 `.long` 指令一一对应。

### 2.5 概念五：ISO 与 El Torito 启动记录

`grub-mkrescue` 把 `iso/` 目录做成**可启动光盘镜像**。传统 BIOS 启动光盘使用
El Torito 规范：光盘里有一份「启动目录」，BIOS 通过它找到 GRUB 的 core image，
再把控制权交给它。所以 `make run` 用 `-cdrom build/kernel.iso` 而不是
`-kernel kernel.elf`——前者走的是课程定义的完整启动链，后者绕过了 GRUB。

---

## 3. 机制精讲与观察方法

本课没有源码，观察对象是冻结的 Lesson 01 产物。先定义路径（在仓库根目录执行）：

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
ISO="lessons/lesson-01-stable/build/kernel.iso"
```

### 3.1 观察一：Multiboot2 header 校验

```bash
grub-file --is-x86-multiboot2 "$K"
echo $?        # 0 = 是合法 Multiboot2 内核
```

这是「最小协议检查」：GRUB 的工具链用它来判断一个 ELF 是否可作为
Multiboot2 内核装载。**注意**：它只证明 header 合法，不证明能启动。

### 3.2 观察二：ELF 身份与入口点

```bash
file "$K"
readelf -h -l -S -W "$K"
```

预期关键输出（课程实测记录，2026-08-01）：

```text
kernel.elf: ELF 32-bit ... Intel 80386
Entry point: 0x100020
```

`0x100020` 说明：内核从物理 1 MiB（`0x100000`）开始，入口在 1 MiB + 0x20 处——
正是 `linker.ld` 中 `.multiboot`（16 字节）之后、`.text` 起始的 `_start`。
`readelf -l` 还能看到 LOAD 段：RX 段与 RW 段分离（没有 RWX 段）。

### 3.3 观察三：.multiboot 节的原始字节

```bash
readelf -x .multiboot "$K"
```

预期（little-endian 解释）：

```text
d6 50 52 e8 | 00 00 00 00 | 18 00 00 00 | 12 af ad 17 | 00 00 08 00 ...
```

| 字节区间 | 值（小端） | 含义 |
|---|---|---|
| `d65052e8` | 0xe85250d6 | Multiboot2 magic |
| `00000000` | 0 | architecture = i386 |
| `18000000` | 0x18 = 24 | header 长度（4×4 + 结束 tag 8） |
| `12afad17` | 0x17adaf12 | 校验和 = -(magic+arch+len)，保证四字段和为 0 |

`00 00 08 00` 是结束 tag（type=0, size=8）。把字节和字段对应起来，
是「规范 → 二进制」的第一课。

### 3.4 观察四：_start 的反汇编

```bash
objdump -d -Mintel --disassemble=_start "$K"
```

预期：`cli → mov esp,0x105000 → xor ebp,ebp → call kernel_main32`。
注意：**没有任何一条指令在做 real mode → protected mode 切换**——证据直接反驳了
「TinyOS 自己切保护模式」的误解。

### 3.5 观察五：ISO 的启动记录与文件

```bash
xorriso -indev "$ISO" -report_el_torito plain
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --
```

预期：ISO 有 `El Torito BIOS boot image`；文件系统中有 `/boot/kernel.elf` 与
`/boot/grub/grub.cfg`。这对应 grub.cfg 里的 `multiboot2 /boot/kernel.elf`。

### 3.6 观察六：真实运行

```bash
make -C lessons/lesson-01-stable run
```

**不要加 `-display none`**：成功画面在 QEMU 图形窗口，不是串口。应看到：

```text
TinyOS lesson 1
Hello from the VGA text console!
Multiboot2 boot succeeded.
```

---

## 4. 数据流与运行逻辑

本课把「看不见的启动链」变成「看得见的证据链」：

```text
make run
  → QEMU 启动 SeaBIOS
  → BIOS 按 El Torito 找到光盘启动记录 → 装入 GRUB
  → GRUB 读 /boot/grub/grub.cfg（menuentry "TinyOS lesson 1"）
  → 执行 multiboot2 /boot/kernel.elf
      ├─ 在镜像前 32 KiB 内找 8 字节对齐的 header → 校验 magic/checksum
      ├─ 按 ELF PT_LOAD 段装载内核到 1 MiB 起
      └─ 准备 i386 交接：EAX=0x36d76289, EBX=MBI 地址
  → 跳转 _start（32 位保护模式）
  → boot.S：cli → 建 16 KiB 栈 → call kernel_main32
  → kernel.c：写 VGA 0xb8000 → 图形窗口出现三行文字
```

每一步，都在第 3 节的观察命令中找到了对应证据（header 字节、入口点、
El Torito 记录、最终画面）。

---

## 5. 观察与验证

### 5.1 依赖

本课只读验证需要：`binutils`（readelf/objdump）、`grub-common`（grub-file）、
`xorriso`、`file`。运行 Lesson 01 另需 `qemu-system-x86` 与构建工具。

### 5.2 验证步骤与预期

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
ISO="lessons/lesson-01-stable/build/kernel.iso"
grub-file --is-x86-multiboot2 "$K"        # 期望：exit 0
file "$K" "$ISO"                          # 期望：ELF 32-bit Intel 80386
readelf -x .multiboot "$K"                # 期望：magic 0xe85250d6、length 0x18、校验和匹配
objdump -d -Mintel --disassemble=_start "$K"  # 期望：cli→mov esp→xor ebp→call
xorriso -indev "$ISO" -report_el_torito plain  # 期望：El Torito BIOS boot image
make -C lessons/lesson-01-stable run      # 期望：QEMU 图形窗口三行文字
```

### 5.3 课程实测记录（2026-08-01，只读验证）

`grub-file` 成功；`file` 识别 `ELF 32-bit … Intel 80386`，entry `0x100020`；
`.multiboot` 位于 `0x00100000`、大小 `0x18`、对齐 8，hex 含 magic/arch/length/匹配
checksum；`objdump` 显示 `cli → mov esp → xor ebp → call`；`xorriso` 显示
El Torito BIOS boot image 与两个 ISO 文件。全部检查只读，未改动任何产物。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `grub-file` 或 `grub-mkrescue` 找不到 | 缺少 GRUB 工具 | 安装 `grub-pc-bin grub-common xorriso mtools` |
| `grub-file --is-x86-multiboot2` 失败 | header 未对齐/未被保留/不在前 32 KiB | 查 `linker.ld` 的 `ALIGN(8)`、`KEEP()`；`readelf -x .multiboot` 对照规范 |
| 误以为 architecture=0 表示只能是 32 位 | 把交接架构当成最终架构 | 复习本课 2.3 节；long mode 是 Lesson 08 的独立里程碑 |
| 误以为 `.code32` 切换保护模式 | 混淆汇编器指令与 CPU 状态 | `objdump` 反汇编 `_start`，找不到任何模式切换指令 |
| 用 `-kernel kernel.elf` 启动异常 | 绕过 GRUB/Multiboot2 路径 | 改用 `-cdrom build/kernel.iso` |
| 终端没文字以为失败 | 输出在 VGA 不在串口 | 看 QEMU 图形窗口，不加 `-display none` |
| 想改 `lesson-01-stable` 做实验 | 稳定目录是冻结证据 | 复制到自己的学习版再改 |

---

## 7. 与 Linux 源码对照

Linux v6.12 的相关启动代码是工程对照：

- `arch/x86/boot/header.S`：x86 启动镜像的早期布局与保护——TinyOS 的
  `linker.ld` 对镜像布局的显式控制与之同理；
- `arch/x86/kernel/head_64.S:119`：早期入口先 `cli`、建立可控环境的受控启动原则
  ——TinyOS 第一课 `_start` 的 `cli` 与之对应；
- `arch/x86/kernel/vmlinux.lds.S`：内核显式控制段布局——TinyOS 的 `linker.ld` 思想一致。

**注意边界**：Linux 的启动协议（Linux boot protocol）不是 Multiboot2；
不能用 Linux 源码解释 BIOS/GRUB/Multiboot2 的规范事实。

---

## 8. 思考题与练习

1. 用一句话分别解释：SeaBIOS、GRUB、Multiboot2、TinyOS 各负责启动链中的哪一段？
2. 打开 `lesson-01-stable/boot.S`，指出哪个指令关中断、哪个建栈、哪个进 C；
   确认没有任何一条指令在切换保护模式。
3. 运行 `readelf -x .multiboot` 并对照规范字段表，把每个字节翻译成字段名。
4. `checksum = -(magic + architecture + length)`：验证该算法使四字段和为 0
   （用十六进制手动加一遍）。
5. 运行 `make -C lessons/lesson-01-stable run`，写下你看到的三个证据
   （画面、header 校验、ISO 记录）分别对应启动链的哪一步。

---

## 9. 本课小结与下一课预告

**小结**：本课建立了启动链的分层模型——CPU（Intel SDM）、固件（SeaBIOS）、
bootloader（GRUB）、协议（Multiboot2）、内核（TinyOS）各司其职；理解了
`.code32` 不切模式、保护模式≠long mode、Multiboot2 header 的校验机制、
El Torito ISO 的结构，并用工具从二进制中读出全部证据。

**下一课预告**：进入 [`lesson-01-stable`](../lesson-01-stable/README.md)，
不再只观察，而是写出第一段 TinyOS 内核代码：Multiboot2 header + 32 位入口 +
直接写 VGA `0xb8000`，在 QEMU 图形窗口显示第一句 Hello。

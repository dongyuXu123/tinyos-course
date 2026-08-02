# 第 0 课：从加电复位到 GRUB 的 Multiboot2 保护模式交接

> **课程状态：学习版（可编辑，尚未归档）**  
> 本课性质：启动链观察与源码阅读课；不新增内核、ISO、bootloader 或构建脚本。  
> 本课对象：已冻结的 `../lesson-01-stable/`；下一课才开始编写并运行第一个 VGA Hello 内核。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：在写第一行 TinyOS 内核代码前，准确划清 CPU、QEMU/SeaBIOS、GRUB、Multiboot2 和 TinyOS 的责任，理解为什么第一课的 `_start` 一开始就是 32 位保护模式代码。

本课程实际使用的启动链是：

```text
CPU 加电复位
  → QEMU 默认 SeaBIOS（传统 BIOS）
  → El Torito BIOS 启动映像中的 GRUB
  → GRUB 读取 ISO 内 grub.cfg
  → multiboot2 /boot/kernel.elf
  → GRUB 校验并装载 Multiboot2 ELF
  → 按 Multiboot2 i386 交接状态跳转 _start
  → TinyOS 的 32 位保护模式入口
```

### 源码锚点铁律：按事实选择正确的权威来源

| 要解释的事实 | 权威依据 | 本课中的边界 |
|---|---|---|
| CPU reset、实模式、保护模式、控制寄存器和 long mode | **Intel® 64 and IA-32 Architectures Software Developer’s Manual, Vol. 3** | Intel 定义 CPU 状态；Linux、GRUB 和 QEMU 都不是 x86 CPU 架构的规范。 |
| 本实验的 BIOS 固件与光盘启动环境 | **QEMU + SeaBIOS**；本机 `/usr/share/seabios/bios.bin` | 这是本课程 QEMU 默认传统 BIOS 路径，不代表所有 PC 固件。 |
| `grub-mkrescue`、`grub-file`、`multiboot2` 命令与 loader 行为 | **GNU GRUB 官方文档/源码** | GRUB 负责读取 ISO、配置和内核；TinyOS 不实现磁盘、ISO 文件系统或 ELF loader。 |
| header 格式、header 搜索约束、i386 交接状态和 information structure | **Multiboot2 官方规范** | Multiboot2 是 loader 与内核的协议，不是 Linux x86 boot protocol。 |
| TinyOS 接手后的启动工程设计 | **Linux v6.12** `arch/x86/boot/header.S`、`arch/x86/kernel/head_64.S:119`、`arch/x86/kernel/vmlinux.lds.S` | Linux 源码是内核工程的对照与学习锚点，不定义 BIOS、GRUB 或 Multiboot2。 |

这张表是课程“每一行教学代码有源可依”原则的前置规则：**不能把 BIOS/GRUB 的事实错误归给 Linux，也不能把 Linux 的启动协议误称为 Multiboot2。**

## 第二部分：核心设计解剖（Design Anatomy）

### 1. 从按下电源到 TinyOS 的完整责任链

```text
(1) CPU reset
    CPU 从架构规定的复位状态开始执行；此时 TinyOS、GRUB 都尚未运行。

(2) QEMU 默认 SeaBIOS
    初始化模拟硬件，按启动顺序选择 CD-ROM，并走传统 BIOS 的 El Torito 光盘启动路径。

(3) GRUB
    GRUB 是独立 bootloader：读取 ISO 文件系统、读取 /boot/grub/grub.cfg、加载模块和内核。

(4) Multiboot2
    GRUB 执行：multiboot2 /boot/kernel.elf
    它验证内核是否包含可识别的 Multiboot2 header，并装载 ELF 的可装载段。

(5) TinyOS _start
    GRUB 依据 Multiboot2 i386 交接约定，将控制权交给 ELF entry point。
    现在才轮到我们自己的 boot.S 执行。
```

当前 Lesson 01 的 ISO 具有 BIOS El Torito 启动项；ISO 内有 `/boot/kernel.elf` 与 `/boot/grub/grub.cfg`。其中的配置为：

```cfg
menuentry "TinyOS lesson 1" {
    multiboot2 /boot/kernel.elf
    boot
}
```

`multiboot2` 的含义不是“BIOS 直接跳到 `_start`”。在跳转前，GRUB 负责识别 Multiboot2 header、装载 ELF，并准备规范定义的 i386 内核交接环境。

### 2. 何时进入保护模式？

答案是：**在当前课程的路径里，GRUB 在跳转到 TinyOS 前已经完成所需的 32 位保护模式交接；不是 TinyOS 的 `_start` 完成的。**

```text
CPU / BIOS 早期阶段           GRUB 的职责             TinyOS 的职责
────────────────────          ────────────            ──────────────────
复位、传统固件路径     →      装载 Multiboot2 ELF →    _start 接手
实模式相关初态                准备 i386 交接状态       已在 32 位保护模式
                                                       cli、临时栈、C 调用
```

第一课 `boot.S` 的关键片段：

```asm
.section .text
.code32
_start:
    cli
    movl $stack_top, %esp
    xorl %ebp, %ebp
    call kernel_main32
```

`.code32` 是汇编器按 32 位指令规则编码的声明；它不是执行“实模式 → 保护模式”切换的指令。这里的 `_start` 做的是：

1. `cli`：保持 GRUB 交接后的早期环境受控；可对照 Linux v6.12 `arch/x86/kernel/head_64.S:119` 的早期受控入口原则。
2. 建立 TinyOS 自己的 16 KiB 临时栈；不依赖 bootloader 的栈状态。
3. 调用 `kernel_main32()`，开始第一课的 VGA 输出。

### 3. 保护模式不是 long mode

```text
实模式（早期固件相关）
   │
   ├── GRUB 完成 Multiboot2 i386 交接
   ▼
32 位保护模式（当前 Lesson 01–03）
   │
   ├── TinyOS 目前在这里运行 VGA 与 PS/2 轮询代码
   ▼
64 位 long mode（后续独立课程）
   ├── TinyOS 自己建立 GDT、页表
   ├── 设置 CR4.PAE、EFER.LME、CR0.PG
   └── 远跳转进入 64 位代码
```

因此，Lesson 01–03 的 `gcc -m32` 与 `ld -m elf_i386` 并不违背课程最终的 x86_64 目标：它们是刻意保留的、由 GRUB Multiboot2 i386 交接进入的可见早期阶段。long mode 必须在后续用单独的小课完成和验证，不能隐藏在 Hello 前面。

## 第三部分：增量代码交付（Incremental Code Delivery）

第 0 课**没有新增内核代码**，也没有伪造 `bootloader.S`。其增量是把下一课的启动对象拆开观察：

```text
lesson-00-learning/
└── README.md                 # 本启动链教案；唯一新文件

lesson-01-stable/             # 已冻结、只读的观察对象
├── Makefile                  # freestanding ELF、ISO、check、run
├── boot.S                    # Multiboot2 header、.code32 _start、临时栈
├── linker.ld                 # 1 MiB 布局，保留并对齐 .multiboot
├── grub.cfg                  # GRUB 的 multiboot2 /boot/kernel.elf
├── kernel.c                  # 第一段 TinyOS VGA 输出
└── build/                    # 生成物：kernel.elf、kernel.iso、ISO staging tree
```

### 必须逐项读懂的五个源文件

| 文件 | 谁使用它 | 本课观察重点 |
|---|---|---|
| `lesson-01-stable/grub.cfg` | GRUB | `multiboot2 /boot/kernel.elf` 表示 GRUB 使用 Multiboot2 loader。 |
| `lesson-01-stable/boot.S` | GNU as / CPU | header 与 `.code32 _start`；它接收交接，不切换保护模式。 |
| `lesson-01-stable/linker.ld` | `ld` | `.multiboot ALIGN(8)` 与 `KEEP()`；让 GRUB 可在镜像前 32 KiB 内找到 header。 |
| `lesson-01-stable/Makefile` | host build tools | `gcc -m32`、`ld -m elf_i386`、`grub-mkrescue`、`grub-file` 和 QEMU 图形启动路径。 |
| `lesson-01-stable/kernel.c` | TinyOS | 从 `0xb8000` 开始写 VGA cell；这是 GRUB 已完成交接后才执行的第一个可见内核行为。 |

`build/` 是生成物而不是编辑源；第 0 课不会覆盖或删除任何 Lesson 01 稳定产物。

## 第四部分：编译与运行验证（Verification）

第 0 课本身没有内核可编译。它的验证是对**既有冻结 Lesson 01 产物**做只读检查。先定义路径：

```bash
K="$(pwd)/../lesson-01-stable/build/kernel.elf"
ISO="$(pwd)/../lesson-01-stable/build/kernel.iso"
```

在 `lesson-00-learning/` 目录执行：

```bash
grub-file --is-x86-multiboot2 "$K"
file "$K" "$ISO"
readelf -h -l -S -W "$K"
readelf -x .multiboot "$K"
objdump -d -Mintel --disassemble=_start "$K"
xorriso -indev "$ISO" -report_el_torito plain
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --
```

应观察到：

```text
kernel.elf: ELF 32-bit ... Intel 80386
Entry point: 0x100020
.multiboot: address 0x00100000, alignment 8, size 0x18
_start:
  cli
  mov esp, ...
  xor ebp, ebp
  call kernel_main32
ISO: El Torito BIOS boot image
ISO files: /boot/kernel.elf and /boot/grub/grub.cfg
```

`.multiboot` 的 little-endian hex 首个 word 是 `d65052e8`，按 32 位小端解释即 Multiboot2 magic `0xe85250d6`；第二个 word `00000000` 是 i386 architecture 字段。它描述的是**GRUB 与当前早期内核的交接架构**，不是“课程最终不再是 x86_64”。

### 观察第一个真实运行结果

第 0 课不启动自己不存在的内核。要观察完整路径，运行已冻结的 Lesson 01：

```bash
make -C ../lesson-01-stable run
```

不要添加 `-display none`：第一课的验收来自 **QEMU 图形窗口**的 VGA，而不是串口。应看见：

```text
TinyOS lesson 1
Hello from the VGA text console!
Multiboot2 boot succeeded.
```

Lesson 01 的稳定教案已记录：构建、`grub-file --is-x86-multiboot2` 和 QEMU VGA `screendump` 已在 2026-07-31 通过。第 0 课将这些现有产物作为可重复观察的证据，而不修改它们。

> **本次实际只读验证记录（2026-08-01）**：`grub-file --is-x86-multiboot2` 对 Lesson 01 的冻结 `kernel.elf` 返回成功；`file` 识别其为 `ELF 32-bit ... Intel 80386`，entry 为 `0x100020`。`readelf` 显示 `.multiboot` 位于 `0x00100000`、大小 `0x18`、对齐 `8`；hex 中包含 magic `0xe85250d6`、architecture `0`、length `0x18` 与匹配 checksum。`objdump` 显示 `_start` 为 `cli → mov esp,0x105000 → xor ebp,ebp → call kernel_main32`。`xorriso` 显示 ISO 有 El Torito BIOS boot image，且有 `/boot/kernel.elf` 和 `/boot/grub/grub.cfg`。所有检查仅读取既有稳定版。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 困惑或现象 | 首先对照的来源 | 正确检查或结论 |
|---|---|---|
| `grub-file` 或 `grub-mkrescue` 找不到 | GNU GRUB 工具文档 | 安装 `grub-pc-bin`、`grub2-common`、`xorriso`、`mtools`；第 0 课只读验证还需要 `binutils`。 |
| `grub-file --is-x86-multiboot2` 失败 | Multiboot2 规范、`linker.ld` | 检查 magic、checksum、8 字节对齐、`.multiboot` 是否由 `KEEP()` 保留且处于镜像前 32 KiB。 |
| 误以为 header 的 `architecture = 0` 表示 TinyOS 永远只能是 32 位 | Multiboot2 i386 交接定义 | 它说明当前 GRUB → 早期内核交接为 i386；课程随后仍会由 TinyOS 进入 x86_64 long mode。 |
| 误以为 `.code32` 自动切换保护模式 | Intel SDM、Multiboot2 machine-state 章节 | `.code32` 只影响汇编编码；当前路径中 GRUB 已完成规定的 32 位交接，`_start` 只是接手。 |
| 把保护模式和 long mode 混为一谈 | Intel SDM Vol. 3、Linux `head_64.S` | 当前 Lesson 01–03 是 32 位保护模式；long mode 还需要 GDT、页表及 CR4/EFER/CR0 的有序转换。 |
| 用 QEMU `-kernel kernel.elf` 代替 ISO 后启动异常 | GRUB / Multiboot2 loader 边界 | 本课程验证的是 GRUB `multiboot2` 路径；使用 `-cdrom build/kernel.iso`，不绕过 GRUB。 |
| 终端没有文本，以为启动失败 | Lesson 01 `kernel.c` 与 Makefile | 内核写 VGA `0xb8000`，不是串口；查看 QEMU 图形窗口，且不要加 `-display none`。 |
| 想修改 `lesson-01-stable` 做实验 | 双存档铁律 | 稳定目录是冻结证据，只读观察；复制到学习版后才可修改。 |
| 误以为 SeaBIOS、GRUB 或 Linux 是同一个层 | 本课第一部分责任表 | SeaBIOS 是本实验固件，GRUB 是 bootloader，Multiboot2 是协议，Linux 是内核工程参考。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 打开 `../lesson-01-stable/grub.cfg`，用自己的话解释 `multiboot2 /boot/kernel.elf` 与 `boot` 各自要求 GRUB 做什么。
2. 打开 `../lesson-01-stable/boot.S` 和 `linker.ld`，将 `.multiboot` 的 8 字节对齐、`KEEP()` 和 `readelf -x .multiboot` 的实际字节逐项对应起来。
3. 用 `objdump -d -Mintel --disassemble=_start` 对照 `boot.S`：指出哪条指令关闭中断、哪条建立 `%esp`、哪条进入 C；确认没有一条在此课执行 real mode → protected mode 转换。
4. 阅读 Intel SDM Vol. 3 的 reset / protected-mode 章节，以及 Multiboot2 规范的 header 与 machine-state 章节；写出“谁定义 CPU 状态”和“谁定义 loader-to-kernel 协议”的区别。
5. 阅读 Linux v6.12 `arch/x86/kernel/head_64.S:119` 与 `arch/x86/kernel/vmlinux.lds.S`：分别找出 Linux 如何表达受控入口与镜像布局；不要将 Linux 的 Linux boot protocol 误用为本课程的 GRUB Multiboot2 协议。
6. 下一课进入 `lesson-01-learning/`：不再只观察启动链，而是亲自让 GRUB 交接后的 TinyOS 直接写 `0xb8000`，显示第一句 VGA Hello。

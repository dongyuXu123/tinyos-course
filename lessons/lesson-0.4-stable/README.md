# Lesson 0.4: GRUB ELF 装载器路径 — 精讲文档

> **课号**：Lesson 0.4（GRUB 源码研读支线第 4 课，文档观察课，不生成内核）
> **主题**：`multiboot2` 命令把 `/boot/kernel.elf` 的 ELF 段按 `PT_LOAD` 装进内存的完整路径
> **课程主线位置**：第 1 阶段支线；0.3 讲完「打开文件」，本课讲「读进内存、记下入口点」
> **前置课程**：[`lesson-0.3-stable/README.md`](../lesson-0.3-stable/README.md)
> （设备、文件系统与路径解析）
> **后续课程**：[`lesson-0.5-stable/README.md`](../lesson-0.5-stable/README.md)
> （Multiboot2 header 校验与 ABI）
> **一句话目标**：看懂 `readelf -l` 的每一列，讲出 GRUB 装载 ELF 时「段→内存→入口点」三步。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能从 `kernel.elf` 的程序头表读出「GRUB 会往哪些地址装什么」，
并描述 `grub_multiboot_load_elf` 的算法步骤。

- **在课程主线中的位置**：0.3 给文件句柄，本课把它消费掉：解析 ELF 头与程序头、按
  `PT_LOAD` 读入内存、`mem` 大于 `file` 的部分清零（`.bss`）。完成装载后，0.5 课讨论的
  header 校验与 0.6 课的 MBI 才有意义。
- **前置知识清单**：
  1. ELF 基本概念：ELF 头、节（section）、程序头（segment/program header）；
  2. [`lesson-0.3-stable`](../lesson-0.3-stable/README.md) 的五层文件抽象（本课读文件用的就是 `grub_file`）；
  3. 认得 TinyOS `linker.ld` 的段布局与 `readelf` 输出。
- **本课交付**：`readelf -l` 逐列精讲 + GRUB ELF 装载算法步骤 + 与 `linker.ld` 的对照表。

---

## 2. 核心概念精讲

### 2.1 概念一：ELF 的「两个视角」

ELF 同时有两套视角：**节（section）** 是编译/链接期视角（`.text`、`.data`、`.bss`），
**段（segment/program header）** 是加载期视角。GRUB 只关心程序头表（`readelf -l`），
因为它描述「运行时要装哪些块、装到哪」。节表对运行时不是必需的。

### 2.2 概念二：PT_LOAD 段与装载语义

程序头类型 `PT_LOAD` 表示「这段要载入内存」。关键字段：

| 字段 | 含义 | TinyOS 实测值 |
|---|---|---|
| `p_offset` | 段内容在文件中的偏移 | 0x1000 |
| `p_vaddr` / `p_paddr` | 虚拟/物理装载地址 | 0x00100000 |
| `p_filesz` | 从文件读入的字节数 | 0x162 |
| `p_memsz` | 内存中占用的字节数 | 0x4002（`.bss` 段） |
| `p_flags` | 权限 R/W/X | R E 或 RW |

**关键规则**：若 `p_memsz > p_filesz`，多出的部分必须清零——这就是 `.bss` 的实现方式
（`linker.ld` 把 `.bss` 单独放段，`p_filesz=0`、`p_memsz=0x4002`）。装载器按
Multiboot2 约定的物理地址语义把内容读到 `p_paddr`。

### 2.3 概念三：GRUB 的 ELF 装载三步

`multiboot2` 命令的装载登记流程（GRUB 2.14，`grub-core/loader/` 目录，符号以 grep 为准）：

```text
loader/multiboot.c（命令处理器）
  → 打开文件（0.3 的五层抽象）
  → 判定 header 类型（ELF？Multiboot2 header？）      ← 0.5 课主题
  → loader/multiboot_mbi2.c：grub_multiboot_load
      → 读 ELF 头（grub-core/kern/elf.c：grub_elf32_open）
      → 读程序头表（grub_elf32_phdrs）
      → 对每个 PT_LOAD：分配内存 → 按 p_offset/p_filesz 读入 → mem>file 清零
      → 记录 entry point
  → grub_loader_set(load, boot, ...)：登记「启动时跳哪」
```

`boot` 命令触发 `grub_loader_boot()` 时，GRUB 生成 MBI（0.6 课）并跳到
entry point（本课是 `0x100020`）。

### 2.4 概念四：为什么 TinyOS 只有两个 LOAD 段

`linker.ld` 用 `. = ALIGN(CONSTANT(MAXPAGESIZE))` 把可写段推到新页边界，从而让链接器生成
**两个** LOAD 段：一个只读可执行（`.multiboot`+`.text`+`.rodata`），一个可写（`.bss`）。
这样 GRUB 只需要两次读入 + 一次清零，且不会出现危险的 RWX 段。这是「链接脚本 → ELF 段 →
装载行为」三者因果链的核心。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（kern/elf.c 与 loader/multiboot_*）

```bash
cd "$GRUB_SRC"
grep -R "grub_multiboot_load_elf" grub-core/loader | head -10
grep -R "grub_elf32_phdrs\|grub_elf32_open" grub-core/kern/elf.c | head -10
grep -R "PT_LOAD" grub-core/loader grub-core/kern | head -10
grep -R "grub_loader_set" grub-core/loader grub-core/kern | head -10
```

**预期输出解读**：第一条定位 32 位 ELF 装载函数（`loader/multiboot_elfxx.c` 模板化实现）；
第二条是通用 ELF 解析（`grub-core/kern/elf.c`）；第三条找到装载循环里对 `PT_LOAD` 的判断；
第四条是 loader 框架的登记点。命名在发行版间可能略异，以 grep 结果为准。

### 3.2 观察一：ELF 头——入口点 0x100020

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
readelf -h -W "$K"
```

实测（节选）：

```text
类别: ELF32; 系统架构: Intel 80386; 入口点地址: 0x100020
程序头起点: 52 (bytes into file); Number of program headers: 3
```

**解读**：入口点就是 GRUB 最终跳转的地址。`0x100020 = 1 MiB + 0x20`，`.multiboot` 占 0x18
字节，按 `.text` 的 `ALIGN(16)` 后 `_start` 落在 `0x100020`。GRUB 只信 ELF 头里的
`e_entry`，不信符号表。

### 3.3 观察二：程序头表——GRUB 装载的「施工图」

```bash
readelf -l -W "$K"
```

实测（完整）：

```text
LOAD      0x001000 0x00100000 0x00100000 0x00162 0x00162 R E 0x1000
LOAD      0x001000 0x00101000 0x00101000 0x00000 0x04002 RW  0x1000
GNU_STACK 0x000000 0x00000000 0x00000000 0x00000 0x00000 RW  0x10
```

逐列解读：

| 列 | 第 1 个 LOAD | 第 2 个 LOAD | 装载器行为 |
|---|---|---|---|
| Offset | 0x1000 | 0x1000 | 从文件偏移 0x1000 起读 |
| VirtAddr/PhysAddr | 0x100000 | 0x101000 | 装到物理 1 MiB / 1 MiB+0x1000 |
| FileSiz | 0x162 | 0 | 从文件读 354 字节 |
| MemSiz | 0x162 | 0x4002 | 内存占用（`.bss` 16 KiB+2） |
| Flg | R E | RW | 只读可执行 / 可写 |
| Align | 0x1000 | 0x1000 | 4 KiB 页对齐 |

**关键观察**：第二个 LOAD 的 `FileSiz=0`、`MemSiz=0x4002`——GRUB 会为它分配 16 KiB+2 字节
内存并**全部清零**，这就是 boot.S 里的 `stack_bottom: .skip 16384` 所在的 `.bss`。
Section→Segment 映射：段 00 = `.multiboot .text .rodata`；段 01 = `.bss`。

### 3.4 观察三：反汇编入口——装载完成后的第一跳

```bash
objdump -d -Mintel --disassemble=_start "$K"
```

实测：

```text
00100020 <_start>:
  100020:  fa                 cli
  100021:  bc 00 50 10 00     mov    esp,0x105000
  100026:  31 ed              xor    ebp,ebp
  100028:  e8 af 00 00 00     call   1000dc <kernel_main32>
```

**解读**：GRUB 跳进 `0x100020` 后第一条指令就是 `cli`。`esp=0x105000` 是 `0x101000 + 0x4002`
即 `.bss` 顶——证明栈正是被清零后的 `.bss` 区域。装载器把内存准备好之后，这一小段汇编
（16 字节）就是 TinyOS 的接管起点。

### 3.5 观察四：节表对照（编译期视角）

```bash
readelf -S -W "$K"
```

实测（节选）：

```text
[ 1] .multiboot PROGBITS 00100000 001000 000018 00  A  0   0  8
[ 2] .text      PROGBITS 00100020 001020 0000e9 00  AX  0   0  1
[ 3] .rodata    PROGBITS 00100110 001110 000052 00  A  0   0  4
[ 4] .bss       NOBITS   00101000 002000 004002 00  WA  0   0 16
```

**解读**：`.bss` 是 `NOBITS`（文件里不占空间），这正是 `FileSiz=0` 的根源；`.multiboot` 在
文件偏移 0x1000（首 4 KiB 内）、对齐 8——为 0.5 课的 header 校验埋下伏笔。

---

## 4. 数据流与运行逻辑

```text
multiboot2 /boot/kernel.elf
  → grub_file_open（0.3：五层抽象拿到 5352 字节流）
  → 读 ELF 头：EI_CLASS=32、e_entry=0x100020
  → 读程序头表（3 项）
  → LOAD#1: 文件偏移 0x1000 起 0x162 字节 → 物理 0x100000（R E）
  → LOAD#2: 分配 0x4002 字节 → 物理 0x101000（RW），全清零（.bss/栈）
  → grub_loader_set 登记 boot 函数与 entry 0x100020
boot
  → grub_loader_boot()：生成 MBI → EAX=0x36d76289, EBX=MBI 地址
  → jmp 0x100020 → cli → 建栈 → kernel_main32
```

至此，「GRUB 装了什么」与「TinyOS 用什么」在内存布局上严丝合缝：1 MiB 起是只读代码，
1 MiB+0x1000 起是清零后的栈与 BSS。

---

## 5. 观察与验证

### 5.1 依赖

`binutils`（readelf/objdump）；源码阅读需 `$GRUB_SRC`。

### 5.2 复现命令清单

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
readelf -h -W "$K"          # 入口 0x100020；3 个程序头
readelf -l -W "$K"          # 两个 LOAD：R E 0x100000 / RW 0x101000
readelf -S -W "$K"          # .bss 是 NOBITS（MemSiz>FileSiz 的原因）
objdump -d -Mintel --disassemble=_start "$K"   # cli→mov esp→call
grub-file --is-x86-multiboot2 "$K"; echo $?    # 0（header 校验，0.5 课详讲）
```

### 5.3 实测记录（2026-08-06，全部只读）

`e_entry=0x100020`；两个 LOAD 段地址 0x100000/0x101000、对齐 0x1000、权限 R E / RW；
LOAD#2 `FileSiz=0` `MemSiz=0x4002`（纯 BSS）；`_start` 反汇编为
`cli; mov esp,0x105000; xor ebp,ebp; call kernel_main32`。

### 5.4 安全边界（本课红线）

只读 ELF；不执行 `strip`/`objcopy` 等写文件工具；`readelf`/`objdump` 仅查询；个人副本实验
不碰 stable 产物。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `readelf -l` 出现 RWX 段 | 链接脚本未分开可写段 | 检查 `linker.ld` 的 `ALIGN(MAXPAGESIZE)` 与 `.data/.bss` 放置 |
| 入口不是 `_start` | `ENTRY(_start)` 缺失或符号改名 | `readelf -h` 对照 `boot.S` 的 `.globl _start` |
| 启动后栈损坏 | `.bss` 栈与内核数据重叠 | 核对两个 LOAD 段地址不重叠；`MemSiz` 计算 |
| 段 `FileSiz` 与 `MemSiz` 不符预期 | 未用 `-m elf_i386` 或 PIE | 用 `file`/`readelf -h` 确认 EXEC 非 DYN |
| GRUB 报 `invalid ELF header` | 文件被截断或非 ELF | `file kernel.elf`；重新构建 |
| 想在个人副本改布局 | 稳定产物冻结 | 复制 `lesson-01-stable` 后再改 `linker.ld` |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/fs/binfmt_elf.c`：Linux 用户态 ELF 装载器同样遍历 `PT_LOAD`、处理
  `memsz>filesz` 清零；GRUB 对内核的装载是同一算法的极简版；
- `linux-v6.12/arch/x86/kernel/vmlinux.lds.S`：Linux 与 TinyOS 一样用链接脚本控制段布局，
  决定装载器看到的 LOAD 段集合。

**边界提醒**：Linux `binfmt_elf` 走 VMA/页表，GRUB 装载 Multiboot2 内核时还在「无分页」的
早期环境，直接按物理地址写入；不能把两者机制混为一谈。

---

## 8. 思考题与练习

1. 概念理解：`p_memsz > p_filesz` 时装载器必须清零，请用 `.bss` 的 `stack_bottom` 说明
   这一规则的用途。
2. 源码定位：在 `$GRUB_SRC` 的 `loader/multiboot_elfxx.c` 中找到 `PT_LOAD` 处理分支，
   记录它如何用 `p_paddr` 决定装载地址。
3. 动手观察：把 `readelf -l` 的两列 `FileSiz`/`MemSiz` 填进 2.2 的字段表，手工算一遍
   `.bss` 清零点。
4. 实验（个人副本）：在 `linker.ld` 里删掉 `ALIGN(CONSTANT(MAXPAGESIZE))`，重新构建并
   观察 `readelf -l` 是否出现 RWX 段。
5. Linux 对照：读 `linux-v6.12/fs/binfmt_elf.c`，找出与 GRUB「清零 BSS」对应的代码。

---

## 9. 本课小结与下一课预告

**小结**：GRUB 只看 ELF 程序头表装载：对每个 `PT_LOAD` 按 `p_offset/p_filesz` 读入内存、
按 `p_paddr` 定位、`memsz>filesz` 清零；`e_entry=0x100020` 是最终跳转目标；TinyOS 的两个
LOAD 段（R E / RW）与 `linker.ld` 的布局决策一一对应。

**下一课预告**：进入 [`lesson-0.5-stable`](../lesson-0.5-stable/README.md)，在装载之前，
GRUB 必须先回答「这是不是合法的 Multiboot2 内核」——精讲 magic、architecture、length、
checksum 四字段与 8 字节对齐、前 32768 字节搜索范围。

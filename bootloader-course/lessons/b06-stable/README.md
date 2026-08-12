# Lesson B06: ELF32 解析 — 精讲文档

> **课号**：Lesson B06（Mini-GRUB 从零写 GRUB 课程第 6 课，可执行课）
> **主题**：ELF32 结构解析：ELF header 校验、program headers、PT_LOAD 段
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」第 1 课
> **前置课程**：[`b05-stable/README.md`](../b05-stable/README.md)（实模式回调磁盘读）；
> 研读支线 0.4（ELF 装载器路径）
> **后续课程**：[`b07-stable/README.md`](../b07-stable/README.md)（Multiboot2 header 校验）
> **一句话目标**：用 `disk_read_lba` 读入自写测试内核，解析其 ELF32 结构并打印
> `e_entry` 与每个 PT_LOAD 段——为 B08 的装载做解析层。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能识别一个 ELF32 文件是否合法、列出所有
PT_LOAD 段（地址/文件大小/内存大小），解析结果与 `readelf -l` 逐项一致。

- **在课程中的位置**：B05 让 loader 能读扇区；本课把这些字节**解读为 ELF**。
  内核是 ELF 文件，loader 必须能从 ELF header 找到程序头表、从程序头找到可装载
  段。对照 GRUB `grub-core/kern/elf.c`（header 校验）与 `elfXX.c`
  （`grub_elfXX_load_phdrs`、`FOR_ELFXX_PHDRS`）。
- **前置知识清单**：
  1. B05：`disk_read_lba` 读盘接口；
  2. ELF 规范：`Elf32_Ehdr` 的 `e_ident/e_machine/e_entry/e_phoff/e_phentsize/
     e_phnum`；`Elf32_Phdr` 的 `p_type/p_paddr/p_filesz/p_memsz`；
  3. 研读支线 0.4（ELF 两种视图：段是装载期概念，节是编译期概念）。
- **本课交付**：`build/b06.img`（软盘 LBA 9 起放 `test-kernel.elf`）；QEMU 上
  loader 打印 `entry=00100018 phnum=02` 与两个 LOAD 段，与 `readelf` 一致。

---

## 2. 核心概念精讲

### 2.1 概念一：ELF 的两种视图与装载视图

**定义**：ELF 文件有**节**（section）与**段**（segment，即 program header）两套
描述。节是编译/链接期的产物；装载只关心段——`PT_LOAD` 段定义了"哪段文件字节、
放到哪个物理地址、内存里占多大"。

**为什么需要**：`readelf -l`（程序头）与 `readelf -S`（节头）看到的布局不同；
GRUB 运行时只读程序头表，不读节表（研读支线 0.4 的结论）。

**工作机制**（本课 test-kernel 的实测）：

```text
readelf -l:
  LOAD  0x100000 filesz=0x5d  memsz=0x5d   R E   ← .multiboot + .text
  LOAD  0x101000 filesz=0x4   memsz=0x404  RW    ← .data + .bss
```

第二段 `filesz=4 < memsz=0x404`：文件里只有 4 字节（`kernel_data`），内存里还要
多出 0x400 字节的零（`kernel_bss`）——这就是 `.bss` 的装载语义。

### 2.2 概念二：ELF header 校验矩阵

**定义**：解析前必须先确认"这是我们要的 ELF"：magic、class、字节序、版本、
machine。GRUB 的 `grub_elf_check_header` 逐项检查，任一失败即报错。

**为什么需要**：loader 面对的是磁盘上任意字节；不校验就解析会越界或误解字段。
fail-closed 是引导代码的基本纪律。

**工作机制**（本课 `elf_parse` 的校验顺序）：

```c
    if (eh->e_ident[EI_MAG0] != ELFMAG0 || ... )
        return -1;                          /* not ELF */
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -2;                          /* 只支持 32 位 */
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return -3;                          /* 只支持小端 */
    if (eh->e_ident[EI_VERSION] != EV_CURRENT)
        return -4;
    if (eh->e_machine != EM_386)
        return -5;                          /* 只支持 i386 */
    if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(struct elf32_phdr))
        return -6;                          /* 没有可用的程序头 */
    if (eh->e_phoff + (u32)eh->e_phnum * eh->e_phentsize > size)
        return -7;                          /* 程序头表越界 */
```

### 2.3 概念三：程序头表的定位与遍历

**定义**：程序头表在文件偏移 `e_phoff`，共 `e_phnum` 项，每项 `e_phentsize`
字节（32 位 ELF 为 56 字节 `Elf32_Phdr`）。遍历步长必须用 `e_phentsize` 而不是
`sizeof`——规范允许 e_phentsize 大于标准大小。

**为什么需要**：`e_phentsize` 是"记录长"；GRUB 的 `grub_elfXX_load_phdrs` 按
`e_phnum * e_phentsize` 一次性读入，`FOR_ELFXX_PHDRS` 按 `e_phentsize` 步进。

**工作机制**（本课遍历）：

```c
    ph = (const struct elf32_phdr *)((const u8 *)buf + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph->p_type == PT_LOAD) { /* 打印 p_paddr/filesz/memsz */ }
        ph = (const struct elf32_phdr *)((const u8 *)ph + eh->e_phentsize);
    }
```

### 2.4 概念四：软盘上放 ELF 文件本体（本课调试实证）

**定义**：loader 解析的是 **ELF 文件本身**（含 ELF header 与程序头表），不是
objcopy 出来的纯段内容。

**为什么需要**：本课最初用 `objcopy -O binary` 生成 `kernel.bin`，它把 ELF header
剥离了——loader 读到的是没有 magic 的裸字节，`-1`（not ELF）失败。修正：软盘
LBA 9 直接放 `test-kernel.elf`，只做 `truncate` 补齐扇区。

**工作机制**：

```makefile
$(BUILD)/kernel.bin: $(BUILD)/test-kernel.elf
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@
```

### 2.5 概念五：两个 PT_LOAD 段的制造（对照 TinyOS linker.ld）

**定义**：内核链接脚本里 `. = ALIGN(CONSTANT(MAXPAGESIZE))` 把 `.data/.bss`
推到新页，ld 据此生成独立的 RW LOAD 段，避免 RWX 混合段。

**为什么需要**：test-kernel 要演示 `filesz < memsz` 的装载语义（B08 用），并让
B06 打印两个段。TinyOS 主线 `linker.ld` 正是这个手法（研读支线 0.1 观察过：
"没有 RWX 段"）。

**工作机制**（test-kernel.ld）：

```ld
    . = 1M;
    .multiboot : { *(.multiboot) }
    .text      : { *(.text) }
    .rodata    : { *(.rodata) }
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data      : { *(.data) }
    .bss       : { *(.bss) }
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B05） |
|---|---|---|
| `stage1.S` | 512 字节引导扇区 | 未变化 |
| `stage2.S` | 切换 + BIOS 回调（B05 全部） | 未变化 |
| `loader.c` | VGA 库 + 磁盘读 + `elf_parse` | 新增 ELF 结构体与解析 |
| `test-kernel.S` | 最小 Multiboot2 测试内核（B06 解析/B08 装载） | 新增 |
| `test-kernel.ld` | 内核链接脚本（1 MiB 起、页对齐分 RW 段） | 新增 |
| `linker.ld` | stage2 链接脚本 | 未变化 |
| `Makefile` | 新增 test-kernel 构建与 LBA 9 写入 | 修改 |
| `build/b06.img` | 软盘镜像（LBA0=stage1, 1-8=stage2, 9-16=kernel） | 新增 |

### 3.2 `loader.c` 精讲

**ELF 常量与结构**（字段布局与规范一致，自然对齐无填充）：

```c
#define EI_MAG0  0
#define EI_CLASS 4
#define EI_DATA  5
#define EI_VERSION 6
#define ELFMAG0   0x7Fu
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define EM_386    3
#define PT_LOAD   1

struct elf32_ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

struct elf32_phdr {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
};
```

**`elf_parse(buf, size)`**：校验矩阵（见 2.2）→ 打印 `entry`/`phnum` → 遍历
PT_LOAD 打印段（见 2.3）。打印格式：

```text
B06 elf: entry=00100018 phnum=02
  LOAD paddr=00100000 filesz=0000005d memsz=0000005d
  LOAD paddr=00101000 filesz=00000004 memsz=00000404
```

**`loader_main`**：读 LBA 9 起的 8 扇区到 `KERNEL_BUF`（0x68000，GRUB scratch
区起点）→ `elf_parse` → 打印 `B06 done: ELF32 parse OK`。常量：

```c
#define KERNEL_BUF   0x00068000u   /* GRUB scratch 区起点，作内核暂存 */
#define KERNEL_LBA   9u            /* 软盘 LBA 9 起放 test-kernel.elf */
#define KERNEL_SECT  8u            /* 占 8 个扇区 */
```

### 3.3 `test-kernel.S` 精讲

Multiboot2 header（B07 校验对象）与 `_start`（B08 装载目标）：

```asm
.set MB2_HEADER_MAGIC,       0xe85250d6
.set MB2_ARCHITECTURE_I386,  0
.set MB2_HEADER_LENGTH,      mb2_end - mb2_start
.set MB2_CHECKSUM,           -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)

.section .multiboot, "a"
.align 8
mb2_start:
    .long MB2_HEADER_MAGIC
    .long MB2_ARCHITECTURE_I386
    .long MB2_HEADER_LENGTH
    .long MB2_CHECKSUM
    .short 0                     /* end tag: type 0 */
    .short 0
    .long 8                      /* end tag: size */
mb2_end:
```

`_start` 直接写 0xB8000 打印 `B08 test-kernel: hello from Multiboot2`（B08 才
运行）；`.data`/`.bss` 制造第二个 RW 段（见 2.5）。

### 3.4 构建管线与主控制流

```text
test-kernel.S --as--> test-kernel.o --ld -T test-kernel.ld--> test-kernel.elf
  --cp+truncate--> kernel.bin(8 扇区) --dd seek=9--> b06.img
stage1/stage2 与 B05 相同（LBA 0 / 1-8）

BIOS → stage1 → stage2 → pm32 → loader_main(C)
  → disk_read_lba(0, 9, 0x68000, 8) → elf_parse → 打印 → hlt
```

`make check` 新增：`test-kernel.elf` 为 ELF32/i386（`file`）、通过
`grub-file --is-x86-multiboot2`、`elf_parse` 符号存在。

---

## 4. 数据流与运行逻辑

```text
软盘 LBA 9..16（test-kernel.elf）--int 0x13--> 0x68000（KERNEL_BUF）
  → elf_parse: e_ident magic/class/data/version → e_machine → e_phnum
  → e_phoff 处程序头表 → 逐个 PT_LOAD：p_paddr/p_filesz/p_memsz
  → vga_hex 打印 → B06 done
```

自动化验证 marker：`B06 elf: entry=`（解析进行）、`B06 done`（成功收尾）。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b06-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b06 check
bootloader-course/scripts/validate-course.sh b06 qemu
```

### 5.2 期望输出

- `make check`：`B06 check PASS: stage1=512+55aa stage2=4096B kernel=ELF32+MB2`
- QEMU：见 3.2 的打印；与 `readelf -h -l` 逐项一致。

### 5.3 成功判据

解析结果与 `readelf -l` 完全一致（entry、每个 LOAD 的 paddr/filesz/memsz）；
`make check` 里 `grub-file` 通过；QEMU trace 无异常。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `B06 error: invalid ELF`（-1） | 软盘上不是 ELF 文件（如 objcopy 剥了头） | `od -An -tx1 -j0 -N16` 检查 LBA 9 首字节应为 `7f 45 4c 46` |
| `invalid ELF`（-2/-3/-4/-5） | class/字节序/版本/machine 不匹配 | `readelf -h` 对照 e_ident 与 e_machine；确认加载的是 ELF32 LSB i386 |
| `invalid ELF`（-6） | e_phnum 为 0 或 e_phentsize 异常 | `readelf -h` 看 e_phnum/e_phentsize |
| `invalid ELF`（-7） | 程序头表越界（缓冲区不够） | 核对 `KERNEL_SECT` 与文件实际大小；`e_phoff + phnum*phentsize <= size` |
| 打印的段与 readelf 不符 | 遍历步长用了 sizeof 而非 e_phentsize | 检查 `(const u8 *)ph + eh->e_phentsize` |
| 只打印一个段 | 链接脚本没分页，全合并成 RWE | 检查 `. = ALIGN(CONSTANT(MAXPAGESIZE))` 是否在 .data 前 |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/kern/elf.c` 的 `grub_elf_check_header`
（magic/version/class/endianness 校验）与 `grub-core/kern/elfXX.c` 的
`grub_elfXX_load_phdrs`（按 `e_phnum * e_phentsize` 读程序头表）、
`FOR_ELFXX_PHDRS`（按 `e_phentsize` 步进遍历）、`grub_elfXX_size`（PT_LOAD
地址区间与最大对齐计算，B08 装载前用）。对照点：

- **相同**：校验顺序（magic → class/data → version → machine）；程序头按
  `e_phentsize` 步进；只关心 PT_LOAD；
- **简化**：GRUB 用 `grub_file` 抽象按需 seek/read；本课整文件读入内存后原地
  解析；GRUB 支持 64 位与双字节序，本课只做 ELF32/LSB；
- **下一步**：B07 校验内核的 Multiboot2 header（GRUB `loader/multiboot.c`），
  B08 用 `grub_elfXX_size` 的区间逻辑做装载。

Linux 对照：`arch/x86/boot/header.S` 与内核 ELF 装载路径同源；仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么遍历程序头必须用 `e_phentsize` 而不是 `sizeof(Elf32_Phdr)`？
2. 动手实验（临时副本）：把 Makefile 的 `kernel.bin` 改回 `objcopy -O binary`，
   观察 `B06 error: invalid ELF`，解释原因。
3. 动手观察：`readelf -h -l -W build/test-kernel.elf`，对照 loader 打印的
   entry/两个 LOAD 段逐项核对。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "e_phentsize\|PT_LOAD" grub-core/kern/elfXX.c`，找出 GRUB 遍历
   与装载段的方式。
5. 综合：画出 test-kernel.elf 从文件字节到内存段的映射（哪个 LOAD 读多少字节、
   哪段要补零），与 B08 的装载步骤对接。

---

## 9. 本课小结与下一课预告

**小结**：本课让 loader 读懂了 ELF。关键收获：(1) ELF 有节/段两套视图，装载只看
程序头；(2) 解析前必须做 magic/class/字节序/version/machine 校验（fail-closed）；
(3) 程序头表按 `e_phoff/e_phentsize/e_phnum` 定位与遍历；(4) `filesz < memsz`
意味着装载时要补零（`.bss`）；(5) 软盘上要放 ELF 文件本体而非 objcopy 的裸段
内容；(6) 链接脚本页对齐可以制造干净的 RE/RW 两个段。解析输出与 `readelf`
逐项一致，证明 loader 对内核文件有了可靠认知。

**下一课预告**：进入 [`b07-stable/README.md`](../b07-stable/README.md)。内核是
ELF，但 loader 必须确认它**声明自己是 Multiboot2 客户**——B07 实现 header 搜索
（前 32 KiB、8 对齐）与 magic/arch/length/checksum 校验，对照 GRUB
`loader/multiboot.c`。

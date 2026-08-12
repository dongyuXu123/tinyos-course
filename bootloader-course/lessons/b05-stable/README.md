# Lesson B05: 实模式回调磁盘读 — 精讲文档

> **课号**：Lesson B05（Mini-GRUB 从零写 GRUB 课程第 5 课，可执行课）
> **主题**：保护模式下的 BIOS 调用：prot_to_real / real_to_prot + 寄存器快照
> **课程位置**：阶段一「实模式与保护模式引导链」第 5 课（阶段一收尾）
> **前置课程**：[`b04-stable/README.md`](../b04-stable/README.md)（保护模式 C 运行时）；
> 研读支线 0.7（GRUB startup.S 的切换代码）
> **后续课程**：[`b06-stable/README.md`](../b06-stable/README.md)（ELF32 解析）
> **一句话目标**：让保护模式下的 C 代码能调 BIOS 中断——临时切回实模式执行
> `int $0x13` 读盘，再切回保护模式。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 提供 `disk_read_lba(drive, lba, buf, count)`，
任何 C 代码都能在保护模式下读磁盘扇区——这是后续读 ELF、读文件系统的基础。

- **在课程中的位置**：B04 让 loader 用 C 输出，但没有任何输入能力。BIOS 中断
  （INT 13/15/10）只在实模式可用，而 loader 已在保护模式。GRUB 的答案是
  **实模式回调**：`prot_to_real`（32→16 位）→ 执行 INT → `real_to_prot`
  （16→32 位）。本课完整复刻这条路径，阶段一（引导链）就此闭环。
- **前置知识清单**：
  1. B03：GDT、CR0.PE、`ljmp` 刷新 CS、保护模式直接 VGA；
  2. B02：INT 13 CHS 读盘与 `ES:BX` 缓冲区约定；
  3. B04：汇编 ↔ C 符号链接、cdecl 调用约定。
- **本课交付**：`build/b05.img`；QEMU 上 C 代码读回 LBA0（引导扇区）与 LBA1
  （stage2 自身）的头部字节并校验 `55 aa` 签名，证明回调闭环成功。

---

## 2. 核心概念精讲

### 2.1 概念一：为什么保护模式调不了 BIOS 中断

**定义**：`int $0xNN` 通过 IDT 分发；BIOS 固件的中断服务程序运行在实模式环境里
（依赖中断向量表 0–0x400、BIOS 数据区与实模式段模型）。

**为什么需要**：loader 进入保护模式后没有 IDT（B03 起 `cli` 且从不建 IDT），
BIOS 中断服务不可达。但磁盘、键盘、显卡服务全在 BIOS 手里——必须能"临时回去"。

**工作机制**（GRUB 的答案）：准备一个**伪实模式**环境：16 位代码段（limit 64K）、
16 位数据段、实模式 IDT（0x400@0），清 CR0.PE 回到实模式执行 `int`，再切回来。

### 2.2 概念二：prot_to_real 与 real_to_prot 的对称结构

**定义**：两个函数互为逆过程，结构完全镜像 GRUB `realmode.S`：

```text
real_to_prot（16→32）：lgdt → CR0.PE=1 → ljmpl 32 位段 → 重载段寄存器
    → 返回地址搬到保护模式栈 → sidt 保存实模式 IDT / lidt 空 IDT → ret

prot_to_real（32→16）：lgdt → sidt 保存保护模式 IDT / lidt 实模式 IDT
    → 保存保护模式栈 → 返回地址放到 0x1FF0 → 切 16 位栈
    → 段寄存器装伪实模式段（0x20）→ ljmp 16 位段 → CR0.PE=0
    → ljmpl 到真实模式（CS=0）→ 段寄存器清零 → sti → retl
```

**为什么需要**：切换不是一条指令的事——CS 必须用远跳刷新，段寄存器必须重载，
栈要迁移，IDT 要换。GRUB 把这两段代码写成固定的对称对，任何 BIOS 调用都复用。

**工作机制**（关键细节，均照抄 GRUB）：

- **返回地址搬运**：`prot_to_real` 把保护模式栈顶的 32 位返回地址存到
  `REAL_STACK`（0x1FF0），并把 `ESP` 也设为 0x1FF0，最后 `retl`（32 位弹出）
  直接取回它——"栈即暂存"；
- **伪实模式段**：`ljmp $0x18` 切到 16 位代码段（limit 0xFFFF、字节粒度），
  DS/ES/FS/GS/SS 装 0x20（16 位数据段），保证清 CR0.PE 前后段缓存一致；
- **IDT 对**：`realidt = {limit 0x400, base 0}`、`protidt = {limit 0, base 0}`；
  实模式用真实 IVT，保护模式用空 IDT（配合 `cli` 保证安全）。

### 2.3 概念三：寄存器快照与 bios_interrupt 封装

**定义**：BIOS 调用需要传一堆寄存器，结果也在寄存器里。GRUB 用
`struct grub_bios_int_registers` 定义一张 32 字节的寄存器表，`grub_bios_interrupt`
负责"结构 ↔ 寄存器"搬运 + 模式切换。

**为什么需要**：C 没有寄存器概念；把"调用 INT"抽象成一个普通函数，C 侧填结构、
读结果，汇编侧处理全部切换细节。

**工作机制**（本课的 `bios_interrupt(u8 intno, struct bios_regs *regs)`）：

```text
C 填 regs（eax/es/ds/flags/ebx/ecx/edi/esi/edx）
  → 汇编拷到低内存快照 BIOS_SNAPSHOT(0x2000)
  → 自修改 int_stub 的中断号字节（同 GRUB int.S）
  → call prot_to_real → 实模式：从快照装载寄存器 → int $0xNN
  → 结果存回快照 → calll real_to_prot → 保护模式：快照拷回 regs
  → C 读 regs.flags 的 CF 判断成败
```

寄存器结构布局（与 GRUB int.S 的偏移一致）：

```c
struct bios_regs {
    u32 eax;   /* +0  */
    u16 es;    /* +4  */
    u16 ds;    /* +6  */
    u16 flags; /* +8  */
    u32 ebx;   /* +12 */
    u32 ecx;   /* +16 */
    u32 edi;   /* +20 */
    u32 esi;   /* +24 */
    u32 edx;   /* +28 */
};
```

### 2.4 概念四：两个真实的汇编陷阱（本课调试实证）

**陷阱一：EAX 的 moffs 编码与 `%cs:` 段前缀冲突**。16 位模式下
`movl %cs:0x2000, %eax` 会汇编成 `ff 2e 66 a1 00 20`（一条垃圾远跳），因为 EAX
的绝对地址装载用 A1 moffs 编码，与段前缀组合时 GNU as 产生错误编码。规避：
装载阶段 `DS=0`（`realcseg` 已清零），用 DS 相对寻址读快照；保存阶段用已验证
正确的 `%cs:` 写。

**陷阱二：`popfw` 是从栈弹，不是从 AX 弹**。恢复 FLAGS 必须
`popw %ax; pushw %ax; popfw` 三步（先把值放回栈顶），直接 `popw %ax; popfw`
会多弹一个栈槽，导致后续 DS/ES 错位（本课实测 DS 与 ES 互换）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B04） |
|---|---|---|
| `stage1.S` | 512 字节引导扇区（读 stage2） | 未变化 |
| `stage2.S` | 切换 + `prot_to_real`/`real_to_prot`/`bios_interrupt` | 全部重写（本课核心） |
| `loader.c` | VGA 库 + `struct bios_regs` + `disk_read_lba` | 新增磁盘读 |
| `linker.ld` | 0x7E00 起铺段 | 未变化 |
| `Makefile` | 新增 `bios_interrupt`/`prot_to_real`/`real_to_prot` 断言 | 修改 |
| `build/b05.img` | 软盘镜像 | 新增 |

### 3.2 `stage2.S` 精讲

**常量**（与 GRUB `memory_raw.h` 逐一对应）：

```asm
.set PROT_MODE_CSEG,    0x08     # 保护模式代码段选择子
.set PROT_MODE_DSEG,    0x10     # 保护模式数据段选择子
.set PSEUDO_REAL_CSEG,  0x18     # 伪实模式代码段选择子（16 位）
.set PSEUDO_REAL_DSEG,  0x20     # 伪实模式数据段选择子（16 位）
.set REAL_STACK,        0x1FF0   # 实模式栈 / 返回地址暂存（= 0x2000 - 0x10）
.set PROT_STACK,        0x70FF0  # 保护模式栈顶（= 0x68000+0x9000+0xf000-0x10）
.set BIOS_SNAPSHOT,     0x2000   # 寄存器快照（32 字节）
.set REGS_PTR,          0x2020   # 寄存器结构指针暂存（4 字节）
```

**GDT**（5 项，与 GRUB `realmode.S` 完全一致；注意 B03 的 code16 是
`0x9A/0x0F`，本课改为 GRUB 的伪实模式 `0x9E/0x00`——conforming + 字节粒度
limit 0xFFFF）：

```asm
    .p2align 5               # 32 字节对齐（同 GRUB）
gdt:
    .word 0, 0
    .byte 0, 0, 0, 0         # 0x00 null
    .word 0xFFFF, 0
    .byte 0, 0x9A, 0xCF, 0   # 0x08 code32
    .word 0xFFFF, 0
    .byte 0, 0x92, 0xCF, 0   # 0x10 data32
    .word 0xFFFF, 0
    .byte 0, 0x9E, 0, 0      # 0x18 code16 伪实模式
    .word 0xFFFF, 0
    .byte 0, 0x92, 0, 0      # 0x20 data16 伪实模式
gdt_end:
    .p2align 5
gdtdesc:
    .word gdt_end - gdt - 1  # 5 个描述符 = 40 字节，limit = 0x27
    .long gdt
```

**`real_to_prot`**（镜像 GRUB，16 位入口）：

```asm
.globl real_to_prot
.code16
real_to_prot:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    lgdtl gdtdesc            # 载入 GDT（lgdtl：32 位基址）
    movl %cr0, %eax
    orl $0x1, %eax           # PE=1
    movl %eax, %cr0
    ljmpl $PROT_MODE_CSEG, $protcseg

.code32
protcseg:
    movw $PROT_MODE_DSEG, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl (%esp), %eax        # 实模式栈上的 32 位返回地址
    movl %eax, REAL_STACK    # 暂存到 0x1FF0
    movl protstack, %eax     # 取保护模式栈顶
    movl %eax, %esp
    movl %eax, %ebp
    movl REAL_STACK, %eax
    movl %eax, (%esp)        # 返回地址放到保护模式栈顶
    xorl %eax, %eax
    sidt realidt             # 保存实模式 IDT
    lidt protidt             # 保护模式用空 IDT
    ret
```

**`prot_to_real`**（镜像 GRUB，32 位入口）：

```asm
.globl prot_to_real
.code32
prot_to_real:
    lgdt gdtdesc
    sidt protidt             # 保存保护模式 IDT（空）
    lidt realidt             # 载入实模式 IDT
    movl %esp, %eax
    movl %eax, protstack     # 保存保护模式栈
    movl (%esp), %eax        # 32 位返回地址
    movl %eax, REAL_STACK    # 存到 0x1FF0（同时是实模式栈的"栈顶内容"）
    movl $REAL_STACK, %eax
    movl %eax, %esp          # 切到实模式栈
    movl %eax, %ebp
    movw $PSEUDO_REAL_DSEG, %ax
    movw %ax, %ds            # 伪实模式数据段
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $PSEUDO_REAL_CSEG, $tmpcseg

.code16
tmpcseg:
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax   # PE=0：回到实模式
    movl %eax, %cr0
    ljmpl $0, $realcseg

realcseg:
    xorl %eax, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    sti
    retl                     # 32 位 ret：从 0x1FF0 弹出返回地址
```

**`bios_interrupt`**（封装 INT 调用，镜像 GRUB `int.S` 的语义）：

```asm
.globl bios_interrupt
.code32
bios_interrupt:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ecx
    pushl %edx

    movb 8(%ebp), %al        # intno（cdecl：8(%ebp)）
    movb %al, int_stub + 1   # 自修改：int 指令的中断号字节（同 GRUB）
    movl 12(%ebp), %edx      # regs 指针
    movl %edx, REGS_PTR      # 跨模式切换保存
    # ... 把 regs 结构逐字段拷到 BIOS_SNAPSHOT（快照，实模式可访问）...

    call prot_to_real
    .code16
    # 实模式：从快照装载寄存器（DS=0 相对寻址；FLAGS 用 push/pop 三步）
    movl BIOS_SNAPSHOT + 0, %eax
    pushl %eax
    movl BIOS_SNAPSHOT + 12, %ebx
    movl BIOS_SNAPSHOT + 16, %ecx
    movl BIOS_SNAPSHOT + 20, %edi
    movl BIOS_SNAPSHOT + 24, %esi
    movl BIOS_SNAPSHOT + 28, %edx
    movw BIOS_SNAPSHOT + 4, %ax
    pushw %ax                # 暂存 ES
    movw BIOS_SNAPSHOT + 6, %ax
    pushw %ax                # 暂存 DS
    movw BIOS_SNAPSHOT + 8, %ax
    pushw %ax                # 暂存 FLAGS
    popw %ax
    pushw %ax                # popfw 从栈顶取，需把 flags 放回栈顶
    popfw                    # FLAGS = 快照值
    popw %ax
    movw %ax, %ds
    popw %ax
    movw %ax, %es
    popl %eax                # EAX 最后赋值（INT 的入口寄存器）
int_stub:
    int $0x13                # 中断号字节由入口自修改
    # ... 把结果寄存器存回快照（%cs: 写，与 DS 无关）...
    calll real_to_prot
    .code32
    # 保护模式：快照拷回 regs 结构 ...
    popl %edx
    popl %ecx
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
```

### 3.3 `loader.c` 精讲

**寄存器结构与中断声明**：

```c
struct bios_regs {
    u32 eax;   /* +0  */
    u16 es;    /* +4  */
    u16 ds;    /* +6  */
    u16 flags; /* +8  */
    u32 ebx;   /* +12 */
    u32 ecx;   /* +16 */
    u32 edi;   /* +20 */
    u32 esi;   /* +24 */
    u32 edx;   /* +28 */
};

void bios_interrupt(u8 intno, struct bios_regs *regs);
```

**`disk_read_sector`**（INT 13 AH=02，参数组装对应 B02 的 CHS 换算）：

```c
static int disk_read_sector(u8 drive, u32 lba, void *buf)
{
    struct bios_regs regs;
    u8 cyl, head, sect;

    lba_to_chs(lba, &cyl, &head, &sect);

    regs.eax = 0x00000201u;            /* AH=02 读扇区，AL=1 */
    regs.ecx = ((u32)cyl << 8) | sect; /* CH=柱面，CL=扇区(1 基) */
    regs.edx = ((u32)head << 8) | drive; /* DH=磁头，DL=盘号 */
    regs.es  = (u16)((u32)buf >> 4);   /* ES:BX = 缓冲区线性地址 */
    regs.ebx = (u32)buf & 0xFu;
    regs.ds  = 0;
    regs.flags = 0x0200u;              /* IF=1 */
    regs.edi = 0;
    regs.esi = 0;

    bios_interrupt(0x13, &regs);
    return (regs.flags & 0x01u) ? -1 : 0;  /* CF=1 -> 失败 */
}
```

**`loader_main` 演示**：读 LBA0（引导扇区），hex 打印头部 8 字节，校验
510/511 字节的 `55 aa` 签名；再读 LBA1 打印 4 字节。期望输出：

```text
B05 Mini-GRUB: protected-mode disk read via BIOS callback
bios_interrupt -> prot_to_real -> int 0x13 -> real_to_prot
LBA0 head: fa 31 c0 8e d8 8e c0 8e
LBA0 signature 55 aa: boot sector read OK
LBA1 head: fa 31 c0 8e
B05 done: prot_to_real/real_to_prot cycle OK
```

### 3.4 构建管线与主控制流

构建与 B04 相同（stage1 链 0x7C00、stage2 链 0x7E00、`truncate` 8 扇区）；
`make check` 新增 `bios_interrupt`/`prot_to_real`/`real_to_prot`/`disk_read_lba`
符号断言。

```text
stage1 → stage2(0x7E00) → 实模式打印 → A20/GDT/CR0.PE → pm32
  → call loader_main(C)
      → disk_read_lba → disk_read_sector → bios_interrupt(0x13, &regs)
          → 快照 → prot_to_real → 实模式装载寄存器 → int 0x13
          → 保存结果 → real_to_prot → 快照拷回 regs
      → 校验 CF → vga_hex 打印头部字节 → 校验 55 aa 签名
  → hlt
```

---

## 4. 数据流与运行逻辑

```text
C 侧 regs 结构（保护模式栈上）
  → BIOS_SNAPSHOT(0x2000) 快照      ← 保护模式，DS=0x10 平坦写
  → prot_to_real: ESP→0x1FF0, CR0.PE=0, CS=0
  → 实模式装载寄存器（DS=0 读快照）→ int 0x13
  → BIOS 把磁盘数据写入 ES:BX（如 0x850:0 = 0x8500 = sector_buf）
  → 结果寄存器 → %cs: 快照写回 → real_to_prot → 保护模式
  → 快照 → C 侧 regs → vga_hex 打印
```

`make check` 与自动化验证 marker：`B05 Mini-GRUB`（banner）、`signature 55 aa`
（读盘成功的硬证据）、`B05 done`。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b05-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b05 check
bootloader-course/scripts/validate-course.sh b05 qemu
```

### 5.2 期望输出

- `make check`：`B05 check PASS: stage1=512+55aa stage2=4096B
  bios_interrupt+disk_read_lba present`
- QEMU 五行输出见 3.3；其中 `LBA0 head: fa 31 c0 ...` 是引导扇区真实字节
  （`fa` = cli，`31 c0` = xorw %ax,%ax，`8e d8` = movw %ax,%ds——正是
  stage1.S 的开头）。

### 5.3 成功判据

`55 aa` 签名校验通过（读盘内容真实）、CF 语义正确（失败路径返回 -1）、
QEMU trace 无 triple fault。切换过程若崩溃，多半在段/栈/IDT 三处（见调试地图）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 读回全零但 CF=0 | 寄存器装载错位（如本课 FLAGS 的 pop 顺序 bug） | 用 `-d in_asm` trace 检查 int 前 EAX/ES/BX/CX/DX；核对 push/pop 配对 |
| 读回全零且怀疑 EAX 错 | `movl %cs:mem,%eax` 被汇编成垃圾（moffs+段前缀） | objdump 看该指令字节；装载阶段改用 DS=0 相对寻址 |
| 切换时 triple fault | GDT/选择子/IDT 错 | 检查 `ljmp $0x18` 与伪实模式描述符；`lidt realidt` 后 CR0.PE=0 顺序 |
| `retl` 弹错地址 | 返回地址没放到 0x1FF0 或 ESP 不是 0x1FF0 | 检查 `movl %eax, REAL_STACK` 与 `movl $REAL_STACK, %esp` 配对 |
| 实模式代码访问错内存 | DS 在装载中途被改写 | 装载阶段先读快照再改 DS；保存阶段用 `%cs:` 写 |
| INT 后 CF 恒 0 | flags 捕获错（pushfw/popw 顺序） | 打印 regs.flags 与 regs.eax 对照 AH 错误码 |

---

## 7. 与 GNU GRUB 源码对照

本课直接镜像 `$GRUB_SRC/grub-core/kern/i386/realmode.S`（`prot_to_real`/
`real_to_prot`、GDT、`realidt`/`protidt`）与 `grub-core/kern/i386/int.S`
（`grub_bios_interrupt`、寄存器结构、自修改 intno）。对照点：

- **完全一致**：GDT 五项布局与字节值；REAL_STACK/PROT_STACK 地址；切换步骤
  与顺序；返回地址"栈即暂存"手法；IDT 切换；`sti` 恢复（GRUB PCBIOS 分支）；
- **简化（已记录）**：GRUB 的 `grub_bios_interrupt` 用自修改立即数（movw/movl
  imm 直接嵌在代码流里）传递全部寄存器；本课用一个低内存快照
  `BIOS_SNAPSHOT(0x2000)` 替代（仅保留 intno 自修改），并在 README 与源码
  注释里说明差异；
- **调用约定**：GRUB 全局用 `-mregparm=3`（参数走 EAX/EDX/ECX），本课用
  cdecl（栈传参），语义相同、接口更通用；
- **后续**：GRUB 的 `biosdisk.mod` 在 `grub_bios_interrupt` 之上做 LBA/CHS
  双路径与多盘枚举；B10 的 E820 将复用本课的 `bios_interrupt` 调 INT 15。

Linux 对照：`arch/x86/boot/` 与 `arch/x86/realmode/` 的 real-mode trampoline
思路同源；仅作工程对照，不作为 GRUB 实现来源（证据边界见
[`docs/grub-implementation-guide.md`](../../docs/grub-implementation-guide.md)）。

---

## 8. 思考题与练习

1. 概念理解：为什么 `prot_to_real` 要把返回地址写到 0x1FF0 而不是某个普通
   内存变量？`retl` 与这个地址有什么关系？
2. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "retl\|REAL_STACK\|protstack" grub-core/kern/i386/realmode.S`，
   对照本课实现逐行核对。
3. 动手实验（临时副本）：把装载阶段的 `pushw %ax; popw %ax; pushw %ax;
   popfw` 改回错误的 `popw %ax; popfw`，观察 DS/ES 如何互换（VGA 输出变化），
   再改回来。
4. 动手观察：用 `objdump -D -b binary -m i8086` 检查 `int_stub` 附近的字节，
   解释为什么 `movl %cs:0x2000,%eax` 会产生错误的 `ff 2e ...` 编码。
5. 综合：画出 `disk_read_lba(0, 0, buf, 1)` 从 C 到 BIOS 再到 C 的完整
   寄存器与栈轨迹（含快照、REAL_STACK、保护模式栈三处状态迁移）。

---

## 9. 本课小结与下一课预告

**小结**：本课为 loader 装上了"眼睛"——保护模式下的 BIOS 回调。关键收获：
(1) BIOS 中断只在实模式可用，切换靠 `prot_to_real`/`real_to_prot` 这对对称
函数；(2) 伪实模式段（0x18/0x20）让清 CR0.PE 前后的段缓存保持一致；
(3) 返回地址用"栈即暂存"（0x1FF0）跨模式传递；(4) 寄存器用 32 字节快照
`bios_interrupt` 封装，C 侧只需填结构；(5) 两个汇编陷阱：EAX 的 moffs 编码
与 `%cs:` 冲突、`popfw` 需要 push 回栈顶。阶段一（引导链）至此完成：从加电
到保护模式、从输出到输入，loader 已具备读盘能力。

**下一课预告**：进入 [`b06-stable/README.md`](../b06-stable/README.md)。有了
`disk_read_lba`，loader 就能读入内核文件——B06 解析 ELF32 结构（ELF header、
program headers、PT_LOAD 校验），对照 GRUB `grub-core/kern/elf.c` 的
`grub_elf32_open`/`grub_elf32_phdrs`。

# Lesson 08: 从 GRUB 的 32 位交接进入 x86_64 long mode — 精讲文档
> **课号**：Lesson 08（可执行课，**第一个双段构建**）
> **主题**：GRUB 仍按 Multiboot2 **i386** ABI 装载 ELF32 内核；TinyOS 在 32 位早期代码里
> 建立四级页表（PML4/PDPT/PD/PT0/PT1 共五页）、按 Intel 规定顺序开启 PAE + long mode，
> 通过 far jump 进入手工编码的 64 位桥接指令，最后运行一个真正以 `-m64` 编译、经
> `objcopy -O binary` + `.incbin` 内嵌回 ELF32 镜像的 64 位 VGA shell。
> **课程主线位置**：第 3 阶段（x86_64 过渡）的核心课；32 位时代的最后一个纯 32 位入口，
> 从此以后所有课程都在 64 位代码上展开。
> **前置课程**：[`lesson-07-stable/README.md`](../lesson-07-stable/README.md)（32 位非 PAE
> 两级页表 + `CR3`/`CR0.PG` 开启顺序）
> **后续课程**：[`lesson-09-stable/README.md`](../lesson-09-stable/README.md)（在 long mode
> 下建立最小异常 IDT：#UD/#PF 终止诊断）
> **一句话目标**：学完本课你能独立解释「ELF loader ABI ≠ 最终执行模式」：镜像整体仍是
> i386 ELF、由 GRUB 按 i386 ABI 装载，但控制流通过四条关键指令与一个 far jump 换成
> 64 位代码段后，执行的却是 `-m64` 编译的二进制。

---

## 1. 课程定位（Mission）
**一句话目标**：能亲手搭建并解释从 32 位保护模式到 x86_64 long mode 的完整状态转换：
分配五张物理页组成四级页表、按 `LGDT → CR3 → CR4.PAE → EFER.LME → CR0.PG → far jump`
的固定顺序切换模式，并在 64 位 shell 里用 `lminfo` 验证映射结果。

- **在课程主线中的位置**：第 7 课的两级页表（PDE/PTE）升级为四级页表（PML4/PDPT/PD/PT），
  32 位 C 代码缩水为"只管 setup"，真正的 C 逻辑全部迁移到 `kernel64.c`。此后所有课
  （IDT、8259A、键盘驱动）都运行在 64 位模式。
- **前置知识清单**：
  1. 第七课：32 位分页、PDE/PTE 格式、`CR3`/`CR0.PG`、identity mapping 实验契约；
  2. Multiboot2 i386 交接 ABI（`EAX = 0x36d76289`、`EBX = MBI`）、GDT/far jump、`rdmsr`/`wrmsr`。
- **本课交付**：QEMU 屏幕上出现 64 位 C banner `TinyOS lesson 8: x86_64 long mode` +
  `64-bit C continuation active`；新命令 `lminfo` 打印五张表帧与 4 MiB identity window。

---

## 2. 核心概念精讲
### 2.1 概念一：long mode 的进入前提与顺序
x86_64 long mode 需要按固定顺序开启（Intel SDM Vol. 3）：先 `CR4.PAE = 1`（long mode
的分页只有 PAE 一种形态）；再 `IA32_EFER.LME = 1`（置 LME 只是"请求"，尚未生效）；
随后 `CR0.PG = 1` 的瞬间 CPU 检查 PAE 与 LME 都已就位，`EFER.LMA` 自动置位才算真正
进入；最后必须用 far jump 重载 CS 为一个 **64 位 code segment**（描述符 `L=1, D=0`），
否则 long mode 生效后 CS 的 D 位语义翻转，下一条指令仍按 32 位解读。

```text
LGDT(gdt64) → mov cr3, pml4 → CR4.PAE = 1 → EFER.LME = 1 → CR0.PG = 1
   → ljmp $0x08, $long_mode_start   （far jump 重载 CS，进入 64 位代码段）
```

### 2.2 概念二：IA-32e 四级页表，每级 512 项
32 位分页用两级查表覆盖 4 GiB；64 位空间太大，查表增加到四级：`CR3 → PML4 → PDPT → PD
→ PT → 4 KiB page`。每一项是 **8 字节**，所以一张 4 KiB 页正好装 `4096/8 = 512` 项。

```text
CR3 ──► PML4[0] ──► PDPT[0] ──► PD[0] ──► PT0[0..511] : 0x00000000 .. 0x001fffff (2 MiB)
                       └────────► PD[1] ──► PT1[0..511] : 0x00200000 .. 0x003fffff (2 MiB)
```

- 每个 PML4 项覆盖 512 GiB、PDPT 项覆盖 1 GiB、PD 项覆盖 2 MiB、PT 项覆盖 4 KiB。本课
  只走 4 KiB 细粒度，只用每张表的第 0/1 项，其余项保持清零（Present=0）。
- 表项属性与第七课一脉相承：`0x003` = Present | Writable 存低 12 位，物理页帧地址存
  高 52 位；**关键区别是表项从 4 字节变 8 字节**，所以 `PAGE_ENTRIES = 512`。
- `PT0[i]` 映射 `i*4KiB`、`PT1[i]` 映射 `(i+512)*4KiB`：两张表拼出 `[0, 4 MiB)`
  identity window，VGA `0xb8000`、内核 1 MiB、栈与 MBI 全部落入。

### 2.3 概念三：long_mode_handoff —— 跨模式的"纸面契约"
`kernel64.bin` 是独立链接的裸二进制，内部没有 ELF32 符号表，不能直接引用
`_kernel_start`、`stack_top` 等 ELF32 符号。两边的 C 结构体在 `kernel.c` 与 `kernel64.c`
里**逐字段一致地重复定义**：32 位侧填数据，64 位侧通过 RDI（SysV x86_64 第一参数）
拿到指针直接读取。字段包括五张表帧地址、allocator 游标与 64 项历史、kernel/stack
边界、MBI 地址/大小、已分配页数。全局实例位于 32 位数据段且 < 4 MiB，64 位侧用 imm32
即可寻址——identity mapping 契约在"跨模式"上的第二次应用。

### 2.4 概念四：双段构建管线（objcopy + incbin）
GRUB 只能装载一个 ELF，且本课需要 ELF32 容器（Multiboot2 i386）。方案是"俄罗斯套娃"：
把 64 位程序链成独立 ELF64 → `objcopy -O binary` 剥成裸字节 → `.incbin` 塞进 ELF32。

```text
kernel64.c --(-m64 -fpie -mno-red-zone)--> kernel64.o
      --(ld -m elf_x86_64 -T kernel64.ld)--> kernel64.elf   (虚拟地址从 0 起)
      --(objcopy -O binary)--> kernel64.bin   (纯字节流, offset 0 = kernel_main64_binary)
      --(.incbin "build/kernel64.bin")--> boot.o 的 .text64 段
      --(ld -m elf_i386 -T linker.ld)--> kernel.elf  (最终 Multiboot2 ELF32)
```

- `-fpie`：让 64 位代码内函数调用尽量用 `E8 rel32` 相对寻址，剥掉符号后字节流自洽。
- 链接成"虚拟地址从 0 起"：裸二进制被塞进 ELF32 后实际落在 1 MiB 附近，任何绝对地址
  都必须以 offset 0 的差值形态存在；`kernel64.ld` 的 `. = 0;` 强制入口函数在 offset 0，
  于是 boot.S 的 `call kernel_main64` 落到内嵌二进制起点 = `kernel_main64_binary`。
- `readelf -rW build/kernel64.elf` 必须无 relocation：一旦有，剥成裸字节后地址全错。

### 2.5 概念五：手工编码的 64 位桥接指令
far jump 之后 CPU 已用 64 位 CS 取指，但外层镜像仍是 ELF32、汇编器仍在 `.code32`
模式——**GAS 不会为 `.code32` 段生成 64 位编码**。解法是用 `.byte` 直接声明机器码
（含义见 3.3 逐字节注释），所以桥接块只有 ~15 字节——小到足以人工核对。

---

## 3. 源码精讲
### 3.1 文件清单与职责
| 文件 | 职责 | 本课增量（相对 Lesson 07） |
|---|---|---|
| `boot.S` | Multiboot2 header、32 位入口、**LGDT→PAE→LME→PG→ljmp、手工编码桥接、`.text64` 内嵌** | 重写 |
| `kernel.c` | 32 位 setup：验证 MBI、分配五页、构造四级页表、**返回 PML4 地址** | 重写（删掉全部 shell，setup-only） |
| `kernel64.c` | **新文件**：64 位 VGA shell + allocator 续接 | 全新 |
| `kernel64.ld` | **新文件**：裸二进制段布局，`. = 0` 强制入口在 offset 0 | 全新 |
| `linker.ld` | ELF32 镜像布局 | 微小变化（新增 `.text64` 段） |
| `Makefile` | 构建管线 | 新增 `CFLAGS64` 与 `kernel64.o→bin→内嵌` 三步 |
| `grub.cfg` | 菜单标题 | 微小变化 |

### 3.2 kernel.c —— 32 位 setup（重写）
`kernel.c` 从第七课"完整 shell"精简为"纯 setup"：VGA、键盘、命令解析全部删除，只留
返回值。三个精讲点：

**① 常量与结构**：`PAGE_ENTRIES 512`（64 位表项 8 字节 → 4 KiB 页装 512 项）、
`PTE_PRESENT_WRITABLE 0x003ULL`（P|W，表项从 u32 升级为 u64），以及 `struct
long_mode_handoff`（与 kernel64.c 逐字段一致的跨段 ABI）与全局实例
`long_mode_handoff`。第六、七课的 allocator 状态全部搬进 handoff：64 位 shell 还要
继续用同一份 allocator，分配历史必须跨模式延续，否则 64 位 `palloc` 可能把 32 位
已用掉的表页再分一遍。

**② `setup_long_mode_tables()` —— 四级页表构建（核心函数）**

```c
static u32 setup_long_mode_tables(void)
{
    volatile u64 *pml4,*pdpt,*pd,*pt0,*pt1; u32 i;
    long_mode_handoff.pml4=phys_alloc_page(); long_mode_handoff.pdpt=phys_alloc_page();
    long_mode_handoff.pd=phys_alloc_page(); long_mode_handoff.pt0=phys_alloc_page();
    long_mode_handoff.pt1=phys_alloc_page();
    if(!table_page_ok(long_mode_handoff.pml4)||!table_page_ok(long_mode_handoff.pdpt)
      ||!table_page_ok(long_mode_handoff.pd)||!table_page_ok(long_mode_handoff.pt0)
      ||!table_page_ok(long_mode_handoff.pt1)) return 0;
    zero_page(long_mode_handoff.pml4); zero_page(long_mode_handoff.pdpt);
    zero_page(long_mode_handoff.pd); zero_page(long_mode_handoff.pt0);
    zero_page(long_mode_handoff.pt1);
    pml4=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pml4;
    pdpt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pdpt;
    pd=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pd;
    pt0=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt0;
    pt1=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt1;
    pml4[0]=long_mode_handoff.pdpt|PTE_PRESENT_WRITABLE;
    pdpt[0]=long_mode_handoff.pd|PTE_PRESENT_WRITABLE;
    pd[0]=long_mode_handoff.pt0|PTE_PRESENT_WRITABLE;
    pd[1]=long_mode_handoff.pt1|PTE_PRESENT_WRITABLE;
    for(i=0;i<PAGE_ENTRIES;i++){ pt0[i]=((u64)i*PAGE_SIZE)|PTE_PRESENT_WRITABLE;
      pt1[i]=((u64)(i+PAGE_ENTRIES)*PAGE_SIZE)|PTE_PRESENT_WRITABLE; }
    return (u32)long_mode_handoff.pml4;
}
```

- **签名与职责**：无参；成功返回 PML4 物理地址（经 `kernel_main32` 转交 boot.S），
  失败返回 0。
- **算法步骤**：① 连调五次 `phys_alloc_page()` 得五张页，立即写进 handoff 字段——
  allocator 历史数组立刻把它们标为已用；② `table_page_ok()` 校验每页 `>= 1 MiB`、
  `< 4 MiB`、4 KiB 对齐（比第七课更严格：**表页必须落在 identity window 内**，否则
  64 位侧无法解引用）；③ 逐页清零——四级结构中任一残留位都是随机映射；④ 链接四级
  `PML4[0]→PDPT[0]→PD[0]→PT0`、`PD[1]→PT1`；⑤ 512 项循环填两张 page table；
  ⑥ 返回 PML4 地址。
- **边界与错误处理**：任一页不满足 `table_page_ok` 立即返回 0，`kernel_main32` 也返回
  0，boot.S 的 `jz halt32` 捕获后停在 `hlt`。
- **为什么五张表**：64 位分页四级每级一个物理帧；从"两级各一张"变成"PML4/PDPT/PD 各
  一张 + PT 两张（4 MiB / 2 MiB）"。

**③ `kernel_main32()`**：保存 magic/mbi、填 handoff 的 kernel/stack 边界、验证 MBI
（失败返回 0）、填 mbi_address/mbi_size，最后 `return setup_long_mode_tables()`。返回值
不再无意义——它是 **PML4 物理地址**，经 EAX 回到 boot.S 的 `movl %eax, %ebx` 进入
`enter_long_mode`；失败则 `testl %eax,%eax; jz halt32` 兜底。

### 3.3 boot.S —— 状态转换 + 手工编码桥接（本课灵魂）
**① 新常量**

```asm
.set IA32_EFER, 0xc0000080   /* EFER MSR 编号 */
.set EFER_LME, 0x00000100    /* bit8: Long Mode Enable */
.set CR4_PAE, 0x00000020     /* bit5: Physical Address Extension */
.set CR0_PG, 0x80000000      /* bit31: Paging */
.set CODE64_SELECTOR, 0x08   /* GDT index 1 的偏移 */
.set DATA_SELECTOR, 0x10     /* GDT index 2 的偏移 */
```

**② `_start` 的返回值协议**：`call kernel_main32` 后 `testl %eax,%eax; jz halt32`；
成功则 `movl %eax, %ebx`（EBX = PML4）再 `call enter_long_mode`。`EAX` 从"可有可无"
升级为"决定性"；失败 `hlt` 死循环，留下可被 QEMU monitor 观察的停点。

**③ `enter_long_mode()` —— 进入 long mode 的七条指令**

```asm
enter_long_mode:
    lgdt gdt64_pointer        /* 1. 加载含 L=1 code descriptor 的 GDT */
    movl %ebx, %cr3           /* 2. 先指定 PML4，PG 生效瞬间就有表可查 */
    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4           /* 3. PAE=1（long mode 前置要求）*/
    movl $IA32_EFER, %ecx
    rdmsr
    orl $EFER_LME, %eax
    wrmsr                     /* 4. LME=1（此刻 LMA 未置位）*/
    movl %cr0, %eax
    orl $CR0_PG, %eax
    movl %eax, %cr0           /* 5. PG=1 → LMA 自动置位，long mode 生效 */
    ljmp $CODE64_SELECTOR, $long_mode_start  /* 6. far jump 重载 CS */
```

- 顺序为什么不能变：CR3 必须在 PG 前写（否则置位瞬间无表可查）；PAE 必须在 PG 前开
  （Intel 规定置 PG 时 PAE=0 且 LME=1 会 #GP）；LME 必须在 PG 前开；`ljmp` 必须最后——
  long mode 生效后 CS.D 位含义翻转，只有 far jump 能换成 64 位代码段描述符。
- `rdmsr`/`wrmsr` 三件套：`ECX=0xc0000080` 选 EFER，`EDX:EAX` 读回，OR 上 LME（bit8）
  再写回，只动 bit8。

**④ `long_mode_start` —— 手工编码的 64 位桥接块**

far jump 之后 CPU 已按 64 位 CS 取指，但汇编器还在 `.code32` 模式。以下 `.byte` 全是
预计算的 64 位机器码：

```asm
long_mode_start:
    .byte 0x66, 0xb8, DATA_SELECTOR, 0x00    /* 66 b8 10 00 = mov ax, 0x0010
                                                (66 前缀+无 REX → 16 位立即数) */
    .byte 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0 /* 8e d8=mov ds,ax 8e c0=mov es,ax
                                                8e d0=mov ss,ax (data selector) */
    .byte 0x48, 0xc7, 0xc4                    /* 48 c7 c4 id = mov rsp, imm32
                                                (REX.W+C7 /0+ModRM c4 → rsp) */
    .long stack_top                           /* imm32 = 栈顶（sign-extend 成 64 位）*/
    .byte 0x48, 0x83, 0xe4, 0xf0              /* 48 83 e4 f0 = and rsp, -16
                                                (83 /4 与 imm8=0xf0 → 16 字节对齐) */
    .byte 0x48, 0x31, 0xed                    /* 48 31 ed = xor rbp, rbp（帧指针清零）*/
    .byte 0x48, 0xc7, 0xc7                    /* 48 c7 c7 id = mov rdi, imm32 */
    .long long_mode_handoff                   /* RDI = handoff 块地址（SysV 第一参数）*/
    .byte 0xe8                                /* e8 rel32 = call */
    .long kernel_main64 - . - 4               /* 相对位移：跳到 .text64 内嵌二进制起点 */
1:
    .byte 0xf4, 0xeb, 0xfd                    /* f4=hlt  eb fd=jmp 1b（死循环兜底）*/
```

逐条含义：`66 b8 10 00` = `mov ax, 0x10`（`b8` 无 REX.W 按 16 位操作数，配 66 前缀）装
data selector；`8e d8/c0/d0` = `mov ds/es/ss, ax`（long mode 段基址恒 0，语义完备）；
`48 c7 c4 imm32` = `mov rsp, imm32`（REX.W+C7 /0+ModRM c4→rsp，立即数符号扩展，
`stack_top` < 4 MiB 安全）；`48 83 e4 f0` = `and rsp, -16`（SysV 要求 call 前 16 字节
对齐）；`48 31 ed` = `xor rbp, rbp`；`48 c7 c7 imm32` = `mov rdi, imm32`（handoff 作第一
参数）；`e8 rel32` = `call`，位移 = `kernel_main64 - . - 4`（`.` 是位移字段地址，算出
「下一条指令 → kernel_main64」的差值）；`f4 eb fd` = `hlt; jmp -3` 死循环兜底。

**⑤ 64 位 GDT 与 `.text64` 内嵌**

```asm
gdt64:
    .quad 0x0000000000000000                    /* 空描述符 */
    .quad 0x00af9a000000ffff                    /* code：P=1,DPL=0,S=1,type=0xa */
                                                /*  flags 0xaf: G=1, D/B=0, L=1 */
    .quad 0x00af92000000ffff                    /* data：P=1,DPL=0,S=1,type=0x2,D/B=1 */
gdt64_end:
gdt64_pointer:
    .word gdt64_end - gdt64 - 1                 /* LGDT limit = 大小-1 */
    .long gdt64                                 /* LGDT base = 物理地址 */
...
.section .text64, "ax"
.align 16
.globl kernel_main64
kernel_main64:
    .incbin "build/kernel64.bin"                /* 裸 64 位二进制原样铺进 ELF32 */
```

`0x00af9a000000ffff` 逐字节拆解：低 4 字节 = limit=0xffff、base15:0=0；byte4=0=
base23:16；byte5=`0x9a`=access byte（P=1、DPL=0、S=1、type=1010=execute/read）；byte6=
`0xaf`=（G=1、**D/B=0**、**L=1**、AVL=0、limit19:16=0xf）；byte7=0=base31:24。
**`L=1,D=0` 正是"64 位代码段"的标志**，缺失则 far jump 后 CPU 仍按 32 位执行。
`lgdt` 的 base 是 32 位 `.long`；gdt64 位于 1 MiB 镜像内、identity window 中，32 位模式
可寻址。`.incbin` 使 Makefile 必须先产出 `build/kernel64.bin` 才能编译 `boot.o`。

### 3.4 kernel64.c —— 真正的 64 位 C 逻辑
所有函数标 `TEXT64`（`section(".text64")` + `noinline`），确保落入 `.text64` 段被
objcopy 打包。三个精讲点：

**① 入口 `kernel_main64_binary()` —— 必须位于裸二进制 offset 0**

```c
__attribute__((section(".text64.entry"), noinline))
void kernel_main64_binary(struct long_mode_handoff*h){
    u16 c=0,n=0; char cmd[32]; u8 code; char ch;
    clear64(&c); text64(&c,"TinyOS lesson 8: x86_64 long mode\n64-bit C continuation active\n");
    prompt64(&c);
    for(;;){
        if(!(inb64(0x64)&1)) continue;      /* 轮询 8042 状态端口的 OBF 位 */
        code=inb64(0x60);                    /* 读 scancode */
        if(code&0x80) continue;              /* 丢弃释放码（bit7）*/
        ch=scan64(code); if(!ch) continue;
        if(ch=='\n'){ putc64(&c,ch); cmd[n]=0; exec64(&c,h,cmd); n=0; }
        else if(ch=='\b'){ if(n){n--;c--;VGA[c]=0x0f20;} }
        else if(n<31){ cmd[n++]=ch; putc64(&c,ch); }
    }
}
```

- **为什么必须在 offset 0**：boot.S 的 `call` 目标是 `.text64` 段开头的 `kernel_main64`
  标签 = `kernel64.bin` 第一字节。`kernel64.ld` 用 `*(.text64.entry)` 排段首，保证该
  函数落在 offset 0——这是"裸二进制调用契约"。
- **参数 `h`**：形参正好接住 RDI 传来的 handoff 指针，C 入口无需任何寄存器组装。
- **循环结构**：完全复用第七课 PS/2 轮询 shell 逻辑，只是搬到 64 位。

**② `lminfo()` —— 验证五张表**

```c
static TEXT64 void lminfo(u16*c,struct long_mode_handoff*h){
    text64(c,"long mode: on\npml4: "); hex64(c,h->pml4);
    text64(c,"\npdpt: "); hex64(c,h->pdpt);
    text64(c,"\npd:   "); hex64(c,h->pd);
    text64(c,"\npt0:  "); hex64(c,h->pt0);
    text64(c,"\npt1:  "); hex64(c,h->pt1);
    text64(c,"\nidentity: 0000000000000000 - 0000000000400000\n");
}
```

从 handoff 读出五张表帧地址，各打印 16 位十六进制（`hex64` 从 bit60 到 bit0 每 4 位
一次，共 16 位）；`identity:` 行是硬编码常量串，宣告映射契约为 `[0, 4 MiB)`。

**③ `alloc64()` —— 64 位侧独立重扫 MBI 的分配器**

```c
static TEXT64 u64 alloc64(struct long_mode_handoff*h){
    const struct mb2_mmap_tag*m; u32 off;
    if(h->allocated_pages==ALLOCATION_HISTORY_MAX) return 0;
    m=(const struct mb2_mmap_tag *)(unsigned long)h->mbi_address;
    for(off=8;off<h->mbi_size;){ const struct mb2_tag*t=...; u32 r=(t->size+7)&~7U;
        if(t->type==6){ m=(const struct mb2_mmap_tag*)t; break; }
        if(t->size<8||r>h->mbi_size-off) return 0; off+=r; }
    if(((const struct mb2_tag*)m)->type!=6) return 0;
    for(off=0;off<m->size-16;off+=m->entry_size){ ... p=up(e->addr); end=down(e->addr+e->len);
        while(p&&p<end){ if(!reserved(h,p)){ h->allocation_history[h->allocated_pages++]=p; return p; } p+=PAGE_SIZE; } }
    return 0;
}
```

- 它**自己**从 MBI 重新走 tag walker 找到 type-6 mmap（不依赖 32 侧状态），分配记录仍
  写进 handoff 的 `allocation_history`——两边共用一份历史，避免重复分配。
- 失败返回 0，`exec64` 打印 `allocator exhausted`。

**④ `exec64()` 命令表**（输出串逐字抄录自源码）

```c
if(eq64(s,"help"))text64(c,"commands: help about clear lminfo pinfo palloc mmap\n");
else if(eq64(s,"about"))text64(c,"TinyOS lesson 8: x86_64 long mode\n");
else if(eq64(s,"lminfo"))lminfo(c,h);
else if(eq64(s,"pinfo")){text64(c,"page size: 0000000000001000\nallocated pages: ");hex64(c,h->allocated_pages);putc64(c,'\n');}
else if(eq64(s,"palloc")){p=alloc64(h); if(p){text64(c,"allocated: ");hex64(c,p);putc64(c,'\n');} else text64(c,"allocator exhausted\n");}
else if(eq64(s,"mmap"))mmap64(c,h);
else if(eq64(s,"clear")){clear64(c);prompt64(c);return;}
else if(s[0])text64(c,"unknown command\n");
prompt64(c);
```

`mmap64` 打印 `"Multiboot2 available ranges:\n"`，对 type-1 entry 输出
`addr +len`（每项 `hex64(e->addr); " +"; hex64(e->len); '\n'`，至多 6 项）。

### 3.5 两个链接脚本与构建管线
- **`kernel64.ld`**（64 位裸二进制）：`. = 0;` 让虚拟地址从 0 起，offset 即地址；
  `.text64 : { *(.text64.entry) *(.text64 .text64.*) }` 把入口函数排最前，确保
  `kernel_main64_binary` 在 offset 0。没有 `.data`/`.bss`：64 位侧刻意不写全局可变数据，
  全部状态走 handoff 指针或栈——裸二进制里不能有"需要重定位的绝对地址"。
- **`linker.ld`**：只新增一行 ` .text64 ALIGN(16) : { *(.text64 .text64.*) }`，把内嵌
  代码放进镜像 RX 区。
- **Makefile 三个新标志**：`-m64`（目标架构 x86_64）、`-fpie`（位置无关代码，函数调用
  尽量相对寻址）、`-mno-red-zone`（关闭 SysV 红区假设——不能假设 `rsp-128` 区域安全）。
- **新增两个目标**：`kernel64.elf` 用 `ld -m elf_x86_64 -T kernel64.ld -nostdlib` 链接，
  随后 `objcopy -O binary` 剥成裸字节；`boot.o` 依赖 `kernel64.bin`（`.incbin` 编译期
  就要读它）。`check` 不变：校验**外层** ELF32 的 Multiboot2 header。

### 3.6 主控制流
```text
GRUB i386 handoff (EAX=magic, EBX=mbi)
  → _start: cli/建栈 → call kernel_main32
  → kernel.c: 验证 MBI → 填 handoff → setup_long_mode_tables(): 五页→四级→返回 PML4
  → boot.S: test eax → mov ebx,eax → enter_long_mode
        → LGDT → CR3=PML4 → CR4.PAE → EFER.LME → CR0.PG → ljmp $0x08
  → long_mode_start (手工编码 64 位桥接): 设 ds/es/ss → rsp 对齐 → rdi=handoff
        → call kernel_main64（=内嵌二进制起点）
  → kernel_main64_binary(h): 清屏 → banner → 轮询 8042 → exec64 命令循环
```

---

## 4. 数据流与运行逻辑
- **跨模式数据流**：32 位 `setup_long_mode_tables()` 填好的五张表帧、allocator
  游标/历史、kernel/stack/MBI 边界全部写进全局 `long_mode_handoff`；boot.S 用
  `mov rdi, imm32` 把该结构地址按 SysV 交给 `kernel_main64_binary`，64 位侧全程通过
  `h` 指针访问同一份内存（identity window 内物理==虚拟）。
- **命令数据流**：PS/2 scancode → `scan64()` → `cmd[]` → `exec64()` 分支 →
  `lminfo`/`palloc`/`mmap`/`pinfo` 各自打印。
- **输出串索引**（逐字抄录自 kernel64.c）：banner `TinyOS lesson 8: x86_64 long mode` +
  `64-bit C continuation active`；`help` → `commands: help about clear lminfo pinfo palloc mmap`；
  `lminfo` → `long mode: on` + 五行表帧 + `identity: 0000000000000000 - 0000000000400000`；
  `pinfo` → `page size: 0000000000001000` + `allocated pages: <hex>`；`palloc` →
  `allocated: <hex>` 或 `allocator exhausted`；`mmap` → `Multiboot2 available ranges:`
  后跟至多 6 行；未知 → `unknown command`；prompt → `tinyos> `。

---

## 5. 构建、运行与验证
依赖：`gcc`、`binutils`（含 `objcopy`，本课新增）、`grub-mkrescue`、`grub-file`、
`qemu-system-x86_64`。

```bash
cd lessons/lesson-08-stable
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
readelf -rW build/kernel64.elf      # 必须报告「没有重定位信息」
nm -u build/kernel.elf              # 必须无未定义符号
objdump -d -Mintel build/kernel.elf # 观察 32 位入口 + 内嵌 64 位代码
```

运行（成功画面在 QEMU 图形窗口，勿加 `-display none`）：`make run`。

在图形窗口输入：

```text
lminfo<Enter>
palloc<Enter>
lminfo<Enter>
mmap<Enter>
pinfo<Enter>
help<Enter>
about<Enter>
```

验收标准：banner 完整显示 `TinyOS lesson 8: x86_64 long mode` 与 `64-bit C continuation
active`；`lminfo` 显示 `long mode: on`、五个互不相同且 4 KiB 对齐的 64 位地址、
`identity: 0000000000000000 - 0000000000400000`；`palloc` 后表帧地址不变、`pinfo` 的
`allocated pages` 增加；`readelf -rW` 无 relocation；`nm -u` 无未定义符号。

> **本次实际验证记录（旧 README 保留）**：`-Werror` build 成功，`grub-file
> --is-x86-multiboot2` 通过；最终 ELF32 有 RX 与 RW 两个 LOAD 段、无 RWX LOAD segment；
> `nm -u` 无未定义符号；内嵌临时 ELF64 报告「没有重定位信息」。QEMU monitor `info
> registers` 确认 `CS64`、`CR0=80000011`、`CR3=0000000000107000`、`CR4=00000020`、
> `EFER=0000000000000500`——paging、PAE、LME/LMA 均有效。VGA 显示 64-bit C banner，
> PS/2 polling shell 接收 `lminfo` 等命令。

---

## 6. 调试地图
| 现象 | 原因 | 检查方法 |
|---|---|---|
| `grub-file` 失败 | Multiboot2 header 非法 | 检查 `.multiboot` 的 8 字节对齐和镜像前 32 KiB 内的位置 |
| setup 直接 halt | MBI 验证失败 | 检查 magic、MBI 对齐、type-6 request、tag size/rounding |
| 开启 PG 后 reset | 进入 long mode 顺序错 | 检查 CR3、PAE、LME 及 64-bit GDT descriptor 的 `L=1,D=0` |
| far jump 后 fault | CS selector/GDTR/jump target 问题 | 检查 code selector=0x08、GDTR base、跳转目标在 identity window |
| 64-bit C 未到达 | 调用目标错位 | 检查 `kernel_main64_binary` 是否位于 `kernel64.bin` offset 0 |
| shell banner 缺字 | VGA 未被映射 | 确认 PT0 覆盖 VGA `0xb8000` |
| stack fault | 栈设置不完整 | 确认 `stack_top` < 4 MiB 且 RSP 在 call 前 16 字节对齐 |
| `lminfo` 表帧交叠 | 分配历史未及时更新 | 检查每次 table allocation 后 `allocated_pages` 立即 +1 |
| `palloc` 重复页 | 半开区间重叠判断漏项 | 检查 kernel/stack/MBI/history 的 overlap predicate |
| `mmap` 乱码 | 步长假定错误 | 永远按 runtime `entry_size` 迭代，不假设固定步长 |
| raw binary relocation | `-fpie` 之外出现绝对地址 | 运行 `readelf -rW build/kernel64.elf`；必须没有 relocation |
| reset/triple fault 无信息 | 状态寄存器未知 | QEMU `info registers` 验证 `CS64`、CR0、CR3、CR4、EFER |

---

## 7. 与 Linux 源码对照
| TinyOS 教学模型 | Linux v6.12 对应实现 | 权威来源 | 简化了什么 |
|---|---|---|---|
| 手工编码 64 位桥接字节 | `arch/x86/kernel/head_64.S` 的早期纯汇编入口 | Intel SDM Vol.3（指令编码） | Linux 有完整 GDT/IDT/tss 汇编；本课只有 ~15 字节 |
| 五页四级表 identity 映射 4 MiB | `head_64.S` 的 `initial_page_table` 大范围映射 | Intel SDM Vol.3 4.5 | Linux 用 2 MiB huge page 静态定义；本课逐 PTE 填 4 KiB 页 |
| LGDT→PAE→LME→PG→ljmp | `head_64.S` 中 CR4/EFER/CR0 位运算顺序一致 | Intel SDM Vol.3 9.8.5 | Linux 还要开 `CR4.PCID`、设置 MSR 等 |
| `-fpie`+objcopy 裸二进制 | `arch/x86/boot/compressed` 的解压缩 stub | Linux 启动文档 | Linux 用 ELF 完整重定位；本课 `.incbin` 静止嵌入 |
| handoff 结构跨段传参 | `head_64.S` 的 `boot_params` 与寄存器传参 | Linux ABI | Linux 传整个 `boot_params`；本课结构更小 |

---

## 8. 思考题与练习
1. **概念理解**：为什么进入 long mode 必须先置 `CR4.PAE`？跳过 PAE 直接置 `EFER.LME`
   再开 `CR0.PG` 会发生什么？（提示：Intel SDM 的 #GP 条件）
2. **源码定位**：far jump 的目标是哪个标签？它为什么一定指向 `.text64` 段
   （提示：`kernel64.bin` 的 offset 0）？
3. **动手实验**：对调 `kernel64.ld` 中 `.text64.entry` 与 `.text64` 的顺序重新构建，
   观察现象并用调试地图第 5 行解释。
4. **动手实验**：把 code descriptor 改成 `0x00af98000000ffff`（L=0）重新构建，预测并
   验证 far jump 后的行为。
5. **Linux 对照**：比较 `arch/x86/kernel/head_64.S` 用 2 MiB huge page 与本课用 4 KiB
   page 的取舍。

---

## 9. 本课小结与下一课预告
**小结**：
1. 本课完成「GRUB i386 ABI 装载 → TinyOS 自己切到 long mode」的完整状态转换，验证了
   「ELF loader ABI ≠ 最终执行模式」。
2. 64 位分页是四级结构：PML4/PDPT/PD/PT，每级表 512 项 × 8 字节；本课用五张 4 KiB 页
   映射 `[0, 4 MiB)`。
3. 进入 long mode 有严格顺序：`LGDT → CR3 → CR4.PAE → EFER.LME → CR0.PG → far jump`，
   任何一步颠倒都会在置 PG 瞬间出问题。
4. far jump 之后的桥接指令是手工编码的 `.byte` 机器码，因为外层镜像仍是 ELF32、
   汇编器仍在 `.code32` 模式。
5. 双段构建管线：`-m64 -fpie -mno-red-zone` 编译 → `ld` 链 `kernel64.elf` →
   `objcopy -O binary` 剥裸字节 → `.incbin` 塞进 ELF32。
6. `kernel64.ld` 的 `. = 0` 是裸二进制调用契约的基石：入口函数必须位于 offset 0，
   `readelf -rW` 必须无 relocation。
7. 32 位与 64 位代码通过 `struct long_mode_handoff` 共享分配历史，防止重复分配。

**下一课**：[`lesson-09-stable/README.md`](../lesson-09-stable/README.md) 将在 long mode
上建立第一个异常 IDT：为 `#UD` 与 `#PF` 提供终止诊断，让「无诊断重启」变成「可打印的
错误原因」。本课的四级页表和 handoff 机制正是第九课异常处理得以定位故障的基础设施。

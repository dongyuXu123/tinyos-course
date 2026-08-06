# Lesson 24: 运行时 GDT/TSS、rsp0 与 #PF IST 异常栈 — 精讲文档

> **课号**：Lesson 24（可执行课）
> **主题**：在内核开中断前，用自建的高地址 **运行时 GDT**（空描述符 + 内核代码/数据 +
> 64 位可用 TSS 描述符）加载任务寄存器（`ltr`），配置 TSS 的 **`rsp0`**（为将来 CPL3→CPL0
> 切换准备的入口栈）与 **IST1**（独立静态异常栈），并让 **#PF 走 IST1**、其余异常/IRQ 保持
> IST0。本课是纯内核（CPL0）准备，**不进入 CPL3**。
> **课程主线位置**：调度与同步阶段的收尾与「特权级可靠性」的开端（Lesson 23 独立 idle →
> **本课 TSS/rsp0/IST** → Lesson 25 起用户态与地址空间）。在引入用户模式前，必须先把
> 「中断/异常时的内核栈从哪来」这个问题钉死。
> **前置课程**：[`lesson-23-stable/README.md`](../lesson-23-stable/README.md)（独立 idle context 与 IRQ0 回退）
> **后续课程**：[`lesson-25-stable/README.md`](../lesson-25-stable/README.md)（内核栈守卫页与受控运行时映射基础）
> **一句话目标**：学完本课你能说出并演示：`runtime_gdt_tss_init` 如何构造 16 字节 TSS
> 描述符、`ltr` 后 TR 指向哪里、`rsp0` 与 `ist1` 各指向哪块静态栈、#PF 门为何单独
> `IST=1`，以及 `isttest` 如何通过 IST1 处理一次致命 #PF 并打印「被中断栈指针」与
> 「handler 栈指针」两个独立地址。

---

## 1. 课程定位（Mission）

**一句话目标**：建立 x86_64 特权级切换所需的全部**内核侧**基础设施：运行时 GDT、
64 位 TSS（`rsp0` + 7 个 IST 槽）、`ltr` 装载、IDT 的 IST 分配，并用可观测命令
（`tssinfo`/`isttest`）验证。

- **在课程主线中的位置**：这是「用户模式之前」的最后一层准备。CPU 在 CPL3→CPL0 的
  中断/异常转移时会从 TSS 读取 `rsp0` 作新栈；而 IST 则让特定异常（本课 #PF）永远在
  独立的已知栈上处理——即使被中断的内核栈本身已损坏。这两者都只与硬件机制相关，
  不依赖任何用户代码，因此必须**先于** CPL3 建立。
- **前置知识清单**：
  1. GDT 描述符位布局与 64 位系统描述符（Intel SDM Vol.3 3.4/3.5，见 `boot.S` 的
     `gdt64` 与 `lgdt` 流程）；
  2. `ltr`/任务寄存器、TSS 结构（`rsp0` 偏移 4、`ist1` 偏移 36、iomap 偏移 102）；
  3. IDT 门描述符的 IST 字段（`struct idt_gate.ist`）与中断门语义；
  4. 异常帧：普通 `exception_frame`（vector/error/rip/cs/rflags）vs 经 IST 的
     `exception_frame_ist`（多了被保存的 `rsp`/`ss`）。
- **本课交付**：`runtime_gdt_tss_init`（lgdt+ltr）、两个静态页对齐栈
  （`tss_rsp0_stack`/`exception_ist_stack`）、#PF 的 IST1 路径、`exception_report_ist`
  致命报告、`tssinfo`/`isttest` 命令、`_Static_assert` 编译期布局校验。

---

## 2. 核心概念精讲

### 2.1 为什么内核需要自己的 GDT 与 TSS

`boot.S` 的引导 GDT 只有三个条目：null、`0x08` 内核代码、`0x10` 内核数据——它足以进
long mode 并运行内核，但**没有 TSS 描述符**，任务寄存器无法加载。而 x86_64 的中断/
异常转移在两个场景需要 TSS：

- **特权级切换**：CPL3 → CPL0 时，CPU 从 TSS 读 `rsp0` 作新栈（本课不进入 CPL3，
  因此 `rsp0` 只配置、不被消费）；
- **IST（Interrupt Stack Table）**：门描述符的 IST 字段非 0 时，CPU 不沿用当前 RSP，
  而是改用 TSS 中对应 `istN` 的栈顶。

所以本课构建**运行时 GDT**（`runtime_gdt[6]`）与 **TSS**（`runtime_tss`），并 `lgdt`
+ `ltr` 激活。

### 2.2 TSS 结构：rsp0 与 7 个 IST

```c
struct tss64 { u32 reserved0;
               u64 rsp0,rsp1,rsp2,reserved1;
               u64 ist1,ist2,ist3,ist4,ist5,ist6,ist7;
               u64 reserved2;
               u16 reserved3,iomap_base;
} __attribute__((packed));
```

- `rsp0`（偏移 4）：CPL3→CPL0 转移时的新栈顶。本课指向 `tss_rsp0_stack` 的页顶
  （`stack_top64(tss_rsp0_stack)`）。CPL3 尚不存在，硬件永不消费它。
- `ist1..ist7`（偏移 36 起，各 8 字节）：IST 栈顶指针。本课只配 `ist1`
  （= `exception_ist_stack` 页顶），给 #PF 用。
- `iomap_base`（偏移 102）：IO 位图基址，设为 `sizeof(runtime_tss)`（104，即无位图）。
- 源码用五条 `_Static_assert` 在编译期锁死布局：
  ```c
  _Static_assert(sizeof(struct tss64)==104,"64-bit TSS");
  _Static_assert(__builtin_offsetof(struct tss64,rsp0)==4,"TSS rsp0 offset");
  _Static_assert(__builtin_offsetof(struct tss64,ist1)==36,"TSS ist1 offset");
  _Static_assert(__builtin_offsetof(struct tss64,iomap_base)==102,"TSS iomap offset");
  _Static_assert(sizeof(struct exception_frame_ist)==56,"IST frame");
  ```

### 2.3 16 字节 TSS 描述符的位拼接

64 位模式下系统描述符占 **16 字节**（两个 GDT 槽），`runtime_gdt_tss_init` 手工拼装：

```c
u64 base=(u64)(unsigned long)&runtime_tss,limit=sizeof(runtime_tss)-1;  // limit=103
...
runtime_gdt[3]=(limit&0xffffULL)          |   // 低 qword 位 15:0  limit[15:0]
              ((base&0xffffffULL)<<16)   |   // 位 39:16        base[23:0]
              (0x89ULL<<40)              |   // 位 47:40        type=0x89（P=1,DPL=0,64位可用TSS）
              (((limit>>16)&0xfULL)<<48) |   // 位 51:48        limit[19:16]
              (((base>>24)&0xffULL)<<56);    // 位 63:56        base[31:24]
runtime_gdt[4]=base>>32;                     // 高 qword          base[63:32]
```

- `0x89` = `1000_1001`：Present=1、DPL=00、type=0b1001（64 位**可用** TSS）。
- 组装后 GDTR：`limit=sizeof(runtime_gdt)-1`（47）、`base=&runtime_gdt`；`lgdt` 加载。
- 随后重载 `DS/ES/SS=0x10`（沿用内核数据段），并 `ltr` 装载 `TSS_SELECTOR(0x18)`。

### 2.4 IDT 的 IST 分配：#PF 独享 IST1

```c
static TEXT64 void set_gate(struct idt_gate *g,u64 target,u8 ist){
    g->offset_low=(u16)target; g->selector=KERNEL_CS;
    g->ist=ist;                  // 门描述符的 IST 字段
    g->type=IDT_GATE_INTERRUPT;
    g->offset_mid=(u16)(target>>16); g->offset_high=(u32)(target>>32);
    g->reserved=0;
}
...
set_gate(&idt[3], runtime_bp_address(),0);       // #BP   IST0（可恢复 iretq）
set_gate(&idt[6], runtime_ud_address(),0);       // #UD   IST0
set_gate(&idt[14],runtime_pf_address(),IST1_INDEX); // #PF  IST1（独立栈）
set_gate(&idt[0x20],runtime_irq0_address(),0);   // PIT   IST0（调度帧路径不变）
set_gate(&idt[0x21],runtime_irq1_address(),0);   // 键盘  IST0
```

只有 #PF 走 IST1，理由：页故障可能由**内核栈自身的越界/坏指针**引发，此时当前 RSP
不可信；IST 保证 handler 一定在 `exception_ist_stack` 上执行。而 IRQ0/IRQ1 继续用 IST0
（沿用当前线程/栈帧），从而保留「IRQ0 唯一帧选择 + 共享 `iretq`」的既有调度结构。

### 2.5 经 IST 的 #PF 帧：多出 rsp/ss

普通异常（IST0）的帧是 `struct exception_frame{vector,error,rip,cs,rflags}`；经 IST 的
转移中，CPU 额外把**被中断的旧 RSP 与 SS** 压栈（因为发生了栈切换）：

```c
struct exception_frame_ist { u64 vector,error,rip,cs,rflags,rsp,ss; };
```

`exception_pf` 汇编把 vector（14）压到硬件 error code 之下，`exception_report_ist`
据此打印**两把栈指针**：

- `saved rsp`：`f->rsp`——被中断时的内核栈指针（可能是损坏/越界的栈）；
- `handler rsp`：当前 `rsp`（`mov %%rsp` 读出）——必在 IST1 区间内；
- `IST1 range`：`exception_ist_stack` 基址到 `runtime_tss.ist1` 顶。

两值不同即证明「处理在独立异常栈上进行」。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-23-stable） |
|---|---|---|
| `boot.S` | 32 位入口、引导 GDT、long mode、`.incbin` blob | 未变化（引导 GDT 仍只 0x08/0x10） |
| `kernel.c` | 32 位引导阶段 | 未变化 |
| `kernel64.c` | 64 位内核本体 | **全部本课增量**（GDT/TSS/IST） |
| `kernel64.ld` | 64 位裸 blob 布局 | 未变化（新增静态栈落入 `.data`） |
| `linker.ld` | 外层 ELF32 段布局 | 未变化 |
| `Makefile` | 双阶段构建、ISO、check/run | 未变化 |
| `grub.cfg` | GRUB 菜单 | 仅 `menuentry` 标题改为 "TinyOS lesson 24: TSS, rsp0, #PF IST stack" |

> 备注：`kernel64.c` 首行注释仍为 `/* Lesson 21: ... */`（陈旧注释，编译与行为无关），
> 本精讲如实标注；其余文本（`about`/横幅/`help`）均已更新到 lesson 24。

### 3.2 kernel64.c 增量精讲

（说明：源码为单行风格，下列代码块按语句重排以便逐行注释，每条语句与源码逐字一致；
省略与 Lesson 23 相同的调度/队列/PMM/键盘部分。）

#### 3.2.1 新常量、结构体与静态栈

```c
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define TSS_SELECTOR 0x18
#define IST1_INDEX 1
struct gdtr { u16 limit; u64 base; } __attribute__((packed));
struct tss64 { u32 reserved0; u64 rsp0,rsp1,rsp2,reserved1;
               u64 ist1,...,ist7; u64 reserved2;
               u16 reserved3,iomap_base; } __attribute__((packed));
struct exception_frame_ist { u64 vector,error,rip,cs,rflags,rsp,ss; };
...
static u8 tss_rsp0_stack[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));  // rsp0 预留栈
static u8 exception_ist_stack[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE))); // IST1 异常栈
static struct tss64 runtime_tss;
static u64 runtime_gdt[6] __attribute__((aligned(16)));
static struct gdtr runtime_gdtr;
```

- 两个栈都是 continuation 常驻静态数组：**不属于 PMM、不属于 worker 栈、不属于 idle**。
- `runtime_gdt[6]`：槽 0=null、1=内核代码、2=内核数据、3-4=TSS 描述符、5=保留。

#### 3.2.2 runtime_gdt_tss_init（本课核心）

```c
static TEXT64 void runtime_gdt_tss_init(void){
    u64 base=(u64)(unsigned long)&runtime_tss,limit=sizeof(runtime_tss)-1;
    u16 selector=TSS_SELECTOR;
    u32 i;
    for(i=0;i<6;i++) runtime_gdt[i]=0;                 // 清零整张 GDT
    runtime_gdt[1]=0x00af9a000000ffffULL;              // 内核代码段（与引导 GDT 相同）
    runtime_gdt[2]=0x00af92000000ffffULL;              // 内核数据段
    runtime_tss.reserved0=0;
    runtime_tss.rsp0=stack_top64(tss_rsp0_stack);      // rsp0 → 静态入口栈顶
    runtime_tss.rsp1=runtime_tss.rsp2=runtime_tss.reserved1=0;
    runtime_tss.ist1=stack_top64(exception_ist_stack); // IST1 → 静态异常栈顶
    runtime_tss.ist2=...=runtime_tss.ist7=0;
    runtime_tss.reserved2=0;
    runtime_tss.reserved3=0;
    runtime_tss.iomap_base=sizeof(runtime_tss);        // 104：无 IO 位图
    runtime_gdt[3]=(limit&0xffffULL)|((base&0xffffffULL)<<16)|
                   (0x89ULL<<40)|(((limit>>16)&0xfULL)<<48)|
                   (((base>>24)&0xffULL)<<56);         // 16 字节描述符低 qword
    runtime_gdt[4]=base>>32;                           // 高 qword（base[63:32]）
    runtime_gdtr.limit=sizeof(runtime_gdt)-1;
    runtime_gdtr.base=(u64)(unsigned long)runtime_gdt;
    __asm__ volatile("lgdt %0"::"m"(runtime_gdtr):"memory");        // 加载运行时 GDT
    __asm__ volatile("movw $0x10,%%ax; movw %%ax,%%ds; movw %%ax,%%es; movw %%ax,%%ss"
                     :::"ax","memory");               // 重载数据段（基址可能已变）
    __asm__ volatile("ltr %0"::"r"(selector):"memory");              // 装载任务寄存器
}
```

逐层分析：

1. **为什么重载 DS/ES/SS？** `lgdt` 只改 GDTR；段寄存器缓存里的基址仍是旧值。
   运行时 GDT 的内核数据描述符与引导 GDT 语义相同（`0x00af92000000ffff`），但为
   消除对旧 GDT 的依赖，显式把 DS/ES/SS 重载为 `0x10`。CS 由调用上下文保持。
2. **TSS 描述符手工位拼接**：见 2.3 节；`0x89` 是 P=1、DPL=0、64 位可用 TSS 的类型字节。
3. **`ltr` 之后**：任务寄存器 TR = 0x18，CPU 缓存 TSS 基址/限长；后续 #PF 经 IST1
   时从 `runtime_tss.ist1` 取栈顶。`tssinfo` 用 `str` 读回 TR 验证。

#### 3.2.3 install_idt 的 IST 分配与 set_gate 签名变化

`set_gate` 增加 `ist` 形参；`install_idt` 里 **只有 `#PF` 传 `IST1_INDEX`**（见 2.4 节）。
这使 `idtinfo` 新增一行：

```c
text64(c,"#PF IST: 0000000000000001\n");
```

#### 3.2.4 exception_pf 汇编与 exception_report_ist（本课核心）

```asm
.global exception_pf
exception_pf:
pushq $14                ; vector=14 压到硬件 error code 之下
movq %rsp,%rdi
andq $-16,%rsp
call exception_report_ist  ; 不再走 exception_common 的通用报告
```

```c
TEXT64 void exception_report_ist(struct exception_frame_ist*f){
    u16 c=0; u64 cr2=0,rsp;
    clear64(&c);
    __asm__ volatile("mov %%rsp,%0":"=r"(rsp));            // handler 当前 RSP
    text64(&c,"TinyOS lesson 24 IST exception\nexception: #PF\n");
    text64(&c,"vector: ");  hex64(&c,f->vector);
    text64(&c,"\nerror:  "); hex64(&c,f->error);
    text64(&c,"\nrip:    "); hex64(&c,f->rip);
    text64(&c,"\nsaved rsp: "); hex64(&c,f->rsp);          // 被中断的旧内核栈指针
    text64(&c,"\nhandler rsp: "); hex64(&c,rsp);          // 当前（必在 IST1 内）
    text64(&c,"\nIST1 range: ");
    hex64(&c,(u64)(unsigned long)exception_ist_stack);     // IST1 基址
    text64(&c," "); hex64(&c,runtime_tss.ist1);            // IST1 栈顶
    __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
    text64(&c,"\ncr2:    "); hex64(&c,cr2);
    text64(&c,"\nCPU halted intentionally.\n");
    for(;;)__asm__ volatile("cli; hlt");                   // 致命：有意停机
}
```

- **两把栈指针对照**是核心证据：`saved rsp`（`f->rsp`，来自 CPU 帧）是被 #PF 打断时的
  内核栈指针；`handler rsp` 是从当前 `rsp` 读出的值。两者不同且后者落在打印的
  `IST1 range` 内，即证明异常在独立 IST 栈上处理。
- `#UD`/其他仍走 `exception_common` → `exception_report`（IST0 路径不变）；
  `#BP` 仍可恢复（`bptest` 返回 shell）。

#### 3.2.5 tssinfo / isttest 命令与启动序列

```c
static TEXT64 void tssinfo(u16*c,struct long_mode_handoff*h){
    u16 tr; struct idt_gate *idt=(struct idt_gate *)(unsigned long)phys_to_high(h->idt_address);
    __asm__ volatile("str %0":"=r"(tr));                    // 读任务寄存器
    text64(c,"TSS/IST: kernel-only transition preparation\nTR: "); hex64(c,tr);
    text64(c,"\nGDTR base/limit: "); hex64(c,runtime_gdtr.base); hex64(c,runtime_gdtr.limit);
    text64(c,"\nTSS base: "); hex64(c,(u64)(unsigned long)&runtime_tss);
    text64(c,"\nrsp0 top: "); hex64(c,runtime_tss.rsp0);
    text64(c,"\nIST1 top: "); hex64(c,runtime_tss.ist1);
    text64(c,"\n#PF IST: "); hex64(c,idt[14].ist&7);        // 应为 1
    text64(c,"\nIRQ0/IRQ1 IST: "); hex64(c,idt[0x20].ist&7);
    hex64(c,idt[0x21].ist&7);                               // 应均为 0
    text64(c,"\nCPL3 entry: not implemented\n");
}
```

- `isttest`（仅全新启动、致命）：
  ```c
  text64(c,"isttest: triggering #PF on IST1 (fatal)\n");
  p=*bad;   // 读未映射的 DYNAMIC_TEST_VA → #PF → IST1 → exception_report_ist
  ```
- `kernel_main64_binary` 在 `cli` 之后、`idle_init()` 之前调用
  `runtime_gdt_tss_init();`——**先有 TSS/IST，再有任何中断可能发生**。启动横幅改为
  `TinyOS lesson 24: TSS, rsp0 and #PF IST stack` + `Kernel-only preparation; IRQ0
  return-frame switching retained`。

### 3.3 构建管线

与 Lesson 23 完全一致。新增静态对象（`tss_rsp0_stack`/`exception_ist_stack`/
`runtime_tss`/`runtime_gdt`）全部落在 `kernel64.ld` 的 `.data`（PROGBITS），随
`objcopy -O binary` 持久化。`_Static_assert` 是编译期检查，`-Wall -Wextra -Werror`
下任何布局偏差直接编译失败——这是「TSS 布局」的自动化守护。

### 3.4 主控制流

```text
kernel_main64_binary → cli
  → runtime_gdt_tss_init()（lgdt 运行时 GDT、重载 DS/ES/SS、ltr → TR=0x18）
  → idle_init() → install_idt()（#PF 门 IST=1，其余 IST=0）→ pit_init/pic_init
  → 横幅 → shell 循环
  → tssinfo：TR=0x18、GDTR/TSS 高地址、rsp0/ist1 栈顶、#PF IST=1、IRQ0/1 IST=0
  → isttest（全新启动）：读 DYNAMIC_TEST_VA → #PF → 经 IST1 换栈
      → exception_report_ist 打印 saved rsp / handler rsp / IST1 range / cr2
      → cli;hlt 有意停机（致命）
```

---

## 4. 数据流与运行逻辑

1. 键入 `tssinfo`：`str` 读回 TR=0x18；`GDTR base/limit` 显示运行时 GDT 高地址与限长
   47；`TSS base` 显示 `runtime_tss` 高地址；`rsp0 top` 为 `tss_rsp0_stack` 页顶、
   `IST1 top` 为 `exception_ist_stack` 页顶；`#PF IST` 为 1、`IRQ0/IRQ1 IST` 为 `0 0`；
   末尾 `CPL3 entry: not implemented`。
2. 正常流程验证：`bptest` 触发 #BP（IST0）→ `breakpoint_report` 打印
   `returning with iretq...` 并返回 shell——证明非 IST 异常路径未受损。
3. 全新启动运行 `isttest`：先打印 `isttest: triggering #PF on IST1 (fatal)`，然后 CPU
   经 IST1 切换到 `exception_ist_stack` 压栈 `saved rsp`，`exception_report_ist` 打印
   两把栈指针与 `IST1 range`、CR2，最后 `CPU halted intentionally.` 停机——有意结束启动。
4. `vmfaulttest`/`pftest` 同样经 #PF/IST1 致命；`udtest` 走 IST0 致命（`exception:
   #UD`）。
5. 回归：`idletest`（shell 睡 150 tick + idle 顶班）、`pctest`/`pcgo`/`pcinfo`
   （event 广播 + 生产者—消费者）、抢占/睡眠/键盘等待/PMM/VM 映射全部保持正常。

---

## 5. 构建、运行与验证

### 5.1 依赖

与 Lesson 23 相同（gcc、binutils、grub-common、xorriso、mtools、qemu-system-x86_64），
无新增工具。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check                     # 期望输出: Multiboot2 header check passed.
readelf -rW build/kernel64.elf # 期望: continuation 无内部重定位
nm -u build/kernel64.elf       # 期望: 无未定义符号
readelf -SW build/kernel64.elf # 期望: .data 为 PROGBITS（含 TSS/两静态栈/GDT）
objdump -d -Mintel build/kernel64.elf  # 期望: 可见 lgdt/ltr；exception_pf 调 exception_report_ist
readelf -lW build/kernel.elf   # 期望: 外层无 RWX LOAD 段
```

静态验收：continuation 无重定位/未定义符号；可写状态物化；外层 Multiboot ELF 无 RWX
LOAD 段；确认运行时 `lgdt` 与 `ltr` 指令、#PF 门 IST 字节为 1、IRQ0 仍是一条
「单帧选择 + 共享 `iretq`」。raw continuation 链接器自身的 RWX 警告仍与受检外层 ELF
无关。

### 5.3 运行与 VGA 验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅逐字如下（来自
`kernel_main64_binary`）：

```text
TinyOS lesson 24: TSS, rsp0 and #PF IST stack
Kernel-only preparation; IRQ0 return-frame switching retained
tinyos> 
```

验证步骤（保留并扩充旧 README 的 VGA 验收流程）：

1. 正常启动，运行 `tssinfo`，核对：
   - `TR` = 0x18；
   - `GDTR base/limit` 为高别名 GDT 地址与 47；
   - `TSS base`/`rsp0 top`/`IST1 top` 均为高别名静态地址；
   - `#PF IST` = 1、`IRQ0/IRQ1 IST` = `0 0`；
   - `CPL3 entry: not implemented`。
2. 运行 `bptest`：必须返回 shell（`#BP returned to shell`），证明 #BP 仍用 IST0 且
   可恢复 `iretq` 完好。
3. 回归 `idletest`、`pctest`/`pcgo`/`pcinfo`、抢占（`preempttest`）、睡眠
   （`sleeptest`）、键盘等待（`kbdwaittest`）、PMM/VM（`vmtest`）、正常 shell 键盘输入。
4. **全新 QEMU 启动**运行 `isttest`：致命 VGA 屏必须标识 #PF，`handler rsp` 落在打印
   ​​的 `IST1 range` 内，并分别显示 `saved rsp`（被中断栈指针）——这是预期终止，结束该启动。
5. `vmfaulttest`/`pftest` 同为致命的全新启动 #PF/IST 测试；`udtest` 是全新启动的
   IST0 致命测试。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `tssinfo` 显示 `TR` 非 0x18 | `ltr` 未执行或 selector 错 | 确认 `runtime_gdt_tss_init` 中 `ltr` 用 `TSS_SELECTOR(0x18)` |
| 触发 #PF 后 triple fault（QEMU 重启/黑屏） | #PF 门 IST 字节非 1，或 `runtime_tss.ist1` 未设 | `readelf`/`gdb` 查 `exception_pf`；确认 `set_gate(&idt[14],...,IST1_INDEX)` 与 `ist1=stack_top64(...)` |
| `tssinfo` 显示 `#PF IST: 0` | `install_idt` 用了旧 `set_gate`（无 ist 参数） | 确认 `set_gate` 签名含 `ist` 且 #PF 传 `IST1_INDEX` |
| `exception_report_ist` 的 `handler rsp` 不在 `IST1 range` 内 | 异常实际没走 IST1（帧/门配置不符） | 核对 `exception_pf` 汇编与 `idt[14].ist`；对照 `saved rsp` 与 `handler rsp` |
| 编译失败于 `_Static_assert` | `struct tss64` 布局与 Intel SDM 偏移不符 | 检查 packed 声明与字段顺序；对照 rsp0@4、ist1@36、iomap@102、size 104 |
| 内核对 0x10 数据段的访问异常 | `lgdt` 后未重载 DS/ES/SS，段缓存基址过期 | 确认 `movw $0x10,%%ax; movw %%ax,%%ds/%%es/%%ss` 存在 |
| `bptest` 不再返回 shell | #BP 门被误配 IST 或入口被改 | `idtinfo` 看 `#BP vector`；确认 `set_gate(&idt[3],...,0)` |
| `isttest` 停在普通异常报告而非 IST 报告 | `exception_pf` 没调 `exception_report_ist` | 反汇编：`exception_pf` 应 `call exception_report_ist`（不再 `jmp exception_common`） |
| `about` 显示 lesson 22/23 | 源码文本未随课更新 | 本课 `about` 已更新为 lesson 24；首行注释陈旧属源码自身问题 |

---

## 7. 与 Linux 源码对照

- **per-cpu TSS**：Linux 每 CPU 一个 `tss_struct`（`arch/x86/include/asm/processor.h`），
  内含 `x86_tss`，经 `load_TR_desc`/`load_sp0` 更新 `rsp0` 并在任务切换时改 `ist`；
  TinyOS `runtime_tss`/`ltr` 是其单 CPU 教学版。
- **`rsp0` 更新**：Linux `arch/x86/kernel/process_64.c::__switch_to` 在切换任务时写
  `cpu_tss_rw->x86_tss.sp0`（现在为 `RSP0`）；TinyOS 本课静态配置一次，无 per-thread
  `rsp0`（旧 README 明示「无 per-thread `rsp0` 更新」）。
- **IST 与 double fault**：Linux 的 `#DF`（vector 8）使用 IST1（`tss.ist[1]`），
  避免在损坏的栈上二次故障；TinyOS 把 IST1 分配给 #PF 是同一「已知栈处理不可信栈」
  思想的另一种分配。
- **异常帧含旧 RSP**：Intel SDM Vol.3 6.14 规定栈切换异常会压入旧 SS/RSP；
  TinyOS `struct exception_frame_ist` 精确对应。
- **`_Static_assert` 守护布局**：Linux 用 `BUILD_BUG_ON`/`offsetof` 检查结构布局
  （如 `arch/x86/kernel/asm-offsets.c`）；TinyOS 的五条 `_Static_assert` 同思路。
- **教学模型简化了什么**：单 CPU 单 TSS、无 per-thread `rsp0`、无用户段选择子、
  无 CPL3 `iretq`、无 syscall ABI、无 IO 位图、无 `#DF` 的独立栈。

---

## 8. 思考题与练习

1. （概念理解）为什么 #PF 需要独立 IST 栈，而 IRQ0/IRQ1 保持 IST0？如果 #PF 也在当前
   栈上处理，什么样的故障会让系统二次崩溃？
2. （源码定位）`runtime_gdt_tss_init` 中 `0x89` 是什么？TSS 描述符为什么占两个 GDT
   槽？`iomap_base=sizeof(runtime_tss)` 的含义是什么？
3. （动手实验）把 `set_gate(&idt[14],...)` 的 IST 参数改成 0，重新构建并在全新启动跑
   `isttest`，观察 `exception_report_ist` 的 `handler rsp` 是否还落在 `IST1 range` 内。
4. （动手实验）把 `struct tss64` 去掉 `__attribute__((packed))`，观察 `_Static_assert`
   是否编译失败，解释对齐填充对偏移的影响。
5. （Linux 对照）Linux 为何把 IST1 分配给 double-fault 而非 page-fault？对比 TinyOS
   把 IST1 给 #PF 的教学取舍，说明各自场景下的理由。

---

## 9. 本课小结与下一课预告

**小结**：本课在纯内核（CPL0）侧补齐了特权级切换的硬件前提：`runtime_gdt_tss_init`
清零并重建运行时 GDT（null + 内核代码/数据 + 16 字节可用 TSS 描述符），`lgdt` 后重载
DS/ES/SS 并 `ltr` 装载任务寄存器；`runtime_tss` 配置了 `rsp0`（面向未来 CPL3→CPL0 的
入口栈）与 `ist1`（独立静态异常栈），五条 `_Static_assert` 在编译期锁死 Intel SDM 布局；
IDT 中只有 #PF 走 IST1，其余异常与 IRQ 保持 IST0，从而保留 IRQ0 单帧选择/共享 `iretq`
的调度结构；`exception_pf` 经 `exception_report_ist` 打印 `saved rsp`/`handler rsp`/
`IST1 range` 三份证据；`tssinfo`/`isttest` 让 TR、GDTR、TSS、IST 分配全部可观测。

**下一课预告**：[`lesson-25-stable/README.md`](../lesson-25-stable/README.md) 将基于本课
的 TSS/IST 基础，为内核栈加**守卫页**（guard page）与受控的运行时映射机制——让越界写
内核栈立刻触发 #PF（经 IST1 定位），为后续引入用户态地址空间与进程切换提供可靠边界。

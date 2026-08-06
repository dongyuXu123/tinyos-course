# Lesson 09: 在 x86_64 long mode 中安装最小异常 IDT — 精讲文档

> **课号**：Lesson 09（可执行课，x86_64 阶段）
> **主题**：在第八课的真实 64 位 continuation 上安装 exception-only IDT，把 `#UD`
> （无效指令）与 `#PF`（缺页）从「无诊断重启」变成「可见、可复现的 VGA 诊断」，
> 并首次引入汇编 stub + C reporter 的异常处理分工。
> **课程主线位置**：第 4 阶段（异常与中断）的第一课；异常 gate 机制是后续可恢复 #BP、
> 8259A PIC、IRQ 键盘驱动的公共地基。
> **前置课程**：[`lesson-08-stable/README.md`](../lesson-08-stable/README.md)（long mode
> 四级页表 + handoff + `.incbin` 双段构建）
> **后续课程**：[`lesson-10-stable/README.md`](../lesson-10-stable/README.md)（可恢复 #BP：
> `int3` → report → `iretq` 返回 shell）
> **一句话目标**：学完本课你能说清「CPU 异常从触发到 VGA 打印」的完整路径：CPU 压 frame
> → 查 IDT gate → 跳 stub → 整理 frame → C reporter 打印 → 有意停机。

---

## 1. 课程定位（Mission）

**一句话目标**：为 long mode 安装 256 项 IDT，填充 vector 6（#UD）与 vector 14（#PF）
两个 gate，让 `udtest`（执行 `ud2`）与 `pftest`（读 `0x00400000`）各产生一份含
vector、error code、RIP/CS/RFLAGS、（#PF 时）CR2 的诊断报告，然后有意停机。

- **在课程主线中的位置**：第八课已能跑 64 位 C，但 CPU exception 没有课程自己的可见
  交付路径——任何错误都是无声重启。本课第一次让 CPU 的异常机制有「输出」。后续的
  #BP 恢复、PIC 中断、键盘 IRQ 全部复用本课的 IDT 结构与 gate 装配逻辑。
- **前置知识清单**：
  1. 第八课：long mode 进入顺序、四级页表、`long_mode_handoff`、`.incbin` 双段构建、
     裸二进制无 relocation 的约束；
  2. Intel 异常模型：vector 号、#UD/#PF 的语义、有/无 error code 的异常清单；
  3. SysV x86_64 调用约定：参数走 RDI/RSI…、调用前 RSP 16 字节对齐、RIP 相对寻址；
  4. 长模式 IDT gate 的 16 字节布局（offset_high 字段是 64 位专属）。
- **本课交付**：新命令 `idtinfo`、`udtest`、`pftest`；`exception_report` 的诊断输出；
  以及「终止型教学 handler」的范式——报告后 `cli; hlt`，不做恢复。

---

## 2. 核心概念精讲

### 2.1 概念一：IDT 与 IDT gate

**直觉**：IDT（Interrupt Descriptor Table）是 CPU 的中断/异常"查号台"。异常或中断到来
时，CPU 拿 vector 号当索引，从 IDT 里取出一项 16 字节的 gate，跳到 gate 指定的 handler。

**准确定义**：`IDTR` 寄存器由 `lidt` 指令加载，含 16 位 limit 与 64 位 base。long mode
下 IDT 可容纳 256 项，每项 16 字节。本课 gate 类型 `0x8e` = interrupt gate（P=1, DPL=0,
type=0xe）。IDT 内存由 32 位 setup 在 `.bss` 中预留的 `idt_backing_store[4096]`
（`16 字节 × 256 项 = 4096 字节`，16 字节对齐）提供，地址经 handoff 传给 64 位侧：

```text
IDTR.base  = handoff.idt_address
IDTR.limit = 256 × 16 - 1 = 0x0fff

IDT[6]  → exception_ud   （#UD，CPU 不压 error code）
IDT[14] → exception_pf   （#PF，CPU 已压 error code）
```

### 2.2 概念二：硬件 frame 与 stub 的补充压栈

**直觉**：异常发生时 CPU 自动往栈里压入「返回现场」，但不同的异常压的东西不一样——
有的压 error code，有的不压。handler 必须知道这一点，否则 frame 就错位了。

**工作机制**：CPU 在跳进 handler 之前压入 `RFLAGS → CS → RIP`（#PF 还会额外先压
`error code`）。stub 的职责是把 frame 补成统一形状：

```text
#UD: stub push synthetic error=0; push vector=6
#PF: CPU push error code;      stub push vector=14

RSP → vector | error | RIP | CS | RFLAGS   （struct exception_frame 的字段顺序）
```

为什么 #UD 要补压 0：无 error code 的异常在栈上只有 `RIP/CS/RFLAGS`，若 reporter 统一按
「vector 在前」读，就必须由 stub 先补一个假 error 0 占位。为什么 #PF 不能补：CPU 已经压了
真实 error code（P/W/U 位），再补一个就多压一层、frame 全错。`exception_frame` 结构体
的字段顺序 `{vector, error, rip, cs, rflags}` 与栈上布局严格一致。

### 2.3 概念三：为什么 gate 里不能填链接期符号值（RIP-relative 解法）

**问题**：`kernel64.elf` 在地址零链接，`exception_ud` 的链接期绝对符号值 ≈ 0。
若把它直接写进 gate 的 offset 字段，异常触发时 CPU 会跳到 0 附近——而真实代码在
1 MiB 镜像里。而且 `objcopy` 剥成裸字节后绝对地址会错。

**解法**：用 RIP-relative 指令在**运行时**取 handler 的真实地址：

```c
static TEXT64 u64 runtime_ud_address(void){u64 v;__asm__ volatile("leaq exception_ud(%%rip),%0":"=r"(v));return v;}
```

`leaq exception_ud(%rip)` 生成 PC 相对寻址——linker 算的是「当前位置到符号的位移」，
这个位移不随二进制被搬到哪里而改变，因此 `kernel64.elf` 依然零 relocation。运行时执行
这条指令，`v` 就是处理器此刻真正能跳到的 handler 地址。

### 2.4 概念四：终止型教学 handler

本课 reporter 的收尾是：

```c
text64(&c,"\nCPU halted intentionally.\n");
for(;;)__asm__ volatile("cli; hlt");
```

**为什么不做 `iretq` 恢复**：`#UD`/`#PF` 是 fatal 类故障；`ud2` 后恢复执行只会再次触发。
本课的诊断目标是「让故障可见、可复现」，不是「从故障里救活」。刻意不实现 `iretq`
recovery、不实现 #DF/NMI/MCE、不开 `sti`、不进 PIC/APIC——这些都留给后续课，避免
本课的教学焦点被中断风暴淹没。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 08） |
|---|---|---|
| `boot.S` | 32 位入口 + long mode 转换 + 桥接 | 未变化（无 diff） |
| `kernel64.ld` / `linker.ld` | 双段布局 | 未变化（无 diff） |
| `Makefile` | 构建管线 | 未变化（无 diff） |
| `kernel.c` | 32 位 setup | 微小变化（handoff 增加 `idt_address`，`.bss` 增 `idt_backing_store`） |
| `kernel64.c` | 64 位 IDT + 异常报告 + shell | 新增 3 结构体、`install_idt`/`set_gate`/`runtime_*_address`/`idtinfo`/`exception_report`，3 条新命令，内联 asm stubs |
| `grub.cfg` | 菜单标题 | 微小变化（`lesson 8` → `lesson 9`） |

### 3.2 kernel.c —— 微增：IDT 的物理落点

```c
/* 256 个 16-byte long-mode gate；IDT 由 64-bit C 在 lidt 前填充。 */
u8 idt_backing_store[4096] __attribute__((aligned(16)));
```

- 在 32 位 `.bss` 中预留 4096 字节、16 字节对齐（long mode gate 结构为 16 字节对齐）。
- 它位于既有 `[0, 0x00400000)` identity window 内、在 `_kernel_end` 之前，因此
  `page_is_reserved()` 的 kernel 半开区间天然保护它不被 `palloc` 复用（调试地图第 10 行
  的依据）。
- `kernel_main32` 只多了一行：`long_mode_handoff.idt_address=(u64)(u32)(unsigned long)idt_backing_store;`
  把它的物理地址写进 handoff，64 位侧 `install_idt` 据此定位 IDT。

### 3.3 kernel64.c —— 本课主力

#### 3.3.1 三个新结构体与常量

```c
#define IDT_ENTRIES 256
#define IDT_GATE_INTERRUPT 0x8e
...
struct idt_gate { u16 offset_low, selector; u8 ist, type; u16 offset_mid; u32 offset_high, reserved; } __attribute__((packed));
struct idtr { u16 limit; u64 base; } __attribute__((packed));
struct exception_frame { u64 vector,error,rip,cs,rflags; };
extern void exception_ud(void); extern void exception_pf(void);
```

- `struct idt_gate` 与 Intel long-mode IDT gate 逐字节对应（offset 拆三段、selector、
  IST 2 位、type 8 位、高 32 位 offset）。`packed` 防止编译器插入填充字节。
- `struct exception_frame` 是 stub 与 C 之间的内存协议：栈上字段顺序必须与它一致。
- `extern void exception_ud/pf(void)` 声明 3.3.5 的内联 asm 标签。

#### 3.3.2 `set_gate()` —— 填一项 gate

```c
static TEXT64 void set_gate(struct idt_gate *g,u64 target){
    g->offset_low=(u16)target;
    g->selector=0x08;          /* 64 位 code selector（第八课 GDT index 1）*/
    g->ist=0;                  /* 不用 IST 栈（无 TSS）*/
    g->type=IDT_GATE_INTERRUPT;/* 0x8e = P=1, DPL=0, interrupt gate */
    g->offset_mid=(u16)(target>>16);
    g->offset_high=(u32)(target>>32);
    g->reserved=0;
}
```

- **输入输出**：`g` 指向 IDT 中的某一项；`target` 是运行时算出的 handler 物理地址。
- **为什么 selector 是 `0x08`**：gate 里的 selector 决定跳转时重载哪个 CS。0x08 指向
  第八课 GDT 的 64 位 code descriptor（`L=1, D=0`），保证异常处理在 64 位代码段执行。
- **type 为什么是 `0x8e` 而非 `0x8f`**：`0x8e` 是 interrupt gate（DPL=0），本课无用户态
  `int` 调用需求；trap gate 留给 #BP 恢复课使用。

#### 3.3.3 `install_idt()` —— 清表 + 装 gate + `lidt`

```c
static TEXT64 void install_idt(struct long_mode_handoff*h){
    struct idt_gate *idt=(struct idt_gate *)(unsigned long)h->idt_address;
    struct idtr d; u32 i;
    for(i=0;i<IDT_ENTRIES;i++){ idt[i].offset_low=0; idt[i].selector=0; idt[i].ist=0;
        idt[i].type=0; idt[i].offset_mid=0; idt[i].offset_high=0; idt[i].reserved=0; }
    set_gate(&idt[6],runtime_ud_address());
    set_gate(&idt[14],runtime_pf_address());
    d.limit=sizeof(struct idt_gate)*IDT_ENTRIES-1;   /* 0xfff */
    d.base=(u64)(unsigned long)idt;
    __asm__ volatile("lidt %0"::"m"(d):"memory");
}
```

- **算法步骤**：① 对 256 项逐个清零（未装 gate 的 vector 触发会 double fault，本课
  不需要它们）；② 用运行时地址装 vector 6 与 14；③ 组装 `struct idtr`（limit =
  `16×256-1 = 0x0fff`，base = IDT 物理地址）；④ `lidt` 装载。
- **边界与错误处理**：IDT 在 BSS、handler 在 `.text64`、VGA/stack 都在 identity window
  内，所以 `lidt` 之后 reporter 的一切访问都不会再 #PF（调试地图第 9 行）。
- **为什么 `kernel_main64_binary` 第一条就要调它**：shell 循环开始前 IDT 必须就位，
  否则 udtest/pftest 一个异常就回到无声重启。

#### 3.3.4 `exception_report()` —— 诊断打印（关键函数）

```c
TEXT64 void exception_report(struct exception_frame*f){
    u16 c=0;u64 cr2=0;
    clear64(&c);
    text64(&c,"TinyOS lesson 9 exception\nexception: ");
    if(f->vector==6)text64(&c,"#UD");
    else if(f->vector==14)text64(&c,"#PF");
    else text64(&c,"unknown");
    text64(&c,"\nvector: ");hex64(&c,f->vector);
    text64(&c,"\nerror:  ");hex64(&c,f->error);
    text64(&c,"\nrip:    ");hex64(&c,f->rip);
    text64(&c,"\ncs:     ");hex64(&c,f->cs);
    text64(&c,"\nrflags: ");hex64(&c,f->rflags);
    if(f->vector==14){ __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
        text64(&c,"\ncr2:    ");hex64(&c,cr2); }
    text64(&c,"\nCPU halted intentionally.\n");
    for(;;)__asm__ volatile("cli; hlt");
}
```

- **签名与职责**：入参是 stub 传来的 `struct exception_frame *`，直接映射异常现场。
- **输入输出**：输出整屏 VGA 诊断。RIP 能直接看出「是哪条指令触发的」；#PF 额外读取
  `CR2`——**必须在 reporter 可能触发任何后续 fault 之前读**（读完才碰 VGA/字符串），
  这是调试地图第 8 行的核心约束。
- **算法步骤**：清屏 → 打头部 → 按 vector 选名字（6→`#UD`，14→`#PF`，否则 `unknown`）
  → 逐行打印 vector/error/rip/cs/rflags → #PF 追加 cr2 → 打印收尾 → `cli; hlt` 死循环。
- **为什么这样设计**：打印全部 16 位十六进制便于对照 QEMU monitor；`#UD` 不打印 CR2
  （#UD 不更新 CR2）；终止型 handler 确保诊断画面不被后续执行流破坏。

#### 3.3.5 内联汇编 stub —— 异常到 C 的桥

```c
__asm__(".section .text64\n"
".global exception_ud\nexception_ud:\n"
"pushq $0\npushq $6\njmp exception_common\n"
".global exception_pf\nexception_pf:\n"
"pushq $14\njmp exception_common\n"
"exception_common:\n"
"movq %rsp,%rdi\nandq $-16,%rsp\ncall exception_report\n"
"1: cli\nhlt\njmp 1b\n");
```

逐条含义：
- `exception_ud:` —— #UD 没有硬件 error code，先 `pushq $0` 补占位，再 `pushq $6`
  压 vector，跳 common；
- `exception_pf:` —— #PF 的 error code 已被 CPU 压好，只 `pushq $14` 压 vector 跳 common；
- `exception_common:` —— 此时 `RSP` 指向 `[vector|error|RIP|CS|RFLAGS]`，`movq %rsp,%rdi`
  按 SysV 把 frame 指针作为第一参数；`andq $-16,%rsp` 把栈 16 字节对齐（`call` 前 ABI
  要求）；`call exception_report` 交棒给 C；
- `1: cli / hlt / jmp 1b`：reporter 返回后的三重兜底，与 C 侧死循环互为保险。

为什么用 `.section .text64`：这些标签必须落入 `.text64`，才会被 `objcopy` 打进裸二进制、
并成为 `leaq exception_ud(%rip)` 的相对寻址目标。

#### 3.3.6 新命令与输出串（逐字抄录自源码）

```c
if(eq64(s,"help"))text64(c,"commands: help about clear lminfo pinfo palloc mmap idtinfo udtest pftest\n");
else if(eq64(s,"about"))text64(c,"TinyOS lesson 9: x86_64 exception IDT\n");
...
else if(eq64(s,"idtinfo"))idtinfo(c,h);
else if(eq64(s,"udtest")){text64(c,"triggering #UD\n");__asm__ volatile("ud2");}
else if(eq64(s,"pftest")){volatile u64 *bad=(volatile u64 *)0x00400000ULL;
    text64(c,"triggering #PF\n");p=*bad;(void)p;}
```

- `idtinfo` 打印：`IDT: exceptions only`、`base: <hex>`、`limit: 0000000000000fff`、
  `#UD vector: 0000000000000006`、`#PF vector: 000000000000000e`。
- `udtest` 的 `ud2` 是 Intel 定义的"保证触发 #UD"的指令，CPU 无条件翻 #UD。
- `pftest` 读取 `0x00400000`——当前 identity window 的**排他上界**（第一个未映射地址），
  在不扩大 map 的前提下稳定触发 #PF，并让 `CR2` 恰好等于 `0000000000400000`。
- **重要约束**：`udtest` 与 `pftest` 都是终止型的，每次必须**单独重启一个 QEMU** 执行，
  不能在同一 boot 里连跑两个。

### 3.4 构建管线

与第八课完全一致（`-m64 -fpie -mno-red-zone` → `kernel64.elf` → `objcopy -O binary` →
`.incbin`）。本课**没有新增任何构建步骤**——IDT 是运行时行为。`readelf -rW
build/kernel64.elf` 必须仍为空，这是 RIP-relative 方案的验收点。

### 3.5 主控制流

```text
kernel_main64_binary(h)
  → install_idt(h): 清 256 gate → set_gate(6,#UD)/set_gate(14,#PF)
        → runtime_ud_address(): leaq exception_ud(%rip) 取运行时地址
        → lidt
  → shell: "udtest" → ud2
        → CPU: 压 RFLAGS/CS/RIP → 查 IDT[6] → 跳 exception_ud
        → stub: push error=0, push vector=6 → exception_common
        → RDI=frame → exception_report() → VGA 诊断 → cli;hlt
  → shell: "pftest" → 读 0x00400000 → #PF（CPU 压 error code）
        → IDT[14] → exception_pf: push vector=14 → common → report（含 CR2）
```

---

## 4. 数据流与运行逻辑

- **数据路径**：`idt_backing_store`（32 位 BSS）物理地址 → handoff.idt_address →
  `install_idt` 定位 IDT；`exception_ud`/`exception_pf` 的运行时地址经 `leaq %rip` 得到
  后写入 gate；异常发生时 CPU 按 gate 跳 stub；stub 把现场整理成
  `exception_frame` 结构传给 `exception_report`。
- **命令数据流**：`udtest`/`pftest` → 触发指令 → 异常 → reporter → VGA 输出。
- **输出串索引**（逐字抄录）：
  - banner：`TinyOS lesson 9: x86_64 exception IDT` + `64-bit C continuation active`
  - `help` → `commands: help about clear lminfo pinfo palloc mmap idtinfo udtest pftest`
  - `about` → `TinyOS lesson 9: x86_64 exception IDT`
  - `idtinfo` → `IDT: exceptions only` / `base: <hex>` / `limit: 0000000000000fff` /
    `#UD vector: 0000000000000006` / `#PF vector: 000000000000000e`
  - `udtest` → `triggering #UD` 后进入报告
  - `pftest` → `triggering #PF` 后进入报告（含 `cr2: 0000000000400000`）
  - 报告头 → `TinyOS lesson 9 exception`；异常名 → `#UD` / `#PF` / `unknown`
  - 报告尾 → `CPU halted intentionally.`

---

## 5. 构建、运行与验证

依赖与第八课相同（gcc、binutils、grub 工具链、QEMU）。

```bash
cd lessons/lesson-09-stable
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf       # 必须无 relocation
objdump -d -Mintel build/kernel64.elf # 应能看到 lidt、mov cr2、ud2
```

运行（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

**验证流程分三个独立 boot**（`udtest` 与 `pftest` 都会有意停机）：

1. **普通 boot**：输入 `idtinfo<Enter>`、`help<Enter>`。预期：`idtinfo` 显示 IDT base、
   `limit: 0000000000000fff`、vector 6 与 14；shell 正常响应。
2. **`udtest` boot**（重启 QEMU 后执行）：输入 `udtest<Enter>`。预期：先打印
   `triggering #UD`，然后整屏报告含 `exception: #UD`、`vector: 0000000000000006`、
   `error:  0000000000000000`、RIP/CS/RFLAGS，末尾 `CPU halted intentionally.`。
3. **`pftest` boot**（再重启 QEMU 后执行）：输入 `pftest<Enter>`。预期：`triggering #PF`
   后报告含 `exception: #PF`、`vector: 000000000000000e`、`error:  0000000000000000`、
   `cr2:    0000000000400000`、RIP/CS/RFLAGS，末尾停机。

> **本次实际验证记录（旧 README 保留）**：warning-free `-Werror` build，
> `grub-file --is-x86-multiboot2` 通过；外层 ELF32 为分离 RX/RW LOAD segments，未出现
> RWX LOAD；`nm -u` 无未定义符号；临时 `kernel64.elf` 无 relocation，反汇编包含 `lidt`、
> `mov cr2`、`ud2`。普通 VGA shell 的 `idtinfo` 显示 IDT base、`limit=0x0fff`、vector
> 6/14，`help` 正常。独立 `udtest` boot 显示 `exception: #UD`、vector 6、error 0、
> RIP/CS/RFLAGS 并 intentional halt；独立 `pftest` boot 显示 `exception: #PF`、vector 14、
> error 0、`CR2=0000000000400000`、RIP/CS/RFLAGS 并 intentional halt。QEMU monitor 在
> 普通路径显示 `CS64`、`CR0=80000011`、`CR4=00000020`、`EFER=0000000000000500`，以及
> `IDT.limit=00000fff`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `grub-file` 失败 | Multiboot2 header 非法 | 检查 `.multiboot` 的 8 字节对齐与镜像前 32 KiB |
| `lidt` 后 reset | IDTR base/limit 或 gate 属性错 | 检查 base 落在 identity window、limit=0x0fff、gate type `0x8e` |
| #UD 变 #GP | gate selector 不是 64 位 code | 确认 `set_gate` 的 selector 是 `0x08` |
| #UD frame 错位 | 无 error-code 异常没补压 | 无 error-code 的 vector 必须由 stub 压 synthetic zero |
| #PF frame 错位 | 有 error-code 异常被补压 | CPU 已压 error code，#PF stub 不可再压 synthetic error |
| handler 地址很小（≈0） | 零链接符号被直接写进 gate | 必须用 RIP-relative `leaq exception_*(%rip)` 取运行时地址 |
| #PF 不触发 | 测试地址在映射内 | `pftest` 必须读排他上界 `0x00400000` |
| `CR2` 不对 | 读取太晚（reporter 先 fault 了） | 在 reporter 任何潜在 fault 前 `mov cr2` |
| reporter 再 fault | 现场不在映射内 | IDT、handler、VGA、stack 必须全部低于 4 MiB |
| allocator 复用 IDT | reservation 漏了 BSS 里的 IDT | kernel 半开区间必须覆盖 `.bss` IDT backing store |
| raw binary relocation | 出现绝对寻址 | `readelf -rW build/kernel64.elf` 必须为空 |
| keyboard regression | 中断路径未就绪 | 本课保持 `cli` 与 i8042 polling，不引入 IRQ path |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux v6.12 对应实现 | 权威来源 | 简化了什么 |
|---|---|---|---|
| `struct idt_gate` 手工装配 | `arch/x86/kernel/idt.c: idt_setup_from_table()` + `arch/x86/include/asm/desc_defs.h` | Intel SDM Vol.3 6.14 | Linux 用位域宏与 gate descriptor 模板，按 CPU feature 批量装 |
| `exception_ud`/`exception_pf` stub | `arch/x86/include/asm/idtentry.h` 的 `DECLARE_IDTENTRY` / `DECLARE_IDTENTRY_ERRORCODE` | Linux x86 IDT entry | 两者区分无/有 error-code entry，正是本课 push 0 vs push 14 的设计同源 |
| `struct exception_frame` | Linux `struct pt_regs` | Linux ABI | Linux 的 pt_regs 还含全套寄存器、段、栈指针；本课只留 5 个字段 |
| 终止型 handler（cli;hlt） | 内核的 `oops`/`die()` 路径 | Linux x86 | Linux 有 dump、backtrace、panic 后重启；本课无恢复策略 |
| `lidt` + 256 项 gate | `idt_setup_early_handler()` 早期建最小 IDT | Intel SDM Vol.3 | Linux 有 IST、NMI、IRQ、smp 全套 |

---

## 8. 思考题与练习

1. **概念理解**：为什么 `#UD` 的 stub 要 `pushq $0` 而 `#PF` 不能？如果 #PF stub 也
   `pushq $0`，`exception_report` 会打印出什么错位？
2. **源码定位**：在 `kernel64.c` 中找出 `leaq exception_ud(%%rip),%0` 所在函数，解释它
   与「直接写符号值」的本质区别，并说明它为什么能让 `readelf -rW` 保持为空。
3. **动手实验**：把 `install_idt` 里 `set_gate(&idt[14],...)` 的 selector 从 `0x08`
   改成 `0x10`（data selector），重新构建运行 `pftest`，观察异常类型变化并用调试地图
   第 3 行解释。
4. **动手实验**：新增第三个 gate——`IDT[0]`（#DE，除零），并为它加一条 `dbtest`
   命令（用 `div` 除以零触发）。注意 #DE 无 error code，stub 写法参照 #UD。
5. **Linux 对照**：阅读 `arch/x86/include/asm/idtentry.h`，对比 `DECLARE_IDTENTRY` 与
   `DECLARE_IDTENTRY_ERRORCODE` 的差异，指出它和本课 `exception_ud`/`exception_pf`
   两个 stub 的对应关系。

---

## 9. 本课小结与下一课预告

**小结**：
1. IDT 是 256 项 16 字节 gate 的表，本课只装 vector 6（#UD）与 14（#PF），其余清零。
2. 异常现场由 CPU + stub 共同拼成：CPU 压 RFLAGS/CS/RIP（#PF 另压 error code），stub
   补 vector（#UD 再补 synthetic error 0），得到统一 `exception_frame`。
3. gate 里的 selector 必须是 64 位 code `0x08`，type 用 interrupt gate `0x8e`。
4. 零链接裸二进制不能用符号绝对值填 gate，必须用 `leaq exception_*(%rip)` 运行时取址，
   保证无 relocation。
5. `#PF` 的 `CR2` 必须在 reporter 任何潜在 fault 之前读取。
6. 本课 handler 是终止型：报告后 `cli; hlt`，刻意不做恢复、不开中断。
7. `pftest` 的 `0x00400000` 是 identity window 排他上界，是最小可复现的 #PF 触发器。

**下一课**：[`lesson-10-stable/README.md`](../lesson-10-stable/README.md) 将把「终止型
handler」升级为「可恢复 handler」：为 `#BP`（vector 3）装 trap gate，`int3` 触发后
report 再 `iretq` 返回 shell——异常第一次不再杀死执行流，这正是下一课 shell 还能继续
交互的关键机制。

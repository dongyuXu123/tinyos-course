# Lesson 11: 8259A PIC 重映射与安全的首次 IRQ1 硬件中断 — 精讲文档

> **课号**：Lesson 11（可执行课，x86_64 阶段）
> **主题**：把第十课的同步软件 trap（#BP）升级为**异步硬件中断**：重映射两块 legacy
> 8259A PIC、让 IDT vector `0x21` 接收键盘 IRQ1、handler 通过 `iretq` 返回；正常 shell
> 仍保持 polling 基线，IRQ1 只在 `irqtest` 显式武装时短暂开启。
> **课程主线位置**：第 4 阶段（异常与中断）从「同步异常」跨入「异步中断」；8259A 重映射、
> EOI、IF 位管理是第十二课 IRQ 驱动键盘 + ring buffer 的硬件前提。
> **前置课程**：[`lesson-10-stable/README.md`](../lesson-10-stable/README.md)（可恢复 #BP：
> gate→stub→report→`iretq` 回路；SysV callee-saved 约定）
> **后续课程**：[`lesson-12-stable/README.md`](../lesson-12-stable/README.md)（IRQ 驱动
> PS/2 键盘 + ring buffer shell：中断把 scancode 入队，shell 从队列消费）
> **一句话目标**：学完本课你能说清「硬件中断为什么比异常难」：中断可能在任意一条指令
> 间隙到来、被中断的代码正使用着全部寄存器，因此 IRQ stub 必须保存/恢复所有通用寄存器，
> 且 handler 必须在返回前 EOI，否则中断会变成风暴。

---

## 1. 课程定位（Mission）

**一句话目标**：安装 vector `0x21` 的 IRQ1 gate，实现一次**一次性、可观察**的键盘硬件
中断验证：`irqtest` 武装 IRQ1 → 按键 → `irq1_record` 读 `0x60`、计数、记录 scancode、
发 master EOI、立刻重新屏蔽全部 PIC 线 → `iretq` 返回后 shell 恢复 polling。

- **在课程主线中的位置**：第九课「异常能打印」、第十课「异常能返回」，本课证明
  「**硬件中断**也能安全进入并返回」。中断的异步性（可能在 shell/allocator 任意指令
  执行期间到达）迫使 stub 保存全部 15 个通用寄存器——这是与 #BP 只保 RBX 的本质区别。
- **前置知识清单**：
  1. 第十课：#BP 可恢复路径、`iretq` 弹栈语义、IDT gate 装配、RIP-relative 取址；
  2. 8259A PIC 的 ICW1–ICW4 初始化序列与 OCW1 屏蔽字、EOI（OCW2）的含义；
  3. CPU 的中断响应：`INTR` → IF 位、interrupt gate 进入时自动清 IF、`iretq` 恢复 IF；
  4. i8042 键盘端口：状态端口 `0x64` 的 OBF 位、数据端口 `0x60` 的 set-1 scancode。
- **本课交付**：`irqtest`/`irqinfo` 两个新命令；`pic_init` 重映射；`irq1_entry` 全
  寄存器保护 stub；一次真实键盘 IRQ1 的 VGA 观察记录。

---

## 2. 核心概念精讲

### 2.1 概念一：异步中断 vs 同步异常——为什么 IRQ stub 必须保存全部 GPR

**直觉**：异常（#UD/#PF/#BP）是**当前指令自己触发**的，进入 handler 时 CPU 的现场是
「正要执行这条指令」。中断是**外部设备随时拉高 INTR 引脚**，CPU 在**任何一条指令的
边界**都可能响应。被中断的代码可能是 `exec64`、`alloc64` 里任何正在用寄存器的指令。

**准确定义**：shell 或 allocator 是普通 C 代码，编译器按 SysV 约定用 caller-saved
寄存器（RAX/RCX/RDX/RSI/RDI/R8–R11）与 callee-saved 寄存器（RBX/RBP/R12–R15）混用。
中断随时打断它们，handler 返回后必须让被中断的代码看到**和中断前完全一致的寄存器**。
因此 IRQ1 的 stub 与 #BP 的「只保 RBX」截然不同，它保存并恢复**全部 15 个 GPR**：

```text
CPU IRQ frame（RFLAGS | CS | RIP）
  ↓ stub 依次 push
RAX RBX RCX RDX RBP RSI RDI R8 R9 R10 R11 R12 R13 R14 R15
  ↓ handler 返回前依次 pop（逆序）
```

注意：中断**不压 error code、也不压 vector**（那是异常和 `int n` 的行为）；stub 无需
补压任何东西，`iretq` 直接弹 CPU 压入的 RIP/CS/RFLAGS。

### 2.2 概念二：8259A PIC 与 ICW 重映射

**动机**：默认的 PIC vector 基址是 `0x08`，与 CPU 异常 vector 0–31 重叠——硬件中断和
异常会撞车。必须把 PIC 的 IRQ 重映射到 32 之后。

**工作机制**：初始化要发 ICW1–ICW4（对命令端口 `0x20`/`0xa0`），再发 OCW1 屏蔽字
（对数据端口 `0x21`/`0xa1`）：

| 字节 | 值 | 含义 |
|---|---|---|
| ICW1 → `0x20`/`0xa0` | `0x11` | 初始化 + 需要 ICW4 + cascade |
| ICW2 → `0x21`/`0xa1` | `0x20`/`0x28` | 主片基址 `0x20`，从片基址 `0x28` |
| ICW3 → `0x21`/`0xa1` | `4`/`2` | 主片 IRQ2 是级联线；从片级联标识 2 |
| ICW4 → `0x21`/`0xa1` | `1` | 8086/88 模式 |
| OCW1 → `0x21`/`0xa1` | `0xff`/`0xff` | 全部 IRQ 线初始屏蔽 |

重映射后：IRQ0 → vector `0x20`，**IRQ1 → vector `0x21`**，IRQ2 级联 → `0x22`……从片
IRQ8 → `0x28`。每次写 ICW 之后 `io_wait64()`（写端口 `0x80`）给 ISA 总线一点时间，
保证慢速 8259A 完成上一条命令。

### 2.3 概念三：EOI、屏蔽字与"键盘所有权"

- **EOI（End Of Interrupt）**：PIC 收到设备中断后会把对应 IRQ 线 hold 住，直到软件向
  命令端口写 `0x20`（`PIC_EOI`）应答。**忘了 EOI，同一条 IRQ 的后续中断永远来不了**
  （对主片 IRQ 而言）。IRQ1 是主片独有线路，只发 **master EOI**（`0x20` 端口）即可；
  从片 IRQ 才需要「从片 EOI + 主片 EOI」两级应答（本课不开从片线路）。
- **屏蔽字**：OCW1 的每一位对应一条 IRQ 线，**1=屏蔽、0=使能**。`0xff` 全屏蔽；
  `0xfd`（`1111 1101`）= 仅 bit1 清零，即**只使能 IRQ1**。
- **键盘所有权冲突**：polling shell 和使能的键盘 IRQ 会**抢同一个 `0x60` 端口**——
  谁先读谁就拿到了 scancode。若长期开放 IRQ1，普通命令的按键会变得不确定。本课的处理
  是"一次性契约"：`irqtest` 才打开 IRQ1，handler 消费一个字节后立刻重新全屏蔽，
  polling 立即夺回独占权。

### 2.4 概念四：中断使能顺序（先建好世界，再开门）

中断必须在一套"就绪世界"里才安全。本课顺序刻意固定：

```text
1. cli                                    ← 默认关中断
2. install_idt(...)                       ← 先装全部 gate（含 vector 0x21）
3. pic_init()                             ← 重映射 PIC 并全屏蔽
4. 以 IF=0 运行 polling shell             ← 正常命令在屏蔽状态下执行
5. 只有 irqtest 才: pic_masks(0xfd,0xff) + sti   ← 最后才开一条 IRQ
```

如果在 gate/栈/handler/PIC 屏蔽全部就绪之前就 `sti`，一个早到的外部 IRQ 会直接变成
#GP/#UD 或 triple fault。本课第 5 步之后**只有一条 IRQ 线**被打开，且 handler 会在
一个字节后自锁（重新全屏蔽），所以即使有陈旧按键待处理也不会失控。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 10） |
|---|---|---|
| `boot.S` / `kernel.c` / `kernel64.ld` / `linker.ld` / `Makefile` | 启动链与构建 | 未变化（无 diff） |
| `kernel64.c` | 64 位 IDT + PIC + IRQ1 + shell | 新增 PIC 宏、3 个 volatile 状态量、`outb64`/`io_wait64`/`pic_masks`/`pic_init`/`runtime_irq1_address`/`irqinfo`/`irq1_record`/`arm_irq1`、`irq1_entry` stub；`install_idt`/`idtinfo`/`exec64`/banner 增量 |
| `grub.cfg` | 菜单标题 | 微小变化（标题含 "PIC remap and IRQ1"） |

**与第十课一样，全部核心增量集中在 `kernel64.c`**。

### 3.2 增量一：PIC 相关宏与 IRQ 状态量

```c
#define PIC1_COMMAND 0x20   /* 主片命令端口 */
#define PIC1_DATA 0x21      /* 主片数据端口（OCW1 屏蔽字） */
#define PIC2_COMMAND 0xa0   /* 从片命令端口 */
#define PIC2_DATA 0xa1      /* 从片数据端口 */
#define PIC_EOI 0x20        /* 发送给命令端口的 EOI 值（OCW2） */
...
static volatile u64 irq1_count;         /* 观察到的 IRQ1 次数 */
static volatile u8 irq1_last_scancode;  /* 最后一次 handler 读到的 scancode */
static volatile u8 irq1_armed;          /* irqtest 是否已武装（决定 irqinfo 文案） */
```

- 三个状态量全部 `volatile`：handler 与 shell 运行在不同的执行路径上（中断 vs polling），
  编译器不得把对它们的读写优化进寄存器。
- `irq1_armed` 同时是"用户界面状态"：`irqinfo` 依据它打印 `armed` 或 `masked`。

### 3.3 增量二：I/O helpers 与 `pic_init()` —— 重映射序列（关键函数）

```c
static TEXT64 u8 inb64(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static TEXT64 void outb64(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static TEXT64 void io_wait64(void){outb64(0x80,0);}   /* 写 ISA 保留端口，等待总线 */
static TEXT64 void pic_masks(u8 master,u8 slave){outb64(PIC1_DATA,master);outb64(PIC2_DATA,slave);}
static TEXT64 void pic_init(void){
    outb64(PIC1_COMMAND,0x11);io_wait64();
    outb64(PIC2_COMMAND,0x11);io_wait64();
    outb64(PIC1_DATA,0x20);io_wait64();   /* ICW2: 主片基址 0x20 */
    outb64(PIC2_DATA,0x28);io_wait64();   /* ICW2: 从片基址 0x28 */
    outb64(PIC1_DATA,4);io_wait64();      /* ICW3: 主片 IRQ2 级联 */
    outb64(PIC2_DATA,2);io_wait64();      /* ICW3: 从片级联标识 2 */
    outb64(PIC1_DATA,1);io_wait64();      /* ICW4: 8086/88 模式 */
    outb64(PIC2_DATA,1);io_wait64();
    pic_masks(0xff,0xff);                 /* 初始化后全屏蔽 */
}
```

- **签名与职责**：无参无返回，把两块 PIC 从默认状态重映射到 `0x20`/`0x28` 基址并全屏蔽。
- **算法步骤**：ICW1(0x11)→ICW2(基址)→ICW3(级联)→ICW4(8086 模式)，主从片各自 4 步，
  中间用 `io_wait64()` 给 ISA 总线时间；最后 OCW1 全屏蔽。
- **为什么 ICW3 是 4 和 2**：主片 ICW3 的每个 bit 表示"该 IRQ 线后接从片"，bit2=4 表示
  IRQ2 是级联线；从片 ICW3 低 3 位表示从片挂在主片的哪条 IRQ 上，`2` = 挂在主片 IRQ2。
- **边界**：QEMU 的 PIIX3/ICH 兼容此序列；屏蔽字必须最后设置，否则重映射期间的任何
  IRQ 都会以旧基址到达。

### 3.4 增量三：`install_idt` 与 `idtinfo` 的增量

```c
set_gate(&idt[3],runtime_bp_address());
set_gate(&idt[6],runtime_ud_address());
set_gate(&idt[14],runtime_pf_address());
set_gate(&idt[0x21],runtime_irq1_address());   /* 新增：IRQ1 gate */
```

- `runtime_irq1_address()` 与异常取址同构：`leaq irq1_entry(%rip)` 保证无 relocation。
- `idtinfo` 输出新增一行：`IRQ1 vector: 0000000000000021`；头部文案改为
  `IDT: exceptions + IRQ1 foundation`。基址映射的依据：IRQ1 是主片 IRQ，重映射后
  vector = `0x20 + 1 = 0x21`。

### 3.5 增量四：`irq1_entry` stub —— 全 GPR 保存/恢复（本课灵魂）

```asm
".global irq1_entry\nirq1_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\n"
"pushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rbp\nandq $-16,%rsp\ncall irq1_record\nmovq %rbp,%rsp\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\n"
"popq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\npopq %rax\niretq\n"
```

逐段含义：
1. **15 条 `pushq`**：按固定顺序把全部 GPR 压栈（RAX…R15）。这保证被中断代码在
   `iretq` 后看到完全相同的寄存器。
2. **`cld`**：清方向标志 DF，保证字符串指令按"地址递增"方向执行（C 代码的隐含假设）。
3. **`movq %rsp,%rbp`**：把"全寄存器栈帧基址"暂存到 RBP——注意此时 RBP 的原值已被
   push 保存，RBP 现在是自由的。
4. **`andq $-16,%rsp`**：`call irq1_record` 前 16 字节对齐（SysV ABI 要求）。
5. **`call irq1_record`**：交棒 C handler；返回后 `movq %rbp,%rsp` 丢弃对齐产生的间隙，
   回到寄存器栈帧。
6. **15 条 `popq`**：严格逆序还原全部 GPR。
7. **`iretq`**：弹出 CPU 压入的 RIP/CS/RFLAGS，回到被中断的指令。

**为什么这样比 #BP 复杂**：#BP 是同步的，可以假设编译器在 `int3` 处已经按 clobber 列表
处理了 caller-saved；而 IRQ 的打断点完全未知，编译器对它**毫无准备**，所以 stub 必须
无条件全保存。

### 3.6 增量五：`irq1_record()` 与 `arm_irq1()` —— 一次性验证契约

```c
TEXT64 void irq1_record(void){
    u16 c=16*COLS;
    irq1_last_scancode=inb64(0x60);   /* 读键盘数据端口（谁先读谁得手） */
    irq1_count++;
    irq1_armed=0;                     /* 一次性：消费后撤掉武装标志 */
    pic_masks(0xff,0xff);             /* 立即重新全屏蔽 */
    outb64(PIC1_COMMAND,PIC_EOI);     /* 必须 EOI，否则 IRQ1 被 hold 住 */
    text64(&c,"TinyOS lesson 11 IRQ1 observed\ncount: ");
    hex64(&c,irq1_count);
    text64(&c,"\nlast scancode: ");
    hex64(&c,irq1_last_scancode);
    text64(&c,"\nIRQ1 masked; polling shell remains usable\n");
}
static TEXT64 void arm_irq1(u16*c){
    irq1_armed=1;
    pic_masks(0xfd,0xff);             /* 只开主片 IRQ1（bit1=0） */
    text64(c,"IRQ1 armed: press one key to validate (that key is not shell input)\n");
    __asm__ volatile("sti":::"memory");
}
```

- **`irq1_record` 算法步骤**：读 scancode → 计数 → 撤武装 → 全屏蔽 → **master EOI** →
  第 16 行起打印观察记录。顺序中 EOI 在最后写（打印不会被打断，因为本课没有嵌套中断），
  屏蔽在 EOI 前完成（防止 EOI 后立刻又到同一 IRQ）。
- **为什么被中断消费的按键不是 shell 输入**：`irqtest` 命令自身的 Enter 释放码
  （scancode `0x9c`）或下一个按键的字节已由 IRQ handler 从 `0x60` 读走，polling 循环
  再也拿不到它。这是**有意为之**的"键盘所有权"演示：IRQ 持有期间 shell 不可见该键。
- **`arm_irq1` 的关键**：`sti` 放在**最后**，且在 IRQ1 屏蔽位已清、gate 已装、
  handler 已就位的条件下执行；`"memory"` clobber 防止编译器重排。
- **低屏观察区**：handler 固定写第 16 行（`16*COLS`）起，不碰 shell 私有 cursor——
  沿用第十课的"下半屏契约"思想。

### 3.7 增量六：命令表与输出串（逐字抄录自源码）

```c
if(eq64(s,"help"))text64(c,"commands: help about clear lminfo pinfo palloc mmap idtinfo irqinfo irqtest bptest udtest pftest\n");
else if(eq64(s,"about"))text64(c,"TinyOS lesson 11: PIC remap and one-shot IRQ1\n");
...
else if(eq64(s,"irqinfo"))irqinfo(c);
else if(eq64(s,"irqtest"))arm_irq1(c);
```

- `irqinfo` 输出：`PIC: master 0x20, slave 0x28`、`IRQ1 count: <hex>`、
  `last scancode: <hex>`、`IRQ1 state: armed` 或
  `masked (polling shell owns keyboard)`；
- `bptest`/`udtest`/`pftest` 保留；其中 `breakpoint_report`/`exception_report` 的
  头串更新为 `TinyOS lesson 11 breakpoint` / `TinyOS lesson 11 exception`；
- boot banner：`TinyOS lesson 11: PIC remap and one-shot IRQ1` +
  `64-bit C continuation active; keyboard polling is safe`。

### 3.8 主控制流

```text
kernel_main64_binary(h)
  → cli
  → install_idt: gate[3/6/14] + gate[0x21]=irq1_entry → lidt
  → pic_init(): ICW1..ICW4 重映射 0x20/0x28 → 全屏蔽
  → polling shell（IF=0）
  → "irqtest" → arm_irq1(): irq1_armed=1, pic_masks(0xfd,0xff), sti
  → 下一个键盘字节 → PIC 拉 INTR → CPU 响应 vector 0x21 → irq1_entry
        → 全 GPR 压栈 → 对齐 → irq1_record()：读 0x60 / 计数 / 全屏蔽 / EOI / 低屏打印
        → 全 GPR 逆序弹出 → iretq
  → shell 恢复 polling（该按键不作为 shell 输入）→ irqinfo/help 仍可用
```

---

## 4. 数据流与运行逻辑

- **硬件数据流**：键盘按下 → i8042 向 `0x60` 缓冲并置 OBF → IRQ1 拉高 → PIC 主片
  (unmasked) → CPU 在指令边界响应 → 查 IDT[0x21] → `irq1_entry`。
- **软件数据流**：stub 全保存 GPR → `irq1_record` 读 `0x60` 得 scancode → 写入
  `irq1_last_scancode`、`irq1_count++` → 全屏蔽 + EOI → 第 16 行打印 → 返回。
- **命令数据流**：`irqinfo` 读三个 volatile 状态量打印；`irqtest` 先打印提示串再武装。
- **输出串索引**（逐字抄录）：
  - banner：`TinyOS lesson 11: PIC remap and one-shot IRQ1` +
    `64-bit C continuation active; keyboard polling is safe`
  - `help` → `commands: help about clear lminfo pinfo palloc mmap idtinfo irqinfo irqtest bptest udtest pftest`
  - `about` → `TinyOS lesson 11: PIC remap and one-shot IRQ1`
  - `idtinfo` → `IRQ1 vector: 0000000000000021`
  - `irqinfo` → `PIC: master 0x20, slave 0x28` / `IRQ1 count: <hex>` /
    `last scancode: <hex>` / `IRQ1 state: masked (polling shell owns keyboard)`
  - `irqtest` → `IRQ1 armed: press one key to validate (that key is not shell input)`
  - handler 记录 → `TinyOS lesson 11 IRQ1 observed` / `count: <hex>` /
    `last scancode: <hex>` / `IRQ1 masked; polling shell remains usable`

---

## 5. 构建、运行与验证

依赖与第十课相同。

```bash
cd lessons/lesson-11-stable
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

预期静态证据：
- Multiboot2 header check 通过；
- 外层 ELF 有独立 RX/RW LOAD 段、无未定义符号；
- `kernel64.elf` 无 relocation（尽管被 raw-embed 在 1 MiB）；
- 反汇编包含 PIC 端口写、`lidt`、`sti`、IRQ1 全 GPR push/pop 序列、master EOI 与 `iretq`。

运行（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

等待 `tinyos> ` prompt，按以下顺序验证：

1. 输入 `idtinfo<Enter>`，确认显示 `IRQ1 vector: 0000000000000021`；
2. 输入 `irqinfo<Enter>`，必须显示 `IRQ1 state: masked (polling shell owns keyboard)`
   且 `IRQ1 count` 为 0；
3. 输入 `irqtest<Enter>`：命令自身的 Enter 释放码，或（若无待处理字节）下一个按键，
   触发第 16 行起的 `IRQ1 observed` 报告，含 count 与原始 scancode；
4. 再输入 `irqinfo<Enter>`，确认 count/scancode 已更新且 IRQ1 恢复 masked；
5. 输入 `help<Enter>`，证明中断测试后 polling 继续可用；
6. 可选：`bptest<Enter>` 后再 `help<Enter>`，回归第十课的同步返回路径。

> **本课验证记录（旧 README 保留）**：构建侧要求 warning-free `-Werror` build、
> `grub-file --is-x86-multiboot2` 通过；`kernel64.elf` 无 relocation；反汇编应包含
> PIC 端口写、`lidt`、`sti`、IRQ1 全 GPR push/pop、master EOI 与 `iretq`。运行侧：
> 普通 shell 中 `idtinfo` 显示 vector `0x21`；`irqinfo` 初始为 masked、count 0；
> `irqtest` 后下一键盘字节触发 `IRQ1 observed` 报告（count 与原始 set-1 scancode），
> 且 IRQ1 随后重新 masked；之后 `help` 证明 polling 未被破坏。`udtest`/`pftest` 因
> 终止型 reporter，需独立 QEMU boot 回归。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `sti` 后 reset | 开中断时机太早 | 验证 `lidt`、PIC 重映射、IRQ gate 全部完成后再打开任何 IRQ 线 |
| IRQ 到达 #UD/#GP | 中断 vector 与异常 vector 重叠 | 主片偏移必须是 `0x20`，IRQ1 因此必须是 vector `0x21` |
| handler 从不执行 | IRQ1 未被使能 | 检查仅在 `irqtest` 武装期间 master mask bit1 清零 |
| IRQ 风暴 | 没发 EOI | IRQ1 返回前必须向 master 命令端口 `0x20` 发 EOI |
| polling 命令丢字符 | IRQ1 长期 unmasked | 不要在 polling 循环也读 `0x60` 时长期开 IRQ1 |
| handler 返回后 fault | GPR 没还原 | 保存/恢复全部 GPR，且严格逆序，再 `iretq` |
| C handler 破坏被中断代码 | 只保护了部分寄存器 | IRQ stub 必须同时保护 caller- 与 callee-saved（不像 #BP 只保 RBX） |
| C 调用 fault 或异常 | 栈未对齐 | `call` 前把 RSP 对齐到 16 字节，返回后还原保存的 frame 指针 |
| 后续从片 IRQ 卡死 | EOI 层级不对 | 从片 IRQ 需要从片 EOI + 主片 EOI；IRQ1 是主片独有，只用 master EOI |
| handler 目标接近零 | 零链接符号直接填 gate | 用 RIP-relative `lea` 取内嵌 raw ELF64 的 handler 运行时地址 |
| 陈旧按键立即触发 | 一次性验证的正常表现 | 查看 `last scancode`，然后用新按键继续 |
| PIC 改动后回归失败 | 终止型 reporter 未独立跑 | 分别独立 boot `udtest` 与 `pftest`（reporter 有意停机） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux v6.12 对应实现 | 权威来源 | 简化了什么 |
|---|---|---|---|
| `pic_init()` 手工 ICW1–ICW4 | `arch/x86/kernel/i8259.c` 的 `init_8259A()` / `legacy_pic` 回调 | Intel 8259A datasheet / SDM | Linux 支持多种 legacy PIC 变体与 `unmask_8259A` 动态策略 |
| `irq1_entry` 全 GPR 保存 | `arch/x86/entry/entry_64.S` 的 `PUSH_AND_CLEAR_REGS` 宏 | Linux x86 entry | Linux 还处理 GS、PTI、idle loop 等；本课只有 15 个 GPR |
| vector `0x21` 单点 IRQ1 | `arch/x86/kernel/irq.c` 的 `do_IRQ()` → `handle_irq_event` | Linux 中断子系统 | Linux 有 IRQ descriptor、threading、nested 支持 |
| 一次性 EOI + 屏蔽 | `arch/x86/kernel/i8259.c` 的 `mask_and_ack_8259A()` | 8259A 规范 | Linux 区分 mask/ack/EOI 时机；本课一次性处理 |
| handler 直接写 VGA | `drivers/tty/serial/` 与 console 同步机制 | Linux console | 本课因 shell 暂停且 IRQ 立即屏蔽才可裸写 VGA |
| 键盘所有权互斥 | `drivers/input/serio/i8042.c` 的 busy port 协议 | i8042 规范 | Linux 有 spinlock 与 serio 驱动层；本课靠一次性屏蔽绕开 |

---

## 8. 思考题与练习

1. **概念理解**：为什么 IRQ 的 stub 必须保存全部 15 个 GPR，而 #BP 只保存 RBX？
   若 IRQ stub 漏掉 RBP 会怎样？（提示：`movq %rsp,%rbp` 需要 RBP 空闲的前提）
2. **源码定位**：在 `kernel64.c` 中找出 `pic_masks(0xfd,0xff)` 与 `pic_masks(0xff,0xff)`
   两处调用，说明 `0xfd` 的 bit 布局与"只开主片 IRQ1"的对应关系。
3. **动手实验**：删掉 `irq1_record` 里的 `outb64(PIC1_COMMAND,PIC_EOI);` 重新构建运行
   `irqtest`，观察后续行为并用调试地图第 4 行解释。
4. **动手实验**：把 `arm_irq1` 的 `sti` 移到 `pic_masks` 之前，重新构建，预测并在 QEMU
   中验证会出现什么问题。
5. **Linux 对照**：阅读 `arch/x86/kernel/i8259.c` 的 `init_8259A`，对比本课 ICW 序列的
   异同，说明 Linux 为什么要支持 `legacy_pic` 这种多实现接口。

---

## 9. 本课小结与下一课预告

**小结**：
1. 8259A 双片通过 ICW1–ICW4 重映射到 `0x20`/`0x28`，IRQ1 因此落在 vector `0x21`，
   与异常 vector 0–31 彻底分离。
2. 异步中断可能在任意指令间隙到来，IRQ1 stub 必须保存并逆序恢复全部 15 个 GPR，
   然后 `iretq`——这与 #BP 只保 RBX 是本质区别。
3. handler 返回前必须发 master EOI（`0x20`），否则 PIC hold 住 IRQ1 造成中断失效；
   从片 IRQ 需要两级 EOI（本课不涉及）。
4. 键盘端口所有权是排他的：polling 与 IRQ 谁先读 `0x60` 谁得手。本课用"一次性武装"
   契约解决：`irqtest` 才开 IRQ1，handler 消费一个字节后立即全屏蔽。
5. 中断使能顺序固定：`cli` → 装 IDT gate → 重映射并屏蔽 PIC → polling → 最后
   `sti`+单线使能，绝不在"世界未就绪"前开门。
6. 中断期间裸写 VGA 只在本课成立：shell 停在 polling 循环、IRQ 已被屏蔽、无嵌套。
7. IRQ1 不是键盘驱动：不解码、不入队、不喂 shell；它是第十二课 ring buffer 方案的
   最小观察基座。

**下一课**：[`lesson-12-stable/README.md`](../lesson-12-stable/README.md) 将把「一次性
观察」升级为「生产级」：IRQ1 驱动的 PS/2 键盘——handler 把 scancode 转成字符后写入
**ring buffer**，shell 从队列消费，与 polling 完全解耦，正式成为 IRQ 驱动的键盘 shell。

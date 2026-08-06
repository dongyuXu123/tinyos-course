# Lesson 12: IRQ1 键盘生产者与 ring-buffer shell — 精讲文档

> **课号**：Lesson 12（可执行课，x86_64 阶段）
> **主题**：把第十一课「一次性 IRQ 观察」升级为**真正的 IRQ 驱动键盘模型**：IRQ1 在
> setup 后**永久使能**、IRQ handler 是唯一读 i8042 数据端口 `0x60` 的代码、shell 从
> 一个 64 项的 ring buffer 消费解码字符——polling 从本课彻底退出键盘路径。
> **课程主线位置**：第 4 阶段（异常与中断）的收官课，也是「生产者/消费者」并发结构的
> 第一次亮相：中断上下文生产、主执行流消费，中间用有界队列解耦。
> **前置课程**：[`lesson-11-stable/README.md`](../lesson-11-stable/README.md)（8259A 重映射、
> IRQ1 gate、全 GPR stub、master EOI、键盘端口所有权）
> **后续课程**：[`lesson-13-stable/README.md`](../lesson-13-stable/README.md)（8254 PIT 周期
> 定时器 IRQ0 tick 与 IRQ1 键盘并存）——本课的 ring buffer 生产-消费结构是未来一切设备
> 驱动（timer、串口、磁盘）的通用骨架。
> **一句话目标**：学完本课你能说清「中断上下文与主执行流如何通过一个有界队列解耦」：
> handler 单次读 `0x60`、解码、入队、EOI；shell 在 `cli` 临界区内出队、空队时
> `sti; hlt` 等待唤醒——空判与等待的过渡不会丢唤醒。

---

## 1. 课程定位（Mission）

**一句话目标**：让键盘输入全程由 IRQ1 驱动：handler 把 set-1 make code 解码成字符入队，
shell 消费队列执行命令；`kbdinfo` 报告 raw/make/overflow 计数与队列水位；IRQ1 不再
需要显式武装，开机即常开。

- **在课程主线中的位置**：第十一课证明了「硬件中断能安全进入并返回」，但它的键盘所有权
  是"一次性让渡"。本课解决所有权问题本身：**handler 独占 `0x60`**，shell 永远不碰端口，
  从而 IRQ1 可以常开。这是从「实验」到「驱动」的跨越。
- **前置知识清单**：
  1. 第十一课：PIC 重映射/屏蔽字、EOI、`sti`/IF 位、全 GPR 保存恢复、interrupt gate
     进入时自动清 IF；
  2. set-1 scancode：make code（bit7=0）与 break code（bit7=1）的区分；
  3. ring buffer（循环队列）：`head` 写、`tail` 读、`(head+1) & (size-1)` 环绕、
     满判 `head+1 == tail`；
  4. `objcopy -O binary` 只输出 PROGBITS 段——NOBITS 的 `.bss` 默认会被丢掉。
- **本课交付**：常开的 IRQ1 键盘输入；`kbdinfo` 诊断命令；64 项 ring buffer；
  `.bss`→`.data` 的链接脚本修补（让队列状态进入裸二进制）。

---

## 2. 核心概念精讲

### 2.1 概念一：生产-消费者解耦——为什么 shell 不再碰端口

**直觉**：第十一课"polling 与 IRQ 抢 `0x60`"的根本矛盾是**双方都在消费同一个字节**。
解法是分工：中断上下文（生产者）负责从硬件读字节，主执行流（消费者）只从队列取
已解码字符。shell 永远不知道端口的存在，也就不存在竞争。

**准确定义**：

```text
i8042 键盘 → IRQ1 → irq1_entry stub → irq1_record()
                                      ├─ 读 0x60（唯一读者）
                                      ├─ 统计 raw/make/overflow
                                      ├─ 解码 make code → kbd_queue[head]
                                      └─ master EOI
kbd_queue（64 项有界环） ──── 解耦层 ────
shell 主循环 → kbd_dequeue() → 出队字符 → 命令处理
```

因为 IRQ1 常开且 handler 是唯一读者，**键盘所有权问题消失了**；`irqtest`/`irqinfo`
的"一次性武装"机制也不再需要（`kbdinfo` 取而代之）。

### 2.2 概念二：有界 ring buffer 与它的三个计数器

- **队列**：`volatile u8 kbd_queue[64]`，`kbd_head`（生产者写位置）、`kbd_tail`
  （消费者读位置），均为 `volatile u8`。`64 = 2^6`，所以用 `& (KBD_QUEUE_SIZE - 1)`
  做环绕，等价于模 64 但更快。
- **满/空判据**：空 ⇔ `head == tail`；满 ⇔ `(head + 1) & 63 == tail`（牺牲一格区分
  满与空）。
- **三个计数**（`kbdinfo` 全部展示）：
  - `irq1_raw_count`：handler 读到的**所有**字节数（含 break code）；
  - `irq1_count`：有效 **make code** 数（bit7=0 且被统计的）；
  - `kbd_overflow_count`：队列满时被丢弃的解码字符数。

### 2.3 概念三：空判与等待的原子过渡（`sti; hlt` 为什么安全）

**问题**：消费者读队列为空后要"睡觉"（`hlt`），但如果 `hlt` 执行时 IF 仍是关的，
一个恰好在这之前到达的 IRQ 会被丢弃，之后永远没人唤醒它——输入丢失。

**解法**：空队时的睡眠是 `sti; hlt` 连用：

```c
if(!kbd_dequeue(&ch)){ __asm__ volatile("sti; hlt":::"memory"); continue; }
```

时序分析（两种情形都安全）：
- **IRQ 在 `sti` 之前已挂起**：`sti` 一旦执行，CPU 立即在 `hlt` 前响应挂起的中断，
  handler 入队并 EOI，`hlt` 醒来后发现队列非空；
- **IRQ 在 `hlt` 之后到来**：`hlt` 被中断唤醒，回到循环再次出队。

所以「空判 → 开中断 → 停机」的整个过渡不会丢失唤醒。另外 `kbd_dequeue` 内部的
出队操作（读 `tail`、取字符、推进 `tail`）被一对 `cli`/`sti` 包成**短临界区**：期间
IF 关闭，IRQ 被推迟，保证 shell 与 handler 不会同时改 `tail`/`head`。

### 2.4 概念四：链接脚本修补——`.bss` 必须变成 PROGBITS

**问题**：`kbd_queue` 等零初始化全局量默认进 `.bss`（ELF 的 NOBITS 段，不占文件字节）。
而 `objcopy -O binary` **只输出 PROGBITS 段**——NOBITS 会被直接丢弃。若队列不在
`kernel64.bin` 里，内嵌后就是"悬空内存"，handler 写队列等于写随机区域。

**解法**（`kernel64.ld` 本课唯一增量）：

```ld
    /* objcopy emits PROGBITS, not NOBITS.  Keep persistent IRQ queue state
     * in the raw continuation, even when C places zero-initialized objects in .bss. */
    .data : { *(.data .data.*) *(.bss .bss.* COMMON) BYTE(0) }
```

把 `.bss`/`COMMON` 输入段并进 `.data` **输出段**：输出段变成 PROGBITS，objcopy 就会把
这些字节（初始值全 0）实实在在写进裸二进制。`BYTE(0)` 保证即使所有输入都为空，段也
非空、能生成。这解释了第十一课为什么可以不管 `.bss`（那时没有跨模式的持久状态需要
进二进制；即使第十一课的三个计数器也在 `.bss`，但 handler 在运行期写它们不需要
"出厂值"）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 11） |
|---|---|---|
| `boot.S` / `kernel.c` / `linker.ld` / `Makefile` | 启动链与构建 | 未变化（无 diff） |
| `kernel64.ld` | 裸二进制布局 | 新增 `.data` 输出段（吞并 `.bss` + `BYTE(0)`） |
| `kernel64.c` | IRQ1 生产者 + ring-buffer shell | 新增 `KBD_QUEUE_SIZE`、5 个队列状态量、`kbdinfo`、`kbd_dequeue`；重写 `irq1_record`、`kernel_main64_binary`；`pic_init` 常量改 `0xfd` |
| `grub.cfg` | 菜单标题 | 微小变化（标题含 "keyboard ring buffer"） |

### 3.2 增量一：队列状态与 `pic_init` 的"常开"变化

```c
#define KBD_QUEUE_SIZE 64
...
static volatile u64 irq1_count, irq1_raw_count, kbd_overflow_count;
static volatile u8 irq1_last_scancode;
static volatile u8 kbd_queue[KBD_QUEUE_SIZE];
static volatile u8 kbd_head, kbd_tail;
```

- 相比第十一课，新增 `irq1_raw_count`、`kbd_overflow_count`、`kbd_queue`、`kbd_head`、
  `kbd_tail`；`irq1_armed` 被删除（IRQ1 常开，不再需要武装标志）。
- 这些状态跨 IRQ handler 与 shell 两条执行路径共享，全部 `volatile` 防优化。
- **`pic_init()` 末尾的屏蔽字从 `0xff,0xff` 改为 `pic_masks(0xfd,0xff)`**：主片只保留
  IRQ1 未屏蔽（bit1=0），**其余全部屏蔽**；从片全屏蔽。这就是"IRQ1 常开"的硬件设置。

### 3.3 增量二：`irq1_record()` —— 生产者（关键函数）

```c
/* IRQ producer reads 0x60 exactly once and enqueues decoded make codes. */
TEXT64 void irq1_record(void){
    u8 raw=inb64(0x60),ch,next;
    irq1_last_scancode=raw;
    irq1_raw_count++;
    if(!(raw&0x80)){
        irq1_count++;
        ch=(u8)scan64(raw);
        if(ch){
            next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));
            if(next==kbd_tail) kbd_overflow_count++;
            else{ kbd_queue[kbd_head]=ch; kbd_head=next; }
        }
    }
    outb64(PIC1_COMMAND,PIC_EOI);
}
```

- **签名与职责**：IRQ1 handler 的 C 主体；无参无返回，只做"读一次、统计、解码、入队、
  EOI"五件事。它是**唯一读 `0x60` 的代码**。
- **算法步骤**：
  1. `raw=inb64(0x60)`——**恰好读一次**，这一字节的所有权归中断上下文；
  2. 更新 `last_scancode` 与 `raw_count`（每个字节都算）；
  3. `if(!(raw&0x80))`：仅处理 make code（bit7=0），break code（bit7=1）被忽略——
     键盘的释放事件不入队；
  4. `scan64(raw)` 解码（支持字母、`;`、Backspace `0x0e`、Enter `0x1c`；不支持修饰键
     与 E0/E1 前缀）；
  5. 若解码出非 0 字符：算 `next=head+1`（环绕），若 `next==tail` 队列满，
     `kbd_overflow_count++` 丢弃；否则 `kbd_queue[head]=ch; head=next` 入队；
  6. `outb64(PIC1_COMMAND,PIC_EOI)`——**最后** EOI，放行下一条 IRQ1。
- **边界与错误处理**：满队丢弃但不阻塞（有界队列的典型行为）；不认识的 make code
  返回 0 直接跳过；EOI 无条件执行（即使 break code 也要 EOI，否则 IRQ 被 hold）。

### 3.4 增量三：`kbd_dequeue()` —— 消费者（关键函数）

```c
static TEXT64 int kbd_dequeue(u8 *ch){
    u8 tail;
    __asm__ volatile("cli":::"memory");
    tail=kbd_tail;
    if(tail==kbd_head){ __asm__ volatile("sti":::"memory"); return 0; }
    *ch=kbd_queue[tail];
    kbd_tail=(u8)((tail+1)&(KBD_QUEUE_SIZE-1));
    __asm__ volatile("sti":::"memory");
    return 1;
}
```

- **签名与职责**：出队一个字符；成功返回 1 并把字符写入 `*ch`，空队返回 0。
- **算法步骤**：① `cli` 进临界区（IRQ 被推迟）；② 快照 `tail`；③ 空判
  `tail==head`——空则 `sti` 出临界区返回 0；④ 否则取 `kbd_queue[tail]`、推进
  `kbd_tail`；⑤ `sti` 出临界区返回 1。
- **为什么临界区必须关中断**：`head` 只被 IRQ handler 改、`tail` 只被 shell 改，但
  空判与推进之间若来了 IRQ 写 `head`，快照的 `tail`/`head` 就可能不一致。关中断让
  「读空判 → 出队 → 推进 tail」成为原子操作。由于临界区极短（几条指令），丢中断的
  影响可忽略——按键会稍后入队，不会丢（i8042 有内部缓冲，handler 迟早读到）。
- **为什么 shell 不碰端口**：`kernel_main64_binary` 的主循环只有 `kbd_dequeue`，没有
  任何 `inb64(0x60)`/`inb64(0x64)`——端口所有权完全归生产者。

### 3.5 增量四：`kernel_main64_binary()` —— sti 常开 + 空队睡眠

```c
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){
    u16 c=0,n=0;char cmd[32];u8 ch;
    __asm__ volatile("cli":::"memory");
    install_idt(h);
    pic_init();                              /* 结尾 pic_masks(0xfd,0xff)：IRQ1 常开 */
    clear64(&c);
    text64(&c,"TinyOS lesson 12: IRQ1 keyboard ring buffer\n64-bit C continuation active; IRQ keyboard input enabled\n");
    prompt64(&c);
    __asm__ volatile("sti":::"memory");      /* 世界就绪后才开中断 */
    for(;;){
        if(!kbd_dequeue(&ch)){ __asm__ volatile("sti; hlt":::"memory"); continue; }
        if(ch=='\n'){ putc64(&c,ch); cmd[n]=0; exec64(&c,h,cmd); n=0; }
        else if(ch=='\b'){ if(n){n--;c--;VGA[c]=0x0f20;} }
        else if(n<31){ cmd[n++]=(char)ch; putc64(&c,(char)ch); }
    }
}
```

- **启动顺序**：`cli` → 装 IDT gate（含 0x21）→ PIC 重映射并**以 `0xfd,0xff` 屏蔽
  （只留 IRQ1）** → 打 banner → 打 prompt → `sti`。开中断永远放在最后（沿用第十一课
  的"先建好世界，再开门"原则），且本课一旦打开就**不再关**。
- **主循环**：队列空 → `sti; hlt` 睡眠（见 2.3 的原子过渡）；队列有字符 →
  与第十一课相同的命令编辑逻辑（Enter 执行、Backspace 退格、31 字符上限）。
- **与第十一课循环的对比**：`if(!(inb64(0x64)&1))...` 的端口轮询彻底消失，输入来源
  从"轮询端口"变成"消费队列"。

### 3.6 增量五：`kbdinfo()` 与命令表（输出串逐字抄录自源码）

```c
static TEXT64 void kbdinfo(u16*c){
    u8 head,tail;
    __asm__ volatile("cli":::"memory");head=kbd_head;tail=kbd_tail;__asm__ volatile("sti":::"memory");
    text64(c,"keyboard: IRQ1 producer, ring-buffer shell consumer\nIRQ1 enabled: yes\nraw bytes: ");
    hex64(c,irq1_raw_count);
    text64(c,"\nmake codes: ");hex64(c,irq1_count);
    text64(c,"\noverflows: ");hex64(c,kbd_overflow_count);
    text64(c,"\nlast raw: ");hex64(c,irq1_last_scancode);
    text64(c,"\nqueue head: ");hex64(c,head);
    text64(c,"\nqueue tail: ");hex64(c,tail);
    putc64(c,'\n');
}
```

- **为什么 `kbdinfo` 也要 cli/sti**：它要同时读 `head` 与 `tail` 两个由不同执行路径
  维护的变量；在 `cli` 临界区内快照，避免打出"半新半旧"的队列状态。
- `help` 串：`commands: help about clear lminfo pinfo palloc mmap idtinfo kbdinfo bptest udtest pftest`
  （`irqinfo`/`irqtest` 被 `kbdinfo` 取代）；`about` 串：
  `TinyOS lesson 12: IRQ1 ring-buffer keyboard shell`；
- `idtinfo` 与 `breakpoint_report`/`exception_report` 的头部串同步更新为
  `TinyOS lesson 12 ...`。

### 3.7 构建管线与链接脚本增量

`Makefile` 与第十一课完全一致；唯一的构建相关增量在 `kernel64.ld` 的 `.data` 段
（见 2.4）。必须再次强调其验收含义：`readelf -rW build/kernel64.elf` 无 relocation 的
同时，`objcopy -O binary` 现在**必须把队列字节带进 `kernel64.bin`**——否则 `kbdinfo`
的计数永远不涨、甚至 handler 写队列会破坏别的数据。

### 3.8 主控制流

```text
kernel_main64_binary(h)
  → cli → install_idt (gate[3/6/14/0x21]) → pic_init (IRQ1 常开)
  → banner + prompt → sti
  → for(;;): kbd_dequeue()
       ├─ 空 → sti; hlt（挂起，等 IRQ 唤醒）
       └─ 有 → 按键字符流 → cmd[] → exec64
  IRQ1 到来 → irq1_entry（全 GPR 保存）→ irq1_record（读0x60/统计/解码/入队/EOI）
       → 全 GPR 恢复 → iretq → 回到 hlt 或主循环
```

---

## 4. 数据流与运行逻辑

- **生产者路径**：i8042 字节 → IRQ1 → stub 全保存 → `irq1_record`：`raw_count++` →
  make code？→ `count++`、`scan64` 解码 → 非 0？→ 入队或 `overflow_count++` → EOI →
  返回。`last_raw` 记录最新字节。
- **消费者路径**：主循环 `kbd_dequeue` 出队 → 按 `\n`/`\b`/普通字符处理 → `cmd[]` 积累
  → Enter 时 `exec64` 执行 → `kbdinfo` 读状态量。
- **输出串索引**（逐字抄录）：
  - banner：`TinyOS lesson 12: IRQ1 keyboard ring buffer` +
    `64-bit C continuation active; IRQ keyboard input enabled`
  - `help` → `commands: help about clear lminfo pinfo palloc mmap idtinfo kbdinfo bptest udtest pftest`
  - `about` → `TinyOS lesson 12: IRQ1 ring-buffer keyboard shell`
  - `idtinfo` → `IRQ1 vector: 0000000000000021`
  - `kbdinfo` → `keyboard: IRQ1 producer, ring-buffer shell consumer` /
    `IRQ1 enabled: yes` / `raw bytes: <hex>` / `make codes: <hex>` /
    `overflows: <hex>` / `last raw: <hex>` / `queue head: <hex>` / `queue tail: <hex>`

---

## 5. 构建、运行与验证

依赖与第十一课相同。

```bash
cd lessons/lesson-12-stable
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

预期证据：
- Multiboot2 header check 通过；`kernel64.elf` 无 relocation；
- IRQ1 为 vector `0x21`；PIC 主片屏蔽字 bit1 清零（`0xfd`）；
- handler 读 `0x60`、发 master EOI、全 GPR stub 以 `iretq` 结尾；
- **shell 路径无反汇编证据表明它读 `0x60`/轮询 `0x64`**（`kernel_main64_binary` 只有
  `kbd_dequeue`）。

运行（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

等待 `tinyos> ` prompt，用 QEMU monitor `sendkey` 或直接按键验证：

1. 输入 `help<Enter>`，确认命令能正常经队列输入；
2. 输入 `kbdinfo<Enter>`，确认 `IRQ1 enabled: yes` 且计数在增长；
3. 输入半个命令 + Backspace + 补齐，确认编辑功能正常；
4. 输入 `idtinfo<Enter>`，确认 vector `0x21`；
5. 输入 `bptest<Enter>`，再输入 `help<Enter>`，确认 #BP 返回后 IRQ 输入仍然可用。

> **本课验证记录（旧 README 保留）**：构建侧要求 Multiboot2 check 通过、内嵌 raw ELF64
> 无 relocation、IRQ1 为 vector `0x21`、PIC master mask 使 bit1 保持清零、handler 读
> `0x60` 且发 master EOI、全 GPR stub 以 `iretq` 结束；shell 中必须没有任何对 `0x60`
> 的读取或对 `0x64` 的状态轮询。运行侧：`help` 正常队列输入；`kbdinfo` 显示 IRQ1 使能
> 且计数器推进；Backspace 编辑正常；`idtinfo` 显示 vector `0x21`；`bptest` 后 `help`
> 仍可用（IRQ 输入在 #BP 恢复后持续工作）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 按键完全无响应 | IRQ1 未使能或 gate 缺失 | 确认 `pic_init` 用 `pic_masks(0xfd,0xff)`、IDT[0x21] 已装且 Present |
| 输入重复/乱序 | 有第二处读 `0x60` | 全源码搜 `inb64(0x60)`，应只出现在 `irq1_record` |
| 队列状态像随机内存 | `.bss` 没进裸二进制 | 确认 `kernel64.ld` 的 `.data` 段吞并 `.bss`；检查 `kernel64.bin` 大小 |
| 快速按键丢字 | 队列满被丢弃 | 用 `kbdinfo` 看 `overflows`；必要时调大 `KBD_QUEUE_SIZE` |
| 输入在空队后永久卡死 | `hlt` 时 IF 关着丢了唤醒 | 空队路径必须用 `sti; hlt`，且 `kbd_dequeue` 返回 0 前已 `sti` |
| 命令串被 IRQ 打断错乱 | 出队未原子化 | `kbd_dequeue` 的读判/出队/推进必须包在 `cli`/`sti` 内 |
| `bptest` 后键盘失效 | 异常返回路径破坏了 IRQ 状态 | 回归确认 #BP stub 的 RBX 保存/`iretq` 未变；IRQ1 屏蔽字未被改动 |
| handler 返回后 fault | 全 GPR 保存恢复不完整 | 对照 15 条 push/pop 严格逆序；检查 `iretq` 前 RSP 恰好指向 CPU frame |
| 中断风暴/重复进入 | 没发 EOI | `irq1_record` 结尾必须有 `outb64(PIC1_COMMAND,PIC_EOI)` |
| 反汇编有端口轮询 | shell 还在读端口 | `kernel_main64_binary` 不应有 `inb64(0x60)`/`inb64(0x64)` |
| 原始计数对但命令不动 | 解码/入队链断裂 | 检查 `scan64` 返回值、`head/tail` 环绕、`kbd_dequeue` 的 `tail==head` 空判 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux v6.12 对应实现 | 权威来源 | 简化了什么 |
|---|---|---|---|
| `irq1_record` 单读者 + 入队 | `drivers/input/serio/i8042.c` 的 `i8042_interrupt()` | i8042 规范 | Linux 走 serio/input 子系统、含 keymap/修饰键/前缀处理 |
| 64 项 ring buffer | `drivers/tty/` 的 tty flip buffer（`struct tty_port`） | Linux tty 层 | Linux 有 flow control、line discipline、wakeup 调度 |
| `cli` 短临界区 | `spin_lock_irqsave`/`local_irq_disable` | Linux 锁语义 | 本课单核、无抢占；Linux 要处理 SMP/NMI |
| `sti; hlt` 空队等待 | `arch/x86/kernel/process.c` 的 `default_idle()` 的 `sti; hlt` | Intel SDM / Linux idle | 本质同源：先开中断再 halt 防止丢唤醒 |
| `.bss`→`.data` 修补 | 正常内核中 `.bss` 是 NOBITS，由 ELF loader 按符号表清零 | ELF 规范 | 本课因 `objcopy -O binary` 无法保留 NOBITS 而手工修补 |
| handler 直接写 VGA | 真实内核绝不从 IRQ 直接刷 console | Linux console 同步 | 本课靠单核 + 无嵌套中断维持安全 |

---

## 8. 思考题与练习

1. **概念理解**：为什么「空判之后直接 `hlt`」会丢唤醒，而「`sti; hlt`」不会？请画出
   两种情形（IRQ 在 sti 前/后到达）的时序。
2. **源码定位**：在 `kernel64.c` 中指出 `irq1_record` 里"队列满时丢弃"的两个连续
   表达式，说明 `next==kbd_tail` 为什么代表满队，以及 `&(KBD_QUEUE_SIZE-1)` 的
   前提是什么。
3. **动手实验**：把 `kernel64.ld` 里的 `.data : { *(.data .data.*) *(.bss .bss.* COMMON) BYTE(0) }`
   删掉，重新构建运行，观察 `kbdinfo` 的计数/队列行为，并用调试地图第 3 行解释。
4. **动手实验**：把 `kbd_dequeue` 空队路径的 `sti` 删掉（保持 `cli` 后直接返回 0），
   重新构建，预测并验证会发生什么（提示：结合 `sti; hlt` 的空队睡眠）。
5. **Linux 对照**：阅读 `arch/x86/kernel/process.c` 中 `default_idle` 的 `sti; hlt`
   序列，解释 Linux 为什么在 idle 时也必须先开中断再 halt，与本课空队路径对照。

---

## 9. 本课小结与下一课预告

**小结**：
1. IRQ1 在 setup 后常开，handler 成为 i8042 数据端口的唯一读者，键盘所有权问题彻底
   消失——polling 从键盘路径退出。
2. 生产-消费者模型通过 64 项有界 ring buffer 解耦：handler 入队、shell 出队，
   `head`/`tail` 分别只被一方维护。
3. handler 每次只读一次 `0x60`：统计 raw/make/overflow 三个计数、忽略 break code、
   解码 make code、满队丢弃并计数，最后无条件发 master EOI。
4. 消费者出队用 `cli`/`sti` 短临界区保证原子性；空队用 `sti; hlt` 睡眠，空判与等待
   的过渡不会丢唤醒——与 Linux idle 的同款技巧。
5. `kernel64.ld` 新增 `.data` 输出段吞并 `.bss`，让队列状态进入裸二进制，否则
   `objcopy -O binary` 会丢弃 NOBITS 段。
6. `kbdinfo` 在 `cli` 临界区内快照 `head`/`tail`，提供生产-消费双方的完整观测。
7. 本课是中断驱动 I/O 的通用骨架：任何设备驱动的「中断入队 + 主循环消费 + 空等待」
   都可以照此模式实现。

**下一课**：[`lesson-13-stable/README.md`](../lesson-13-stable/README.md) 将增加 8254 PIT
周期定时器（IRQ0 tick），让中断驱动的输入与时钟并存；`irq1_entry` 的全寄存器保护、EOI
纪律与 ring buffer 生产-消费结构，会作为该课（及所有后续设备驱动）的公共基础被复用。

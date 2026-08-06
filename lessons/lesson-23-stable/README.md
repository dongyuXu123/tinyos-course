# Lesson 23: 独立 idle context 与无 runnable 时的 IRQ0 回退 — 精讲文档

> **课号**：Lesson 23（可执行课）
> **主题**：移除调度器「shell 永远 runnable」的隐含假设：引入一个**独立的静态栈 idle
> context**，当没有普通 TCB 可运行时，IRQ0 回退到 `idle_frame` 跑 `sti; hlt` 空转；
> 当某个生产者（PIT/IRQ1/信号量）让普通线程就绪后，下一次 IRQ0 再切回普通帧。
> **课程主线位置**：调度与同步阶段的可靠性转折点（Lesson 22 event/信号量 → **本课 idle**）。
> 本课让**所有**普通线程（包括 shell）都能安全阻塞，不再把 shell 当隐式兜底线程；
> Lesson 24 在此基础上加入 TSS/`rsp0`/IST 异常栈。
> **前置课程**：[`lesson-22-stable/README.md`](../lesson-22-stable/README.md)（固定 event / 计数信号量与生产者—消费者）
> **后续课程**：[`lesson-24-stable/README.md`](../lesson-24-stable/README.md)（运行时 GDT/TSS、rsp0 与 #PF IST 异常栈）
> **一句话目标**：学完本课你能说出并演示：idle 为什么「不是第四个 TCB」、IRQ0 中断 idle
> 时帧存到哪里、`next_runnable` 的 `0xff` 哨兵如何驱动「普通→idle→普通」的回退路径，
> 以及 `idletest` 如何让 shell 睡 150 个 tick 而由 idle 顶班。

---

## 1. 课程定位（Mission）

**一句话目标**：用 `idle_trampoline` + `idle_frame` + `idle_running` 三件套实现独立
空闲上下文，使调度器在「无普通 runnable」时不再依赖 shell，并保留统计证据。

- **在课程主线中的位置**：前几课调度器隐含假设「线程 0（shell）总是 runnable」——
  `next_runnable` 扫不到 worker 时 `return current_thread`，shell 成为兜底。这使 shell
  永远不能睡眠/阻塞。本课打破该假设：idle 成为唯一兜底。这也是向「用户进程可以阻塞、
  内核必须有人跑」的真实内核形态靠拢。
- **前置知识清单**：
  1. Lesson 18 的 `struct irq0_frame` 与「IRQ0 保存→调度→恢复→唯一 `iretq`」机制；
  2. Lesson 22 的 `next_runnable` 轮转、`quantum_left`、`reap_finished_threads`；
  3. `thread_sleep_ticks` 的 `sti; hlt` 阻塞循环与 `wake_sleepers` 到期唤醒；
  4. `struct thread.frame` 保存的是「该线程被打断时那份 IRQ0 帧」的指针。
- **本课交付**：`idletest` 命令（shell 睡 150 tick）；`threadinfo` 新增
  `idle switches/ticks`；`ps` 新增 idle 行（running/ready + frame + static stack）；
  `kernel_main64_binary` 在 `install_idt` 前调用 `idle_init()`。

---

## 2. 核心概念精讲

### 2.1 idle 为什么「不是第四个 TCB」

```c
static u8 idle_running;
static struct irq0_frame *idle_frame;
static u8 idle_stack[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
```

- idle **不在 `threads[]` 里**：它不能等队列（没有 `mailbox`/`wake_tick`/`received`）、
  不拥有 PMM 帧（栈是编译期静态数组，页面粒度过对齐）、被 `thread_stack_owned` 与
  `reap_finished_threads` 排除在外。
- 它是一个**独立 continuation**：一份合成 IRQ0 帧 + 一块静态栈。首次切入时
  `idle_trampoline` 把 `r12`（栈顶种子）装入 `rsp`，然后永远 `sti; hlt` 循环。
- 为什么独立？真实内核的 idle 线程专管「没活干时停 CPU」；把 shell 绑死在兜底角色
  上，shell 就永远不能阻塞。独立 idle 让「回退」与「shell 交互」彻底解耦。

### 2.2 idle_trampoline 与合成首帧

汇编侧新增：

```asm
.global idle_trampoline
idle_trampoline:
movq %r12,%rsp      ; 把合成帧里预置的 r12（栈顶）装进 rsp
1: sti              ; 开中断
hlt                 ; 空转：等下一个中断（IRQ0 每 10ms 来一次）
jmp 1b              ; 中断返回后回到循环，反复 hlt
```

`idle_init()` 构造首帧（与 worker 首帧同构，但 `rip=idle_trampoline`）：

```c
static TEXT64 void idle_init(void){
    struct irq0_frame*f=(struct irq0_frame *)(void *)(idle_stack+PAGE_SIZE-sizeof(*f));
    f->r15=f->r14=f->r13=f->r11=f->r10=f->r9=f->r8=f->rdi=f->rsi=f->rbp=
        f->rdx=f->rcx=f->rbx=f->rax=0;
    f->r12=(u64)(unsigned long)(idle_stack+PAGE_SIZE);   // 静态栈顶
    f->rip=runtime_idle_trampoline_address();
    f->cs=0x08;
    f->rflags=0x202;
    idle_frame=f;
    idle_running=0;
    idle_switches=idle_ticks=0;
}
```

`kernel_main64_binary` 在关中断、`install_idt` 之前调用 `idle_init()`——此时还没有任何
中断，idle 帧与统计安全就位。

### 2.3 IRQ0 中断 idle 时，帧存到哪里？

```c
if(idle_running){ idle_frame=f; idle_ticks++; }
else threads[current_thread].frame=(u64)(unsigned long)f;
```

- 普通线程被打断：帧存进它的 TCB（`threads[current_thread].frame`）——与旧行为一致。
- idle 被打断：帧存进 **`idle_frame`**（同一个变量，覆盖旧帧）。因为 idle 不是 TCB，
  没有 `threads[]` 槽位可存。
- `idle_ticks` 累计「idle 被中断的次数」；`idle_switches` 累计「切入 idle 的次数」
  （二者略有差异：一次连续 idle 期间会多次被打断）。

### 2.4 无普通 runnable 时的回退：0xff 哨兵

`next_runnable` 扫不到任何 `RUNNING`/`RUNNABLE` 普通线程时不再 `return current_thread`，
而是返回哨兵 `0xff`：

```c
static TEXT64 u8 next_runnable(void){
    u32 n;
    for(n=1;n<=THREAD_COUNT;n++){
        u8 i=(u8)((round_robin+n)%THREAD_COUNT);
        if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){
            round_robin=i; return i;
        }
    }
    return 0xff;                    // 无普通线程可跑 → 交给 idle 处理
}
```

`irq0_schedule` 据此分四种情形（见 3.2 节详析）：

| 情形 | next 值 | 处理 |
|---|---|---|
| 已在 idle，仍无可跑 | `0xff` 且 `idle_running` | `return f` 继续 idle（不重复计数） |
| 普通线程中，无可跑 | `0xff` 且非 idle | 置 `idle_running=1`、`idle_switches++`，`return idle_frame` |
| 在 idle，普通线程就绪 | 具体 id 且 `idle_running` | 置 `idle_running=0`，切换回该线程帧 |
| 普通线程间切换 | 具体 id 且非 idle | 原时间片切换逻辑不变 |

### 2.5 shell 也能睡了：wake_sleepers 与 thread_sleep_ticks 的放宽

- `wake_sleepers` 的扫描起点从 `i=1` 改为 `i=0`——shell 现在也可能处于
  `THREAD_SLEEPING`，必须被按时唤醒。
- `thread_sleep_ticks` 的守卫从 `if(id&&...)` 改为 `if(!idle_running&&...)`：
  任何普通线程（含 shell）在非 idle 时都能入睡；idle 本身不会调用它。
- `idletest` 就是证据：shell 打印 `idletest: shell sleeping while idle runs`，
  调 `thread_sleep_ticks(150)` 睡 150 tick，期间所有普通线程都不 runnable，
  IRQ0 切到 idle；150 tick 后 `wake_sleepers` 唤醒 shell，打印
  `idletest: shell resumed through IRQ0`。
- `reap_finished_threads` 的守卫变为 `(idle_running||i!=current_thread)`：idle 运行时
  `current_thread` 保持旧值（调度器不为 idle 更新它），若不做短路，会误把「已 finished
  的旧当前线程」跳过回收。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-22-stable） |
|---|---|---|
| `boot.S` | 32 位入口、页表、long mode、`.incbin` blob | 未变化 |
| `kernel.c` | 32 位引导阶段 | 未变化 |
| `kernel64.c` | 64 位内核本体 | **全部本课增量**（idle context） |
| `kernel64.ld` | 64 位裸 blob 布局 | 未变化（`idle_stack` 落入 `.data`） |
| `linker.ld` | 外层 ELF32 段布局 | 未变化 |
| `Makefile` | 双阶段构建、ISO、check/run | 未变化 |
| `grub.cfg` | GRUB 菜单 | 仅 `menuentry` 标题改为 "TinyOS lesson 23: independent idle context" |

> 备注：`kernel64.c` 首行注释仍为 `/* Lesson 21: ... */`，`about` 命令输出仍为
> `TinyOS lesson 22: event and counting semaphore on IRQ0 return frames`——两处都是
> 源码自身未随本课更新的陈旧文本（`help` 列表与启动横幅已更新到 lesson 23），
> 本精讲如实标注。

### 3.2 kernel64.c 增量精讲

（说明：源码为单行风格，下列代码块按语句重排以便逐行注释，每条语句与源码逐字一致；
省略与 Lesson 22 相同的 PMM/VM/IDT/键盘/event/信号量部分。）

#### 3.2.1 idle 三件套与全局计数

```c
static u8 idle_running;                          // 1=当前在 idle 上下文
static struct irq0_frame *idle_frame;            // idle 的合成帧 / 被打断时保存处
static u8 idle_stack[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));  // 静态页对齐栈
static u64 ...,idle_switches,idle_ticks;         // 切入计数 / 被打断计数
```

- `idle_stack` 在编译期占据 4 KiB `.data`，页对齐；`idle_init` 在栈顶扣去
  `sizeof(struct irq0_frame)` 构造首帧。
- `idle_switches` 与 `idle_ticks` 是本课的实际 idle 统计；旧的 `idle_worker_ticks`
  （Lesson 19 起统计「量子耗尽停在 shell」）保留为历史兼容数据。

#### 3.2.2 irq0_schedule 的 idle 改造（本课核心）

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){
    u8 old,next;
    ticks++;
    outb64(PIC1_COMMAND,PIC_EOI);
    if(idle_running){ idle_frame=f; idle_ticks++; }     // ① 中断的是 idle
    else threads[current_thread].frame=(u64)(unsigned long)f;
    wake_sleepers();                                    // ② 唤醒到期的睡眠者(含 shell)
    reap_finished_threads();
    if(!idle_running&&quantum_left){                    // ③ idle 不消耗量子
        quantum_left--;
        if(quantum_left) return f;
    }
    old=current_thread;
    next=next_runnable();                               // ④ 可能返回 0xff 哨兵
    quantum_left=TIME_SLICE_TICKS;
    if(next==0xff){                                     // ⑤ 无普通 runnable
        if(idle_running) return f;                      //   已在 idle：继续（不重复计数）
        if(threads[old].state==THREAD_RUNNING)
            threads[old].state=THREAD_RUNNABLE;         //   旧线程退回就绪
        idle_running=1; idle_switches++;
        return idle_frame;                              //   切入 idle
    }
    if(idle_running){                                   // ⑥ 从 idle 切回普通线程
        idle_running=0;
        threads[next].state=THREAD_RUNNING;
        current_thread=next;
        threads[next].switches++; preempt_switches++;
        return (struct irq0_frame *)(unsigned long)threads[next].frame;
    }
    if(next==old){ if(old==0) idle_worker_ticks++; return f; }  // ⑦ 历史兼容
    if(threads[old].state==THREAD_RUNNING) threads[old].state=THREAD_RUNNABLE;
    threads[next].state=THREAD_RUNNING;
    current_thread=next;
    threads[next].switches++; preempt_switches++;
    return (struct irq0_frame *)(unsigned long)threads[next].frame;  // ⑧ 普通切换
}
```

逐段分析：

1. **①帧归属**：idle 被打断时帧存 `idle_frame` 而非任何 TCB；这是 idle「不是 TCB」
   的落地表现。
2. **③量子语义**：`!idle_running && quantum_left` —— idle 运行期间不递减量子，
   避免「idle 空转还把量子耗尽」的无意义行为；普通线程的量子消耗逻辑不变。
3. **⑤回退进入 idle**：条件是 `next==0xff`（没有普通 runnable）。把旧线程从 RUNNING
   降为 RUNNABLE 再切入 idle，返回 `idle_frame`——IRQ0 汇编把它装入 `rsp` 后
   `iretq`，CPU 开始执行 `idle_trampoline` 的 `sti; hlt`。
4. **⑥从 idle 回到普通**：一旦 `wake_sleepers`/IRQ1/`sem_up` 之类让某普通线程
   runnable，下一次 IRQ0 的 `next_runnable` 就返回具体 id；此分支清除 `idle_running`、
   选该线程、计数并返回其帧。
5. **⑦兼容计数**：`next==old` 且是 shell 时仍计 `idle_worker_ticks`，仅作历史数据。

#### 3.2.3 wake_sleepers / thread_sleep_ticks / reap_finished_threads 放宽

```c
static TEXT64 void wake_sleepers(void){u32 i;
    for(i=0;i<THREAD_COUNT;i++)                       // 从 0 开始：shell 也能睡
        if(threads[i].state==THREAD_SLEEPING&&tick_due(ticks,threads[i].wake_tick)){
            threads[i].state=THREAD_RUNNABLE; sleep_wakeups++; }}
static TEXT64 void thread_sleep_ticks(u64 delta){
    ...
    if(!idle_running&&threads[id].state==THREAD_RUNNING){  // 非 idle 的普通线程可睡
        threads[id].wake_tick=ticks+delta; threads[id].state=THREAD_SLEEPING; }
    ...
}
static TEXT64 void reap_finished_threads(void){u32 i;
    for(i=1;i<THREAD_COUNT;i++)
        if((idle_running||i!=current_thread)&&         // idle 时 current_thread 已陈旧
           threads[i].state==THREAD_FINISHED&&threads[i].stack_phys){
            u64 p=threads[i].stack_phys; threads[i].stack_phys=0;
            (void)pmm_free_page(p); }}
```

- `wake_sleepers` 与 `thread_sleep_ticks` 放宽后，`idletest` 里的 shell 睡 150 tick
  到点即醒；`reap_finished_threads` 的短路保证 idle 运行期仍能回收 finished 栈。

#### 3.2.4 ps / threadinfo 输出增量

- `ps64` 在三行普通线程后追加 idle 行：
  ```c
  text64(c,"idle "); text64(c,idle_running?"running":"ready");
  text64(c," frame "); hex64(c,(u64)(unsigned long)idle_frame);
  text64(c," stack static\n");
  ```
- `threadinfo` 标题改为 `scheduler: PIT preemptive independent idle`，
  `current:` 变为 `idle`/`thread` + 当前 id，并新增：
  ```c
  text64(c,"\nidle switches/ticks: "); hex64(c,idle_switches);
  hex64(c,idle_ticks);
  ```

#### 3.2.5 exec64 的 idletest 命令与启动序列

```c
}else if(eq64(word,"idletest")){
    if(!noargs64(arg)) usage64(c,"idletest");
    else{
        text64(c,"idletest: shell sleeping while idle runs\n");
        thread_sleep_ticks(150);                    // shell 自己睡 150 tick
        text64(c,"idletest: shell resumed through IRQ0\n");
    }
}
```

- `kernel_main64_binary` 在 `__asm__ volatile("cli"...)` 之后、`install_idt(h)` 之前
  插入 `idle_init();`——idle 首帧在任何中断可能发生前就绪。
- `help` 列表在 `pcinfo` 后插入 `idletest`。

### 3.3 构建管线

与 Lesson 22 完全一致。`idle_stack[4096]` 是静态数组，落入 `kernel64.ld` 的 `.data`
段（`PROGBITS`），随 `objcopy -O binary` 进入镜像——因此它是「持久、零分配」的栈。
`make check` 仍校验外层 Multiboot2 header；内部 raw continuation 的 RWX LOAD 警告
属已知长期现象，与受检外层 ELF 无关。

### 3.4 主控制流

```text
kernel_main64_binary → cli → idle_init()（idle_frame 就绪）→ install_idt → pit/pic
  → 横幅("TinyOS lesson 23: independent idle context") → shell 循环
  → idletest → 打印"shell sleeping while idle runs" → thread_sleep_ticks(150)
  → shell state=SLEEPING → sti;hlt
  → 下一 IRQ0：next_runnable 返回 0xff → idle_running=1 → return idle_frame
  → idle_trampoline：sti;hlt（此后每个 IRQ0 把帧存 idle_frame、idle_ticks++）
  → 150 tick 到点：wake_sleepers 唤醒 shell（扫描含 i=0）
  → 下一 IRQ0：next=0 → idle_running=0 → return shell 帧
  → shell 打印"idletest: shell resumed through IRQ0" → 继续接受命令
```

---

## 4. 数据流与运行逻辑

1. 键入 `idletest` → 屏幕先出 `idletest: shell sleeping while idle runs`。
2. shell 置 `THREAD_SLEEPING`、`wake_tick=ticks+150`，`sti; hlt` 阻塞。
3. 下一个 IRQ0：`wake_sleepers` 未到期；`next_runnable` 找不到任何普通 runnable
   （worker 未启动、shell 在睡）→ 返回 `0xff` → `idle_switches++`、`idle_running=1`、
   `return idle_frame`。`idle_trampoline` 开始 `sti; hlt` 空转。
4. 空转期间每个 IRQ0：`idle_frame=f; idle_ticks++;`；`threadinfo` 显示
   `current: idle` 与不断增长的 `idle switches/ticks`。
5. 150 tick 到点：`wake_sleepers`（现扫描 `i=0`）把 shell 置 runnable；下一 IRQ0 的
   `next_runnable` 返回 0 → `idle_running=0`，`return shell 帧`。
6. shell 恢复打印 `idletest: shell resumed through IRQ0`，继续接受命令——证明
   「shell 睡了、idle 顶班、IRQ0 把 shell 换回来」全链路成立。
7. `pctest`/`pcgo`/`pcinfo` 回归：两 worker `blocked-event` 时同样由 idle 兜底，
   完成态不变量不变。

---

## 5. 构建、运行与验证

### 5.1 依赖

与 Lesson 22 相同（gcc、binutils、grub-common、xorriso、mtools、qemu-system-x86_64），
无新增工具。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check                     # 期望输出: Multiboot2 header check passed.
readelf -rW build/kernel64.elf # 期望: continuation 无内部重定位
nm -u build/kernel64.elf       # 期望: 无未定义符号
readelf -SW build/kernel64.elf # 期望: .data 为 PROGBITS（含 idle_stack 静态栈）
objdump -d -Mintel build/kernel64.elf  # 期望: 出现 idle_trampoline；IRQ0 仍单 iretq
readelf -lW build/kernel.elf   # 期望: 外层无 RWX LOAD 段
```

静态验收要点：IRQ0 路径仍是一条「C 选帧 + 统一寄存器恢复/`iretq`」序列；IRQ1 不得
引用 idle 调度逻辑；raw continuation 的 RWX 警告不等于外层 ELF 失败。

### 5.3 运行与 VGA 验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅逐字如下（来自
`kernel_main64_binary`）：

```text
TinyOS lesson 23: independent idle context
IRQ0 return-frame switching; high stacks, PMM, VM slot and keyboard enabled
tinyos> 
```

验证步骤（保留并扩充旧 README 的 VGA 验收流程）：

1. 运行 `idletest`：VGA 必须显示 `idletest: shell sleeping while idle runs`，约 1.5 秒
   后显示 `idletest: shell resumed through IRQ0`，随后 shell 能继续接受命令。
2. 立即 `threadinfo`：`idle switches/ticks` 必须非零——证明 shell 睡眠期间真的选过
   `idle_frame`；`current:` 应显示 `thread`（已回到 shell）。
3. `pctest` → `pcinfo`：两个 event 等待者在位；`pcgo` 后完成态 `pcinfo` 必须显示
   wake-all 为 2、produced/consumed 各 4、`P errors` 为 0、环形缓冲 `used` 为 0、
   `spaces==2`、`items==0`、`P errors/ok` 末尾 `yes`。
4. 再过一次 PIT 边界后 `ps`：两个 finished worker 的 `stack-pa` 为 0（已在 idle 期被
   `reap_finished_threads` 回收）；`meminfo` 保持 `tracked = free + used: yes`。
5. 回归 `kbdwaittest`、`sleeptest`、`preempttest`、PMM/VM（`vmtest`）、PIT
   （`tickinfo`）、键盘（`kbdinfo`）、可恢复 `bptest`。
6. 致命项 `vmfaulttest`/`pftest`/`udtest` 使用全新独立 QEMU 启动验证（预期
   CR2 `00000000003ff000`、CR2 `0000000000400000`、`#UD`）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `idletest` 后 shell 永不恢复 | `wake_sleepers` 没扫 `i=0`（或 `wake_tick` 未记录） | 确认 `wake_sleepers` 循环从 `i=0` 开始；`threadinfo` 看 `sleep wakeups` |
| `idletest` 期间 `threadinfo` 显示 `current: thread 0` 而非 `idle` | 调度没切到 idle（`next_runnable` 返回了别的值） | 检查 `next_runnable` 是否返回 `0xff` 哨兵；确认 shell 状态确为 `sleeping` |
| `idle switches` 不增长但 `idle ticks` 增长 | 在 idle 中断时 `next==0xff` 分支直接 `return f`（已 idle）——符合预期 | 这是正常行为；`idle_switches` 只在「切入」时 +1 |
| `idle ticks` 为 0 | IRQ0 从未在 `idle_running` 时打断 idle | 确认 `if(idle_running){idle_frame=f;idle_ticks++;}` 位置正确 |
| `pctest` 后 worker 栈没被回收 | `reap_finished_threads` 的 `i!=current_thread` 在 idle 期误判 | 确认守卫是 `(idle_running||i!=current_thread)` |
| `about` 显示 lesson 22 | 源码 `about` 文本未随本课更新（陈旧） | `help` 与启动横幅已更新；此为源码自身不一致 |
| 空转时 CPU 占满 | idle 没执行 `hlt`（如 `sti` 前中断未开） | 反汇编 `idle_trampoline`：应为 `movq %r12,%rsp; sti; hlt; jmp` 循环 |
| 量子在 idle 期被耗尽 | `!idle_running&&quantum_left` 判断缺失 | 确认量子递减只发生在非 idle 情形 |

---

## 7. 与 Linux 源码对照

- **idle 线程**：Linux 每 CPU 一个 idle 线程（`init_idle`，`kernel/sched/core.c`），
  运行 `cpu_startup_entry` 中的 `do_idle()`；TinyOS 的 `idle_trampoline` 是其教学版
  （单 CPU、只 `hlt`）。
- **调度器无 runnable 时的回退**：Linux `schedule()` 在 `pick_next_task` 无任务时
  选中 idle（`idle_sched_class`）；TinyOS `irq0_schedule` 的 `next==0xff` 分支即
  `pick_next_task` 的 idle 兜底。
- **`sti; hlt` 空转**：Linux `arch/x86/kernel/process.c::default_idle` 的
  `safe_halt`（`cli; sti; hlt`）；TinyOS `idle_trampoline` 的 `sti; hlt` 循环与其一致。
- **唤醒路径**：Linux `wake_up_process` 把进程置 runnable 并触发 `schedule()`；
  TinyOS `wake_sleepers`/IRQ1/`sem_up` 把线程置 runnable，下一次 IRQ0 完成切换——
  「置位与切换解耦」的两者相同。
- **教学模型简化了什么**：单核单 idle、无 per-CPU runqueue、idle 无栈守卫页、
  无 cpuidle 深度（C-states）、无 `need_resched` 位；idle 不可被 fork/迁移。

---

## 8. 思考题与练习

1. （概念理解）idle 为什么不能是第四个 TCB？如果硬塞进 `threads[]`，哪些机制会坏掉
   （`thread_stack_owned`、`reap_finished_threads`、`wake_sleepers`）？
2. （源码定位）在 `irq0_schedule` 中指出处理 `next==0xff` 的两个分支，说明为什么
   「已在 idle」分支不递增 `idle_switches`。
3. （动手实验）把 `idletest` 的 `thread_sleep_ticks(150)` 改成 `thread_sleep_ticks(0)`，
   重新运行，解释 delta=0 时 `if(!delta)delta=1` 的作用与观察到的行为。
4. （动手实验）把 `wake_sleepers` 的扫描起点改回 `i=1`，重新运行 `idletest`，
   观察 shell 是否还能醒来，验证「shell 可睡」对唤醒扫描的依赖。
5. （Linux 对照）Linux 的 `default_idle` 与 TinyOS `idle_trampoline` 都执行 `hlt`；
   两者在「何时退出 hlt」与「谁负责再次调度」上的分工有何异同？

---

## 9. 本课小结与下一课预告

**小结**：本课用「独立 idle context」打破了「shell 永远 runnable」的旧假设：`idle_stack`
静态页对齐栈 + `idle_trampoline` 的 `sti; hlt` 循环构成真正的空转上下文；`idle_init`
在开中断前构造合成首帧；`next_runnable` 返回 `0xff` 哨兵驱动「普通→idle→普通」的回退
路径；IRQ0 中断 idle 时帧存 `idle_frame`、idle 不消耗量子；`wake_sleepers`/`thread_sleep_ticks`
放宽后 shell 也能睡（`idletest` 睡 150 tick），`reap_finished_threads` 用 `idle_running`
短路解决 current_thread 陈旧问题；`idle switches/ticks` 与 `ps` 的 idle 行提供了完整证据链。

**下一课预告**：[`lesson-24-stable/README.md`](../lesson-24-stable/README.md) 将进入
异常与特权级可靠性：为内核建立**运行时 GDT/TSS**，设置 TSS 的 `rsp0`（特权级切换时的
内核栈指针）与 #PF 的 **IST** 异常栈，让任何内核路径（含 idle、阻塞中的 shell、worker）
发生异常时都有确定的内核栈可用——这是向用户态切换前的最后一块拼图。

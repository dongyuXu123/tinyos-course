# Lesson 80: 定时器驱动调度 — 精讲文档

> **课号**：Lesson 80（主线源课编号 Lesson 73 线）
> **本课主题**：定时器驱动调度（timer-driven scheduling）——PIT（8254）以 100 Hz 产生 tick，驱动 `ticks` 时钟、时间片 `quantum_left`、sleep 唤醒与 idle 调度
> **课程主线位置**：调度 / COW 元数据教学模型阶段（Lesson 64 起）。本课是 Lesson 79（主动抢占）之后的调度课，把「抢占」背后的定时器机制拆成三层：PIT 硬件配置、时钟/定时器元数据模型、sleep 与 idle 调度。
> **前置课程**：[`../lesson-79-stable/README.md`](../lesson-79-stable/README.md)（主动抢占：`irq0_schedule` 的 `quantum_left` 递减与切换决策）
> **后续课程**：[`../lesson-81-stable/README.md`](../lesson-81-stable/README.md)（context switch 上下文切换元数据：CPU 帧、栈指针交换、用户上下文保存/恢复）
> **本课一句话目标**：理解"定时器如何驱动调度"——PIT 的 100 Hz tick 是 `ticks` 时钟、时间片、sleep 唤醒、idle 切换的单一时间源，并能用 `clocktest`/`timertest`/`sleeptimetest`/`tickinfo` 做确定性验证。
> **保留的原始快照信息**：This checkpoint models bounded scheduling and copy-on-write metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l80test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能讲清楚 PIT 通道 0 如何被配置成 100 Hz、每次 IRQ0 如何推进全局 `ticks` 并驱动 `clock_model`/`timer_model`/`sleep_model` 三个元数据模型，以及 `sleeptest`/`idletest` 如何让线程"睡觉"再被定时器"叫醒"。
- **在课程主线中的位置**：本阶段的核心问题是"调度由什么推动"。Lesson 79 展示了抢占决策（`irq0_schedule`），本课回答"谁在催它跑"——定时器。三条线索：① 硬件层 `pit_init`（8254 模式 3，除数 11932 → 100 Hz）；② 时钟/定时器/sleep 三个固定元数据模型（`clock_model`/`timer_model`/`sleep_model`）与 `clocktest`/`timertest`/`sleeptimetest` 验证命令；③ 调度层的 `thread_sleep_ticks`/`wake_sleepers`/idle。下一课（81）讲上下文切换本身。
- **前置知识清单**（学本课之前必须掌握）：
  1. 抢占式调度基本概念与 `irq0_schedule` 的切换决策（Lesson 79）；
  2. 8259 PIC 与 IRQ0 中断流程：`pic_init` 重新映射、`irq0_entry` 压 GPR、EOI（Lesson 33 起）；
  3. `struct thread`/`threads[3]` 线程模型与 `start_threads`（Lesson 61 起）；
  4. VGA 文本输出 `text64`/`hex64`/`putc64` 与 `exec64` 命令分派（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_73_model` + `lesson_73_state` + `l80test`；把 `l79test` 改名为 `l72test`；`about` 与 banner 更新为「Lesson 80: 定时器驱动调度」。计时相关验证命令 `clocktest`/`timertest`/`sleeptimetest`/`tickinfo` 均可用。

---

## 2. 核心概念精讲

### 2.1 PIT：系统的心跳（tick 的来源）

**直觉**：一个操作系统的调度器需要一个"心跳"——一个每 10 ms 响一次的闹钟，让内核有机会检查"该不该把 CPU 交给别人"。这个闹钟就是 Intel 8254 可编程间隔定时器（PIT）通道 0。

**工作机制**：
- PIT 通道 0 连接 IRQ0；配置为"模式 3（方波）"后，它用 1.19318 MHz 的基准时钟除以装载值 `PIT_DIVISOR` 产生中断频率。
- 本内核：`PIT_RATE_HZ 100`、`PIT_DIVISOR 11932` → 约 100 Hz，即每 10 ms 一个 tick。
- `pit_init` 通过 I/O 端口编程：先写命令字 `0x36` 到 `PIT_COMMAND (0x43)`，再把除数低字节、高字节依次写到 `PIT_CHANNEL0 (0x40)`。

```
PIT 基准时钟 1.19318 MHz ──÷11932──► 100 Hz ──► IRQ0 ──► irq0_schedule
                                                  │
                            ticks++ / quantum_left-- / wake_sleepers()
```

### 2.2 三个定时元数据模型：clock / timer / sleep

Linux 把时间拆成多个抽象：单调时钟（jiffies/monotonic）、定时器（timer wheel）、睡眠（hrtimer/睡眠队列）。本课用三个固定容量记录对应：

```c
struct clock_model { u64 monotonic_ticks,monotonic_ns,realtime_ns,reads; };
struct timer_model { u64 deadline_tick,interval_ticks,expirations,arms,reads; u8 armed,readable,periodic,canceled; };
struct sleep_model { u64 requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes; u8 active,interrupted; };
```

- `clock_model`：把 `ticks` 换算成纳秒（`ticks*(1000000000ULL/PIT_RATE_HZ)`），模拟单调时钟读时间。
- `timer_model`：一个"单臂定时器"——`timer_arm(delay,interval)` 设定截止 tick，`timer_poll` 在每次读之前检查是否到期，支持一次性/周期模式与取消。
- `sleep_model`：一次"睡眠"的完整记账：请求的 tick 数、截止 tick、唤醒 tick、剩余 tick。

三者都遵守「固定元数据 + 确定性验证」：真实时钟中断只有 `ticks++` 一处，其余全在元数据层模拟。

### 2.3 sleep 与 idle：让出 CPU 的两种姿势

- **sleep（自愿阻塞）**：`thread_sleep_ticks(delta)` 把当前线程置 `THREAD_SLEEPING` 并记 `wake_tick=ticks+delta`，然后 `sti; hlt` 等待。每个 tick，`irq0_schedule` 调用 `wake_sleepers()` 把到期的 sleeping 线程转回 `THREAD_RUNNABLE`。
- **idle（空闲线程）**：当就绪队列为空，`irq0_schedule` 切到独立 idle 线程（`idle_trampoline` 的 `sti; hlt` 无限循环）。idle 也有自己的 4 KiB 静态栈与伪造 `irq0_frame`。
- `sleeptest` 启动两个 worker，分别睡 `SLEEP_A_TICKS=120`（1.2 s）与 `SLEEP_B_TICKS=270`（2.7 s）——用"不同唤醒时刻"验证 `wake_sleepers` 的到期判断。

### 2.4 「固定元数据 + 确定性验证」教学模型

同 Lesson 79 的教学法：元数据真实、行为不执行、确定性验证。`struct lesson_73_model` 是调度 checkpoint 的又一个投影：

```c
struct lesson_73_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

`l80test` 断言 `b==a+1` 且四标志全真，与 `l79test` 形式一致，仅模型编号与初始化值（73,74,75,76）不同——证明"新增一课的元数据不破坏既有课"。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-79） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：PIT/时钟/定时器/sleep 元数据、调度器、`exec64` 分派 | **主要增量**：新增 `struct lesson_73_model`、`lesson_73_state`、`l80test()`；把 `l79test` 改名为 `l72test`；`exec64` 的 `l80test`/`l72test` 分支与 `about`、banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 80 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的固定元数据记录与 `l80test`

```c
struct lesson_73_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_73_model lesson_73_state;
static TEXT64 void l80test(u16*c){lesson_73_state=(struct lesson_73_model){73U,74U,75U,76U,1,1,1,1};int ok=lesson_73_state.valid&&lesson_73_state.active&&lesson_73_state.ready&&lesson_73_state.accounted&&lesson_73_state.b==lesson_73_state.a+1U;text64(c,"l80test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 73 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_73_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };`：与 lesson-79 的 `lesson_72_model` 同构。4 个 u32 计数 + 4 个 u8 调度标志。
- `static struct lesson_73_model lesson_73_state;`：单一全局实例，静态限定。
- `lesson_73_state=(struct lesson_73_model){73U,74U,75U,76U,1,1,1,1};`：初始化 `a=73,b=74,c=75,d=76`，四标志为真。
- `int ok=...`：5 条件与（四标志 + `b==a+1`）。这是"调度记账一致性"的确定性断言。
- `text64(c,"l80test: ");` 与 `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 73 fallback reported");`：成功串/失败串都来自源码字面量。
- `putc64(c,'\n');`：结尾换行。

#### (b) 上一课回归测试改名为 `l72test`

lesson-79 的 `l79test` 在本课改名为 `l72test`（内容不变，仍校验 `lesson_72_state`）。至此 `exec64` 的命令集合是 `l64 l65 l69 l70 l71 l72 l80`——`lKtest` 始终对应 `lesson_K_model`，回归命名与模型编号对齐。

```c
static TEXT64 void l72test(u16*c){lesson_72_state=(struct lesson_72_model){72U,73U,74U,75U,1,1,1,1};int ok=lesson_72_state.valid&&lesson_72_state.active&&lesson_72_state.ready&&lesson_72_state.accounted&&lesson_72_state.b==lesson_72_state.a+1U;text64(c,"l72test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 72 fallback reported");putc64(c,'\n');}
```

#### (c) PIT 硬件配置：`pit_init` 与 tick 的产生

```c
static TEXT64 void pit_init(void){outb64(PIT_COMMAND,0x36);outb64(PIT_CHANNEL0,(u8)PIT_DIVISOR);outb64(PIT_CHANNEL0,(u8)(PIT_DIVISOR>>8));}
```

逐行注释：
- `outb64(PIT_COMMAND,0x36)`：写命令字到 0x43。`0x36` = 通道 0、先写低字节再写高字节（lobyte/hibyte）、模式 3（方波）、16 位计数。
- `outb64(PIT_CHANNEL0,(u8)PIT_DIVISOR)`：装载计数低字节（`11932 & 0xff`）。
- `outb64(PIT_CHANNEL0,(u8)(PIT_DIVISOR>>8))`：装载计数高字节（`11932 >> 8`）。

配置完成后 PIT 以 11932 分频产生约 100 Hz 的 IRQ0。`irq0_schedule` 的第一个动作就是 `ticks++`——**全局 tick 时钟只有这一个递增点**，这是「确定性」的基石。

#### (d) 时钟模型：`clock_update` 与 `clocktest`

```c
static TEXT64 void clock_update(void){clock_model.monotonic_ticks=ticks;clock_model.monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ);clock_model.realtime_ns=clock_model.monotonic_ns;}
static TEXT64 void clocktest(u16*c){u64 a,b;clock_update();a=clock_model.monotonic_ns;clock_model.reads++;clock_update();b=clock_model.monotonic_ns;text64(c,"clocktest: ");text64(c,b>=a&&b==ticks*(1000000000ULL/PIT_RATE_HZ)?"monotonic PIT clock conversion passed":"BROKEN");putc64(c,'\n');}
```

- `clock_update`：`monotonic_ns = ticks * (1e9 / 100)` = `ticks * 10ms`。真实内核里单调时钟是 per-CPU 的高精度计数，本课用 100 Hz tick 线性外推，作为最小教学模型。
- `clocktest`：读两次 `monotonic_ns`，断言第二次 ≥ 第一次，且等于按公式计算的精确值——确定性验证时钟换算无漂移。
- `clockinfo` 输出：`clock ticks/ns: <ticks>/<ns> reads: <n> PIT Hz: 64`（`64` 是 `PIT_RATE_HZ=100` 的十六进制，`hex64` 打印）。

#### (e) 定时器模型：`timer_arm`/`timer_poll`/`timer_read`/`timer_cancel`

```c
static TEXT64 void timer_poll(void){if(!timer_model.armed||timer_model.canceled)return;if(!tick_due(ticks,timer_model.deadline_tick))return;timer_model.expirations++;timer_model.readable=1;if(timer_model.periodic)timer_model.deadline_tick+=timer_model.interval_ticks;else timer_model.armed=0;}
static TEXT64 void timer_arm(u64 delay,u64 interval){timer_model.deadline_tick=ticks+delay;timer_model.interval_ticks=interval;timer_model.periodic=interval!=0;timer_model.armed=1;timer_model.readable=0;timer_model.canceled=0;timer_model.arms++;}
static TEXT64 u64 timer_read(void){u64 n=timer_model.readable?timer_model.expirations:0;timer_model.expirations=0;timer_model.readable=0;timer_model.reads++;return n;}
static TEXT64 void timer_cancel(void){timer_model.armed=0;timer_model.canceled=1;timer_model.readable=0;timer_model.expirations=0;}
```

- `timer_arm(delay,interval)`：设定 `deadline_tick=ticks+delay`；`interval!=0` 表示周期模式。真实 Linux 的 `mod_timer` 是全局 timer wheel 上的一个节点，本课压缩成单个 `timer_model` 记录。
- `timer_poll`：读前轮询。`tick_due(now,deadline)` 用无符号回绕比较 `(u64)(now-deadline)<(1ULL<<63)` 判断"是否已到期"——正确处理 tick 计数回绕，这是至少 3 行的实质边界分析点。
- `timer_read`：把到期数读出并清零（模拟一次性消费）；`timer_cancel` 清除 armed 并置 `canceled`。
- `timertest` 验证一次性（`timer_arm(2,0)`）、周期（`timer_arm(1,1)`）、到期可读、取消四个场景，输出 `timertest: deadline, expiration, periodic, and cancel passed`。

#### (f) sleep 模型：`thread_sleep_ticks` 与 `wake_sleepers`

```c
static TEXT64 void thread_sleep_ticks(u64 delta){u64 flags;u8 id=current_thread;if(!delta)delta=1;flags=irq_save64();if(!idle_running&&threads[id].state==THREAD_RUNNING){threads[id].wake_tick=ticks+delta;threads[id].state=THREAD_SLEEPING;}irq_restore64(flags);while(threads[id].state==THREAD_SLEEPING)__asm__ volatile("sti; hlt");}
static TEXT64 void wake_sleepers(void){u32 i;for(i=0;i<THREAD_COUNT;i++)if(threads[i].state==THREAD_SLEEPING&&tick_due(ticks,threads[i].wake_tick)){threads[i].state=THREAD_RUNNABLE;sleep_wakeups++;}}
```

- `thread_sleep_ticks(delta)`：中断保护下把当前线程置 `THREAD_SLEEPING`、记 `wake_tick`，随后 `sti; hlt` 循环自旋等待。`hlt` 让 CPU 进入停机状态，直到下一个 IRQ0 唤醒；醒来后若还在 sleeping（没到 wake_tick）继续 `hlt`——这是"省电地等待定时器"的最小实现。
- `wake_sleepers`：在每次 `irq0_schedule` 开头被调用，扫描全部线程，把 `SLEEPING` 且 `tick_due` 的线程转 `RUNNABLE`，`sleep_wakeups++`。
- `sleeptimetest` 用固定 `ticks` 值（`old+delay-1` 与 `old+delay`）模拟"到期前一 tick / 到期时刻"两个快照，断言 `remaining_ticks` 与 `active` 的转移，输出 `sleeptimetest: deadline sleep and wake accounting passed`。
- `sleeptest` 启动两个 worker：`start_threads(1)`，worker 每轮 `thread_sleep_ticks(id==1?SLEEP_A_TICKS:SLEEP_B_TICKS)`——线程 1 睡 120 tick、线程 2 睡 270 tick，唤醒时刻不同，验证到期判断。输出 `sleeptest: two timed workers started`。

#### (g) idle 线程与 `idletest`

```c
static TEXT64 void idle_init(void){struct irq0_frame*f=(struct irq0_frame *)(void *)(__idle_stack_end-sizeof(*f));f->r15=...=0;f->r12=(u64)(unsigned long)__idle_stack_end;f->rip=runtime_idle_trampoline_address();f->cs=0x08;f->rflags=0x202;idle_frame=f;idle_running=0;idle_switches=idle_ticks=0;}
```

- idle 线程复用一个静态 `idle_stack[PAGE_SIZE]` 与 `idle_trampoline`（汇编 `movq %r12,%rsp; 1: sti; hlt; jmp 1b`）。当 `irq0_schedule` 找不到可运行线程时，`idle_running=1; return idle_frame`，CPU 进入 idle 的 `sti; hlt` 循环。
- 每次 IRQ0 打断 idle，`idle_ticks++`；切出 idle 时 `idle_switches++`。
- `idletest` 命令演示 shell 自愿睡眠：
```c
else if(eq64(word,"idletest")){if(!noargs64(arg))usage64(c,"idletest");else{text64(c,"idletest: shell sleeping while idle runs\n");thread_sleep_ticks(150);text64(c,"idletest: shell resumed through IRQ0\n");}}
```
shell（线程 0）睡 150 tick，期间无其他可运行线程 → idle 运行；150 tick 后 `wake_sleepers` 把 shell 叫醒，shell 打印第二行。

#### (h) `tickinfo` 与 `exec64` 增量分支

```c
static TEXT64 void tickinfo(u16*c){u64 t;__asm__ volatile("cli":::"memory");t=ticks;__asm__ volatile("sti":::"memory");text64(c,"PIT channel 0: 0000000000000064 Hz\nticks: ");hex64(c,t);text64(c,"\nuptime (centiseconds): ");hex64(c,t);putc64(c,'\n');}
```

- `tickinfo`/`uptime` 命令：用 cli/sti 读 `ticks`（避免与 IRQ0 竞争），打印 PIT 频率 `0000000000000064`（=100 的十六进制）、当前 `ticks` 与以厘秒计的 uptime。因为 100 Hz = 每秒 100 tick，`ticks` 数值即厘秒数。

`exec64` 增量（本课新增/改名分支）：

```c
}else if(eq64(word,"l72test")){if(!noargs64(arg))usage64(c,"l72test");else l72test(c);}
}else if(eq64(word,"l80test")){if(!noargs64(arg))usage64(c,"l80test");else l80test(c);}
```

- 两个分支都挂在既有 if-else 链上；`help` 列表有意不含它们（已知教学简化）。
- `about` 分支：`text64(c,"Lesson 80: 定时器驱动调度\n");`

#### (i) 内核主入口 banner 增量

```c
text64(&c,"Lesson 80: 定时器驱动调度\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");prompt64(&c);
```

- 开机第一屏第一行 `Lesson 80: 定时器驱动调度`，第二行与 lesson-79 相同（syscall ABI + bounded reclaim 边界）。初始化序列 `pit_init(); pic_init();` 在 banner 之前完成——banner 打印时 PIT 已在 100 Hz 跑。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '定时器驱动调度' README.md
	@grep -q 'l80test' kernel64.c
	@grep -q 'Lesson 80' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 80 checks passed.'
```

- 与 lesson-79 唯一差异是三条 `grep` 的关键字与 printf 文案。
- 其余构建链不变：`kernel64.o`（`-m64 ... -mno-red-zone -mno-sse ...`）→ `kernel64.bin`（`ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary`）→ `boot.o`（`-m32` 内嵌）→ `kernel.elf`（`ld -m elf_i386 -T linker.ld`）→ `grub-mkrescue` ISO。

### 3.4 主控制流

```
GRUB ──► boot.S ──► kernel_main32 ──► long_mode_start ──► kernel_main64_binary
    ├─ pit_init()（0x36 + divisor 11932 → 100 Hz IRQ0）
    ├─ install_idt / pic_init（IRQ0 → irq0_entry）
    ├─ banner: "Lesson 80: 定时器驱动调度\nGETTICKS, ..."
    └─ for(;;) 键盘循环（sti; hlt）
        │ 每个 tick：IRQ0 ──► irq0_schedule
        │   ├─ ticks++
        │   ├─ quantum_left--（耗尽则抢占切换）
        │   ├─ wake_sleepers()（到期 sleeping 转 RUNNABLE）
        │   └─ 无就绪线程时切 idle（idle_trampoline: sti; hlt）
        ├─ "sleeptest" ──► start_threads(1) ──► worker 睡 120/270 tick
        ├─ "idletest" ──► thread_sleep_ticks(150) ──► idle 运行 → 被唤醒
        └─ "l80test" ──► lesson_73_state 校验 ──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`pit_init` 编程 PIT 到 100 Hz；`kernel_main64_binary` 打印 `Lesson 80: 定时器驱动调度` banner 与 `tinyos> `。
2. **tick 流**：每个 10 ms，IRQ0 触发 `irq0_schedule`：`ticks++` → `quantum_left--` → `wake_sleepers()` → 需要时抢占/idle 切换。
3. **时钟**：`clocktest` 读两次 `monotonic_ns` 断言单调不减且等于 `ticks*(1e9/PIT_RATE_HZ)`；`tickinfo` 打印 `PIT channel 0: 0000000000000064 Hz` 与 `uptime (centiseconds)`。
4. **定时器**：`timertest` 依次验证一次性定时器（delay 2 tick）、周期定时器（interval 1 tick）、到期读取、取消；`timer_poll` 的 `tick_due` 用无符号回绕比较。
5. **sleep/idle**：`sleeptest` 让线程 1、2 分别睡 120/270 tick；到期由 `wake_sleepers` 唤醒。`idletest` 让 shell 睡 150 tick，期间 idle 运行，随后被 IRQ0 唤醒并打印 `idletest: shell resumed through IRQ0`。
6. **checkpoint**：`l80test` 校验 `lesson_73_state` 的 5 条件，打印 `l80test: bounded scheduling and copy-on-write checkpoint passed`。

输出串与源码逐字一致：`l80test: ` + `bounded scheduling and copy-on-write checkpoint passed`；`timertest: deadline, expiration, periodic, and cancel passed`；`PIT channel 0: 0000000000000064 Hz`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致，需 `gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`（详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
make check
```

**运行**：

```bash
make run
```

> 成功画面在 QEMU 图形窗口，请勿加 `-display none`。

**验证步骤与预期输出**（输出串从源码逐字抄录）：

1. 开机第一屏应显示：
   ```
   Lesson 80: 定时器驱动调度
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l80test`，预期输出：
   ```
   l80test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l80test: Lesson 73 fallback reported`。）
3. 输入 `l72test`（回归），预期输出：
   ```
   l72test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `clocktest`，预期输出：
   ```
   clocktest: monotonic PIT clock conversion passed
   tinyos>
   ```
5. 输入 `timertest`，预期输出：
   ```
   timertest: deadline, expiration, periodic, and cancel passed
   tinyos>
   ```
6. 输入 `sleeptimetest`，预期输出：
   ```
   sleeptimetest: deadline sleep and wake accounting passed
   tinyos>
   ```
7. 输入 `tickinfo`，预期出现（`ticks` 为十六进制数字）：
   ```
   PIT channel 0: 0000000000000064 Hz
   ticks: ...
   uptime (centiseconds): ...
   ```
8. 输入 `sleeptest`，预期输出：
   ```
   sleeptest: two timed workers started
   tinyos>
   ```
9. 输入 `idletest`，预期输出：
   ```
   idletest: shell sleeping while idle runs
   idletest: shell resumed through IRQ0
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 80 checks passed.`；QEMU 中 `l80test` 与三个计时测试命令全部打印 `...passed` 即代表本课验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `tickinfo` 的 `ticks` 恒为 0 | PIT/IRQ0 未工作（`pit_init` 没执行或中断被屏蔽） | 确认 `kernel_main64_binary` 里 `install_idt`/`pit_init`/`pic_init` 在 `sti` 之前完成；`irq0_schedule` 第一行应为 `ticks++` |
| `sleeptest` 后 worker 永不醒来 | `wake_sleepers` 的 `tick_due` 判断被绕过 | 检查 `wake_sleepers` 是否在 `irq0_schedule` 中被调用；确认 `SLEEP_A_TICKS/SLEEP_B_TICKS` 值合理 |
| `timertest` 打印 `BROKEN` | `timer_model` 状态没按预期转移（如 `canceled` 没被清） | 对照 `timer_arm`/`timer_poll`/`timer_read`/`timer_cancel` 四函数的字段写点；`timertest` 的 `a/b/d/e` 四个断言逐项核对 |
| `clocktest` 打印 `BROKEN` | `ticks` 在两次读取间被 IRQ0 推进导致换算不一致 | `clocktest` 两次 `clock_update` 之间没有 `ticks` 修改；确认 `PIT_RATE_HZ` 仍是 100 |
| `make check` 失败于第一条 grep | README 主题字串与 Makefile 不一致 | `grep '定时器驱动调度' README.md` 核对字面 |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l80test` 符号 | `grep -q 'l80test' kernel64.c` |
| 输入 `idletest` 卡死 | shell 睡眠期间没有任何 tick 推进 | 确认 QEMU 未加 `-display none`、IRQ0 使能；idle 的 `sti; hlt` 依赖 PIT 中断 |
| `l80test` 打印 fallback 串 | `lesson_73_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=73,b=74`） |
| QEMU 无图形输出 | `-display` 被设为 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：PIT 与 jiffies/HZ**
- TinyOS 教学模型：`pit_init` 把 8254 配成 100 Hz，`ticks` 全局计数；`PIT_RATE_HZ 100`、`PIT_DIVISOR 11932`。
- Linux 实现：`arch/x86/kernel/i8253.c`（PIT 驱动）与 `kernel/time/timekeeping.c`。现代 Linux 用 `CONFIG_HZ`（100/250/1000）定义 tick 频率，`jiffies_64` 全局计数，`jiffies` 是其低 32 位；PIT 已退居启动期时钟与备用时钟源。
- 权威来源：Intel 8254 datasheet、Intel SDM Vol.3A §17；Linux `arch/x86/kernel/i8253.c`、`include/linux/jiffies.h`。
- 教学简化：TinyOS 的 `ticks` 直接就是 tick 数（等价 jiffies 的简化），没有 jiffies 到纳秒的 `jiffies_to_nsecs` 换算层，`clock_model.monotonic_ns` 一次性线性换算。

**对照点 2：定时器（timer wheel / hrtimer）**
- TinyOS 教学模型：单个 `timer_model`，`timer_arm`/`timer_poll` 处理一个定时器，`tick_due` 用无符号回绕比较。
- Linux 实现：`kernel/time/timer.c` 的 timer wheel（多级哈希轮，`__mod_timer`/`run_timer_softirq`）处理 tick 粒度定时器；`kernel/time/hrtimer.c` 的高精度 hrtimer 用红黑树处理亚 tick 定时器。到期检查本质都是"当前时间与到期时间的回绕安全比较"。
- 教学简化：单臂定时器 + 手动 `timer_poll`，没有 wheel、没有红黑树、没有软中断批量运行。

**对照点 3：sleep 与唤醒（睡眠队列）**
- TinyOS 教学模型：`thread_sleep_ticks` 置 `THREAD_SLEEPING` 后 `sti; hlt`；`wake_sleepers` 每 tick 线性扫描 3 个线程。
- Linux 实现：`kernel/sched/wait.c`（wait queue）、`kernel/time/hrtimer.c` 的 `schedule_timeout`，任务睡眠时挂到定时器，到期由 `wake_up_process`/`try_to_wake_up` 唤醒（`kernel/sched/core.c`）。
- 教学简化：3 线程线性扫描即可；Linux 需在唤醒路径处理负载均衡、唤醒抢占（`wakeup_preempt`）、`wake_affine` 等，TinyOS 全部省略。

**对照点 4：idle 线程**
- TinyOS 教学模型：`idle_trampoline` 的 `sti; hlt` 无限循环，`idle_frame` 是静态栈上的伪造帧。
- Linux 实现：每个 CPU 的 `idle` 线程（cpuidle 子系统，`drivers/cpuidle/`），在 `arch_cpu_idle()` 里执行 `hlt`/`mwait` 等指令并统计 idle 时间。
- 教学简化：一个静态 idle 线程 + `idle_ticks/idle_switches` 计数，无 cpuidle 状态机。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `tick_due` 用 `(u64)(now-deadline)<(1ULL<<63)` 而不是简单的 `now>=deadline`？在 tick 计数回绕（wrap-around）时哪个更安全？
2. **源码定位**：找到 `pit_init`，说明 `0x36` 各比特的含义（通道、读写方式、模式、BCD）。把 `PIT_DIVISOR` 改成 5966 后，tick 频率会变成多少？`tickinfo` 的 Hz 输出会变吗？
3. **动手实验**：把 `TIME_SLICE_TICKS` 从 2 改成 8，重新 `make run`，用 `sleeptest` + `threadinfo` 观察 `quantum left` 与 `sleep wakeups` 的变化。
4. **动手实验**：执行 `idletest` 时让 shell 睡 150 tick，期间 `threadinfo` 无法输入；改为先 `start_threads(1)`（sleeptest）再观察两个 worker 的不同唤醒时刻（120 vs 270 tick）。改完请**恢复原值**。
5. **Linux 对照**：阅读 `kernel/time/timer.c` 的 timer wheel 与 TinyOS `timer_model` 的差异，说出教学模型丢弃了哪两类能力（大量定时器、亚 tick 精度）。

---

## 9. 本课小结与下一课预告

- 本课把「定时器驱动调度」拆成 PIT 硬件、时钟/定时器/sleep 元数据、调度集成三层：`pit_init` 的 100 Hz 心跳是唯一时间源。
- 你掌握了 `ticks` 唯一递增点（`irq0_schedule` 开头）、`clock_model` 的纳秒换算、`timer_model` 的单臂定时器与回绕安全比较、`sleep_model` 的到期记账。
- 你理解了 `thread_sleep_ticks` + `wake_sleepers` 的"睡眠/唤醒"循环，以及 idle 线程在就绪队列为空时的兜底运行。
- 你验证了 `l80test`（本课 checkpoint）、`l72test`（回归改名）与四个计时测试命令的确定性输出。
- 你对照了 Linux 的 jiffies、timer wheel、wait queue、cpuidle，知道教学模型在单臂定时器与 3 线程扫描上的简化边界。

**下一课预告**：Lesson 81「context switch 上下文切换元数据」。本课的 tick 只是"敲门"，真正把 CPU 从 A 线程换到 B 线程的是 `irq0_frame` 的保存与恢复、`irq0_schedule` 返回帧指针、以及 `thread_trampoline` 的首帧伪造——下一课将逐字节精讲这些上下文切换元数据，衔接点正是本课 `irq0_schedule` 中 `threads[current_thread].frame=(u64)(unsigned long)f` 这一行。

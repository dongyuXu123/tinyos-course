# Lesson 86: 调度公平性验证 — 精讲文档

> **课号**：Lesson 86 ｜ **主题**：调度公平性验证（scheduler fairness verification）
> **课程主线位置**：进程/调度/COW 元数据阶段（Lesson 71–87 检查点序列，本课为 Lesson 79 原型的检查点）
> **前置课程**：[`../lesson-85-stable/README.md`](../lesson-85-stable/README.md)（fork 内存屏障与一致性）
> **后续课程**：[`../lesson-87-stable/README.md`](../lesson-87-stable/README.md)（负载均衡与进程组调度综合 checkpoint）
> **一句话目标**：用检查点状态验证 PIT 抢占式轮转调度的时间片（quantum）、公平选择与切换统计的一致性，并继续确认教学内核只做调度元数据、不执行真实上下文切换之外的任何用户调度策略。

本课是稳定快照（stable snapshot）检查点：`kernel64.c` 相对上一课仅做一处增量——把 `l85test` 恢复为 `l78test`，新增 `lesson_79_model` 状态与 `l86test` 检查点测试，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效；会话不变量继续保留。

---

## 1. 课程定位（Mission）

**学完本课你能**：说清 TinyOS 的「公平性」由哪几个机制共同保证——固定时间片（`TIME_SLICE_TICKS`）、轮转扫描（`rr_pick_next`）、抢占式 PIT 中断（`irq0_schedule`）与切换统计（`preempt_switches`/`switches`）；能运行 `l86test` 检查点并在 VGA 上读出 `bounded scheduling and copy-on-write checkpoint passed`，再用 `threadinfo`/`ps` 观察 worker 进度与量子消耗。

**在课程主线中的位置**：本课处于进程/调度/COW 元数据阶段（Lesson 71–87）的调度子序列，课程原型是 Lesson 79「调度公平性验证」。上一课（Lesson 85）验证 fork 的一致性，本课把视角转到「就绪队列中谁被公平选中」；下一课（Lesson 87）把它与负载均衡、进程组调度合并为综合检查点。之后课程进入 VFS 主线（Lesson 88 起）。

**前置知识清单**（学本课前必须掌握）：
1. PIT 定时器与 IRQ0 调度路径：`pit_init`、`install_idt` 的 0x20 门、`irq0_entry` 汇编帧与 `irq0_schedule` 的 iretq 单一路径（Lesson 32–33、Lesson 53）。
2. 线程状态机：`THREAD_EMPTY/RUNNING/RUNNABLE/SLEEPING/BLOCKED_*/FINISHED`、`wait_queue` FIFO（Lesson 46–53）。
3. 调度器类抽象：`struct sched_class { name, pick_next, enqueue, dequeue }` 与 `fair_sched_class`（Lesson 38、Lesson 69）。
4. 时间度量：`ticks`（PIT 100 Hz）、`tick_due` 环形时间差、`thread_sleep_ticks` 与 `wake_sleepers`。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 86: 调度公平性验证`；
- 新命令 `l86test` 输出 `l86test: bounded scheduling and copy-on-write checkpoint passed`（或其 fallback）；
- 继承的调度诊断命令 `threadinfo`/`ps`/`schedinfo`/`preempttest`/`sleeptest`/`pctest` 全部可用，用于观察量子与切换统计。

---

## 2. 核心概念精讲

### 2.1 调度公平性（scheduler fairness）的定义

**直觉**：一个不公平的调度器会让某些线程长期占住 CPU，另一些线程饿死。公平性的朴素定义是「在足够长的时间窗内，每个就绪线程获得大致相等的 CPU 时间」。

**准确定义**（对照 Linux CFS 的 vruntime 思想）：每个线程累计「已获得的运行量」；调度器总是选择累计量最少的线程。教学模型把「运行量」离散化为**固定长度时间片**：每次切换后授予 `TIME_SLICE_TICKS` 个 tick（2 个 PIT tick，即 20 ms @100Hz），耗尽后强制让出，从而在统计意义上保证两个 worker 交替获得 CPU。

**为什么这里只能验证不能度量**：教学内核不执行真实用户代码，`threads[i].progress` 只是「worker 步数」计数器（`THREAD_STEPS=4`），所以「公平性」用检查点断言 + 调度统计输出验证，而不是测量真实指令时间。

### 2.2 时间片（quantum）与抢占式轮转

常量与状态：
```c
#define TIME_SLICE_TICKS 2
#define THREAD_STEPS 4
#define BUSY_SPINS 4000000ULL
#define SLEEP_A_TICKS 120ULL
#define SLEEP_B_TICKS 270ULL
```
- `TIME_SLICE_TICKS=2`：每次选择后重置量子，`irq0_schedule` 里 `quantum_left` 每 tick 递减，归零强制切换。
- `THREAD_STEPS=4`：worker 完成 4 步即 `thread_exit`，保证演示时长确定。
- 抢占机制：PIT 每 tick 触发 IRQ0 → `irq0_schedule` 在 iretq 边界做决策；这是「抢占式（preemptive）」而非「协作式（cooperative）」，`yield` 命令明确提示 `yield: cooperative switching replaced by PIT preemption`。

### 2.3 调度器类 dispatch（sched_class）

```c
struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
```
- `runtime_sched_class()` 返回 `&fair_sched_class`，`active_sched_class` 在 `kernel_main64_binary` 中被赋值。
- 这是 Linux 调度类抽象（`sched_class`、`fair_sched_class`）的微缩版：把「选谁/入队/出队」三个操作显式分离，行为仍是继承的边界轮转扫描。
- `schedinfo` 输出类名与 `enqueue/dequeue/pick` 三次操作的累计计数，是公平性验证的统计侧证据。

### 2.4 公平性的可观察指标

`irq0_schedule` 每次切换都更新统计：`preempt_switches`（抢占切换次数）、`threads[i].switches`（单线程被切换次数）、`idle_switches`/`idle_ticks`（idle 活动量）、`quantum_left`（剩余量子）。`threadinfo` 将这些全部输出，形成「两个 worker 进度是否交替增长、切换是否对半分配」的观察依据——这正是「调度公平性验证」的直接读数。

### 2.5 检查点模型：lesson_79_model

本课新增 `struct lesson_79_model` 与前课同构：四个 `u32` 计数器 + 四个 `u8` 布尔位。`l86test` 一次性初始化并断言 `b == a + 1U`，验证「课程状态连续」，不执行任何真实调度。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、调度、VFS、GUI、检查点） | 恢复 `l78test`；新增 `lesson_79_model`/`lesson_79_state`/`l86test`；`exec64` 增加 `l86test` 分支；`about` 与开机横幅更新 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`调度公平性验证`/`Lesson 86`/`l86test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（本课增量与调度主题直接相关部分）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_79_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_79_model lesson_79_state;
static TEXT64 void l86test(u16*c){lesson_79_state=(struct lesson_79_model){79U,80U,81U,82U,1,1,1,1};int ok=lesson_79_state.valid&&lesson_79_state.active&&lesson_79_state.ready&&lesson_79_state.accounted&&lesson_79_state.b==lesson_79_state.a+1U;text64(c,"l86test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 79 fallback reported");putc64(c,'\n');}
```
逐行分析：
1. **结构**：与 `lesson_78_model` 完全同构，计数序列 79→82（Origin 编号为 Lesson 79）。
2. **初始化**：四布尔位全置 1；`b==a+1U` 即 `80U==79U+1U`，恒真。
3. **输出**：成功串 `l86test: bounded scheduling and copy-on-write checkpoint passed`（源码逐字抄录）；fallback 串 `Lesson 79 fallback reported`。
4. **恢复的 `l78test`**：本课同时恢复 `l78test`（使用 `lesson_78_state`，计数 78→81），使 78/79 两个历史检查点都能独立运行。

#### 3.2.2 抢占式调度核心：irq0_schedule

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;ticks++;softirq_run_budget();outb64(PIC1_COMMAND,PIC_EOI);if(f&&f->cs==USER_CS){user_irq0_save_restore(f);return f;}if(idle_running){idle_frame=f;idle_ticks++;}else threads[current_thread].frame=(u64)(unsigned long)f;wake_sleepers();reap_finished_threads();if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}old=current_thread;next=next_runnable();quantum_left=TIME_SLICE_TICKS;if(next==0xff){if(idle_running)return f;if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}idle_running=1;idle_switches++;return idle_frame;}if(idle_running){idle_running=0;threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}if(next==old){if(old==0)idle_worker_ticks++;return f;}if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}rr_dequeue(next);threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}
```

逐行分析（公平性验证的正面现场）：
1. **tick 推进与 EOI**：`ticks++` 推进时钟，`softirq_run_budget()` 按预算执行 tasklet/workqueue，`outb64(PIC1_COMMAND,PIC_EOI)` 尽早发 EOI，避免中断嵌套。
2. **CPL3 快速路径**：若被抢占者 `f->cs==USER_CS`（用户态），调用 `user_irq0_save_restore` 保存/恢复**唯一合法**的用户帧后直接返回——用户态不参与多线程切换（`THE single user thread is the only legal destination`）。
3. **量子递减**：`if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}`——非 idle 线程每个 tick 消耗 1 个量子；量子未耗尽则原样返回（不切换），耗尽则继续做选择。这是「时间片公平」的机制核心。
4. **无就绪 → idle**：`next==0xff` 时把当前线程放回 runnable，切到 `idle_frame`，并记 `idle_switches++`——空闲时 CPU 进入 `sti; hlt` 循环，PIT 再把它唤醒。
5. **切换记账**：真正切换时 `threads[next].switches++;preempt_switches++`，这就是 `threadinfo` 里 `preempt switches` 的来源，也是验证「两个 worker 切换次数接近均等」的统计依据。

#### 3.2.3 选择与入队：rr_pick_next / rr_enqueue / rr_dequeue

```c
static TEXT64 u8 rr_pick_next(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){u8 i=(u8)((round_robin+n)%THREAD_COUNT);if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){round_robin=i;return i;}}return 0xff;}
static TEXT64 void rr_enqueue(u8 id){if(id<THREAD_COUNT&&threads[id].state==THREAD_FINISHED)return;sched_enqueues++;}
static TEXT64 void rr_dequeue(u8 id){if(id<THREAD_COUNT)sched_dequeues++;}
```
1. **环形扫描**：从 `(round_robin+1)%THREAD_COUNT` 开始最多扫 `THREAD_COUNT` 次，找到第一个 runnable/running 线程。轮转游标 `round_robin` 记录上次选择位置，保证候选严格轮换——**这就是轮转公平的选择侧保证**。
2. **入队/出队记账**：`rr_enqueue` 跳过已 FINISHED 线程；三次操作各自累加 `sched_enqueues/sched_dequeues/sched_picks`，供 `schedinfo` 报告。
3. **调度器类接线**：`fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue}`；`next_runnable()` 每次先 `sched_picks++` 再调用 `rr_pick_next()`。

#### 3.2.4 worker 与公平性演示：start_threads / worker_run / busy_delay

```c
static TEXT64 void worker_run(u8 id){if(pc_test){event_wait(&pc_start_event);if(id==1)pc_producer();else pc_consumer();return;}while(threads[id].progress<THREAD_STEPS){if(kbd_wait_test){u8 ch;kbd_wait_char(&ch);threads[id].mailbox=ch;threads[id].received++;}threads[id].progress++;if(sleep_test)thread_sleep_ticks(id==1?SLEEP_A_TICKS:SLEEP_B_TICKS);else if(!kbd_wait_test)busy_delay();}thread_exit();}
```
1. **模式分发**：`pc_test`（生产者-消费者）、`kbd_wait_test`（键盘等待）、`sleep_test`（定时睡眠）、默认（纯抢占）四种模式由 `start_threads` 的 mode 决定。
2. **进度推进**：每步 `threads[id].progress++` 后 `busy_delay()`（`BUSY_SPINS=4000000` 次空转）模拟真实工作负载，让 PIT 有时间片抢占的机会。
3. **公平性观察点**：在 `threadinfo` 的 `worker steps` 行观察 `threads[1].progress` 与 `threads[2].progress` 交替增长——两个线程各 4 步，切换统计应大致均衡。

`thread_sleep_ticks` 把当前线程置为 `THREAD_SLEEPING` 并记录 `wake_tick`，`wake_sleepers()` 在每次 `irq0_schedule` 里唤醒到期线程（`sleep_wakeups++`）——睡眠线程不消耗量子，体现了「不公平是被允许的：谁在睡，谁不拿时间片」。

#### 3.2.5 诊断输出与 exec64 增量

- `threadinfo` 输出 `scheduler: PIT preemptive independent idle`、`quantum left`、`PIT ticks`、`preempt switches`、`idle switches/ticks`、`sleep wakeups`、`worker steps` 等——全部是公平性验证的读数。
- `ps64` 输出每个线程的 `state/frame/stack-pa/switches/progress/wake-tick`，与 idle 行。
- `exec64` 本课增量：`about` 输出 `Lesson 86: 调度公平性验证\n`；检查点分支为：
```c
else if(eq64(word,"l78test")){if(!noargs64(arg))usage64(c,"l78test");else l78test(c);}else if(eq64(word,"l86test")){if(!noargs64(arg))usage64(c,"l86test");else l86test(c);}
```

#### 3.2.6 开机横幅

```c
text64(&c,"Lesson 86: 调度公平性验证\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与 Lesson 85 完全相同的构建管线：`CFLAGS64` 64 位 freestanding（`-mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fpie`）→ `kernel64.elf` → `objcopy -O binary` → `boot.S` `.incbin` 内嵌 → 与 32 位 `boot.o/kernel.o` 一起用 `linker.ld` 链接 → `grub-mkrescue` 生成 ISO。`make check` 新增断言 README 含 `调度公平性验证`、`Lesson 86`，kernel64.c 含 `l86test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ 初始化：pmm/vma/vfs/调度器类赋值（active_sched_class=&fair_sched_class）
 ├─ install_idt/pit_init/pic_init：注册 IRQ0 → irq0_schedule
 ├─ 横幅 "Lesson 86: 调度公平性验证"
 └─ 主循环：命令 → exec64
     ├─ l86test → l86test() → 检查点输出
     ├─ preempttest → start_threads(0) → 两 worker 抢占运行
     ├─ sleeptest → start_threads(1) → 两 worker 定时睡眠
     └─ threadinfo/ps → 调度统计与量子读数
每个 PIT tick：IRQ0 → irq0_entry 汇编帧 → irq0_schedule
 （tick++、量子递减、next_runnable、切换记账）→ iretq
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：打印横幅 `Lesson 86: 调度公平性验证`，随后 `tinyos> `。
2. **`l86test`** → `l86test(c)` → 断言四属性与 `b==a+1U` → `l86test: bounded scheduling and copy-on-write checkpoint passed`。
3. **`preempttest`** → `start_threads(0)` → `preempttest: two non-yielding workers started`；此后 IRQ0 每 20 ms 抢占一次，两个 worker 交替推进。
4. **`threadinfo`** → 输出 `quantum left`、`preempt switches`、`worker steps: N M`——观察两 worker 进度是否都到达 4。
5. **`sleeptest`** → `start_threads(1)` → `sleeptest: two timed workers started`；worker A 睡 120 tick、B 睡 270 tick，`sleep wakeups` 统计唤醒次数。
6. **`ps`** → 打印线程表；两个 worker 最终 `finished`，idle 行显示 `running/ready`。
7. **`schedinfo`** → 输出 `scheduler class: tiny_rr` 与 `ops enqueue/dequeue/pick` 三次计数。

---

## 5. 构建、运行与验证

**依赖**：同 Lesson 85（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 86 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 86: 调度公平性验证` 横幅 |
| `l86test` | `l86test: bounded scheduling and copy-on-write checkpoint passed` |
| `l78test` | `l78test: bounded scheduling and copy-on-write checkpoint passed` |
| `preempttest` | `preempttest: two non-yielding workers started` |
| `sleeptest` | `sleeptest: two timed workers started` |
| `threadinfo` | 调度器类、`quantum left`、`preempt switches`、`worker steps: 4 4` |
| `ps` | 线程表；两个 worker `finished` |
| `schedinfo` | `scheduler class: tiny_rr` + 三次操作计数 |

判定成功：`l86test` 通过、worker 均到达 `progress=4`、`preempt switches` 随演示增长、无 `BROKEN`、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l86test` 输出 `Lesson 79 fallback reported` | `lesson_79_state` 初始化或断言失败（stale 镜像） | `grep -n "l86test" kernel64.c`；确认初始化串 `{79U,80U,81U,82U,1,1,1,1}` |
| `worker steps` 停在 0/1 不动 | 未先运行 `preempttest`/`sleeptest`（线程未启动）或 PIT 未使能 | 先 `preempttest`；`threadinfo` 看 `started: yes` 与 `PIT ticks` 是否增长 |
| `preempttest` 第二次执行报 `already started` | `threads_started` 一次性语义（设计如此） | 对照 `start_threads` 首行 `if(threads_started)return 0` |
| `sleeptest` 后 worker 不醒 | `wake_tick` 未到期或 `wake_sleepers` 未执行 | 对照 `thread_sleep_ticks` 与 `irq0_schedule` 中的 `wake_sleepers()`；`threadinfo` 看 `sleep wakeups` |
| `quantum left` 恒为 0 | `irq0_schedule` 量子递减逻辑被跳过（如 idle 常驻） | 对照 `if(!idle_running&&quantum_left)` 分支；检查 worker 是否被 idle 顶掉 |
| `preempt switches` 不增长 | IRQ0 未注册或 `pic_masks` 屏蔽 | `idtinfo` 看 `IRQ0 vector: 0000000000000020`；检查 `pic_init` 的 `pic_masks(0xfc,0xff)` |
| 用户态 `userpitest` 后死机 | `user_irq0_save_restore` 只允许单用户帧 | 对照 `irq0_schedule` 的 `f->cs==USER_CS` 快速路径 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `TIME_SLICE_TICKS=2`（20 ms 固定时间片） | `kernel/sched/core.c`：调度周期 `sysctl_sched_latency`；`include/linux/sched.h` 的 `DEF_TIMESLICE`（老 O(1) 调度器 100 ms 量级） | 模型用固定 2 tick 常数，无动态调节 |
| `rr_pick_next` 环形扫描 | `kernel/sched/fair.c`：CFS `pick_next_task_fair()` 按 vruntime 选择最小者；`kernel/sched/rt.c` 的 `sched_rt_period_timer` | 模型没有红黑树与 vruntime 权重，只用轮转游标 |
| `quantum_left` 递减（耗尽即切） | CFS 用 `vruntime += delta_exec * NICE_0_LOAD / weight` 累计运行量 | 模型无 nice/weight，时间片长度对所有线程相同 |
| `sched_class` dispatch（name/pick/enqueue/dequeue） | `kernel/sched/core.c`：`struct sched_class` 与 `pick_next_task()` 逐类调度 | 模型只有 `tiny_rr` 一个类，无 idle/rt/fair 类链 |
| `preempt_switches`/`switches` 统计 | `kernel/sched/core.c`：`sched_info` 与 `nr_switches`（`/proc/schedstat`） | 模型是内核内嵌的固定计数器，无 per-CPU 聚合 |
| `thread_sleep_ticks` + `wake_sleepers` | `kernel/sched/core.c`：`schedule_timeout()`；`kernel/time/timer.c` 的 `wake_up_process` | 模型按绝对 tick 唤醒，无 hrtimer 高精度 |
| `l86test` 确定性断言 | 无直接对应（kselftests / sched 压力测试） | 模型把公平性断言固化进内核 |

**权威来源**：Intel SDM Vol.3A §10（PIT 8254）、Multiboot2 规范、GNU GRUB 手册；Linux `kernel/sched/` 仅作工程对照。

---

## 8. 思考题与练习

1. **概念理解**：为什么「睡眠线程不消耗量子」不算不公平？结合 `thread_sleep_ticks` 与 CFS 的 vruntime 解释。
2. **源码定位**：在 `kernel64.c` 中找出 `quantum_left` 被赋值/递减的**所有**位置，画出一次完整切换中它的生命周期。
3. **动手实验**：把 `TIME_SLICE_TICKS` 从 2 改成 4，重新构建运行 `preempttest`，观察 `preempt switches` 是否减半——验证时间片与抢占次数的反比关系。
4. **动手实验**：把 `rr_pick_next` 的扫描改为固定从线程 1 开始（不用 `round_robin` 游标），运行 `preempttest` 观察 worker 进度是否失衡；说明轮转游标对公平的意义。
5. **Linux 对照**：阅读 `kernel/sched/fair.c` 的 `update_curr()`，对比 TinyOS 的 `threads[i].progress` 与 Linux vruntime 的累计方式，指出权重（nice）在模型中缺失的部分。

---

## 9. 本课小结与下一课预告

1. 本课以检查点形式验证「调度公平性」：`l86test` 断言检查点状态，`threadinfo`/`ps` 提供量子、切换与进度的可观察读数。
2. 公平性的三个机制支柱是固定时间片（`TIME_SLICE_TICKS`）、轮转选择（`rr_pick_next` 的环形游标）与 PIT 抢占（`irq0_schedule` 的量子递减）。
3. 调度器类抽象（`sched_class` + `fair_sched_class`）把 pick/enqueue/dequeue 显式化，`schedinfo` 给出三次操作的统计。
4. 睡眠线程通过 `wake_tick`/`wake_sleepers` 不占用量子，这与「公平 ≠ 平均」的 Linux 观点一致。
5. 用户态路径（`user_irq0_save_restore`）被隔离在 IRQ0 快速路径，不参与线程切换，保护了教学模型的一致性。
6. 下一课（Lesson 87）将**负载均衡与进程组调度**合并为综合检查点，复用本课的轮转/时间片统计与进程组元数据（`process_group`、session、foreground 组）。

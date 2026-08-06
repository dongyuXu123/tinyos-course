# Lesson 120: 跨 CPU 唤醒 — 精讲文档

> **课号**：Lesson 120（统一课程编号 120）
> **主题**：跨 CPU 唤醒（唤醒原语的完整形态：单发、广播、到期扫描、中断直投）
> **课程主线位置**：第 12 阶段「并发、SMP 与 RCU 检查点序列」的收尾检查点课
> **前置课程**：[Lesson 119 SMP 启动元数据](../lesson-119-stable/README.md)
> **后续课程**：[Lesson 121 per-CPU runqueue](../lesson-121-stable/README.md)（下一序列起点）
> **一句话目标**：把「唤醒」拆成四条路径（`waitq_wake_one` 单发、`waitq_wake_all` 广播、`wake_sleepers` 到期扫描、`irq1_record` 中断直投），说清每条路径如何把一个线程从阻塞/睡眠状态推回 `THREAD_RUNNABLE`，以及它在真实 SMP 里的「跨 CPU」语义。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能画出 TinyOS 的四条唤醒通道及其适用场景（信号量/管道/键盘的单发唤醒、事件的广播唤醒、睡眠的到期唤醒、IRQ1 的直投唤醒），并解释「唤醒 = 改状态、运行 = 等下一个时间片」这条铁律在跨 CPU 场景下的含义。
- **在课程主线中的位置**：本课是并发/SMP 检查点序列（115–120）的收尾。五课依次覆盖了排队（115）、数据隔离（116）、窗口与屏障（117）、CPU 状态（118）、启动元数据（119），本课把这些机制汇到「唤醒」这一个动作上——它是信号量、事件、睡眠、键盘等待共用的最终出口。下一课（121）起进入 per-CPU runqueue 的新主题。
- **前置知识清单**：
  1. `struct wait_queue`/`struct event`/`struct semaphore` 与 `waitq_enqueue/dequeue`（Lesson 115）；
  2. `irq_save64`/acquire-release 屏障与竞态窗口（Lesson 117）；
  3. `irq0_schedule`/`rr_pick_next` 的调度机制与 `THREAD_RUNNABLE` 语义（Lesson 118）；
  4. `thread_sleep_ticks`/`wake_tick`/`tick_due` 的睡眠记账与 PIT 时间片（`PIT_RATE_HZ=100`）。
- **本课交付**（可见结果）：
  - 新检查点命令 `l120test`（`lesson_113_model` 校验），并恢复 `l112test` 分支；
  - `pcinfo`（事件+双信号量唤醒计数）、`kbdinfo`（`direct deliveries` 直投计数）、`threadinfo`（`sleep wakeups`）三个唤醒仪表盘；
  - `pctest`/`pcgo`（广播唤醒）、`sleeptest`（到期唤醒）、`kbdwaittest`（中断直投）三个可复现实验；
  - 启动横幅与 `about` 均标注本课主题 `跨 CPU 唤醒`。

---

## 2. 核心概念精讲

### 2.1 「唤醒」到底是什么

**定义**：唤醒是「把一个不再具备运行条件、因此被调度器忽略的线程，重新置为可调度」的单向状态迁移：

```
THREAD_SLEEPING / THREAD_BLOCKED_KBD / THREAD_BLOCKED_EVENT / THREAD_BLOCKED_SEM
        ─────────────────────────── 唤醒 ───────────────────────────▶  THREAD_RUNNABLE
```

**为什么只是改状态**：TinyOS 单核下，唤醒者与被唤醒者共享同一颗 CPU。唤醒者不可能「把 CPU 交给」被唤醒者（那会嵌套调用），只能把状态改好，由下一个 IRQ0 时间片的 `irq0_schedule` 把它挑出来。这条铁律是「唤醒 ≠ 立刻运行」的教学内核。

**跨 CPU 语义**：真实 SMP 里，唤醒者所在的 CPU A 和被唤醒线程所在的 CPU B 不是同一颗。Linux 的做法是 `try_to_wake_up` 把任务放入 B 的运行队列后，给 B 发一个重调度 IPI（`smp_send_reschedule`），让 B 在下一个中断/边界点自行检查。TinyOS `NR_CPUS=1`，没有 IPI，因此用「改状态 + 等下一次 IRQ0」来**建模**这个跨 CPU 交接——这是本课最重要的简化，也是理解后文 per-CPU runqueue 的跳板。

### 2.2 四条唤醒通道

| 通道 | 函数 | 唤醒对象 | 唤醒者上下文 | 计数 |
|------|------|----------|--------------|------|
| 单发唤醒 | `waitq_wake_one` | 队列头部一个 `THREAD_BLOCKED_*` 线程 | `sem_up`/`pipe_try_*`/`irq1_record` | `q->wake_one` |
| 广播唤醒 | `waitq_wake_all` | 队列里所有 `THREAD_BLOCKED_EVENT` 线程 | `event_set`（`pcgo`） | `q->wake_all` |
| 到期唤醒 | `wake_sleepers` | 所有 `wake_tick` 已到的 `THREAD_SLEEPING` 线程 | IRQ0 时间片 | `sleep_wakeups` |
| 中断直投 | `irq1_record` 内的 `waitq_wake_one` | 一个 `THREAD_BLOCKED_KBD` 线程，并顺带投递字符到 mailbox | IRQ1 中断 | `kbd_direct_deliveries` |

### 2.3 丢失唤醒与重试（回顾 + 收口）

四条通道都必须与「检查-入队」配对，否则会丢失唤醒：
- `sem_down`/`event_wait`/`kbd_wait_char` 在 `irq_save64` 临界区内「检查条件 + 入队」，条件不满足才阻塞；
- 唤醒者同样在临界区内「改条件 + 出队 + 改状态」；
- 两边都持同一把「关中断」屏障，因此不存在「唤醒发生在入队之前、信号被永久错过」的窗口——唤醒要么唤醒已入队的线程，要么被唤醒者随后的检查直接看到（事件 `signaled`、信号量 `count`）。

### 2.4 唤醒与调度器的衔接

被唤醒线程回到 `THREAD_RUNNABLE` 后，只是「候选」；真正回到 CPU 上执行要等 `rr_pick_next` 在下次 IRQ0 选中它。被唤醒线程从挂起点的 `sti; hlt` 之后的 `for(;;)` 重新竞争条件（`sem_down` 重查 `count`、`event_wait` 重查 `signaled`、`kbd_wait_char` 重查 `mailbox`），从而完成「唤醒 → 调度 → 恢复执行 → 重验证」闭环。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-119） |
|------|------|------------------------------|
| `boot.S` | 32 位入口 / 长模式 / 内嵌 kernel64.bin | 未变化 |
| `kernel.c` | 32 位页表与 handoff 构建 | 未变化 |
| `kernel64.c` | 64 位内核主体（四条唤醒通道与命令） | **唯一增量**：`lesson_113_model`/`l120test`、恢复 `l112test`、exec64 分支、about/banner |
| `kernel64.ld` | 64 位段布局与守卫栈 ASSERT | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `Makefile` | 构建/检查/运行 | 仅 `check` grep 串换成本课主题 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

> **勘误说明**：旧 README 声称命令为 `l113test`，但源码 `exec64` 中本课新增命令为 `l120test`（`kernel64.c` 中不存在 `l113test` 分支）；`l112test` 在本课恢复，检查点链条为 `l107test … l112test → l120test`。

### 3.2 `kernel64.c` 精讲

#### 3.2.1 通道一：`waitq_wake_one`（单发唤醒）

```c
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){
  u8 id;if(!waitq_dequeue(q,&id))return 0;                 /* 队列空：没有可唤醒者 */
  if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0; /* 越界/状态不符：丢弃 */
  threads[id].state=THREAD_RUNNABLE;                       /* 唤醒=置可调度 */
  q->wake_one++;*out=id;return 1;}
```
- 三个调用方：`sem_up`（唤醒 `THREAD_BLOCKED_SEM`）、`pipe_try_write/read`（唤醒 `THREAD_BLOCKED_EVENT`）、`irq1_record`（唤醒 `THREAD_BLOCKED_KBD`）；
- 状态参数 `state` 让一个通用队列服务于多种阻塞原因，唤醒时精确匹配，避免误唤醒一个正在别处阻塞的线程；
- 返回 1 表示真的唤醒了一个线程，调用方据此记账（`s->wakes++`、`pipe_model.wake_readers++` 等）；
- 队列空时返回 0：信号量/管道此时「信号保留在计数里」，等待者到达时直接通过检查，不会丢失。

#### 3.2.2 通道二：`waitq_wake_all`（广播唤醒）与 `event_set`

```c
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){
  u8 id,n=0;while(waitq_dequeue(q,&id))
    if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}
  q->wake_all+=n;return n;}
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;
  e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
```
- `waitq_wake_all` 循环出队直到清空，把每个 `THREAD_BLOCKED_EVENT` 者置 `THREAD_RUNNABLE`，返回唤醒个数累计进 `wake_all`；
- `event_set` 先 `signaled=1` 再广播：后到的等待者因 `signaled` 已置位而直接返回，先到者被广播唤醒——两种时序都不会丢事件；
- `pcgo` 命令打印 `pcgo: event set; broadcast wake-all issued`，`pcinfo` 的 `E waits/all` 与 `E sig/wait` 可验证广播是否生效；
- **跨 CPU 视角**：广播在 Linux 里对应 `__wake_up_locked`/`wake_up_all` 唤醒等待队列上的全部任务，每个任务再经 `try_to_wake_up` 投递到各自目标 CPU。

#### 3.2.3 通道三：`wake_sleepers`（到期唤醒）

```c
static TEXT64 void wake_sleepers(void){u32 i;
  for(i=0;i<THREAD_COUNT;i++)
    if(threads[i].state==THREAD_SLEEPING&&tick_due(ticks,threads[i].wake_tick)){
      threads[i].state=THREAD_RUNNABLE;sleep_wakeups++;}}
```
- 由 `irq0_schedule` 在每个 PIT 时间片开头调用，是纯「时钟驱动」的唤醒：不依赖显式的 signal，只比较 `ticks >= wake_tick`；
- `tick_due(now,deadline)` 用 `(u64)(now-deadline)<(1ULL<<63)` 的环绕安全比较，避免 `ticks` 回绕问题；
- 被唤醒线程的 `thread_sleep_ticks` 从 `while(state==THREAD_SLEEPING) sti; hlt` 挂起点恢复，回到调用者继续执行——`idletest` 演示的正是 shell 睡 150 tick 后由这条通道救回；
- **跨 CPU 视角**：Linux 对应时钟软中断里对 per-CPU `timer_list` 的扫描，到期的定时器会唤醒对应任务并可能触发目标 CPU 的重调度。

#### 3.2.4 通道四：`irq1_record`（中断直投）

```c
TEXT64 void irq1_record(void){u8 raw=inb64(0x60),ch,next,id;irq1_last_scancode=raw;irq1_raw_count++;
  if(!(raw&0x80)){irq1_count++;ch=(u8)scan64(raw);if(ch){
    if(waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id)){  /* 有等待线程：直投到 mailbox */
      threads[id].mailbox=ch;threads[id].mailbox_ready=1;kbd_direct_deliveries++;}
    else {next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));        /* 无人等待：退回公共环形队列 */
      if(next==kbd_tail)kbd_overflow_count++;else{kbd_queue[kbd_head]=ch;kbd_head=next;}}}}
  outb64(PIC1_COMMAND,PIC_EOI);}
```
- 运行在 IRQ1 中断上下文，是唯一「唤醒 + 数据投递」一体的通道：`waitq_wake_one` 命中后直接把按键写进线程的 `mailbox` 并置 `mailbox_ready`；
- `kbd_direct_deliveries` 统计直投次数，`kbdinfo` 显示 `direct deliveries: N` 与 `wake-one: N`；
- 无人等待时退回 `kbd_queue` 公共环形缓冲（`kbd_overflow_count` 防溢出），等待者下次循环再查 mailbox；
- `kbd_wait_char` 的「先查 mailbox、再入队、被唤醒后重查」与直投通道构成完整的「中断生产者 → 等待队列消费者」模型。

#### 3.2.5 唤醒的消费者侧：`event_wait` / `sem_down` / `kbd_wait_char`

```c
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;
  for(;;){u64 flags=irq_save64();
    if(e->signaled){irq_restore64(flags);return;}            /* 已被广播过：立即返回 */
    if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}
    irq_restore64(flags);
    while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}  /* 挂起等唤醒 */
```
- 三个等待原语结构同构：临界区里「查条件 → 入队 → 阻塞」；挂起期间 `sti; hlt` 让出 CPU；被唤醒后**回到 `for(;;)` 重查条件**；
- 重查不是可有可无：广播唤醒多个等待者后，第一个恢复者可能把资源（`count`/`signaled`）取走，后恢复者必须重等——这就是唤醒后的「二次竞争」；
- 这一步把四条唤醒通道与消费者闭环，也呼应 lesson-117 的竞态窗口分析。

#### 3.2.6 检查点增量：`l120test`

```c
struct lesson_113_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_113_model lesson_113_state;
static TEXT64 void l120test(u16*c){lesson_113_state=(struct lesson_113_model){113U,114U,115U,116U,1,1,1,1};
int ok=lesson_113_state.valid&&lesson_113_state.active&&lesson_113_state.ready&&lesson_113_state.accounted
        &&lesson_113_state.b==lesson_113_state.a+1U;
text64(c,"l120test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 113 fallback reported");putc64(c,'\n');}
```
- 模型号推进到 `lesson_113`，同时恢复 `l112test`（`lesson_112_state`）分支，检查点链条 `l107test → … → l112test → l120test` 齐整；
- 初始值 `{113,114,115,116,1,1,1,1}` 恒满足 `b==a+1`，输出恒为 `l120test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；
- `about`/横幅为 `Lesson 120: 跨 CPU 唤醒`。

### 3.3 构建管线（Makefile / linker）

- 构建链路与 lesson-115~119 完全一致（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` → `kernel64.ld` → `boot.S` 内嵌 → 外层 ELF → `grub-mkrescue`）；
- `make check` grep 串：`跨 CPU 唤醒`、`l120test`、`Lesson 120`，全过后打印 `Multiboot2 and Lesson 120 checks passed.`。

### 3.4 主控制流

```mermaid
graph TD
    A[kernel_main64_binary] --> B[横幅 Lesson 120: 跨 CPU 唤醒]
    B --> C[键盘循环]
    C --> D[exec64]
    D -->|pctest| E[两个 worker 阻塞在 pc_start_event]
    D -->|pcgo| F[event_set: signaled=1 + waitq_wake_all 广播]
    F --> G[event_wait 的 for(;;) 重查 signaled → 放行]
    G --> H[pc_producer/pc_consumer: sem_up/sem_down 单发唤醒]
    D -->|sleeptest| I[thread_sleep_ticks 置 SLEEPING+wake_tick]
    I --> J[IRQ0: wake_sleepers 到期→RUNNABLE]
    D -->|kbdwaittest| K[worker 阻塞 kbd_waitq]
    K --> L[IRQ1: irq1_record 直投 mailbox + waitq_wake_one]
    L --> M[kbd_wait_char 重查 mailbox → 收字符]
    D -->|l120test| N[lesson_113_model 校验→通过串]
```

---

## 4. 数据流与运行逻辑

1. **广播唤醒**：敲 `pctest` 建两个 worker（`event_wait` 各入队 `pc_start_event.waitq`、置 `THREAD_BLOCKED_EVENT`）；敲 `pcgo` → `event_set` 先置 `signaled` 再 `waitq_wake_all` 广播 → 两 worker 状态回 `THREAD_RUNNABLE`；下一个 IRQ0 时间片 `rr_pick_next` 依次选中它们，`event_wait` 重查 `signaled` 通过、放行进入 `pc_producer`/`pc_consumer`；`sem_up`/`sem_down` 用单发唤醒交换空位与货物；
2. **到期唤醒**：`sleeptest` 让 worker1 睡 120 tick、worker2 睡 270 tick（`wake_tick=ticks+delta`）；每个 IRQ0 里 `wake_sleepers` 比对 `tick_due(ticks,wake_tick)`，到期的置 `THREAD_RUNNABLE`（`sleep_wakeups++`）；
3. **中断直投**：`kbdwaittest` 后按键，IRQ1 的 `irq1_record` 命中 `kbd_waitq` 里的 worker，把字符写进其 `mailbox`（`kbd_direct_deliveries++`），worker 从 `sti; hlt` 醒来重查 mailbox 收字符；
4. **检查点**：敲 `l120test` 填充 `lesson_113_state` 并断言，打印 `l120test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。

---

## 5. 构建、运行与验证

**依赖**：`gcc`、`ld`、`objcopy`、`grub-file`、`grub-mkrescue`、`qemu-system-x86_64`。

**构建**：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 成功输出：
```
Multiboot2 and Lesson 120 checks passed.
```

**运行**：
```bash
make run
```
> 成功画面在 QEMU 图形窗口（VGA 终端），请勿加 `-display none`。

**验证步骤**（预期输出串全部从 `kernel64.c` 逐字抄录）：

1. 启动横幅：`Lesson 120: 跨 CPU 唤醒`；
2. `about` → `Lesson 120: 跨 CPU 唤醒`；
3. `l120test` → `l120test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；
4. `pctest` → `pctest: producer and consumer blocked on start event; run pcgo`；
5. `pcgo` → `pcgo: event set; broadcast wake-all issued`；随后 `pcinfo` 的 `P prod/cons` 应为 `4 4`、`P errors/ok` 应为 `0 yes`，`E sig/wait` 显示 `1 0`；
6. `kbdwaittest` → `kbdwaittest: two FIFO keyboard waiters started`，按键后 `kbdinfo` 的 `direct deliveries` 增长；
7. `sleeptest` → `sleeptest: two timed workers started`，随后 `threadinfo` 的 `sleep wakeups` 增长；
8. 回归：`threadinfo`、`ps`、`lockatomictest`、`hhtest`、`pipetest`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `pcgo` 后 `pcinfo` 显示 `P prod/cons` 停在 0 | `event_set` 广播未生效：`waitq_wake_all` 状态参数不是 `THREAD_BLOCKED_EVENT`，或 worker 未入队 | 看 `pcinfo` 的 `E waits/all`；确认 `event_wait` 入队时 `threads[id].state==THREAD_RUNNING` |
| `pcinfo` 显示 `P errors/ok: 非0 no` | 生产序号与消费期望错位 | 检查 `pc_producer`/`pc_consumer` 的 `irq_save64` 临界区是否包住环形缓冲读写 |
| `kbdwaittest` 后按键 worker 不醒 | IRQ1 未装或 `irq1_record` 未调用 `waitq_wake_one` | `kbdinfo` 看 `IRQ1 enabled: yes` 与 `make codes` 是否增长；确认直投分支命中 |
| `sleeptest` 后 worker 永不醒 | `wake_tick` 未设置或 `wake_sleepers` 未在 IRQ0 调用 | `threadinfo` 看 `sleep wakeups`；检查 `thread_sleep_ticks` 的 `wake_tick=ticks+delta` |
| 唤醒后线程「醒一次就消失」 | 被唤醒者 `for(;;)` 重查条件不满足，又立即阻塞，视觉上像没醒 | 用 `threadinfo` 观察 `worker steps` 是否逐次递增；对照 `pcinfo` 的 `S/I count/wait` |
| `make check` 失败 | README/源码缺主题串 | 确认 README 含 `跨 CPU 唤醒` 与 `Lesson 120`，kernel64.c 含 `l120test` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学简化 |
|------------------|----------------|----------|
| `waitq_wake_one`（出队+置 RUNNABLE+计数） | `kernel/sched/wait.c` 的 `__wake_up_common`（唤醒等待队列头一个）、`wake_up_process` | TinyOS 只改 `threads[id].state`，无 `task_struct` 迁移、无 `select_task_rq` |
| `waitq_wake_all` + `event_set` 广播 | `__wake_up_common` 的 exclusive 处理与 `wake_up_all`；`struct completion` 的 `complete_all` | TinyOS 无 WQ_FLAG_EXCLUSIVE 语义，一律唤醒全部 |
| `wake_sleepers` 到期扫描 + `tick_due` | `kernel/time/hrtimer.c` 的到期回调；`wake_up_process` 把到期任务放回运行队列 | TinyOS 每个 IRQ0 线性扫 `THREAD_COUNT` 个线程，无红黑树定时器 |
| `irq1_record` 中断直投 mailbox | `drivers/input/serio` 键盘中断唤醒等待队列；`wake_up_interruptible` 配合 `tty` 缓冲 | TinyOS 直投失败退公共环形队列，无 `tty_buffer` 层 |
| 唤醒后由 IRQ0 调度器择机恢复 | `kernel/sched/core.c` 的 `try_to_wake_up`：`ttwu_queue` 把任务放入目标 CPU 运行队列，必要时 `smp_send_reschedule` 发 IPI | TinyOS 单核无 IPI、无运行队列迁移；`wake_up` 到 `run` 的延迟=至多一个时间片 |
| `sem_up`/`event_set` 的「改条件+唤醒」原子段 | `kernel/locking/semaphore.c` 的 `up()` 在 `raw_spin_lock_irqsave` 内 `__up` | TinyOS 用关中断代替自旋锁；Linux 还需 `preempt_disable` 保护 |

权威来源：Intel SDM Vol.3A（中断与 IF 位）、Linux `kernel/sched/core.c`（try_to_wake_up/ttwu_queue）、`kernel/sched/wait.c`、`include/linux/wait.h`。

---

## 8. 思考题与练习

1. **概念理解**：为什么「唤醒」只是改状态而不能立即切换 CPU？在 TinyOS 里，从 `sem_up` 调用到被唤醒线程真正运行，最坏要等多久？（提示：`TIME_SLICE_TICKS` 与 IRQ0 频率）
2. **源码定位**：在 `kernel64.c` 中找出所有把线程状态从 `THREAD_BLOCKED_*` 或 `THREAD_SLEEPING` 改回 `THREAD_RUNNABLE` 的代码行，按 §2.2 的四条通道归类，并记录对应的计数器变量。
3. **动手实验**：把 `event_set` 中 `waitq_wake_all` 的状态参数改成 `THREAD_BLOCKED_SEM`，重新构建运行 `pctest`/`pcgo`，观察两个 worker 是否卡死，并用 `pcinfo` 解释原因。
4. **动手实验**：修改 `wake_sleepers`，让它在 `tick_due` 满足时把 `wake_tick` 设为 `ticks+delta`（可重复唤醒），观察 `threadinfo` 的 `sleep wakeups` 如何增长，说明真实内核为何需要 `del_timer` 语义。
5. **Linux 对照**：阅读 `kernel/sched/core.c` 中 `try_to_wake_up` 的 `ttwu_queue` 与 `smp_send_reschedule` 路径，说明「跨 CPU 唤醒」在 Linux 里为何需要 IPI；如果把 TinyOS 扩到 `NR_CPUS=2`，本课的四条通道各自需要增加什么同步手段？

---

## 9. 本课小结与下一课预告

- 本课把「唤醒」这一最终动作拆成四条通道：单发（`waitq_wake_one`）、广播（`waitq_wake_all`）、到期（`wake_sleepers`）、中断直投（`irq1_record`）；
- 每条通道都遵守铁律：唤醒 = 置 `THREAD_RUNNABLE`，运行 = 等下一个 IRQ0 时间片，被唤醒者回到 `for(;;)` 重查条件；
- 四条通道与消费者的「检查-入队-挂起-重查」严格配对，借 `irq_save64` 屏障杜绝丢失唤醒；
- 唤醒计数（`wake_one/wake_all/sleep_wakeups/kbd_direct_deliveries`）由 `pcinfo`/`threadinfo`/`kbdinfo` 可视化，实验可重复；
- 检查点推进到 `lesson_113_model`，命令 `l120test` 恒输出通过串，`l112test` 分支恢复；
- 下一步 [Lesson 121 per-CPU runqueue](../lesson-121-stable/README.md) 进入新主题：把「唤醒后去哪排队」从单一全局扫描升级为每 CPU 独立的运行队列——本课的「唤醒-调度-恢复」闭环将直接变成 runqueue 的 enqueue/pick 操作。

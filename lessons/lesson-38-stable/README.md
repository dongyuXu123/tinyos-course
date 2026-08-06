# Lesson 38: Linux 风格有界等待队列、wake_one/wake_all 与 scheduling-class 抽象 — 精讲文档

> **课号**：Lesson 38（可执行课）
> **主题**：Linux 风格有界等待队列、`wake_one`/`wake_all` 与 scheduling-class 抽象
> **课程主线位置**：第 5 阶段「Linux 风格内核抽象」第二课。上一课引入
> `task_struct` 元数据，本课把「同步原语（事件/信号量/键盘等待）」与「调度器入口」
> 改造成 Linux 形状：等待队列操作改名对齐 `wait.h`，调度器包一层
> `struct sched_class` 函数指针表对齐 `kernel/sched/sched.h`。
> **前置课程**：[`lesson-37-stable/README.md`](../lesson-37-stable/README.md)
> **后续课程**：[`lesson-39-stable/README.md`](../lesson-39-stable/README.md)
> **一句话目标**：能讲清楚「有界 FIFO 等待队列 + wake_one/wake_all」与
> 「sched_class 函数指针表」这两个 Linux 抽象在 TinyOS 里的教学等价物，
> 并用 `schedinfo` 观察到 enqueue/dequeue/pick 三个操作的真实计数。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——Linux 为什么用「等待队列」把阻塞线程挂起来、
用 `wake_up` 系列唤醒；为什么调度器把「选下一个任务」抽象成 `sched_class` 的
`pick_next_task`/`enqueue_task`/`dequeue_task` 操作集；并在 TinyOS 里指出
对应的 `struct wait_queue`、`waitq_wake_one/waitq_wake_all`、
`struct sched_class` 与 `tiny_rr` 实现。

- **在课程主线中的位置**：继承 lesson-33/34 的 PIT 抢占调度（`irq0_schedule`）、
  lesson-35/36 的键盘/事件/信号量等待队列、lesson-37 的 `task_struct` 档案。
  本课**不改调度策略**（仍是 round-robin 扫描），只把调用点改成通过
  `active_sched_class` 的函数指针走——这是「抽象边界」而非「新策略」。
- **前置知识清单**：
  1. `struct wait_queue`（环形 FIFO、`head`/`tail`/`count`）与
     `event`/`semaphore` 的 wait/signal 语义（lesson-35/36）；
  2. `irq0_schedule` 的抢占流程：`quantum_left`、`next_runnable()`、
     idle 进入/退出、`preempt_switches`（lesson-33/34）；
  3. `THREAD_BLOCKED_KBD/EVENT/SEM` → `THREAD_RUNNABLE` 的迁移不变量；
  4. lesson-37 的 `task_table`/`task_transition`（本课保留但休眠，见 3.4 节）。
- **本课交付**：`schedinfo` 命令显示当前调度类名与三个操作计数器；
  `waitq_push`/`waitq_pop` 更名为 `waitq_enqueue`/`waitq_dequeue`；
  `fair_sched_class`（`tiny_rr`）作为唯一激活类；`kbdinfo`/`pcinfo`/
  `threadinfo` 继续展示等待队列活动。

---

## 2. 核心概念精讲

### 2.1 概念一：Linux 等待队列 —— 「先挂起，后唤醒」

Linux 中当一个任务想等待某个事件时，它把自己挂到该事件的等待队列上并睡眠，
事件发生时内核用 `wake_up()` 把队首/所有等待者唤醒。TinyOS 用固定数组
`struct wait_queue` 做同样的事，但容量是 `WAIT_QUEUE_CAP`（= `THREAD_COUNT-1`）。

```c
struct wait_queue { u8 ids[WAIT_QUEUE_CAP],head,tail,count; u64 enqueues,wake_one,wake_all; };
```

- `ids[]`：环形数组，存等待者的线程 id（u8）；
- `head`/`tail`：环形缓冲头尾；`count`：当前排队人数；
- `enqueues`/`wake_one`/`wake_all`：三个只增计数器，供 `kbdinfo`/`threadinfo` 观察。

### 2.2 概念二：enqueue / dequeue 与 wake_one / wake_all

本课最重要的改名是 `waitq_push`→`waitq_enqueue`、`waitq_pop`→`waitq_dequeue`，
让函数名与 Linux `enqueue`/`dequeue` 词汇对齐。四个原语的职责：

| 函数 | 动作 | 语义 |
|---|---|---|
| `waitq_enqueue(q,id)` | 入队（满则失败） | 阻塞前的「挂起」步骤 |
| `waitq_dequeue(q,&id)` | 出队（空则失败） | 唤醒时的「取等待者」步骤 |
| `waitq_wake_one(q,state,&out)` | 出队一个且状态必须匹配 | 唤醒单个等待者 |
| `waitq_wake_all(q,state)` | 出队全部且逐个校验状态 | 广播唤醒（event_set 用） |

**核心不变量**：唤醒不只是「出队」，还必须把 `THREAD_BLOCKED_*` 改为
`THREAD_RUNNABLE`——即出队 + 状态迁移一步完成。状态不匹配的等待者不会被误唤醒
（防御性校验）。

### 2.3 概念三：Linux `struct sched_class` —— 调度器把「策略」变成「对象」

Linux 的调度器不是一个大 switch，而是每类调度策略实现一组操作：
`pick_next_task()`（选下一个）、`enqueue_task()`（把任务放进运行队列）、
`dequeue_task()`（从运行队列取出）。`struct sched_class` 把这些操作包成
函数指针表，`sched_class_highest`/`for_each_class` 链式遍历。TinyOS 抄这个形状：

```c
struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static struct sched_class *active_sched_class;
```

`name` 用于 `schedinfo` 显示；`pick_next` 封装旧 `next_runnable()`；
`enqueue`/`dequeue` 在 `irq0_schedule` 的切换边界被调用并计数。

### 2.4 概念四：抽象边界 vs 新策略 —— 本课的纪律

`tiny_rr`（`fair_sched_class`）的三个实现函数只是把旧逻辑包起来：
`rr_pick_next` 就是原来的 `next_runnable` 扫描，`rr_enqueue`/`rr_dequeue`
只做计数（真实 run 队列在课程里就是 `threads[]` 数组本身，无需搬动）。
**结论**：行为不变，但以后想加第二个 `sched_class`（比如 `tiny_idle`）只需
实现三个函数并切换 `active_sched_class`——这就是 Linux 多调度类模型的骨架。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-37） |
|---|---|---|
| `boot.S` | Multiboot2 → long mode | 未变化 |
| `kernel.c` | 32 位引导 | 未变化 |
| `kernel64.c` | 64 位内核主体 | **核心**：sched_class 表 + 等待队列改名 + `schedinfo` |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | 未变化 |
| `grub.cfg` | 装载 | 未变化 |

### 3.2 结构 / 宏 / 全局变量精讲

```c
/* Lesson 38: Linux scheduler classes are represented by a tiny dispatch table.
 * The policy remains the inherited bounded round-robin scan; the abstraction
 * makes enqueue/dequeue and pick-next explicit without changing behavior. */
struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static u64 sched_enqueues, sched_dequeues, sched_picks;
static u8 rr_pick_next(void);
static void rr_enqueue(u8 id);
static void rr_dequeue(u8 id);
static struct sched_class fair_sched_class;
static struct sched_class *active_sched_class;
```

逐行注释：
- 头注释明确声明：这是「tiny dispatch table」，策略仍是继承的 round-robin；
- `struct sched_class`：4 个成员——名字 + 3 个函数指针；`u8` 返回值表示线程 id
  （`0xff` 表示无可运行）；
- `sched_enqueues/sched_dequeues/sched_picks`：三个全局计数器，只增不减；
- 三个函数先声明后定义，`fair_sched_class` 需要在定义后初始化；
- `active_sched_class`：当前激活类指针，`next_runnable()` 与 `irq0_schedule`
  都通过它间接调用——这就是「可替换策略」的挂钩点。

等待队列相关的既有定义（本课改名后统一为 enqueue/dequeue 词汇）：

```c
#define WAIT_QUEUE_CAP (THREAD_COUNT-1)
struct wait_queue { u8 ids[WAIT_QUEUE_CAP],head,tail,count; u64 enqueues,wake_one,wake_all; };
struct event { u8 signaled; volatile struct wait_queue waitq; u64 sets,resets,waits,wakes; };
struct semaphore { u8 count,max; volatile struct wait_queue waitq; u64 downs,ups,blocks,wakes,overflows; };
```

`volatile` 修饰 `waitq`：等待队列会被 IRQ 上下文（`irq1_record` 里的
`waitq_wake_one`）与普通线程上下文并发读写，必须用 volatile 防止编译器重排。

### 3.3 函数精讲：等待队列四原语

**`waitq_enqueue` / `waitq_dequeue`**（改名后的环形队列读写）

```c
static TEXT64 int waitq_enqueue(volatile struct wait_queue*q,u8 id){
    if(q->count>=WAIT_QUEUE_CAP)return 0;
    q->ids[q->head]=id;q->head=(u8)((q->head+1)%WAIT_QUEUE_CAP);
    q->count++;q->enqueues++;return 1;}
static TEXT64 int waitq_dequeue(volatile struct wait_queue*q,u8 *id){
    if(!q->count)return 0;*id=q->ids[q->tail];
    q->tail=(u8)((q->tail+1)%WAIT_QUEUE_CAP);q->count--;return 1;}
```

算法步骤（enqueue）：① 满则失败（`count>=WAIT_QUEUE_CAP`，有界）；② 写 `ids[head]`
并把 head 环形前进（`%WAIT_QUEUE_CAP`）；③ 计数与 `enqueues` 递增。
dequeue 对称：空则失败，否则取 `ids[tail]`、tail 环形前进、计数递减。
边界：u8 取模保证 head/tail 永远不会越界；容量固定为 `THREAD_COUNT-1`，不会
出现「等待者超过槽位」的无界增长。

**`waitq_wake_one` / `waitq_wake_all`**（唤醒 = 出队 + 状态迁移）

```c
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){
    u8 id;if(!waitq_dequeue(q,&id))return 0;
    if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0;
    threads[id].state=THREAD_RUNNABLE;q->wake_one++;*out=id;return 1;}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){
    u8 id,n=0;while(waitq_dequeue(q,&id))
        if(id&&id<THREAD_COUNT&&threads[id].state==state){
            threads[id].state=THREAD_RUNNABLE;n++;}q->wake_all+=n;return n;}
```

分析（每个 ≥3 行）：
- `waitq_wake_one`：① 出队一个等待者；② 防御校验：id 必须非 0、合法且当前状态
  正是传入的 `THREAD_BLOCKED_*`——避免把已变性的线程错误唤醒；③ 状态改为
  `THREAD_RUNNABLE`、`wake_one++`、回传 id。它同时被键盘 IRQ1
  （`irq1_record` 的「直接投递」路径）与 `sem_up` 使用；
- `waitq_wake_all`：循环出队全部，逐个做同样校验并计数 `n`，最后
  `wake_all += n` 并返回 n。`event_set` 用它做广播；如果队列里混有非
  `THREAD_BLOCKED_EVENT` 的等待者，会被安全跳过（防御性设计）；
- 两个函数都不关中断：调用方负责（`sem_up`/`event_set`/`irq1_record`
  都在关中断或中断上下文内调用）；
- 命名与 Linux `__wake_up_common` 的「唤醒一个/唤醒全部」语义对应，
  但 Linux 用双向链表 + `wait_queue_entry`，教学模型用环形数组 + u8 id。

**调用点改名**：`event_wait`、`sem_down`、`kbd_wait_char` 里原来的
`waitq_push(&e->waitq,id)` 全部改为 `waitq_enqueue(...)`，语义零变化——
这是纯命名层对齐，验证方式是对照 lesson-36/37 的行为回归。

### 3.4 函数精讲：sched_class 三个实现与挂钩

**`rr_pick_next` / `rr_enqueue` / `rr_dequeue` 与 `fair_sched_class`**

```c
static TEXT64 u8 rr_pick_next(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){
    u8 i=(u8)((round_robin+n)%THREAD_COUNT);
    if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){
        round_robin=i;return i;}}return 0xff;}
static TEXT64 void rr_enqueue(u8 id){if(id<THREAD_COUNT&&threads[id].state==THREAD_FINISHED)return;sched_enqueues++;}
static TEXT64 void rr_dequeue(u8 id){if(id<THREAD_COUNT)sched_dequeues++;}
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
static TEXT64 u8 next_runnable(void){sched_picks++;return active_sched_class->pick_next();}
```

- `rr_pick_next`：完全复用旧 `next_runnable()` 的环形扫描——从 `round_robin+1`
  开始找 `RUNNABLE` 或 `RUNNING` 的线程，找到就推进 `round_robin`；扫完
  一圈返回 `0xff`（无任务可切，走 idle）；
- `rr_enqueue`：本课它只是计数；唯一的特殊分支是跳过 `THREAD_FINISHED` 的
  线程（已结束的不算入运行队列活动）；
- `rr_dequeue`：同理只计数；`id<THREAD_COUNT` 防御越界；
- `fair_sched_class` 是静态初始化：名字 `tiny_rr` + 三个实现函数；
- `next_runnable` 现在是**包装函数**：先 `sched_picks++` 再调用当前类的
  `pick_next`——所有选任务的路径都从这里走，计数集中在一处。

**`irq0_schedule` 中的挂钩点**（切换/进入 idle 时）

```c
if(next==0xff){if(idle_running)return f;
    if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;
        active_sched_class->enqueue(old);}
    idle_running=1;idle_switches++;return idle_frame;}
...
if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;
    active_sched_class->enqueue(old);}
active_sched_class->dequeue(next);
threads[next].state=THREAD_RUNNING;current_thread=next;
threads[next].switches++;preempt_switches++;
return (struct irq0_frame *)(unsigned long)threads[next].frame;
```

- 进入 idle 时把旧线程 `enqueue`（记录被换出）；
- 真正切换时先 `enqueue(old)` 再 `dequeue(next)`，顺序与 Linux
  `__schedule` 的「把当前任务入队/出队、取下一个」一致；
- `dequeue(next)` 的语义：从运行队列取出下一个，表示「它被选中执行」；
- 三处计数（`sched_enqueues`/`sched_dequeues`/`sched_picks`）随每次 PIT 抢占增长，
  `schedinfo` 即可观察。

**`schedinfo`**（观察命令）

```c
static TEXT64 void schedinfo(u16*c){text64(c,"scheduler class: ");
    text64(c,active_sched_class?active_sched_class->name:"none");
    text64(c,"\nops enqueue/dequeue/pick: ");hex64(c,sched_enqueues);
    text64(c," ");hex64(c,sched_dequeues);text64(c," ");hex64(c,sched_picks);
    text64(c,"\nwait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions\n");}
```

先显示类名（`tiny_rr`），再显示三个计数器，最后一行声明等待队列纪律。

**关于 task 表的休眠（源码事实）**：lesson-38 的 `kernel_main64_binary` 不再执行
lesson-37 的 `task_table[0..3]` 初始化与 `task_table_validate()`（该段在
37→38 的 diff 中被移除）。因此本课 `task_table` 全槽 `valid==0`：
`tasklist` 只打印表头、`taskvalidate` 会输出 `BROKEN`——这是中间快照的既有事实，
lesson-39 用 `task_model_init()` 恢复初始化。`task_transition`/`task_table_validate`
等函数仍然存在并被编译，但调用 `task_transition` 会因 `!valid` 静默返回 0。

### 3.5 构建管线（Makefile / linker）

Makefile 与 lesson-37 完全一致，无新增目标/标志。关键点回顾：64 位侧
`-m64 -ffreestanding -fpie -mno-red-zone`，32 位侧 `-m32`；
`check` 目标跑 `grub-file --is-x86-multiboot2`；`run` 用
`qemu-system-x86_64 -accel tcg -cdrom build/kernel.iso`。`kernel64.ld` 的
`KEEP`/`ALIGN` 与栈守卫段不变。

### 3.6 主控制流

```text
kernel_main64_binary
  ├─ active_sched_class=&fair_sched_class; 三个计数器清零
  ├─ pmm_init / address_space_init
  ├─ GDT/TSS/IDT/PIT/PIC 初始化
  ├─ 打印 lesson-38 横幅
  └─ 键盘循环 → exec64：
        schedinfo / kbdwaittest / pctest+pcgo / pcinfo / threadinfo / kbdinfo / ...
  └─ IRQ0 路径：irq0_entry → irq0_schedule
        → next_runnable()(=active_sched_class->pick_next())
        → enqueue(old)/dequeue(next) → iretq 恢复新帧
```

---

## 4. 数据流与运行逻辑

以 `kbdwaittest` + 敲键盘为例串起 wake_one：

```text
输入 "kbdwaittest" → start_threads(2) → 两个 worker 调 kbd_wait_char
  → 各自 waitq_enqueue(&kbd_waitq,id) 入队 → 状态置 THREAD_BLOCKED_KBD
敲任意键 → IRQ1 → irq1_record
  → waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id) 成功
  → threads[id].state=THREAD_RUNNABLE；kbd_waitq.wake_one++；kbd_direct_deliveries++
  → 被唤醒 worker 拿到 mailbox 字符，继续推进 progress
查看 "kbdinfo"：waiters / wake-one / direct deliveries 与计数一致
查看 "threadinfo"：kbd enqueue/one/all 三个等待队列计数器
```

以 `pctest`/`pcgo`/`pcinfo` 串起 wake_all：

```text
pctest → 生产者/消费者阻塞在 pc_start_event（event_wait 各自 waitq_enqueue）
pcgo   → event_set(&pc_start_event)
  → waitq_wake_all(&pc_start_event.waitq,THREAD_BLOCKED_EVENT) 广播
  → 两个 worker 全部变 RUNNABLE，事件 wakes++ 
pcinfo → E sig/wait、E waits/all、S/I 信号量计数、P prod/cons/errors/ok
```

调度类计数路径：`irq0_schedule` 每次时间片结束调用 `next_runnable()`
（`sched_picks++`）并在切换点 `enqueue(old)`/`dequeue(next)`；
`preempttest` 跑一会后 `schedinfo` 能看到三个计数都在增长。

---

## 5. 构建、运行与验证

### 5.1 依赖

与旧课相同：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 header check passed.`。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：

```text
TinyOS lesson 38: Linux-style wait queues and scheduling-class model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

验证步骤（命令与预期输出，输出串从源码逐字）：

```bash
about
```

预期：`TinyOS lesson 38: bounded wait queues and scheduler-class dispatch`

```bash
schedinfo
```

预期（源码逐字字符串，计数为十六进制动态值）：

```text
scheduler class: tiny_rr
ops enqueue/dequeue/pick: 0000000000000000 0000000000000000 0000000000000000
wait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions
```

```bash
preempttest     # 先启动两个 worker
schedinfo       # 一段时间后三个计数非零
kbdwaittest     # 然后敲几个键
kbdinfo         # waiters/wake-one/direct deliveries 反映唤醒活动
pctest          # 生产-消费测试
pcgo            # pcgo: event set; broadcast wake-all issued
pcinfo          # E/S/I/P 计数；P errors/ok 最终为 yes
```

回归基线（继承命令）：`processtest`（`process lifecycle: bounded two bounded
program objects ready`）、`vmtest`（`vmtest: two-slot dual-alias
map/ownership/unmap/free passed`）、`taskvalidate`（本课输出 `BROKEN`，
原因见 3.4 节「task 表休眠」——这是源码事实，lesson-39 修复）、
`syscallinfo`、`cpl3test`、`userpitest` 保持行为不变。

### 5.4 课程实测记录（2026-08，稳定快照）

`make clean && make -j` 与 `make check` 通过；`schedinfo` 显示
`scheduler class: tiny_rr`；`preempttest` 运行后三个计数递增；
`kbdwaittest` 敲键后 `kbdinfo` 的 wake-one/direct deliveries 增长；
`pctest`+`pcgo`+`pcinfo` 最终 `ok: yes`；`taskvalidate` 确认为 `BROKEN`
（task 表休眠）。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `schedinfo` 显示 `none` | `active_sched_class` 未赋值 | 检查 `kernel_main64_binary` 首行 `active_sched_class=&fair_sched_class` |
| `schedinfo` 计数一直为 0 | 未运行任何会产生抢占的测试 | 先 `preempttest`/`sleeptest` 再查 `schedinfo` |
| `kbdwaittest` 敲键无反应 | IRQ1 未开或 `pic_masks` 掩码错误 | `kbdinfo` 查 raw bytes；确认主片掩码 `0xfc` |
| `pcinfo` 的 `ok` 为 no | 生产/消费步数没跑完或序列错乱 | 检查 `pc_producer`/`pc_consumer` 的 progress 与 `pc_expected` |
| `taskvalidate` 显示 `BROKEN` | 本课 task 表未初始化（源码事实） | 复习 3.4 节；lesson-39 的 `task_model_init` 会修复 |
| 唤醒后线程仍阻塞 | `waitq_wake_one` 状态不匹配被防御性跳过 | 核对唤醒时传入的 `THREAD_BLOCKED_*` 与线程实际状态 |
| 等待队列「丢等待者」 | 环形队列满（`WAIT_QUEUE_CAP`） | `threadinfo` 查 `kbd waiters` 与 `count`；容量=THREAD_COUNT-1 |
| help 列表顺序与 lesson-37 不同 | 本课在列表中加入 `schedinfo` | 对照源码 `exec64` 的 help 字符串逐字检查 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `kernel/sched/wait.c`、`include/linux/wait.h`、
`kernel/sched/sched.h` 与 `kernel/sched/core.c`**：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `struct wait_queue { u8 ids[]; head; tail; count; }` | `include/linux/wait.h` 的 `wait_queue_head_t` + `wait_queue_entry`（双向链表） | 教学模型用**固定环形数组**替代链表；Linux 的等待者可以动态加入/删除 |
| `waitq_enqueue`/`waitq_dequeue` | `__add_wait_queue` / `__remove_wait_queue` | 命名对齐；Linux 的入队可带 `exclusive` 标志 |
| `waitq_wake_one` | `wake_up` / `wake_up_process`（`kernel/sched/core.c`） | 教学模型限定「状态必须为 THREAD_BLOCKED_* 才唤醒」 |
| `waitq_wake_all` | `wake_up_all` / `__wake_up_common`（`kernel/sched/wait.c`） | `event_set` 广播对应 Linux 事件对象的 `wake_up_all` |
| `struct sched_class { pick_next; enqueue; dequeue; }` | `kernel/sched/sched.h` 的 `struct sched_class`（`pick_next_task`/`enqueue_task`/`dequeue_task`/`check_preempt_curr`…） | 教学模型只保留 3 个操作；Linux 还有 `check_preempt_curr`、`yield_task`、`task_tick` 等 |
| `fair_sched_class` + `active_sched_class` | `kernel/sched/fair.c` 的 `fair_sched_class`、`sched_class_highest` 链 | 教学模型只有**一个**类且不含 CFS 红黑树；`active_sched_class` 是切换挂钩 |
| `next_runnable()` 包装 `pick_next` | `__schedule()` 里 `pick_next_task()` | 选任务入口集中、可插拔 |
| `THREAD_RUNNABLE/RUNNING/BLOCKED_*` | `include/linux/sched.h` 的 `TASK_RUNNING`、`TASK_INTERRUPTIBLE` 等 | lesson-37 的状态位已对齐；本课在此基础上做阻塞/唤醒迁移 |

**权威来源**：Intel SDM（IRQ/`cli; hlt` 语义）、Multiboot2 规范、GNU GRUB——
均为既有机制背景，本课未引入新硬件。

**教学模型简化了什么**：
1. Linux 等待队列是链表 + `wake_up` 会检查 `task->state` 并调用
   `try_to_wake_up`（涉及 rq 锁、SMP）；教学模型只在单核、关中断临界区内做
   数组出队 + 状态赋值；
2. Linux `struct sched_class` 按优先级链式遍历、支持抢占检测；教学模型只有
   一个 `tiny_rr`，且 `enqueue`/`dequeue` 只是计数（真实调度仍由
   `irq0_schedule` 直接操作 `threads[]`）；
3. 无动态分配：等待者上限 = `THREAD_COUNT-1`，不建模「等待者超过容量」的
   Linux 路径（Linux 可以任意多）；
4. 无 SMP、无 runqueue 锁、无 CFS 红黑树、无优先级与 `nice` 值。

---

## 8. 思考题与练习

1. **概念理解**：`waitq_wake_one` 为什么要在唤醒前校验 `threads[id].state==state`？
   如果去掉这行，什么场景会出错？（提示：线程可能已自行离开阻塞态。）
2. **源码定位**：找出 `waitq_enqueue` 的所有调用点（`event_wait`、`sem_down`、
   `kbd_wait_char`），说明每个调用点入队后的线程状态分别是什么。
3. **动手实验**：在 `rr_enqueue` 里把 `sched_enqueues++` 改成
   `sched_enqueues+=2`，重新构建运行 `preempttest` 后查 `schedinfo`，
   观察计数翻倍，然后改回（勿提交）。
4. **Linux 对照**：打开 `kernel/sched/wait.c` 的 `__wake_up_common`，对照
   `waitq_wake_all`，列出「链表 vs 数组」「带锁 vs 关中断」两处差异。
5. **设计思考**：如果要新增一个 `tiny_idle_favor` 调度类（优先选 idle），
   需要实现哪三个函数、在哪个文件里注册、如何切换 `active_sched_class`？

---

## 9. 本课小结与下一课预告

**小结**：本课把「同步」与「调度」两块改造成 Linux 形状——等待队列用
`waitq_enqueue`/`waitq_dequeue`/`waitq_wake_one`/`waitq_wake_all` 四个原语，
唤醒 = 出队 + `THREAD_BLOCKED_* → THREAD_RUNNABLE` 一步完成；调度器新增
`struct sched_class` 函数指针表（`tiny_rr`），`next_runnable` 与
`irq0_schedule` 的切换点通过 `active_sched_class` 间接调用并计数；
`kbdwaittest`/`pctest`/`pcgo` 继续验证 wake_one/wake_all 的有界行为。
一个源码事实：本课 task 表休眠（`taskvalidate` 显示 BROKEN），lesson-39
恢复初始化。

**下一课预告**：进入 [`lesson-39-stable/README.md`](../lesson-39-stable/README.md)，
在 task 表之上加 `fork_model`/`clone_model` 元数据模拟：子任务获得新的
PID/TID、parent 指向父任务、用户镜像元数据复制、内核资源显式共享，
并新增 `task_model_init()` 修复 task 表初始化。

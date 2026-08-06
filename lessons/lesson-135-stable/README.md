# Lesson 135: 调度与并发综合诊断 — 精讲文档

> **课号**：Lesson 135（可执行课，checkpoint 快照）
> **主题**：调度与并发综合诊断——把 PIT 抢占式调度器（`irq0_schedule`）、轮转
> pick 算法、信号量/事件/等待队列与生产者-消费者模型串成「CPU 怎么分、线程怎么
> 等、诊断怎么看」的一张图，并追加 checkpoint 模型 `lesson_128_model`。
> **课程主线位置**：诊断/网络主题的「检查点课」序列（Lesson 133–138），位于
> Lesson 134（内存压力诊断）之后、Lesson 136（SMP/RCU 回归验证）之前。
> **前置课程**：[`lesson-134-stable/README.md`](../lesson-134-stable/README.md)
> **后续课程**：[`lesson-136-stable/README.md`](../lesson-136-stable/README.md)
> **一句话目标**：学完本课你能说清 TinyOS 的调度与并发全貌——IRQ0 每个 tick 如何
> 换线程、`rr_pick_next` 如何挑下一个、信号量/事件如何让线程阻塞与唤醒、生产者-
> 消费者如何用双信号量不丢序，以及 checkpoint 模型 `l135test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂调度子系统三层——①时间片轮转（`irq0_schedule` +
`rr_pick_next` + `fair_sched_class` 分发表）；②阻塞同步（`wait_queue`/`event`/
`semaphore` 与 `sti; hlt` 自旋等待）；③生产者-消费者（`pc_producer`/`pc_consumer`
双信号量，`pctest`→`pcgo`→`pcinfo` 三命令联动）；并掌握新增 `lesson_128_model`/
`l135test` 断言与 `threadinfo`/`ps`/`schedinfo` 诊断命令。

- **在课程主线中的位置**：与 Lesson 133–138 同属「诊断/网络主题的检查点课」。
  `kernel64.c` 相对 Lesson 134 仅 3 处增量：`l134test`→`l127test` 改名、新增
  `struct lesson_128_model` 与 `l135test`、exec64/about/banner 文案换成「调度与
  并发综合诊断」。调度与并发机制全部继承自早期课程，本课侧重讲解，不新增机制。
- **前置知识清单**：
  1. IRQ0/PIT 中断路径：`irq0_entry` 压 GPR → `irq0_schedule` 在 `iretq` 边界换栈；
  2. 线程对象模型：`threads[3]`（shell + 两个 worker）、`THREAD_*` 状态机、
     `struct irq0_frame` 保存现场；
  3. `wait_queue`/`event`/`semaphore` 的元数据实现与 `sti; hlt` 等待循环；
  4. `pmm_alloc`/`pmm_free_page`（Lesson 134 视角）为线程栈分配帧。
- **本课交付**：理解调度与并发诊断仪表盘（`threadinfo`/`pcinfo`/`ps`/`schedinfo`）；
  会用 `preempttest`/`pctest`/`pcgo` 做并发实验；`l127test`、`l135test` 两个
  checkpoint 测试。

---

## 2. 核心概念精讲

### 2.1 概念一：抢占式轮转调度（preemptive round-robin）

**直觉**：PIT 每 100 Hz 触发一次 IRQ0。每次 tick，`irq0_schedule` 决定「继续跑当前
线程」还是「换下一个」。这与 Linux 的 `scheduler_tick`→`schedule` 抢占路径同构，
只是 TinyOS 的 pick 是固定轮转。

**工作机制**：
```text
IRQ0 到来 → 压现场 → irq0_schedule(f)：
  ① ticks++，先跑 softirq 预算；
  ② 用户态帧直接保存/恢复，不做线程切换；
  ③ 时间片 quantum_left 未耗尽 → 返回原帧（不切换）；
  ④ 耗尽 → next = rr_pick_next()（从 round_robin 顺时针找 RUNNABLE/RUNNING）；
  ⑤ 无可跑线程 → 切 idle；否则切到 next 的保存帧。
```
`rr_pick_next` 从 `round_robin+1` 开始扫 3 个线程，命中第一个可跑的并更新
`round_robin` 指针；找不到返回 `0xff`。这就是「时间片轮转」最朴素的教学版。

### 2.2 概念二：阻塞同步原语（wait queue / event / semaphore）

**直觉**：线程不能永远 busy-loop 等条件。TinyOS 把「等」实现为：关中断登记进
`wait_queue` → 置 `THREAD_BLOCKED_*` → 开中断 `sti; hlt` 睡到被唤醒。

- **wait queue**：环形数组存等待线程 id，`waitq_enqueue`/`waitq_dequeue`/
  `waitq_wake_one`/`waitq_wake_all` 四件套，容量 `THREAD_COUNT-1`。
- **event**：一个 `signaled` 位 + 一个 waitq。`event_set` 置位并广播唤醒
  `THREAD_BLOCKED_EVENT` 等待者；`event_wait` 若已置位立即返回。
- **semaphore**：`count` 计数 + 上限 `max` + waitq。`sem_down` 有资源减一返回，否则
  登记并阻塞；`sem_up` 增一（超 `max` 记 `overflows++`）并 `wake_one` 一个
  `THREAD_BLOCKED_SEM` 等待者。

**为什么**：生产者-消费者只需要两个信号量就能正确同步——这是 Dijkstra 经典教学
模型，TinyOS 用它直接驱动 `pc_producer`/`pc_consumer` 两个真实 worker 线程。

### 2.3 概念三：生产者-消费者（producer-consumer）

```text
pc_spaces 信号量 = 缓冲空位（初始 PC_BUFFER_CAP=2）
pc_items   信号量 = 缓冲商品数（初始 0）
生产者：sem_down(pc_spaces) → 写 pc_buffer → sem_up(pc_items)
消费者：sem_down(pc_items)  → 读 pc_buffer → sem_up(pc_spaces)
```

`pctest` 用 mode=3 启动两个 worker 并让他们阻塞在 `pc_start_event`；`pcgo` 触发
`event_set` 广播放行；之后每个 worker 各做 4 步（`THREAD_STEPS`），用 `busy_delay`
制造交替。消费者校验 `value==pc_expected`，不匹配记 `pc_sequence_errors`——这是
「并发正确性」的量化指标，`pcinfo` 里输出 `P errors/ok`。

### 2.4 概念四：确定性 checkpoint 模型

**直觉**：检查点课用「结构体 + 赋值 + 断言」固化主题。Lesson 135 新增
`struct lesson_128_model`，`l135test` 赋为 `{128U,129U,130U,131U,1,1,1,1}` 后做
五连断言（`valid && active && ready && accounted && b==a+1`），输出恒为
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。字段 `a` 从
`128U` 起头 = 课号 135 − 7，回锚到 Lesson 128 检查点。**教学模型：不执行任何并发
代码，只校验元数据自洽。**

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 134） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（959 行）：调度/并发/进程/VFS/GUI/checkpoint | `l134test`→`l127test`；新增 `struct lesson_128_model`、`l135test`；exec64 增加 `l127test`/`l135test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `调度与并发综合诊断`、`l135test`、`Lesson 135` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：调度与并发实现精讲

#### 3.2.1 调度器分发表与线程对象

```c
enum thread_state { THREAD_EMPTY, THREAD_RUNNING, THREAD_RUNNABLE, THREAD_SLEEPING, THREAD_BLOCKED_KBD, THREAD_BLOCKED_EVENT, THREAD_BLOCKED_SEM, THREAD_FINISHED };
struct thread { u64 frame,stack_phys,switches,progress,wake_tick,received; u8 state,id,mailbox,mailbox_ready; };
static struct thread threads[THREAD_COUNT];
static u8 current_thread,round_robin,threads_started,sleep_test,kbd_wait_test,pc_test;
static struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
static struct sched_class *active_sched_class;
```

- 状态机 8 态：EMPTY/RUNNING/RUNNABLE/SLEEPING/BLOCKED_KBD/BLOCKED_EVENT/
  BLOCKED_SEM/FINISHED——对应 Linux 的 `TASK_RUNNING/TASK_INTERRUPTIBLE/
  TASK_UNINTERRUPTIBLE` 与多个等待队列的组合。
- `sched_class` 是 Linux `struct sched_class`（`kernel/sched/sched.h`）的三函数版
  本：`pick_next`/`enqueue`/`dequeue`。`fair_sched_class` 实现为 `tiny_rr`，
  `active_sched_class` 指向它——「调度器类」抽象让后续换算法不必改 `irq0_schedule`。

#### 3.2.2 rr_pick_next：轮转 pick 算法

```c
static TEXT64 u8 rr_pick_next(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){u8 i=(u8)((round_robin+n)%THREAD_COUNT);if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){round_robin=i;return i;}}return 0xff;}
```

- 签名与职责：从 `round_robin+1` 起顺时针扫 `THREAD_COUNT` 个线程，返回第一个
  RUNNABLE/RUNNING 的 id，并更新 `round_robin` 光标；无人可跑返回 `0xff`。
- 算法步骤：`n` 从 1 到 3 枚举偏移；`(round_robin+n)%THREAD_COUNT` 计算候选；
  状态命中即选。
- 边界：`round_robin` 本身可指向前任当前线程（其状态是 RUNNING），所以搜索条件同时
  包含 RUNNABLE 和 RUNNING，避免「没人可跑却拒选当前线程」。
- 为什么：这是轮转 pick-next 的极简版——Linux 的 `pick_next_task` 还要考虑 CFS
  vruntime、rt 优先级等，TinyOS 只保留公平轮转的**循环性**。

#### 3.2.3 irq0_schedule：每个 tick 的调度决策（本课主题核心）

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;ticks++;softirq_run_budget();outb64(PIC1_COMMAND,PIC_EOI);if(f&&f->cs==USER_CS){user_irq0_save_restore(f);return f;}if(idle_running){idle_frame=f;idle_ticks++;}else threads[current_thread].frame=(u64)(unsigned long)f;wake_sleepers();reap_finished_threads();if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}old=current_thread;next=next_runnable();quantum_left=TIME_SLICE_TICKS;if(next==0xff){if(idle_running)return f;if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}idle_running=1;idle_switches++;return idle_frame;}if(idle_running){idle_running=0;threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}if(next==old){if(old==0)idle_worker_ticks++;return f;}if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}rr_dequeue(next);threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}
```

- 签名与职责：IRQ0 处理器调用的调度主函数；返回 `struct irq0_frame *`——这个指针
  就是 IRQ0 汇编 stub 恢复给 `%rsp` 的目标帧（`irq0_entry` 中
  `call irq0_schedule; movq %rax,%rsp`）。
- 算法步骤（编号）：
  1. `ticks++`（全局时钟），先跑 `softirq_run_budget()` 清软件中断预算；
  2. `outb64(PIC1_COMMAND,PIC_EOI)` 回 EOI；
  3. 用户态帧（`f->cs==USER_CS`）走 `user_irq0_save_restore` 保存并原样返回——
     教学内核不为用户线程做任务切换；
  4. 保存当前内核线程现场（idle 运行中则记录 `idle_frame`）；
  5. `wake_sleepers()`（睡满 wake_tick 的转 RUNNABLE）、`reap_finished_threads()`
     （FINISHED 且栈非空的归还 PMM）；
  6. 时间片未耗尽 → 扣减并返回原帧（不切换）；
  7. 耗尽 → `next_runnable()`；若无人可跑切 idle（`idle_switches++`），否则按三种
     情形切换（idle→thread、next==old 保持、thread→thread）。
- 边界与错误处理：`next==0xff` 且已在 idle 则直接返回 `idle_frame`；切换前把旧线程
  RUNNING→RUNNABLE 并 `rr_enqueue(old)`（记账），对 next 做 `rr_dequeue(next)`。
- 为什么这样设计：**「返回帧指针」即「切换」**——switch 不在 C 代码里保存/恢复
  寄存器，而是靠 IRQ0 的 `iretq` 弹不同栈实现。这是「栈上切换」教学范式
  （Linux 的 `switch_to` 用 `__switch_to_asm` 做同样的事，只是指令更多）。

#### 3.2.4 同步原语：wait queue / event / semaphore

```c
static TEXT64 int waitq_enqueue(volatile struct wait_queue*q,u8 id){if(q->count>=WAIT_QUEUE_CAP)return 0;q->ids[q->head]=id;q->head=(u8)((q->head+1)%WAIT_QUEUE_CAP);q->count++;q->enqueues++;return 1;}
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){u8 id;if(!waitq_dequeue(q,&id))return 0;if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0;threads[id].state=THREAD_RUNNABLE;q->wake_one++;*out=id;return 1;}
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
static TEXT64 void sem_down(struct semaphore*s){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(s->count){s->count--;s->downs++;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&s->waitq,id)){s->blocks++;threads[id].state=THREAD_BLOCKED_SEM;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_SEM)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_up(struct semaphore*s){u64 flags=irq_save64();u8 id;if(s->count<s->max){s->count++;s->ups++;}else s->overflows++;if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id))s->wakes++;irq_restore64(flags);}
```

- `waitq_enqueue`：满则失败（容量 2），否则环形写 `ids[head]` 并推进 `head`，记账
  `enqueues`——环形队列不丢数据是并发正确性的前提。
- `waitq_wake_one`：出队一个 id，校验 `id` 合法且状态匹配 `state`（例如
  `THREAD_BLOCKED_SEM`）才置 RUNNABLE；状态不匹配就丢弃（防「唤醒错人」）。
- `event_set`：`irq_save64` 关中断保护 → 置 `signaled` → `waitq_wake_all` 广播唤醒
  所有 `THREAD_BLOCKED_EVENT` → `irq_restore64`。为什么带锁：`pcgo` 的广播与
  IRQ0 可能并发。
- `sem_down` 是「自旋等」模式：循环内关中断检查 `count`；有资源减一即返回，否则
  把自己登记进 waitq 并置 `THREAD_BLOCKED_SEM`；开中断后 `while(状态==BLOCKED_SEM)
  sti; hlt`——**hlt 让出 CPU 给 idle/其他线程**，这是内核线程不忙等的关键。
- `sem_up`：`count` 未到 `max` 则增一，超限记 `overflows++`；随后 `wake_one` 唤醒
  一个等待者。

#### 3.2.5 生产者-消费者：双信号量驱动真实线程

```c
static TEXT64 void pc_reset(void){u64 flags=irq_save64();pc_head=pc_tail=pc_used=pc_next=pc_expected=0;pc_produced=pc_consumed=pc_sequence_errors=0;pc_start_event.signaled=0;pc_start_event.sets=pc_start_event.resets=pc_start_event.waits=pc_start_event.wakes=0;waitq_reset(&pc_start_event.waitq);sem_init(&pc_spaces,PC_BUFFER_CAP,PC_BUFFER_CAP);sem_init(&pc_items,0,PC_BUFFER_CAP);irq_restore64(flags);}
static TEXT64 void pc_producer(void){u8 value;while(threads[1].progress<THREAD_STEPS){sem_down(&pc_spaces);{u64 flags=irq_save64();value=pc_next++;pc_buffer[pc_head]=value;pc_head=(u8)((pc_head+1)%PC_BUFFER_CAP);pc_used++;pc_produced++;irq_restore64(flags);}threads[1].progress++;sem_up(&pc_items);busy_delay();}thread_exit();}
static TEXT64 void pc_consumer(void){u8 value;while(threads[2].progress<THREAD_STEPS){sem_down(&pc_items);{u64 flags=irq_save64();value=pc_buffer[pc_tail];pc_tail=(u8)((pc_tail+1)%PC_BUFFER_CAP);pc_used--;if(value!=pc_expected)pc_sequence_errors++;pc_expected++;pc_consumed++;irq_restore64(flags);}threads[2].progress++;sem_up(&pc_spaces);busy_delay();}thread_exit();}
```

- `pc_reset`：一次性清空缓冲、计数器、start event 与两个信号量。`pc_spaces` 初始
  满（=2），`pc_items` 初始空（=0）——教科书初始化。
- `pc_producer`（线程 1）：循环 `THREAD_STEPS=4` 次；`sem_down(&pc_spaces)` 拿空位；
  关中断把 `pc_next++` 写进 `pc_buffer[head]`；开中断；`sem_up(&pc_items)` 通知
  消费者；`busy_delay()` 人为制造调度交替。
- `pc_consumer`（线程 2）：`sem_down(&pc_items)` 等商品；读 `pc_buffer[tail]`；
  `if(value!=pc_expected)pc_sequence_errors++` 校验顺序；`sem_up(&pc_spaces)` 还空位。
- 并发正确性：**双信号量保证任何时刻缓冲最多 2 项、消费者按生产顺序消费**。
  `pc_sequence_errors` 非零即代表有乱序/丢项——`pcinfo` 的 `P errors/ok` 行
  （`prod==THREAD_STEPS&&cons==THREAD_STEPS&&!used&&!errors&&sc==PC_BUFFER_CAP&&!ic
  &&!sw&&!iw` 为 `yes`）是并发综合诊断的总开关。

#### 3.2.6 诊断命令：threadinfo / pcinfo / ps / schedinfo

```c
static TEXT64 void threadinfo(u16*c){text64(c,"scheduler: PIT preemptive independent idle\ncurrent: ");text64(c,idle_running?"idle":"thread");text64(c," ");hex64(c,current_thread);text64(c,"\nnext scan: ");hex64(c,round_robin);text64(c,"\nstarted: ");text64(c,threads_started?"yes":"no");text64(c,"\nmode: ");text64(c,pc_test?"pctest":kbd_wait_test?"kbdwaittest":sleep_test?"sleeptest":"preempttest");text64(c,"\nquantum left: ");hex64(c,quantum_left);text64(c,"\nPIT ticks: ");hex64(c,ticks);text64(c,"\npreempt switches: ");hex64(c,preempt_switches);text64(c,"\nidle switches/ticks: ");hex64(c,idle_switches);text64(c," ");hex64(c,idle_ticks);text64(c,"\nsleep wakeups: ");hex64(c,sleep_wakeups);text64(c,"\nkbd waiters: ");hex64(c,kbd_waitq.count);text64(c,"\nkbd enqueue/one/all: ");hex64(c,kbd_waitq.enqueues);text64(c," ");hex64(c,kbd_waitq.wake_one);text64(c," ");hex64(c,kbd_waitq.wake_all);text64(c,"\nworker steps: ");hex64(c,threads[1].progress);text64(c," ");hex64(c,threads[2].progress);text64(c,"\nIRQ0 schedules: yes\n");}
```

- `threadinfo`：调度器仪表盘——当前线程/轮转光标/启动与否/运行模式（pctest/
  kbdwaittest/sleeptest/preempttest）/剩余时间片/PIT ticks/抢占切换次数/idle
  切换与 tick/睡眠唤醒/键盘等待者/worker 进度。
- `pcinfo`（见 §3.2.5 数据来源）：打印 start event（`E sig/wait`、`E waits/all`）、
  两个信号量（`S count/wait`、`I count/wait`）、缓冲（`R used/cap`）与
  `P prod/cons`、`P errors/ok`——生产者-消费者的完整状态快照。
- `ps64`（exec64 的 `ps` 命令）：逐线程打印 id/state/frame/stack-pa/stack-high/
  switches/progress/wake-tick/received/mailbox，末尾打印 idle 状态。
- `schedinfo`：打印调度器类名 `tiny_rr` 与 enqueue/dequeue/pick 三个操作计数。
- 四个命令合起来 = `threadinfo`（当前决策）、`pcinfo`（同步状态）、`ps`（逐线程
  明细）、`schedinfo`（类分派统计），构成「调度与并发综合诊断」的命令矩阵。

#### 3.2.7 本课新增 checkpoint：lesson_128_model 与 l135test

```c
struct lesson_128_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_128_model lesson_128_state;
static TEXT64 void l135test(u16*c){lesson_128_state=(struct lesson_128_model){128U,129U,130U,131U,1,1,1,1};int ok=lesson_128_state.valid&&lesson_128_state.active&&lesson_128_state.ready&&lesson_128_state.accounted&&lesson_128_state.b==lesson_128_state.a+1U;text64(c,"l135test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 128 fallback reported");putc64(c,'\n');}
```

- 字段语义：4 个 u32 连续编号（a=128、b=129、c=130、d=131）+ 4 个状态位
  （valid/active/ready/accounted）。`a` 从 `128U` 起头 = 课号 135 − 7，回锚到
  Lesson 128 检查点。
- 断言逻辑：`ok` 五连真（四个状态位 + `b==a+1`）输出成功串，否则失败串
  `Lesson 128 fallback reported`。
- 为什么：回归探针，保证调度/并发继承机制在相邻课间不被改坏；消息里的
  "concurrency, SMP, RCU" 描述的是整个内核机制覆盖面，本函数不执行任何并发代码。

#### 3.2.8 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l127test")){if(!noargs64(arg))usage64(c,"l127test");else l127test(c);}else if(eq64(word,"l135test")){if(!noargs64(arg))usage64(c,"l135test");else l135test(c);}
```

- 本课把上一课的 `l134test` 分支改名 `l127test`（复用 `lesson_127_model`），新增
  `l135test` 分支。**勘误**：旧 README 写的 `Commands: l128test` 与源码不符，源码中
  可用的 checkpoint 命令是 `l127test` 与 `l135test`。
- about：`else text64(c,"Lesson 135: 调度与并发综合诊断\n");`；开机横幅：
  `text64(&c,"Lesson 135: 调度与并发综合诊断\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 并发实验命令链：`preempttest`（mode 0）、`sleeptest`（mode 1）、
  `kbdwaittest`（mode 2）、`pctest`+`pcgo`（mode 3），配合 `threadinfo`/`pcinfo`/
  `ps`/`schedinfo` 观察。

### 3.3 构建管线（Makefile / linker）

- 构建流程与 Lesson 134 完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；外层 ELF32 +
  `.incbin` 双段）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`调度与并发综合诊断`、`l135test`、`Lesson 135`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       active_sched_class=&fair_sched_class（分发表就绪）
       → pmm_init → vma_init/reclaim_init → vfs_init
       → runtime_gdt_tss_init → idle_init → install_idt → pit_init + pic_init
       → 横幅 "Lesson 135: 调度与并发综合诊断\n..." → shell 循环
  exec64 命令 → preempttest/sleeptest/kbdwaittest/pctest:start_threads(mode)
             → pcgo:event_set 广播 → IRQ0 抢占驱动 pc_producer/pc_consumer
             → threadinfo/pcinfo/ps/schedinfo:诊断输出
             → l127test/l135test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以并发实验为例：

1. **`pctest`** → exec64 命中 `pctest` 分支 → `start_threads(3)`：`pc_reset` 初始化
   双信号量与 start event，创建两个 worker 线程（栈帧来自 `pmm_alloc`）→ 输出
   `pctest: producer and consumer blocked on start event; run pcgo`。
2. **`pcgo`** → `event_set(&pc_start_event)` 置位并广播唤醒 → 输出 `pcgo: event set;
   broadcast wake-all issued`。此后 IRQ0 每个 tick 抢占，`pc_producer`（线程 1）与
   `pc_consumer`（线程 2）用 `pc_spaces`/`pc_items` 互斥推进，各做 4 步。
3. **`pcinfo`** → 打印 `E sig/wait`、`S count/wait`、`I count/wait`、
   `R used/cap`、`P prod/cons`、`P errors/ok`——全部计数器来自 `pc_*` 全局量，
   末行 `yes` 表示 `prod==cons==4 && used==0 && errors==0`（并发正确）。
4. **`l135test`** → 对 `lesson_128_state` 赋值并五连断言 → 输出
   `l135test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。

要点：调度驱动（IRQ0）是「时间」来源，信号量/事件是「同步」来源，`pc_*` 计数器是
「正确性」证据，三个层次全部暴露给 VGA 诊断。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-135-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `调度与并发综合诊断`、`l135test`、`Lesson 135` 与 kernel64.c 中的 `l135test`，
  全部命中输出 `Multiboot2 and Lesson 135 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅
  第一行为 `Lesson 135: 调度与并发综合诊断`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 135: 调度与并发综合诊断`
  2. `l135test` → `l135test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  3. `l127test` → `l127test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  4. `preempttest` → `preempttest: two non-yielding workers started`
  5. `threadinfo` → 首行 `scheduler: PIT preemptive independent idle`，随后
     `mode: preempttest`（或 pctest 等）与 `IRQ0 schedules: yes` 结尾
  6. `pctest` → `pctest: producer and consumer blocked on start event; run pcgo`；
     `pcgo` → `pcgo: event set; broadcast wake-all issued`
  7. `pcinfo` → 末行 `P prod/cons: 0000000000000004 0000000000000004` 与
     `P errors/ok: 0000000000000000 yes`（两个 worker 各 4 步跑完后）
  8. `ps` → 首行 `threads: id state frame stack-pa stack-high switches progress
     wake-tick received last`，随后三个 `thread N` 行与 idle 行
  9. `schedinfo` → 首行 `scheduler class: tiny_rr`
- **如何判断成功**：上述命令逐一打印预期串即成功；`pcinfo` 末行必须为 `yes`
  （生产者-消费者无丢序）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l135test` 输出 `Lesson 128 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l135test` 赋值 `{128U,129U,130U,131U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `pctest` 后 `pcgo` 无反应或 `pcinfo` 的 prod/cons 卡住 | `event_set` 广播未唤醒或 worker 未启动 | `pcinfo` 看 `E sig/wait`；确认先 `pctest` 再 `pcgo`；`threadinfo` 看 `mode: pctest` |
| `pcinfo` 的 `P errors/ok: ... no` | 生产者/消费者丢序（`pc_sequence_errors>0`）或进度未跑完 | 检查 `pc_consumer` 的 `value!=pc_expected` 判断与 `sem_up(&pc_spaces)` 配对；`ps` 看 worker progress |
| 两个 worker 从不交替（`threadinfo` 的 preempt switches 不涨） | IRQ0 未开或 PIT 未初始化 | `tickinfo`/`uptime` 看 ticks 是否增长；`idtinfo` 看 IRQ0 vector 0x20 |
| `preempttest` 报 `PMM allocation failed` | 线程栈帧分配不到（`pmm_alloc` 返回 0） | `meminfo` 看 free 帧；`pfree` 释放不再用的帧后重试 |
| `threadinfo` 显示 `mode:` 与预期不符 | 上次实验的模式标志残留 | `start_threads` 每次会重写 `sleep_test/kbd_wait_test/pc_test`；重启 QEMU 或换 mode 重跑 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 135: 调度与并发综合诊断`；`make check` grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **调度器入口**：TinyOS `irq0_schedule` 由 IRQ0 在 `iretq` 边界切换返回帧；Linux
   `kernel/sched/core.c` 的 `schedule()` 由 `scheduler_tick`/`schedule_tail` 驱动，
   `__schedule()` 里 `prev->sched_class->pick_next_task` 选下一任务。两者都是
   「tick 驱动 + 返回目标现场」。
2. **调度器类分派**：TinyOS `sched_class{name,pick_next,enqueue,dequeue}` 对应
   `kernel/sched/sched.h` 的 `struct sched_class`（`pick_next_task`/`enqueue_task`/
   `dequeue_task`）。`tiny_rr` 是最简实现，Linux 有 `fair_sched_class`/`rt_sched_class`
   等；本课 `active_sched_class` 相当于 `__sched_class_highest` 的选择。
3. **轮转 pick**：TinyOS `rr_pick_next` 循环扫；Linux 时间片轮转在 `SCHED_RR`
   （`kernel/sched/rt.c`）中由 `sched_rt_rr` 维护运行队列。TinyOS 不做 CFS
   vruntime/红黑树。
4. **阻塞同步**：TinyOS wait queue/event/semaphore 对应 Linux `kernel/sched/
   wait.c`（`wait_queue_head_t`）、`include/linux/semaphore.h`（`down`/`up`）与
   `arch/x86` 的 `hlt` 空闲循环；Linux 的 `wake_up` 还要走 `try_to_wake_up` +
   rq 锁，TinyOS 简化为关中断的 waitq 操作。
5. **生产者-消费者**：双信号量模型是 Linux 的教科书出处（`include/linux/
   semaphore.h` 注释即引用 Dijkstra）；实际内核更多用 `mutex`+`wait_queue` 与
   `ring buffer`（如 `kfifo`）。
6. **调度统计**：TinyOS `preempt_switches`/`idle_ticks`/`sched_picks` 对应 Linux
   `/proc/schedstat` 与 `sched_info` 的 `sched_info_switch` 计数；`threadinfo` 即
   `/proc/sched_debug` 的极简版。

**权威来源**：Intel SDM Vol.3A（中断/IDT）、Linux `kernel/sched/core.c`/
`kernel/sched/sched.h`、`kernel/sched/wait.c`。
**教学模型简化了什么**：单核 NR_CPUS=1（无 runqueue 锁）、无优先级/CFS/实时类、
无 `switch_to` 寄存器级切换（靠 iretq 弹栈）、无唤醒竞争窗口（关中断原子段）。

---

## 8. 思考题与练习

1. **概念理解**：`rr_pick_next` 为什么同时接受 `THREAD_RUNNABLE` 和
   `THREAD_RUNNING`？如果只接受 RUNNABLE，什么场景下会错误返回 `0xff`？
2. **源码定位**：在 `kernel64.c` 中找出 `irq0_schedule` 返回的帧指针是如何被
   `irq0_entry` 汇编消费的（`movq %rax,%rsp` 之后的 `pop` 顺序），说明「换栈即
   切换」的原理。
3. **动手实验**：把 `PC_BUFFER_CAP` 从 2 改成 1，重跑 `pctest`+`pcgo`+`pcinfo`，
   观察 `P errors/ok` 是否仍为 `yes`；再把生产者/消费者的 `busy_delay` 删掉，看
   交替行为与计数变化。
4. **动手实验**：先 `pctest` 但**不**执行 `pcgo`，执行 `pcinfo`，记录
   `E sig/wait` 的值；再 `pcgo` 后执行 `pcinfo`，解释 wait 计数与 sig 的差异。
5. **Linux 对照**：阅读 `kernel/sched/core.c` 的 `__schedule()` 与
   `kernel/sched/wait.c` 的 `__wake_up_common`，对比它们与 `irq0_schedule`/
   `waitq_wake_one` 的职责边界，指出 TinyOS 砍掉的关键机制（如 rq 锁、唤醒失败
   重试）。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是检查点课，`kernel64.c` 相对 Lesson 134 只有 3 处小增量，调度与并发机制
   全部继承，主题由 banner/about 文案「调度与并发综合诊断」标识。
2. 抢占式轮转由 `irq0_schedule` 在每个 PIT tick 决策，返回帧指针即切换目标。
3. `rr_pick_next` 从轮转光标顺时针挑第一个可跑线程，无人可跑切 idle。
4. 阻塞同步三件套 wait queue/event/semaphore 用关中断登记 + `sti; hlt` 等待实现
   不忙等的内核线程。
5. 生产者-消费者用 `pc_spaces`/`pc_items` 双信号量保证缓冲上限与消费顺序，
   `pc_sequence_errors` 是并发正确性指标。
6. 诊断命令矩阵 `threadinfo`/`pcinfo`/`ps`/`schedinfo` 覆盖决策、同步、明细、统计
   四个层面。
7. 旧 README 的 `Commands: l128test` 已勘误为源码实际的 `l127test` 与 `l135test`。

**下一课**：[`lesson-136-stable/README.md`](../lesson-136-stable/README.md) 主题为
「SMP/RCU 回归验证」，将站在本课的 `cpu_local`/原子操作/自旋锁（`raw_spinlock`）/
softirq 预算之上，讲解 `lockatomictest`/`softirqtest` 与 NR_CPUS=1 的 per-CPU 语义，
并追加新的 checkpoint 模型 `lesson_129_model`（命令 `l136test`）。两课的衔接点：
本课的 IRQ0 抢占里已经调用了 `softirq_run_budget`，下节课细看它的原子与预算
机制。
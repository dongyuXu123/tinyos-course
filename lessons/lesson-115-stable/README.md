# Lesson 115: 信号量与等待队列并发 — 精讲文档

> **课号**：Lesson 115（统一课程编号 115）
> **主题**：信号量与等待队列并发（semaphore / wait queue / event 联合建模）
> **课程主线位置**：第 12 阶段「并发、SMP 与 RCU 检查点序列」中的检查点课（lesson-107 … 115 … 120 一路递增）
> **前置课程**：[Lesson 114 原子操作与内存序](../lesson-114-stable/README.md)
> **后续课程**：[Lesson 116 per-CPU 数据访问](../lesson-116-stable/README.md)
> **一句话目标**：读懂本课如何用「计数信号量 + 环形等待队列 + 事件对象」把并发线程组织起来，并说清 `pctest`/`pcgo`/`kbdwaittest` 背后的阻塞与唤醒路径。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能画出本课内核里「生产者/消费者、键盘等待线程、管道读写者」各自的阻塞（sleep）与唤醒（wake）完整路径，并说清信号量、等待队列、事件三者各自承担什么职责、为什么需要 `irq_save64` 临界区。
- **在课程主线中的位置**：本课处于并发/SMP 主题的检查点课序列中。此前的 Lesson 22 前后已引入信号量与等待队列机制代码，Lesson 100–114 逐个叠加了软中断、原子操作、自旋锁、per-CPU 元数据与内存序；本课不再新增机制，而是以「信号量与等待队列并发」为主题把既有机制串起来做确定性验证。后续 lesson-116 起依次聚焦 per-CPU 数据访问、竞态窗口、SMP 状态等主题。
- **前置知识清单**（至少 3 项）：
  1. 线程状态机 `enum thread_state`（`THREAD_RUNNING/RUNNABLE/SLEEPING/BLOCKED_KBD/BLOCKED_EVENT/BLOCKED_SEM/FINISHED`）与 IRQ0 时间片抢占调度（`irq0_schedule`）；
  2. `irq_save64()`/`irq_restore64()` 关开中断临界区，以及 `pushfq/popq/cli/sti` 的语义（来自 Lesson 114 内存序）；
  3. PIT 时钟中断（`PIT_RATE_HZ=100`）与 `ticks` 全局计数、`busy_delay` 忙等；
  4. 物理页分配 `pmm_alloc`/`pmm_free_page` 与线程栈的创建流程 `start_threads`。
- **本课交付**（可见结果）：
  - 新检查点命令 `l115test`（旧的 `l114test` 分支收回为 `l107test`），输出确定性通过串；
  - 沿用并可验证的并发演示命令：`pctest`→`pcgo`→`pcinfo`（事件+双信号量+环形缓冲）、`kbdwaittest`（键盘等待队列直投）、`sleeptest`/`preempttest`、`pipetest`/`polltest`；
  - 启动画面横幅与 `about` 命令均标注本课主题。

---

## 2. 核心概念精讲

### 2.1 计数信号量（Counting Semaphore）

**定义**：本课用 `struct semaphore { u8 count,max; volatile struct wait_queue waitq; ... }` 表示一个计数信号量：`count` 是当前可用资源数，`max` 是上限，`waitq` 是排队等待者的环形队列。

**为什么需要**：互斥锁只能保证「同一时刻只有一个人进厕所」；但生产者/消费者场景里，我们关心的是「缓冲区里还有几个空位」「缓冲区里已有几个货」，这是一种**数量**语义，锁无法表达，需要信号量。信号量把「资源计数」与「阻塞排队」合并成一个原语。

**工作机制**（对照源码逐行见 §3.2.3）：
- `sem_down`（P 操作）：若 `count>0` 则减一立即返回；否则把自己入队并置 `THREAD_BLOCKED_SEM`，随后 `sti; hlt` 挂起等待；
- `sem_up`（V 操作）：若 `count<max` 则加一；再从 `waitq` 唤醒一个 `THREAD_BLOCKED_SEM` 线程使其回到 `THREAD_RUNNABLE`。

**示意**（缓冲区容量 2 的生产者-消费者）：

```
生产者线程1                消费者线程2
sem_down(pc_spaces)  count 2→1→0…      sem_down(pc_items)  count 0→阻塞入队
  写入 pc_buffer[]                     …
sem_up(pc_items)     count 0→1         唤醒消费
```

### 2.2 环形等待队列（Wait Queue）

**定义**：`struct wait_queue { u8 ids[WAIT_QUEUE_CAP],head,tail,count; ... }`，容量 `WAIT_QUEUE_CAP=THREAD_COUNT-1=2`，用环形数组保存**线程 id**（不是函数指针、不是栈帧），`head` 指向入队位置、`tail` 指向出队位置。

**为什么需要**：信号量/事件/键盘/管道都需要「一批线程在某个条件不满足时挂起、条件满足时被唤醒」。与其每个机制各写一遍排队逻辑，不如抽出一个统一的 FIFO 队列；它只存 id，不含唤醒时机的判断，是「排队」这件事本身。

**工作机制**：
- `waitq_enqueue`：队满返回 0（调用方不阻塞），否则写 `ids[head]`、`head=(head+1)%CAP`、`count++`；
- `waitq_dequeue`：队空返回 0，否则从 `ids[tail]` 取 id、`tail=(tail+1)%CAP`、`count--`；
- `waitq_wake_one`：出队一个 id，校验 `id` 合法且**该线程状态恰好是期待的被阻塞态**（如 `THREAD_BLOCKED_SEM`），把它置为 `THREAD_RUNNABLE`；
- `waitq_wake_all`：循环出队并逐个唤醒，用于事件广播。

**关键设计点**：唤醒操作只改 `threads[id].state`，不直接去抢 CPU；真正把线程切回运行态的是下一个 IRQ0 时间片里的调度器（`irq0_schedule` 里的 `next_runnable()`）。这就是「唤醒 ≠ 立刻运行」的教学模型。

### 2.3 事件对象（Event / 二进制信号量）

**定义**：`struct event { u8 signaled; volatile struct wait_queue waitq; ... }`，`signaled` 是持久化的「已置位」标志。

**为什么需要**：`pctest` 场景要求两个工作线程先就位、由 shell 敲 `pcgo` 一次性放行。事件正好是「一次广播、多个等待者、之后立即返回」的语义：线程先 `event_wait` 阻塞，`event_set` 置位并 `waitq_wake_all` 广播唤醒全部等待者。这与 Linux 的 `struct completion` 一一对应。

### 2.4 临界区与 `irq_save64`

**定义**：`irq_save64()` 用 `pushfq; popq %0; cli` 把旧 IF 位保存进 `flags` 并关中断；`irq_restore64(flags)` 只在旧 IF 为 1 时 `sti`。

**为什么需要**：本课所有共享对象（`pc_buffer`、`pc_used`、`sem->count`、`waitq` 的 head/tail/count）都可能被 IRQ0 抢占打断；若信号量在 `count--` 和入队之间被打断，另一线程就可能看到不一致状态。**关中断是单核上最简单可靠的自旋锁替代品**——本课 `NR_CPUS=1`，所以「关中断」即「独占 CPU」，等价于关抢占。

**模式**：所有 `sem_down/sem_up/event_set/event_wait/pc_reset/pc_producer/pc_consumer/pcinfo` 都用「`flags=irq_save64(); ...; irq_restore64(flags);`」包住对共享状态的短临界区，并在临界区**外**做 `sti; hlt` 自旋等待。

### 2.5 阻塞-唤醒的完整闭环

```
线程执行 sem_down(count==0)
  → irq_save64 关中断
  → waitq_enqueue(自己) + state=THREAD_BLOCKED_SEM
  → irq_restore64
  → while(state==THREAD_BLOCKED_SEM) sti; hlt   // 挂起，让出 CPU
  ↑
另一线程 sem_up()
  → irq_save64
  → count++ / waitq_wake_one → 把阻塞者置为 THREAD_RUNNABLE
  → irq_restore64
下一个 IRQ0 时间片：irq0_schedule → next_runnable 选中它 → 恢复其 frame
  → 被唤醒者从 sti; hlt 处继续，重新 sem_down，这次 count>0，返回
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-114） |
|------|------|------------------------------|
| `boot.S` | 32 位入口、Multiboot2 头、进入长模式、内嵌 `kernel64.bin` | 未变化 |
| `kernel.c` | 32 位主函数、构建页表与 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核主体：信号量/等待队列/事件/PC 模型与全部命令 | **本课唯一有增量的源码** |
| `kernel64.ld` | 64 位裸二进制段布局（`.text64`、三块守卫栈） | 未变化 |
| `linker.ld` | 外层 ELF 段布局（Multiboot 头在前 32768 字节内） | 未变化 |
| `Makefile` | 构建/检查/运行 | 仅 `check` 目标的 grep 串换成本课主题 |
| `grub.cfg` | GRUB 菜单 | 未变化（仍写着 lesson 52 的标题） |

> **勘误说明**：旧 README 声称命令为 `l108test`，但源码 `exec64` 中本课新增的命令是 `l115test`（`kernel64.c` 内不存在 `l108test` 分支）；`help` 命令的静态命令清单也早于 `l115test` 而没列出它。本文以源码为准，统一采用 `l115test`。

### 3.2 `kernel64.c` 精讲

#### 3.2.1 数据结构（本课主题相关）

```c
#define WAIT_QUEUE_CAP (THREAD_COUNT-1)          /* 容量=3-1=2，只允许一个等待者 */
#define PC_BUFFER_CAP 2                          /* 有界环形缓冲容量=2 */
struct wait_queue { u8 ids[WAIT_QUEUE_CAP],head,tail,count; u64 enqueues,wake_one,wake_all; };
struct event { u8 signaled; volatile struct wait_queue waitq; u64 sets,resets,waits,wakes; };
struct semaphore { u8 count,max; volatile struct wait_queue waitq; u64 downs,ups,blocks,wakes,overflows; };
static volatile struct wait_queue kbd_waitq;     /* 键盘等待队列：kbdwaittest 用 */
static struct wait_queue pipe_read_wait,pipe_write_wait; /* 管道读写等待队列 */
```
- `WAIT_QUEUE_CAP` 取 `THREAD_COUNT-1`：三个线程（shell+两个 worker）里最多只有一个会因本对象阻塞（生产者/消费者同时最多一方在等），容量 2 已足够且**有界**；
- `waitq` 全部声明为 `volatile`：它会跨中断被读写，`volatile` 阻止编译器对 head/tail/count 的乱序缓存；
- 每个对象都带 `enqueues/wake_one/wake_all/...` 计数器，供 `pcinfo`/`pipeinfo` 之类的诊断命令读出，体现「元数据可验证」的课程风格。

#### 3.2.2 检查点增量：`l115test`

```c
struct lesson_108_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_108_model lesson_108_state;
static TEXT64 void l115test(u16*c){lesson_108_state=(struct lesson_108_model){108U,109U,110U,111U,1,1,1,1};
int ok=lesson_108_state.valid&&lesson_108_state.active&&lesson_108_state.ready&&lesson_108_state.accounted
        &&lesson_108_state.b==lesson_108_state.a+1U;
text64(c,"l115test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 108 fallback reported");putc64(c,'\n');}
```
- 结构体 `lesson_108_model` 与既有 `lesson_105/106/107_model` 完全同构：四个 `u32` 加四个 `u8` 标志位，是「检查点护照」，只做确定性校验、不驱动真实硬件；
- 初始值 `{108U,109U,110U,111U,...}` 保证了 `b==a+1`（即 109==108+1）恒成立，`valid/active/ready/accounted` 全为 1，因此 `ok` 恒真，输出恒为通过串；
- 输出串二选一，通过时逐字为 `l115test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；仅当模型未被正确赋值（不可能）才打印 `Lesson 108 fallback reported`；
- **勘误**：lesson-114 的 `l114test` 分支在本课收回为 `l107test`，新增 `l115test` 分支，checkpoint 链条从 `l106test`/`l107test` 直跳 `l115test`。

#### 3.2.3 信号量、事件与等待队列函数

`waitq_wake_one` 与 `waitq_wake_all`：
```c
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){
  u8 id;if(!waitq_dequeue(q,&id))return 0;             /* 队列空：无事可做 */
  if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0; /* 越界/状态不符：丢弃 */
  threads[id].state=THREAD_RUNNABLE;                   /* 唤醒=改状态，不抢 CPU */
  q->wake_one++;*out=id;return 1;}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){
  u8 id,n=0;while(waitq_dequeue(q,&id))
    if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}
  q->wake_all+=n;return n;}
```
- 唤醒前**校验状态必须精确匹配**：例如 `sem_up` 只唤醒 `THREAD_BLOCKED_SEM`，防止误把正在运行或已退出线程拉起；
- `wake_all` 返回唤醒个数并累加进 `wake_all` 统计，供诊断命令确认广播行为；
- 队列为空时两函数都安全返回 0/0，语义为「没有等待者，信号保持可用或仅计数」。

`sem_down` / `sem_up` / `sem_init`：
```c
static TEXT64 void sem_init(struct semaphore*s,u8 count,u8 max){s->count=count;s->max=max;waitq_reset(&s->waitq);
  s->downs=s->ups=s->blocks=s->wakes=s->overflows=0;}
static TEXT64 void sem_down(struct semaphore*s){u8 id=current_thread;
  for(;;){u64 flags=irq_save64();                       /* 关中断进入临界区 */
    if(s->count){s->count--;s->downs++;irq_restore64(flags);return;}  /* 有资源：直接拿走 */
    if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&s->waitq,id)){s->blocks++;threads[id].state=THREAD_BLOCKED_SEM;}
    irq_restore64(flags);
    while(threads[id].state==THREAD_BLOCKED_SEM)__asm__ volatile("sti; hlt");}}  /* 挂起等唤醒 */
static TEXT64 void sem_up(struct semaphore*s){u64 flags=irq_save64();u8 id;
  if(s->count<s->max){s->count++;s->ups++;}else s->overflows++;   /* 超过上限：记账不置负 */
  if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id))s->wakes++;
  irq_restore64(flags);}
```
- `sem_down` 的 `for(;;)` 是关键：被唤醒后**重新竞争** `count`，而不是无条件放行——这正是信号量的「唤醒后校验」语义，等价于 Linux 的 `down()` 在 `sleeper.function` 里检查 `signal_pending` 后重试；
- 入队前提是「线程此刻 `THREAD_RUNNING`」，避免把自己（已阻塞的 id）重复入队或入队一个非法 id；`waitq_enqueue` 返回 0（队满）时不置阻塞态，而是继续自旋重试——因为单核上同时只有两个 worker，队满只是瞬时现象；
- `sem_up` 先加计数再唤醒，顺序保证被唤醒者重检时 `count` 必然已包含本次资源（无 lost wakeup）；
- `overflows++` 使 `sem_up` 在 count 已达 max 时不会越界，保证不变式 `0<=count<=max`。

`event_set` / `event_wait`：
```c
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;
  e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;
  for(;;){u64 flags=irq_save64();
    if(e->signaled){irq_restore64(flags);return;}        /* 已置位：立即放行 */
    if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}
    irq_restore64(flags);
    while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}
```
- 与信号量不同，事件用 `signaled` 位而非计数：`event_set` 之后再来等待的线程**立即返回**（信号不会丢失，而是「记住」了），这正是 `pcgo` 要求的行为——若 shell 在 worker 就位前就敲了 `pcgo`，事件仍会放行后续等待者；
- `wake_all`（广播）对应 `pc_start_event` 有 1 个生产者 + 1 个消费者同时等待的场景。

#### 3.2.4 生产者/消费者：`pc_reset` / `pc_producer` / `pc_consumer`

```c
static TEXT64 void pc_reset(void){u64 flags=irq_save64();pc_head=pc_tail=pc_used=pc_next=pc_expected=0;
  pc_produced=pc_consumed=pc_sequence_errors=0;pc_start_event.signaled=0;
  pc_start_event.sets=pc_start_event.resets=pc_start_event.waits=pc_start_event.wakes=0;
  waitq_reset(&pc_start_event.waitq);sem_init(&pc_spaces,PC_BUFFER_CAP,PC_BUFFER_CAP);sem_init(&pc_items,0,PC_BUFFER_CAP);
  irq_restore64(flags);}
static TEXT64 void pc_producer(void){u8 value;
  while(threads[1].progress<THREAD_STEPS){                      /* 循环 4 次 */
    sem_down(&pc_spaces);                                       /* 等空位 */
    {u64 flags=irq_save64();value=pc_next++;pc_buffer[pc_head]=value;   /* 临界区写缓冲 */
     pc_head=(u8)((pc_head+1)%PC_BUFFER_CAP);pc_used++;pc_produced++;irq_restore64(flags);}
    threads[1].progress++;sem_up(&pc_items);busy_delay();}}      /* 放货并让出时间片 */
static TEXT64 void pc_consumer(void){u8 value;
  while(threads[2].progress<THREAD_STEPS){
    sem_down(&pc_items);                                         /* 等有货 */
    {u64 flags=irq_save64();value=pc_buffer[pc_tail];pc_tail=(u8)((pc_tail+1)%PC_BUFFER_CAP);
     pc_used--;if(value!=pc_expected)pc_sequence_errors++;pc_expected++;pc_consumed++;irq_restore64(flags);}
    threads[2].progress++;sem_up(&pc_spaces);busy_delay();}}     /* 归还空位 */
```
- 同步由**两个信号量**完成：`pc_spaces` 初值 2（空位数）、`pc_items` 初值 0（货数），此即教科书「有界缓冲」的双信号量解法；
- 环形缓冲本身的读写在第二层 `irq_save64` 临界区内完成：外层信号量保证「空位/有货」的先后关系，内层临界区保证 `head/tail/used` 三者的原子可见；
- `pc_next`/`pc_expected` 生成序号并逐项比对，任何错序都会累计到 `pc_sequence_errors`，由 `pcinfo` 显示——这是本课「确定性验证」的关键指标；
- `busy_delay()` 在每次生产/消费后忙等 `BUSY_SPINS=4000000` 次，给另一个线程留下被 IRQ0 抢占的机会，保证真的发生并发交错。

#### 3.2.5 键盘等待队列：`kbd_wait_char` 与 `irq1_record`

```c
static TEXT64 void kbd_wait_char(u8 *out){u8 id=current_thread;
  for(;;){u64 flags=irq_save64();
    if(id&&threads[id].mailbox_ready){*out=threads[id].mailbox;threads[id].mailbox_ready=0;irq_restore64(flags);return;}
    if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&kbd_waitq,id))threads[id].state=THREAD_BLOCKED_KBD;
    irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_KBD)__asm__ volatile("sti; hlt");}}
TEXT64 void irq1_record(void){u8 raw=inb64(0x60),ch,next,id;irq1_last_scancode=raw;irq1_raw_count++;
  if(!(raw&0x80)){irq1_count++;ch=(u8)scan64(raw);if(ch){
    if(waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id)){     /* 有等待线程：直接投递到信箱 */
      threads[id].mailbox=ch;threads[id].mailbox_ready=1;kbd_direct_deliveries++;}
    else {next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));          /* 无人等待：入公共环形队列 */
      if(next==kbd_tail)kbd_overflow_count++;else{kbd_queue[kbd_head]=ch;kbd_head=next;}}}}
  outb64(PIC1_COMMAND,PIC_EOI);}
```
- `irq1_record` 是 IRQ1 键盘中断入口，运行在**中断上下文**；它优先尝试 `waitq_wake_one` 做「直投」（把按键放进等待线程的 `mailbox`），只有无人等待时才退回公共 FIFO 队列；
- `kbd_direct_deliveries` 统计直投次数，体现「等待队列唤醒 + 数据随唤醒送达」的典型中断驱动模式；
- `kbd_wait_char` 与 `sem_down` 同构：先查信箱（即先检查条件），不满足再入队阻塞，被唤醒后重新查——避免错过已投递的字符。

#### 3.2.6 调度器与阻塞态的衔接

```c
enum thread_state { THREAD_EMPTY, THREAD_RUNNING, THREAD_RUNNABLE, THREAD_SLEEPING,
                    THREAD_BLOCKED_KBD, THREAD_BLOCKED_EVENT, THREAD_BLOCKED_SEM, THREAD_FINISHED };
static TEXT64 void worker_run(u8 id){if(pc_test){event_wait(&pc_start_event);   /* pctest 模式：先等 pcgo */
  if(id==1)pc_producer();else pc_consumer();return;}...}
```
- `irq0_schedule` 只选择 `THREAD_RUNNING/RUNNABLE` 的线程；`THREAD_BLOCKED_*` 与 `THREAD_SLEEPING` 一律不参与选择，这就是「阻塞线程自动让出 CPU」的实现机制；
- 被唤醒线程的状态被置为 `THREAD_RUNNABLE`，在下一次 IRQ0 调度中被 `rr_pick_next` 选中，从挂起点（`sti; hlt` 之后）继续执行。阻塞线程在 `sti; hlt` 挂起期间不消耗量子，调度器会切到 idle 或另一个 worker。

### 3.3 构建管线（Makefile / linker）

- `CFLAGS64 := -m64 -ffreestanding -fpie ... -mno-red-zone -mno-sse -mno-sse2 -mno-mmx`：64 位裸机编译，关栈金丝雀、关 SIMD（避免指令产生 `xmm` 保存需求），`-mno-red-zone` 保证中断/异常帧不被打进红区；
- 两级链接：`kernel64.c → kernel64.elf（-T kernel64.ld，-m elf_x86_64）→ objcopy 成裸 binary`，再由 `boot.S` 用 `.incbin "build/kernel64.bin"` 嵌入外层 ELF；
- 外层 `linker.ld` 让 `.multiboot` 段以 8 字节对齐出现在镜像开头（Multiboot2 规范要求头在最初 32768 字节内），并把 `.data/.bss` 对齐到新页避免 RWX 段；
- `kernel64.ld` 用 `ASSERT` 校验三块守卫栈（idle/rsp0/ist1）精确 4096 字节，对应 `stack_guards_init` 的栈保护；
- `grub-mkrescue` 生成可启动 ISO；`make check` 依次执行 `grub-file --is-x86-multiboot2` 与三个 grep（本课主题串、`l115test`、`Lesson 115`），全过后打印 `Multiboot2 and Lesson 115 checks passed.`。

### 3.4 主控制流

```mermaid
graph TD
    A[GRUB → _start 32位] --> B[kernel_main32 建页表/交互结构]
    B --> C[kernel_main64_binary 打印横幅 Lesson 115: 信号量与等待队列并发]
    C --> D[循环: kbd_dequeue 读键盘]
    D --> E[exec64 解析命令]
    E -->|pctest| F[start_threads(3): 建两个 worker, pc_reset 双信号量]
    E -->|pcgo| G[event_set pc_start_event → waitq_wake_all 广播]
    F --> H[worker_run: event_wait → pc_producer / pc_consumer]
    H --> I[sem_down/sem_up 交换空位与货, 每步 busy_delay]
    I --> J[IRQ0 irq0_schedule 时间片切换]
    E -->|l115test| K[lesson_108_model 校验 → 打印通过串]
    E -->|kbdwaittest| L[两个 worker 等 kbd_waitq, IRQ1 直投 mailbox]
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 完成 IDT/PIC/PIT/栈保护初始化后，在 VGA 上打印 `Lesson 115: 信号量与等待队列并发` 与 `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata`，随后进入键盘循环；
2. **检查点验证**：敲 `l115test` → `exec64` 命中 `l115test` 分支 → `l115test(c)` 把 `lesson_108_state` 填成 `{108,109,110,111,1,1,1,1}` → 断言 `b==a+1` 且四标志位全真 → VGA 输出 `l115test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；
3. **生产者消费者**：敲 `pctest` → `start_threads(3)` 分配两个线程栈、把两个 worker 的入口设成 `thread_trampoline_c`、`pc_reset` 重置事件与双信号量 → 两个 worker 进 `worker_run` 后 `event_wait` 阻塞在 `pc_start_event`；敲 `pcgo` → `event_set` 广播 → 双 worker 开始交替 `sem_down/sem_up`，共 4 步后 `thread_exit`；敲 `pcinfo` 读出 `E sig/wait`、`S count/wait`（空位信号量）、`I count/wait`（货信号量）、`R used/cap`、`P prod/cons`、`P errors/ok`；
4. **键盘等待**：敲 `kbdwaittest` → 两个 worker 阻塞在 `kbd_waitq`；此时敲普通字母键，IRQ1 走 `irq1_record` → `waitq_wake_one` 直投到线程 `mailbox`（`kbd_direct_deliveries++`），线程收到字符计数到 `received`；
5. **睡眠/抢占**：`sleeptest` 让两个 worker 分别 `thread_sleep_ticks(120/270)`，`wake_sleepers` 在 IRQ0 里到期置 `THREAD_RUNNABLE`；
6. **管道**：`pipeinfo`/`pipetest`/`polltest` 演示 `pipe_read_wait`/`pipe_write_wait` 两个等待队列在满/空边界上的阻塞与唤醒计数。

---

## 5. 构建、运行与验证

**依赖**：`gcc`（支持 `-m32` 与 `-m64`）、`ld`、`objcopy`、`grub-file`、`grub-mkrescue`、`qemu-system-x86_64`。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 成功输出：
```
Multiboot2 and Lesson 115 checks passed.
```

**运行**：
```bash
make run
```
> 成功画面在 QEMU 图形窗口（VGA 终端），请勿加 `-display none`；串口 `-serial stdio` 只用于调试输出。

**验证步骤**（预期输出串全部从 `kernel64.c` 逐字抄录）：

1. 启动横幅：`Lesson 115: 信号量与等待队列并发`；
2. `about` → `Lesson 115: 信号量与等待队列并发`；
3. `l115test` → `l115test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；
4. `pctest` → `pctest: producer and consumer blocked on start event; run pcgo`；
5. `pcgo` → `pcgo: event set; broadcast wake-all issued`；
6. 等 worker 跑完后 `pcinfo` 应看到 `P prod/cons: 4 4`、`P errors/ok: 0 yes`（`pcinfo` 的 `ok` 条件为 `prod==THREAD_STEPS&&cons==THREAD_STEPS&&!used&&!errors&&sc==PC_BUFFER_CAP&&!ic&&!sw&&!iw`）；
7. `kbdwaittest` → `kbdwaittest: two FIFO keyboard waiters started`，随后敲几个键，`ps` 里 worker 的 `received` 应增长；
8. `sleeptest` → `sleeptest: two timed workers started`；
9. 回归命令仍可用：`ps`、`threadinfo`、`pipetest`、`polltest`、`lockatomictest`（输出 `lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`）等。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `pctest` 后敲 `pcgo`，`pcinfo` 显示 `prod/cons` 停在 0 | `pc_start_event` 未置位或 worker 未进入 `event_wait`；`event_set` 的 `wake_all` 在 worker 入队前执行导致先放行后入队立即返回（不会卡死，只可能看到 waits 为 0） | 在 `event_wait` 入口与 `event_set` 打印状态；观察 `pcinfo` 的 `E waits/all` |
| `pcinfo` 显示 `P errors/ok: 非0 no` | 生产序号与消费期望错位：`pc_next`/`pc_expected` 步进被破坏，或环形缓冲 `head/tail` 未在临界区内更新 | 检查 `pc_producer`/`pc_consumer` 中 `irq_save64` 临界区是否包住缓冲读写 |
| `ps` 里 worker 状态停在 `blocked-sem` 不前进 | `sem_up` 的唤醒状态不匹配：`waitq_wake_one` 要求精确 `THREAD_BLOCKED_SEM`，若线程已因别的原因离开该状态则不会唤醒 | 核对 `waitq_wake_one` 第三个参数是否为 `THREAD_BLOCKED_SEM`；观察 `sem` 的 `wakes/blocks` 计数 |
| `kbdwaittest` 后按键 worker 收不到字符 | 键盘中断未装（`install_idt` 后 IRQ1 向量）或 `irq1_record` 未调用 `waitq_wake_one` 直投 | 敲 `kbdinfo` 看 `irq1_count`/`kbd_direct_deliveries`；确认 `ps` 中 worker `received` 是否增长 |
| 两个 worker 都卡死、idle 不再切换 | `sti; hlt` 挂起期间 IF 未恢复，或 `sem_up` 在某 worker 挂起前执行完导致计数漂移 | 检查 `irq_save64/irq_restore64` 配对；`pcinfo` 核对 `sc/ic` 与理论值 |
| `make check` 失败于 grep | README 或源码缺少本课主题串/命令名 | 确认 README 含 `信号量与等待队列并发` 与 `Lesson 115`，kernel64.c 含 `l115test` |
| `about` 显示旧课号 | `exec64` 中 `about` 分支字符串未随课号更新 | grep `eq64(word,"about")` 所在行，比对 `Lesson 115:` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学简化 |
|------------------|----------------|----------|
| `struct semaphore { count,max,waitq }` + `sem_down`/`sem_up` | `include/linux/semaphore.h`、`kernel/locking/semaphore.c` 的 `struct semaphore`、`down()`/`up()` | Linux 用自旋锁 `lock` 保护 `count`，支持睡眠唤醒与中断安全的 `down_interruptible`；TinyOS 单核下用 `irq_save64` 关中断代替锁，无中断打断版 down，无超时版本 |
| `struct wait_queue`（环形 ids） + `waitq_wake_one/all` | `include/linux/wait.h`：`wait_queue_head_t`、`wait_event`/`__wake_up`；唤醒本质也是把 `wait_queue_entry_t` 从链上摘除并回调 | Linux 队列持函数指针条目、链表式、支持互斥唤醒（WQ_FLAG_EXCLUSIVE）；TinyOS 只存线程 id、容量固定 2，无 exclusive 位 |
| `struct event` + `event_set`/`event_wait` | `include/linux/completion.h`、`kernel/sched/completion.c`：`complete()`/`wait_for_completion()` | Linux 用 `done` 计数可重复等待；TinyOS 的 `signaled` 是单次置位，`pcgo` 只能触发一次 |
| 双信号量有界缓冲（`pc_spaces`/`pc_items` + 环形数组） | 内核中 `kernel/irq_work`、`kernel/ring_buffer`、各驱动环形缓冲；经典双信号量 P/V | TinyOS 每一步 `busy_delay` 人为制造交错，`pc_sequence_errors` 把「乱序」显式记账，便于教学观察；真实内核靠调度延迟与内存序保证 |
| `irq_save64`/`irq_restore64`（pushfq+cli / sti） | `include/linux/irqflags.h`：`local_irq_save`/`local_irq_restore` | Linux 还要配合 `preempt_disable` 与 per-CPU `irq_count` 防止递归；TinyOS 只保存/恢复 IF 一位 |
| `kbd_waitq` 中断直投 mailbox（`kbd_direct_deliveries`） | 输入子系统的 waitqueue 唤醒（`drivers/input/serio`、`tty` 层 wake up）；`wake_up_interruptible` | TinyOS 只在「恰好有等待者」时直投，否则退回公共队列；真实驱动通常双缓冲加软中断排队 |
| 唤醒只改状态、由 IRQ0 调度器择机恢复 | `kernel/sched/core.c` `try_to_wake_up()`：置 `TASK_RUNNING` 并 `ttwu_queue` 投递到目标 CPU 运行队列 | TinyOS 单核、无 IPI、无运行队列迁移，唤醒后必须等下一个 100Hz 时间片；Linux 唤醒通常直接 `resched_curr` 触发目标 CPU 调度 |

权威来源：Intel SDM Vol.3A（中断/EFLAGS 的 IF 位、`CLI/STI`、`PUSHFQ/POPFQ`）、Multiboot2 规范（header 在镜像前 32768 字节且 8 字节对齐）、GNU GRUB 手册（`grub-mkrescue` 与 `grub-file`）。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `sem_down` 用 `for(;;)` 在唤醒后重新检查 `count`，而不是在 `sem_up` 时直接把 count 转交给等待者？如果改成后者会发生什么（提示：两个线程同时被唤醒）？
2. **源码定位**：在 `kernel64.c` 中找出 `THREAD_BLOCKED_SEM`、`THREAD_BLOCKED_EVENT`、`THREAD_BLOCKED_KBD` 三个状态各自被写入的地方，画出「谁把它写进去、谁把它改回 RUNNABLE」的关系表。
3. **动手实验**：把 `PC_BUFFER_CAP` 从 2 改成 4 后重新 `make run`，跑 `pctest`/`pcgo`/`pcinfo`，观察 `R used/cap` 与 `S/I count/wait` 的变化；再把 `THREAD_STEPS` 从 4 改成 8，解释 `P prod/cons` 的最终值。
4. **动手实验**：把 `sem_up` 中 `waitq_wake_one` 的状态参数误写成 `THREAD_BLOCKED_EVENT`，重新构建运行 `pctest`，观察消费者/生产者是否死锁，并用 `pcinfo` 定位原因。
5. **Linux 对照**：阅读 `kernel/locking/semaphore.c` 中 `down()` 的 `sleeper.function`，说明它如何在被唤醒后重新尝试 `__down_common`；与本课 `sem_down` 的 `for(;;)` 循环相比，TinyOS 省略了哪两个分支（超时、信号中断）？

---

## 9. 本课小结与下一课预告

- 本课把「信号量、等待队列、事件」三个并发原语串成一个有界缓冲的生产者-消费者模型：`sem_down/sem_up` 承担资源计数与排队，`waitq_*` 承担统一的 FIFO 阻塞/唤醒，`event_set/event_wait` 承担一次性的广播放行；
- 关键不变式：`0<=sem->count<=sem->max`、唤醒只改 `THREAD_RUNNING→THREAD_RUNNABLE`、被唤醒者必须重新竞争、所有共享读写包在 `irq_save64` 临界区内；
- 阻塞线程靠 `sti; hlt` 让出 CPU，由 IRQ0 调度器在下一时间片择机恢复——「唤醒 ≠ 立刻运行」；
- 键盘等待队列演示了中断上下文里的「直投 + 信箱」模式，与公共 FIFO 退路并存；
- 检查点命令 `l115test` 用 `lesson_108_model` 对「并发、SMP、RCU、诊断元数据」做确定性校验，输出恒为通过串；
- 下一步 [Lesson 116 per-CPU 数据访问](../lesson-116-stable/README.md) 把镜头转向每个 CPU 私有的数据区（`cpu_local`/`this_cpu()`），回答「同一份全局计数如何按 CPU 隔离」的问题；它与本课的衔接点在于：信号量/等待队列的统计计数（`downs/wakes` 等）在 Linux 里正是 per-CPU 数据的第一批应用对象。

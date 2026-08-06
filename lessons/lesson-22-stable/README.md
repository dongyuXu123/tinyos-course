# Lesson 22: 固定 event、计数信号量与生产者—消费者 — 精讲文档

> **课号**：Lesson 22（可执行课）
> **主题**：在 Lesson 21 的通用 `wait_queue` 之上实现两种同步原语：**固定 event**
> （一次性手动复位事件，`event_set` 用 `waitq_wake_all` 广播放行）与**计数信号量**
> （`sem_down`/`sem_up` 用 `waitq_push`/`waitq_wake_one` 传递缓冲槽与条目），
> 并让两个 PMM worker 完成有限「生产者—消费者」演示。
> **课程主线位置**：调度与同步阶段的收官课（Lesson 21 通用队列 → **本课 event/信号量**）。
> Lesson 19–21 分别给了「等时间」「等键盘」「通用登记」的机制，本课用它们搭建第一个
> 真正的同步程序：两个线程经同一环形缓冲交换 0..3 四个确定值。
> **前置课程**：[`lesson-21-stable/README.md`](../lesson-21-stable/README.md)（有界通用等待队列）
> **后续课程**：[`lesson-23-stable/README.md`](../lesson-23-stable/README.md)（独立 idle context 与 IRQ0 回退）
> **一句话目标**：学完本课你能说出并演示：event 的「先置谓词、后广播」语义、信号量
> down 的「先扣 token、否则排队」与 up 的「先还 token、再 wake_one」顺序，以及
> `pcgo` 一次广播唤醒两个 worker、让生产/消费各跑 4 轮且零序列错误的全过程。

---

## 1. 课程定位（Mission）

**一句话目标**：用 Lesson 21 的队列原语构造 `struct event` 与 `struct semaphore`，
运行 `pctest → pcgo` 三步验证，使 `pcinfo` 的不变量全部成立（`P errors/ok: ... yes`）。

- **在课程主线中的位置**：这是「同步原语」的落地课。前几课解决「谁在跑」，本课解决
  「什么时候一起跑」（event 广播）与「资源何时可用」（信号量计数）。Lesson 23 将把这些
  「阻塞在同步对象上」的线程从 shell 回退角色中解放出来——引入独立 idle context。
- **前置知识清单**：
  1. Lesson 21 的 `wait_queue` 四个操作（push/pop/wake_one/wake_all）及其「调用方须持
     关中断临界区」契约；
  2. `THREAD_BLOCKED_*` 状态与 `sti; hlt` 阻塞循环、IRQ0 帧替换调度；
  3. `kbd_wait_char` 的「先查谓词、后入队」无丢失唤醒写法（event_wait 复用同一模式）；
  4. `struct thread`、`start_threads` 原子启动、PMM 栈所有权。
- **本课交付**：三个新命令 `pctest`/`pcgo`/`pcinfo`；`struct event`/`struct semaphore`
  类型及六套操作；两槽环形缓冲的确定性生产者—消费者演示；`pcinfo` 的完整不变量报告。

---

## 2. 核心概念精讲

### 2.1 event：手动复位事件（一次性启动闸门）

```c
struct event { u8 signaled; volatile struct wait_queue waitq;
               u64 sets,resets,waits,wakes; };
static struct event pc_start_event;
```

- `signaled` 是谓词：为 1 表示「事件已发生」。`event_wait` 先看谓词，为真立即返回；
  否则 `waitq_push` 入队并 `blocked-event` 阻塞。
- `event_set` 的顺序是**先置谓词、再广播**：
  ```c
  static TEXT64 void event_set(struct event*e){
      u64 flags=irq_save64();
      e->signaled=1;
      e->sets++;
      e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);   // 广播放行全部
      irq_restore64(flags);
  }
  ```
  为什么先置谓词？因为 `wake_all` 之后被唤醒的 worker 回到 `event_wait` 的 `while` 循环，
  重查 `signaled` 必须看到 1，否则它们会**再次入队阻塞**——广播就白做了。
- 这是**手动复位**语义：`signaled` 一旦为 1 就一直为 1（`pc_reset` 时才清 0），
  与 Lesson 23 之后常见的 auto-reset 事件形成对照。`pcgo` 命令因此只允许设置一次
  （`pcgo: start event already set`）。

### 2.2 event_wait：谓词优先的无丢失等待

```c
static TEXT64 void event_wait(struct event*e){
    u8 id=current_thread;
    for(;;){
        u64 flags=irq_save64();
        if(e->signaled){ irq_restore64(flags); return; }        // 谓词已真：直接放行
        if(id&&threads[id].state==THREAD_RUNNING
           &&waitq_push(&e->waitq,id)){                         // 否则入队
            e->waits++;
            threads[id].state=THREAD_BLOCKED_EVENT;
        }
        irq_restore64(flags);
        while(threads[id].state==THREAD_BLOCKED_EVENT)
            __asm__ volatile("sti; hlt");
    }
}
```

与 `kbd_wait_char` 完全同构：**查谓词与入队在同一关中断临界区**，`event_set` 的
`signaled=1` 与 `wake_all` 也关中断，因此不会出现「先查谓词为假、正打算入队时事件被
设置」的丢失窗口。`waitq_push` 失败（队列满）时状态不变、下一轮重试，永不挂死。

### 2.3 计数信号量：count 资源 + 等待队列

```c
struct semaphore { u8 count,max; volatile struct wait_queue waitq;
                   u64 downs,ups,blocks,wakes,overflows; };
static struct semaphore pc_spaces,pc_items;
```

- `count` 是可用资源数（`pc_spaces` 初值 2 = 缓冲容量；`pc_items` 初值 0）；
  `max` 是上限，`sem_up` 超过上限计 `overflows`（教学防御）。
- `sem_down`（消费一个 token）：
  ```c
  static TEXT64 void sem_down(struct semaphore*s){
      u8 id=current_thread;
      for(;;){
          u64 flags=irq_save64();
          if(s->count){ s->count--; s->downs++; irq_restore64(flags); return; } // 有货先扣
          if(id&&threads[id].state==THREAD_RUNNING
             &&waitq_push(&s->waitq,id)){                    // 没货则排队
              s->blocks++;
              threads[id].state=THREAD_BLOCKED_SEM;
          }
          irq_restore64(flags);
          while(threads[id].state==THREAD_BLOCKED_SEM)
              __asm__ volatile("sti; hlt");
      }
  }
  ```
  顺序是「**先扣 token，否则入队**」——与 Linux `down()` 的「若负则睡眠」同一思路。
- `sem_up`（还一个 token）：
  ```c
  static TEXT64 void sem_up(struct semaphore*s){
      u64 flags=irq_save64(); u8 id;
      if(s->count<s->max){ s->count++; s->ups++; }
      else s->overflows++;
      if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id)) s->wakes++;
      irq_restore64(flags);
  }
  ```
  顺序是「**先还 token，再 wake_one**」：被唤醒的消费者醒来后重跑 `sem_down` 循环，
  在临界区内重查并扣除 `count`——唤醒只是提示，扣减永远发生在持锁重查时，
  **不会出现两个消费者同时看到 count=1 的竞争**。

### 2.4 生产者—消费者：两个信号量传递槽与条目

- `pc_spaces`：初始 = 2，表示空槽数；生产者 `sem_down(pc_spaces)` 拿一个空槽。
- `pc_items`：初始 = 0，表示满槽数；消费者 `sem_down(pc_items)` 拿一个条目。
- 生产者写入后 `sem_up(pc_items)`；消费者取走后 `sem_up(pc_spaces)`。这保证缓冲
  `pc_used` 永不越过 `PC_BUFFER_CAP`，也永不出现负值——两条「流量」互锁。
- 序列校验：生产者按 `pc_next++` 依次写 0..3；消费者校验读到的值必须等于
  `pc_expected`，否则 `pc_sequence_errors++`。配合 `pcinfo` 的 `errors` 字段，
  任何顺序破坏都会显形。

### 2.5 调度回退问题（本课预告 Lesson 23）

两个 worker 都可能同时 `blocked-event`/`blocked-sem`。此时系统仍靠 **shell（线程 0）**
作为「唯一 runnable」兜底：`next_runnable` 扫不到 runnable worker 就停在 shell，
`idle_worker_ticks` 增长。这在教学上成立，但把「回退」职责绑在 shell 上——Lesson 23
将引入独立 idle context 解除这个耦合。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-21-stable） |
|---|---|---|
| `boot.S` | 32 位入口、页表、long mode、`.incbin` blob | 未变化 |
| `kernel.c` | 32 位引导阶段 | 未变化 |
| `kernel64.c` | 64 位内核本体 | **全部本课增量**（event/semaphore/pc） |
| `kernel64.ld` | 64 位裸 blob 布局 | 未变化 |
| `linker.ld` | 外层 ELF32 段布局 | 未变化 |
| `Makefile` | 双阶段构建、ISO、check/run | 未变化 |
| `grub.cfg` | GRUB 菜单 | 仅 `menuentry` 标题改为 "TinyOS lesson 22: event and semaphore" |

> 备注：`kernel64.c` 首行注释仍写 `/* Lesson 21: ... */`，这是源码自身的陈旧注释
> （未随本课主题更新），不影响编译与行为，本精讲按实际内容处理。

### 3.2 kernel64.c 增量精讲

（说明：源码为单行风格，下列代码块按语句重排以便逐行注释，每条语句与源码逐字一致；
省略与 Lesson 21 相同的 PMM/VM/IDT/键盘部分。）

#### 3.2.1 新类型与全局状态

```c
enum thread_state { ..., THREAD_BLOCKED_EVENT, THREAD_BLOCKED_SEM, THREAD_FINISHED };
#define PC_BUFFER_CAP 2                      // 两槽环形缓冲
struct event { u8 signaled; volatile struct wait_queue waitq;
               u64 sets,resets,waits,wakes; };
struct semaphore { u8 count,max; volatile struct wait_queue waitq;
                   u64 downs,ups,blocks,wakes,overflows; };
static struct event pc_start_event;          // 启动闸门事件
static struct semaphore pc_spaces,pc_items;  // 空槽信号量 / 条目信号量
static u8 pc_buffer[PC_BUFFER_CAP],pc_head,pc_tail,pc_used,pc_next,pc_expected;
static u64 pc_produced,pc_consumed,pc_sequence_errors;
static u8 ...,pc_test;                       // mode==3 的测试开关
```

- `THREAD_BLOCKED_EVENT`/`THREAD_BLOCKED_SEM` 与 `blocked-kbd` 平级：三种阻塞原因，
  `thread_state_name` 分别输出 `blocked-event`/`blocked-sem`。
- 每个 `struct event`/`struct semaphore` 都内嵌自己的 `wait_queue`——一个队列服务
  一个同步对象，这是 Linux `wait_queue_head` 内嵌于对象内的同款布局。

#### 3.2.2 waitq_reset：队列复用辅助

```c
static TEXT64 void waitq_reset(volatile struct wait_queue*q){
    q->head=q->tail=q->count=0;
    q->enqueues=q->wake_one=q->wake_all=0;
}
```

`start_threads` 与 `pc_reset` 用它把 `kbd_waitq`/`pc_start_event.waitq`/信号量队列恢复
到初始态，保证同一次启动内统计干净、无残留等待者。

#### 3.2.3 event_set / event_wait（见 2.1/2.2 节）

要点：`event_set` 在同一个临界区内「置谓词 + 广播」，且用 `waitq_wake_all` 返回的
`n` 累加 `e->wakes`——`wake-all` 在本课第一次被真正调用（Lesson 21 只导出未使用）。

#### 3.2.4 sem_init / sem_down / sem_up（见 2.3 节）

`sem_init` 设定初始 `count` 与 `max` 并清零全部统计；`pc_reset` 里
`sem_init(&pc_spaces,PC_BUFFER_CAP,PC_BUFFER_CAP)`、`sem_init(&pc_items,0,PC_BUFFER_CAP)`
分别建立「空槽=2」「条目=0」两个互补信号量。

#### 3.2.5 pc_reset / pc_producer / pc_consumer（本课核心）

```c
static TEXT64 void pc_reset(void){
    u64 flags=irq_save64();
    pc_head=pc_tail=pc_used=pc_next=pc_expected=0;
    pc_produced=pc_consumed=pc_sequence_errors=0;
    pc_start_event.signaled=0;
    pc_start_event.sets=pc_start_event.resets=pc_start_event.waits=pc_start_event.wakes=0;
    waitq_reset(&pc_start_event.waitq);
    sem_init(&pc_spaces,PC_BUFFER_CAP,PC_BUFFER_CAP);
    sem_init(&pc_items,0,PC_BUFFER_CAP);
    irq_restore64(flags);
}
static TEXT64 void pc_producer(void){
    u8 value;
    while(threads[1].progress<THREAD_STEPS){
        sem_down(&pc_spaces);                                  // 等空槽
        { u64 flags=irq_save64();                              // 临界区：写缓冲
          value=pc_next++;
          pc_buffer[pc_head]=value;
          pc_head=(u8)((pc_head+1)%PC_BUFFER_CAP);
          pc_used++; pc_produced++;
          irq_restore64(flags); }
        threads[1].progress++;
        sem_up(&pc_items);                                     // 通知有条目
        busy_delay();
    }
    thread_exit();
}
static TEXT64 void pc_consumer(void){
    u8 value;
    while(threads[2].progress<THREAD_STEPS){
        sem_down(&pc_items);                                   // 等条目
        { u64 flags=irq_save64();                              // 临界区：读缓冲+校验
          value=pc_buffer[pc_tail];
          pc_tail=(u8)((pc_tail+1)%PC_BUFFER_CAP);
          pc_used--;
          if(value!=pc_expected) pc_sequence_errors++;         // 序列校验
          pc_expected++; pc_consumed++;
          irq_restore64(flags); }
        threads[2].progress++;
        sem_up(&pc_spaces);                                    // 归还空槽
        busy_delay();
    }
    thread_exit();
}
```

逐层分析：

1. **互斥用临界区而非信号量**：读写环形缓冲只可能发生在 worker 自身线程上下文，
   但 IRQ0 抢占可能把另一个 worker 换进来——所以用 `irq_save64` 把「读/写+更新索引」
   包成原子区，防止生产与消费同时改 `pc_head/pc_tail/pc_used`。
2. **确定性数据流**：生产者每次写 `pc_next++` 的连续值 0..3；消费者严格校验
   `value==pc_expected` 后 `pc_expected++`。若调度顺序导致读到乱序，`errors` 立即非零。
3. **两信号量互锁**：`pc_used` 的上界由 `pc_spaces` 保证（拿不到空槽就阻塞），
   下界由 `pc_items` 保证；完成时 `pcinfo` 检查 `used==0`。
4. **边界**：`sem_up` 的 `max` 防御使 `count` 不会超过容量；`pcgo` 只允许在
   `!signaled` 时设置一次；`pctest` 只能启动一次（`threads_started`）。

#### 3.2.6 worker_run 与 start_threads 的 pctest 分支

```c
static TEXT64 void worker_run(u8 id){
    if(pc_test){                                    // mode==3：生产者/消费者模式
        event_wait(&pc_start_event);                // 两个 worker 都先等闸门
        if(id==1) pc_producer(); else pc_consumer();
        return;
    }
    while(threads[id].progress<THREAD_STEPS){ ... 旧路径 ... }
    thread_exit();
}
static TEXT64 int start_threads(u8 mode){
    ...
    pc_test=mode==3;
    if(pc_test) pc_reset();                         // 启动即重置 pc 状态
    sleep_wakeups=idle_worker_ticks=kbd_direct_deliveries=0;
    waitq_reset(&kbd_waitq);
    ...
}
```

- `start_threads(3)` 在临界区内调用 `pc_reset()`，使 event 未置位、信号量回到初值、
  缓冲清空——一切就绪后两个 worker 才被置 runnable，它们首跑即 `event_wait` 阻塞，
  等 `pcgo` 广播。
- `threadinfo` 的 `mode:` 三态判断扩展为 `pc_test?"pctest":kbd_wait_test?"kbdwaittest":
  sleep_test?"sleeptest":"preempttest"`。

#### 3.2.7 pcinfo 与 exec64 新命令

```c
static TEXT64 void pcinfo(u16*c){
    ... 在 irq_save64 临界区内快照 es/ew/sc/sw/ic/iw/used/waits/wakes/prod/cons/errors ...
    text64(c,"E sig/wait: ");  hex64(c,es); text64(c," "); hex64(c,ew);      // 事件谓词/等待者
    text64(c,"\nE waits/all: "); hex64(c,waits); text64(c," "); hex64(c,wakes); // 入队/广播数
    text64(c,"\nS count/wait: "); hex64(c,sc); text64(c," "); hex64(c,sw);   // spaces 信号量
    text64(c,"\nI count/wait: "); hex64(c,ic); text64(c," "); hex64(c,iw);   // items 信号量
    text64(c,"\nR used/cap: "); hex64(c,used); text64(c," "); hex64(c,PC_BUFFER_CAP); // 环形缓冲
    text64(c,"\nP prod/cons: "); hex64(c,prod); text64(c," "); hex64(c,cons); // 生产/消费计数
    text64(c,"\nP errors/ok: "); hex64(c,errors); text64(c," ");
    text64(c,prod==THREAD_STEPS&&cons==THREAD_STEPS&&!used&&!errors
           &&sc==PC_BUFFER_CAP&&!ic&&!sw&&!iw?"yes":"no");  // 完成不变量
    putc64(c,'\n');
}
```

- `exec64` 新增三条命令：`pctest`（`start_threads(3)`）、`pcgo`（守卫 `!pc_test` 与
  `!signaled` 后调 `event_set`）、`pcinfo`。`help` 列表在 `kbdwaittest` 后插入
  `pctest pcgo pcinfo`。
- `pcinfo` 的输出格式串：
  `E sig/wait: `、`E waits/all: `、`S count/wait: `、`I count/wait: `、
  `R used/cap: `、`P prod/cons: `、`P errors/ok: `——每个字段先 hex 后空格分隔。

### 3.3 构建管线

与 Lesson 21 完全一致，无新增标志。`kernel64.ld` 的 `.data` 段（PROGBITS）保存
`pc_start_event`/`pc_spaces`/`pc_items`/`pc_buffer` 等全部持久同步状态。静态验收注意
旧 README 的提示：**裸 blob 链接器可能对 kernel64 报长期存在的 RWX LOAD 警告**，
该警告属于内部 continuation，勿与受检的外层 Multiboot ELF（`readelf -lW build/kernel.elf`）
混淆。

### 3.4 主控制流

```text
kernel_main64_binary → 横幅 → shell 循环
  → pctest → start_threads(3) → pc_reset()（event 复位、spaces=2、items=0、缓冲清空）
  → worker 1/2 首跑 → event_wait(&pc_start_event) → blocked-event（两个都入队）
  → pcgo → event_set：signaled=1 → waitq_wake_all → 两个 worker 变 runnable
  → producer: down(spaces) → 写 value 0 → up(items) → busy_delay → down(spaces) ...
  → consumer: down(items) → 读校验 0 → up(spaces) → busy_delay → ...
  → 各 4 轮后 finished → reap_finished_threads 还栈 → pcinfo 不变量全绿
```

---

## 4. 数据流与运行逻辑

1. `pctest` → 输出 `pctest: producer and consumer blocked on start event; run pcgo`。
   `ps`：worker 1/2 均 `blocked-event`，`pcinfo` 显示 `E sig/wait: 0 2`（谓词 0、
   等待者 2）、`E waits/all: 2 0`。
2. `pcgo` → 守卫通过 → `event_set` → 输出 `pcgo: event set; broadcast wake-all issued`。
   同一临界区内 `signaled=1` 且 `waitq_wake_all` 广播两个 worker；`pcinfo` 变为
   `E waits/all: 2 2`（wakes=2）。
3. 生产者与消费者经 PIT 抢占交替执行：每轮 `spaces`/`items` 在 0–2 间互补摆动，
   `R used/cap` 保持 ≤2；`P prod/cons` 同步推进到 4，`P errors/ok: 0 yes`。
4. 完成态：`S count/wait: 2 0`（空槽全回）、`I count/wait: 0 0`（条目清空）、
   `R used/cap: 0 2`；shell 全程可交互（event 广播不经过键盘直接投递路径）。
5. 下一个 PIT 边界后 `ps` 显示两 worker `finished` 且 `stack-pa` 归零；`meminfo`
   的 `tracked = free + used: yes` 保持，`free` 回到测试前基线。

---

## 5. 构建、运行与验证

### 5.1 依赖

与 Lesson 21 相同（gcc、binutils、grub-common、xorriso、mtools、qemu-system-x86_64），
无新增工具。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check                     # 期望输出: Multiboot2 header check passed.
readelf -rW build/kernel64.elf # 期望: continuation 无内部重定位
nm -u build/kernel64.elf       # 期望: 无未定义符号
readelf -SW build/kernel64.elf # 期望: 可写 .data 为 PROGBITS（保存 pc 同步状态）
objdump -d -Mintel build/kernel64.elf  # 期望: IRQ0 唯一帧选择/iretq；invlpg 保留
readelf -lW build/kernel.elf   # 期望: 外层无 RWX LOAD 段
```

**注意**：内部 raw continuation 链接（`build/kernel64.elf` 的 LOAD 段）若出现 RWX
警告，是长期存在的已知现象，不等于外层 Multiboot ELF 失败——以 `readelf -lW
build/kernel.elf` 为准。

### 5.3 运行与 VGA 验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅逐字如下（来自
`kernel_main64_binary`）：

```text
TinyOS lesson 22: event and counting semaphore
IRQ0 return-frame switching; high stacks, PMM, VM slot and keyboard enabled
tinyos> 
```

验证步骤（保留并扩充旧 README 的 QEMU VGA 流程）：

1. `pctest` → `ps`/`pcinfo`（广播前必查）：
   - 两个 worker 均 `blocked-event`；
   - `E sig/wait` 为 `0 2`（谓词 0、等待者 2）；
   - `E waits/all` 为 `2 0`；
   - 两 worker 的 PMM 栈仍占用（`ps` 的 `stack-pa` 非 0）。
2. `pcgo` → `pcinfo`（完成态必查）：
   - `E waits/all` 的 `all`（wake-all）为 2；
   - `P prod/cons` 均为 4；
   - `P errors` 为 0；
   - `R used/cap` 为 `0 2`（used 归零）；
   - `S count/wait` 为 `2 0`、`I count/wait` 为 `0 0`；
   - 两个信号量等待者计数均为 0；
   - `P errors/ok` 末尾为 `yes`（完整）。
3. 再过一个 PIT 调度边界后 `ps`：worker `stack-pa` 回收为 0；`meminfo` 保持
   `tracked = free + used` 且 `free` 回到测试前计数。
4. 回归 `kbdwaittest`、`sleeptest`、`preempttest`、PMM/VM（`vmtest`）、PIT
   （`tickinfo`）、键盘（`kbdinfo`）、可恢复 `bptest`。
5. `vmfaulttest`、`pftest`、`udtest` 只在隔离的全新启动中运行；全程不接受任何
   意外 QEMU 异常/三重故障/CR2/非法指令/页错误。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `pcgo` 后 worker 不推进，`P prod/cons` 停 0 | `event_set` 未把 `signaled` 置 1 就广播，worker 醒来重查谓词为假再次入队 | `pcinfo` 看 `E sig/wait` 与 `E waits/all`；核对 `event_set` 中 `signaled=1` 在 `waitq_wake_all` 之前 |
| `pcgo` 报 `pcgo: run pctest first` | 未启动 `pctest` 就 `pcgo` | 顺序：先 `pctest` 后 `pcgo` |
| `pcgo` 报 `pcgo: start event already set` | event 已手动复位为置位态 | 事件一次性；重启 `make run` 或接受该提示 |
| 生产者把缓冲写满后死等 | `pc_spaces` 用尽且消费者没被唤醒 | `pcinfo` 看 `S count/wait`（应为 0/1 徘徊）与 `I count/wait` |
| `P errors` 非 0 | 消费者读到的值 ≠ `pc_expected`（顺序破坏） | 检查 `pc_next`/`pc_expected` 初值与 `sem_down/up` 配对 |
| 完成态 `S count/wait` 不是 `2 0` | 某次 `sem_up` 没执行或 `count` 溢出 | 检查 `sem_up` 的 `count<s->max` 分支与 `pc_consumer` 的 `up(pc_spaces)` |
| `pcinfo` 的 `ok` 为 `no` | 任一不变量（prod/cons/used/errors/sc/ic/sw/iw）不满足 | 逐个比对七个字段与完成态预期 |
| 缓冲数据错乱但 errors=0 | 读/写缓冲没在临界区内，IRQ0 抢占穿插 | 确认 `pc_producer`/`pc_consumer` 的 `irq_save64` 临界区包住索引更新 |
| 多次 `pctest` 后统计残留 | `pc_reset` 未在 `start_threads(3)` 中调用 | 确认 `if(pc_test) pc_reset();` 存在且在同临界区 |

---

## 7. 与 Linux 源码对照

- **event/completion**：Linux `kernel/sched/completion.c` 的 `complete()` 先置
  `done` 再 `wake_up`，与 TinyOS `event_set` 的「先置谓词、后广播」顺序一致；
  `wait_for_completion` 对应 `event_wait`。
- **信号量**：Linux `kernel/locking/semaphore.c` 的 `down()`/`up()`——
  `down` 先尝试 `__down_common` 减计数，失败则加入 `wait_list`；`up` 先
  `s->count++` 再唤醒。TinyOS `sem_down`/`sem_up` 与之一一对应，只是用
  `irq_save64` 代替 `raw_spin_lock_irqsave`。
- **生产者—消费者经典模型**：两信号量（empty/full）守护有界缓冲，出自 Dijkstra 的
  P/V 教材模型；Linux 的 `kfifo`（`lib/kfifo.c`）是内核版有界 FIFO。TinyOS 的
  `pc_spaces`/`pc_items` 即 empty/full 的改名。
- **唤醒是提示、持锁重查**：Linux `down()` 被唤醒后再次进入循环重试；TinyOS
  `sem_down`/`event_wait` 的 `while(state==BLOCKED_*)` 重查循环同理。
- **教学模型简化了什么**：无信号量的公平/优先级队列（严格 FIFO）、无 `down_interruptible`
  超时、无 `completion` 的动态分配、无锁抽象（直接关中断）、单核假设。

---

## 8. 思考题与练习

1. （概念理解）`event_set` 为什么必须「先置 `signaled`、再 `waitq_wake_all`」？
   反过来会怎样？（提示：被唤醒者重查谓词。）
2. （源码定位）找出 `sem_down`/`sem_up` 中「唤醒者不直接给数据、消费者持锁重查」的
   证据；说明为什么这避免了两个消费者同时扣到同一个 token。
3. （动手实验）把 `PC_BUFFER_CAP` 改成 4，`pc_producer` 中把 `sem_up(&pc_items)` 删除，
   重新运行 `pctest`/`pcgo`，观察消费者卡在哪个状态、`pcinfo` 哪些字段异常。
4. （动手实验）把 `pc_producer` 的写入临界区去掉（不包 `irq_save64`），反复运行多次，
   观察 `P errors` 是否出现非 0——解释抢占在什么时刻破坏序列。
5. （Linux 对照）Linux `complete()` 与 TinyOS `event_set` 的置位与唤醒顺序对比；
   Linux 信号量的 `count` 允许为负（表示等待者数），TinyOS 为什么用 `blocks` 单独计数？

---

## 9. 本课小结与下一课预告

**小结**：本课在通用等待队列上搭出真正的同步程序：`struct event` 以「谓词 + 广播」
实现一次性启动闸门（`event_set` 先置位再 `waitq_wake_all`）；`struct semaphore` 以
`count` 承载资源量（`sem_down` 先扣后排队、`sem_up` 先还再 `wake_one`）；两槽环形缓冲
与 `pc_spaces`/`pc_items` 互锁，构成确定性生产者—消费者演示，`pc_next`/`pc_expected`
序列校验让顺序错误当场显形；`pctest`/`pcgo`/`pcinfo` 三命令配合 `E/S/I/R/P` 五行报告，
把同步原语的全部状态与不变量暴露在 VGA 上。全程仍无堆、无动态线程，IRQ0 独占帧选择。

**下一课预告**：[`lesson-23-stable/README.md`](../lesson-23-stable/README.md) 将解决本课
暴露的回退问题：当所有普通线程都阻塞时，调度器当前靠 shell 兜底。下一课引入**独立 idle
context**——专门的空闲线程 + 无普通 runnable 时的 IRQ0 回退路径，让 shell 也可以安全阻塞，
为最终每个线程拥有独立内核栈的异常/中断可靠性铺路。

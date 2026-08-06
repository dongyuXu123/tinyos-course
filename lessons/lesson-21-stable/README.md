# Lesson 21: 有界通用等待队列与 wake_one / wake_all — 精讲文档

> **课号**：Lesson 21（可执行课）
> **主题**：把 Lesson 20 的「键盘专用 FIFO」抽象成固定容量、零分配的**通用等待队列**
> `struct wait_queue`，提供 `waitq_wake_one`（唤醒一个）与 `waitq_wake_all`（全部唤醒）
> 两种语义；键盘路径成为它的第一个使用者。
> **课程主线位置**：调度与同步阶段（Lesson 20 键盘专用队列 → **本课 通用等待队列** →
> Lesson 22 event/信号量）。本课完成「先专用、后通用」的抽象跳跃：队列不再关心
> 「等什么」，只负责「登记谁、唤醒谁」。
> **前置课程**：[`lesson-20-stable/README.md`](../lesson-20-stable/README.md)（键盘阻塞等待队列 `wake_one`）
> **后续课程**：[`lesson-22-stable/README.md`](../lesson-22-stable/README.md)（固定 event / 计数信号量与生产者—消费者）
> **一句话目标**：学完本课你能说出：通用等待队列的契约（先 `waitq_push` 后置阻塞、
> `wake_one` 恰唤醒一个、`wake_all` 清空队列）、为什么「唤醒」与「数据投递」必须分离
> （唤醒者重查信箱），以及 `waitq_wake_all` 为什么在本课导出而不接命令。

---

## 1. 课程定位（Mission）

**一句话目标**：把 Lesson 20 写死在 `kbd_wait_push/pop` 里的逻辑重构成参数化的
`struct wait_queue` 方法，让同一个队列既能被 IRQ1 的 `wake_one` 用，也能被下一课的
event/信号量生产者的 `wake_all` 用。

- **在课程主线中的位置**：本课是「抽象化」的一课——不新增设备、不新增中断，只是把
  队列逻辑从「键盘专用」提升为「通用」：容量、头尾、计数、三种统计全收进结构体，
  操作全变成以 `volatile struct wait_queue *` 为第一参数的方法。Lesson 22 直接在此之上
  叠 event 与计数信号量。
- **前置知识清单**：
  1. Lesson 20 的 `kbd_wait_push/pop` 环形 FIFO 与 `kbd_wait_char` 的无丢失唤醒协议；
  2. IRQ1 直接投递路径（`kbd_wait_pop` 出队 + 写信箱 + 置 runnable）；
  3. `struct thread` 的 `mailbox`/`mailbox_ready` 字段与 `blocked-kbd` 状态；
  4. `volatile` 在「中断上下文写、线程上下文读」中的作用。
- **本课交付**：`struct wait_queue` 类型及其四个操作；`threadinfo` 新增
  `kbd waiters`/`kbd enqueue/one/all` 三合一统计；`waitq_wake_all` 作为导出的
  全局符号为下一课预留；键盘行为与 Lesson 20 保持等价。

---

## 2. 核心概念精讲

### 2.1 从「键盘专用」到「通用队列」：数据抽象

Lesson 20 的队列状态散落在六个全局变量里：

```c
static volatile u8 kbd_waiters[THREAD_COUNT-1],kbd_wait_head,kbd_wait_tail,kbd_wait_count;
static u64 ... kbd_wait_enqueues,kbd_wake_one,...;
```

Lesson 21 把它们收进一个类型：

```c
#define WAIT_QUEUE_CAP (THREAD_COUNT-1)   // 容量恰好 2（两个 worker）
struct wait_queue {
    u8 ids[WAIT_QUEUE_CAP],   // 环形槽位
       head,                  // 写端
       tail,                  // 读端
       count;                 // 占用数
    u64 enqueues,             // 入队统计
       wake_one,              // wake_one 成功次数
       wake_all;              // wake_all 累计唤醒数
};
static volatile struct wait_queue kbd_waitq;   // 唯一的队列实例
```

- 好处一：**语义中立**——队列不知道「里面等的是键盘」还是「信号量」，只登记 worker id。
- 好处二：**统计归一**——`enqueues/wake_one/wake_all` 跟随队列走，`threadinfo` 一处展示。
- 好处三：**可复用**——Lesson 22 只需再声明一个 `struct wait_queue` 实例。

### 2.2 队列契约：四条不变式

| 操作 | 契约 | 不变式保证 |
|---|---|---|
| `waitq_push(q,id)` | 满则返回 0；否则写入 `ids[head]`、`head=(head+1)%CAP`、`count++` | 调用方必须先 push 成功再置 `blocked-kbd` |
| `waitq_pop(q,&id)` | 空则返回 0；否则取 `ids[tail]`、`tail=(tail+1)%CAP`、`count--` | FIFO：总是先出最老的 |
| `waitq_wake_one(q,state,&out)` | pop 后校验 `state`，成功才置 runnable 并 `wake_one++` | 恰唤醒一个 |
| `waitq_wake_all(q,state)` | 循环 pop，逐一把合法等待者置 runnable，`wake_all += n` | 清空队列 |

关键约定：**所有操作都在关中断临界区或中断上下文内发生**（IRQ0 调度帧、IRQ1、或
`kbd_wait_char` 的 `irq_save64`），因此无需加锁，也保证 head/tail/count 不会被并发撕裂。

### 2.3 wake_one 与 wake_all：唤醒粒度的两种语义

```c
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){
    u8 id;
    if(!waitq_pop(q,&id)) return 0;                       // 队列空 → 无等待者
    if(!id||id>=THREAD_COUNT||threads[id].state!=state) return 0;  // 守卫：状态必须匹配
    threads[id].state=THREAD_RUNNABLE;                    // 只唤醒这一个
    q->wake_one++;
    *out=id;
    return 1;
}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){
    u8 id,n=0;
    while(waitq_pop(q,&id))                               // 清空队列
        if(id&&id<THREAD_COUNT&&threads[id].state==state){ // 逐个守卫校验
            threads[id].state=THREAD_RUNNABLE; n++;
        }
    q->wake_all+=n;
    return n;                                             // 返回实际唤醒数
}
```

- `state` 参数（本课传 `THREAD_BLOCKED_KBD`）让队列**通用化**：同一段代码可服务任意
  阻塞原因；「状态必须匹配」防止唤醒一个已经不在该状态的线程（如重复唤醒）。
- `wake_one` 只 pop 一个；`wake_all` 循环 pop 到空。IRQ1 的键盘投递用 `wake_one`——
  一个键只属于一个等待者；`wake_all` 是为 Lesson 22 的「生产者一次性放行所有消费者」
  准备的。

### 2.4 唤醒 ≠ 数据：mailbox 重查协议

被 `wake_one` 唤醒的 worker 只是「变得可运行」——它回到 `kbd_wait_char` 的
`while(threads[id].state==THREAD_BLOCKED_KBD) sti; hlt` 循环后面，**并不会自动拿到字符**。
IRQ1 必须先把键写进该 worker 的 `mailbox` 并置 `mailbox_ready`，worker 下一轮循环
重查信箱才能读到。这就是「唤醒与投递分离」：`waitq_wake_all` 只唤醒、不投递数据，
所以被 `wake_all` 的等待者醒来后信箱是空的，会再次入队等待——这正是旧 README 验证
条目「无投递则无进展」的含义。

### 2.5 为什么 waitq_wake_all 本课不接命令？

`waitq_wake_all` 用 `TEXT64`（非 static）导出为全局符号，但 `exec64` 的命令表里
**没有**触发它的命令。原因：键盘事件是「一对一」的（一个键给一个等待者），在本课的
测试场景里调用 `wake_all` 只会让被唤醒的 worker 因信箱为空而重新排队，没有可观察的
正向效果。它的真正消费者是 Lesson 22 的 event/semaphore 生产者（`signal` 放行全部）。
> 注意：旧 README 提到运行 `wqall` 命令验证「无投递则无进展」——**该命令并不存在于
> 本课冻结源码的命令表中**（`help` 列表可查证），这是旧文档与实际源码的一处不一致，
> 本精讲按源码如实处理。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-20-stable） |
|---|---|---|
| `boot.S` | 32 位入口、页表、long mode、`.incbin` blob | 未变化 |
| `kernel.c` | 32 位引导阶段 | 未变化 |
| `kernel64.c` | 64 位内核本体 | **全部本课增量**（通用等待队列） |
| `kernel64.ld` | 64 位裸 blob 布局 | 未变化 |
| `linker.ld` | 外层 ELF32 段布局 | 未变化 |
| `Makefile` | 双阶段构建、ISO、check/run | 未变化 |
| `grub.cfg` | GRUB 菜单 | 仅 `menuentry` 标题改为 "TinyOS lesson 21: generic wait queues" |

### 3.2 kernel64.c 增量精讲

（说明：源码为单行风格，下列代码块按语句重排以便逐行注释，每条语句与源码逐字一致；
省略与 Lesson 20 相同的 PMM/VM/IDT/定时部分。）

#### 3.2.1 struct wait_queue 与 kbd_waitq 实例

```c
#define WAIT_QUEUE_CAP (THREAD_COUNT-1)         // =2：容纳两个 worker
struct wait_queue {
    u8 ids[WAIT_QUEUE_CAP],head,tail,count;    // 环形槽位 + 头/尾/计数
    u64 enqueues,wake_one,wake_all;            // 三种统计
};
static volatile struct wait_queue kbd_waitq;   // volatile：IRQ1 写、线程读
```

- 数组、头尾、计数全为 `u8`；`count` 永远不会超过 2，`u8` 足够。
- `volatile` 继承自 Lesson 20 的约定：IRQ1 在中断上下文改 `head/tail/count`，
  线程上下文与 `threadinfo`/`kbdinfo` 读取。

#### 3.2.2 waitq_push / waitq_pop：环形 FIFO 的通用化

```c
static TEXT64 int waitq_push(volatile struct wait_queue*q,u8 id){
    if(q->count>=WAIT_QUEUE_CAP)return 0;          // 满：拒绝（防御，永不挂死）
    q->ids[q->head]=id;
    q->head=(u8)((q->head+1)%WAIT_QUEUE_CAP);      // 环形回绕
    q->count++; q->enqueues++;
    return 1;
}
static TEXT64 int waitq_pop(volatile struct wait_queue*q,u8 *id){
    if(!q->count)return 0;
    *id=q->ids[q->tail];
    q->tail=(u8)((q->tail+1)%WAIT_QUEUE_CAP);      // FIFO：最老优先
    q->count--;
    return 1;
}
```

与 Lesson 20 的 `kbd_wait_push/pop` 相比，只是把六个全局变量换成 `struct wait_queue*`
成员访问，算法逐字相同——这就是「提取（extract）」，行为零变化。

#### 3.2.3 waitq_wake_one / waitq_wake_all（本课核心）

（代码见 2.3 节。）要点分析：

1. **`waitq_wake_one` 的三段式**：`pop`（拿最老等待者）→ 守卫（id 合法且状态匹配）→
   生效（置 runnable、计数、回填 `*out`）。IRQ1 拿到 `*out` 后才知道把字符写给谁。
2. **守卫的动机**：`state` 参数使队列通用；若出队的线程已经不在 `state`（例如被
   `wake_all` 抢先、已 finished），则放弃这次唤醒——宁可让键走 shell 环形缓冲区，
   也不唤醒一个状态不符的线程。
3. **`waitq_wake_all` 清空队列**：`while(waitq_pop(...))` 一直 pop 到空；每个合法
   等待者都被置 runnable，返回总数 `n`，累计进 `q->wake_all`。被唤醒者若信箱为空，
   会按 2.4 节协议重新排队。
4. **导出方式不同**：`wake_one` 是 `static`（仅供 IRQ1 用）；`wake_all` 是非 static
   全局符号（`TEXT64 u8 waitq_wake_all(...)`），供 Lesson 22 的 event/semaphore 代码引用。

#### 3.2.4 kbd_wait_char 与 irq1_record 的调用点替换

```c
static TEXT64 void kbd_wait_char(u8 *out){
    u8 id=current_thread;
    for(;;){
        u64 flags=irq_save64();
        if(id&&threads[id].mailbox_ready){          // 先查信箱（可能已投递）
            *out=threads[id].mailbox; threads[id].mailbox_ready=0;
            irq_restore64(flags); return;
        }
        if(id&&threads[id].state==THREAD_RUNNING
           &&waitq_push(&kbd_waitq,id))             // 调用点：kbd_wait_push → waitq_push
            threads[id].state=THREAD_BLOCKED_KBD;
        irq_restore64(flags);
        while(threads[id].state==THREAD_BLOCKED_KBD)
            __asm__ volatile("sti; hlt");
    }
}
TEXT64 void irq1_record(void){
    ...
    if(ch){
        if(waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id)){  // 调用点替换
            threads[id].mailbox=ch; threads[id].mailbox_ready=1;
            kbd_direct_deliveries++;
        } else { ... shell 环形缓冲区 ... }
    }
    outb64(PIC1_COMMAND,PIC_EOI);
}
```

- `kbd_wait_char` 的无丢失唤醒协议（查信箱 + 入队同临界区）保持不变，只是
  `kbd_wait_push(id)` 变成 `waitq_push(&kbd_waitq,id)`。
- `irq1_record` 原来手写的「pop + 校验 + 置 runnable + 计数」被 `waitq_wake_one` 一行
  替代；投递数据（写 `mailbox`）仍由 IRQ1 自己完成——队列管「唤醒谁」，IRQ1 管
  「给什么」。

#### 3.2.5 start_threads / threadinfo / kbdinfo 的统计迁移

```c
sleep_wakeups=idle_worker_ticks=kbd_direct_deliveries=0;
kbd_waitq.head=kbd_waitq.tail=kbd_waitq.count=0;
kbd_waitq.enqueues=kbd_waitq.wake_one=kbd_waitq.wake_all=0;   // 队列内统计清零
```

- `threadinfo` 标题改为 `scheduler: PIT preemptive generic wait queues`，统计行变为：
  ```c
  text64(c,"\nkbd waiters: "); hex64(c,kbd_waitq.count);
  text64(c,"\nkbd enqueue/one/all: "); hex64(c,kbd_waitq.enqueues);
  hex64(c,kbd_waitq.wake_one); hex64(c,kbd_waitq.wake_all);
  ```
- `kbdinfo` 的 `waiters`/`wake-one` 改从 `kbd_waitq.count`/`kbd_waitq.wake_one` 读取，
  输出格式不变。

#### 3.2.6 exec64 与启动横幅

命令表与 Lesson 20 完全一致（**没有新增命令**，也没有 `wqall`）；仅文本变化：
`about` 输出 `TinyOS lesson 21: generic wait queues on IRQ0 return frames`，启动横幅
第一行改为 `TinyOS lesson 21: generic wait queues`。

### 3.3 构建管线

与 Lesson 20 完全一致。`kernel64.ld` 的 `.data` 段以 `PROGBITS` 保存 `kbd_waitq`
的持久状态（含统计字段），`objcopy -O binary` 后这些字段随 blob 进镜像。

### 3.4 主控制流

```text
kernel_main64_binary → 横幅("TinyOS lesson 21: generic wait queues") → shell 循环
  → kbdwaittest → start_threads(2)（清空 kbd_waitq 的 head/tail/count/统计）
  → worker A: kbd_wait_char → waitq_push(&kbd_waitq,A) → blocked-kbd → sti;hlt
  → worker B: 同理入队 → 队列 count=2，threadinfo 显示 waiters=2
  → 按键 → IRQ1: waitq_wake_one(&kbd_waitq,BLOCKED_KBD,&id) → pop A
      → A 置 runnable、mailbox=ch、mailbox_ready=1、direct_deliveries++
  → IRQ0 换入 A → 重查信箱得键 → progress++ → 再次 waitq_push 入队
  → 4 键后 finished；reap_finished_threads 归还栈
  → waitq_wake_all（导出符号）：Lesson 22 的生产者将在此调用
```

---

## 4. 数据流与运行逻辑

1. `kbdwaittest` → `start_threads(2)`：输出 `kbdwaittest: two FIFO keyboard waiters started`；
   `kbd_waitq` 的 `enqueues` 从 0 开始累计。
2. 两个 worker 相继 `waitq_push` 入队并 `blocked-kbd`；`threadinfo` 显示
   `kbd waiters: 0000000000000002`、`kbd enqueue/one/all: 2 0 0`（all 恒为 0）。
3. 每按一键：`waitq_wake_one` pop 出队一个，`wake_one` 与 `kbdinfo` 的
   `direct deliveries` 同步 +1；worker 消费信箱后 `progress` +1 并再次入队。
4. 由于 `waitq_wake_all` 本课无人调用，`threadinfo` 的 `all` 计数器始终为 0——
   这是「唤醒所有」语义在本课只导出未启用的直接证据。
5. 两 worker 各收满 4 键 finished；`meminfo` 栈回收回到基线，行为与 Lesson 20 等价。

---

## 5. 构建、运行与验证

### 5.1 依赖

与 Lesson 20 相同（gcc、binutils、grub-common、xorriso、mtools、qemu-system-x86_64），
无新增工具。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check                     # 期望输出: Multiboot2 header check passed.
readelf -rW build/kernel64.elf # 期望: 无内部重定位
nm -u build/kernel64.elf       # 期望: 无未定义符号；waitq_wake_all 为已定义全局符号
readelf -SW build/kernel64.elf # 期望: .data 为 PROGBITS（含 kbd_waitq 持久状态）
readelf -lW build/kernel.elf   # 期望: 外层无 RWX LOAD 段
```

### 5.3 运行与 VGA 验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅逐字如下（来自
`kernel_main64_binary`）：

```text
TinyOS lesson 21: generic wait queues
IRQ0 return-frame switching; high stacks, PMM, VM slot and keyboard enabled
tinyos> 
```

验证步骤（保留并扩充旧 README 的 QEMU VGA 流程）：

1. 跑 `kbdwaittest`，`ps`/`threadinfo`：两个 worker 均 `blocked-kbd`，
   `kbd waiters` 为 2，`kbd enqueue/one/all` 中 `enqueues` 到 2、`one` 为 0、`all` 为 0。
2. 逐个按键：每次恰好一个 FIFO worker 收到字符（`received` +1、`last` 更新），
   `wake-one` 与 `direct deliveries` 同步增长，`kbd waiters` 在 1–2 间摆动。
3. **wake_all 语义观察**：本课没有 shell 命令能触发 `waitq_wake_all`（旧 README 的
   `wqall` 命令并不存在于源码命令表）；观察 `threadinfo` 的 `all` 计数保持 0，
   说明无 mailbox 投递时 worker 不会凭空推进——这是「唤醒≠数据」的静态证据。
4. 等待期间 `tickinfo`/`threadinfo` 的 PIT 计数持续增长；直接投递模式下敲的键是
   测试输入，队列清空后 shell 键入恢复为命令。
5. 完成后 `meminfo`：finished 栈仅在另一当前栈下回收，`free` 回基线。
6. 回归 `sleeptest`、`preempttest`、`hhinfo`、`hhtest`、`vmtest`、`tickinfo`、`kbdinfo`、
   可恢复 `bptest`；`vmfaulttest`/`pftest`/`udtest` 另起独立启动（预期
   CR2 `00000000003ff000`、CR2 `0000000000400000`、`#UD`）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 按键后 worker 不醒，`wake-one` 不增长 | `waitq_wake_one` 的守卫失败（出队线程状态不是 `BLOCKED_KBD`），或队列为空 | `ps`/`threadinfo` 看状态与 `kbd waiters`；核对传给 `waitq_wake_one` 的 `state` 参数 |
| `threadinfo` 的 `all` 计数突然非 0 | 有代码调用了 `waitq_wake_all`（本课不应发生） | `nm build/kernel64.elf` 找 `waitq_wake_all` 引用；确认不是 Lesson 22 的产物 |
| `kbd waiters` 显示 3 或负数 | `waitq_push/pop` 的 count 增减不配对或回绕错 | 检查 `head/tail` 的 `%WAIT_QUEUE_CAP` 与 `count++/--` 对称性 |
| worker 被 `wake_all` 后死循环忙转 | 醒来信箱为空，`kbd_wait_char` 又 `waitq_push` 入队，反复被唤醒 | 这是预期语义（唤醒≠数据）；没有命令能触发，若出现检查是否有外部调用 |
| `ps` 的 `received`/`last` 列异常 | `worker_run` 的 mailbox 写回与 `received++` 顺序 | 核对 `kbd_wait_char(&ch); threads[id].mailbox=ch; threads[id].received++;` |
| `waitq_wake_all` 报未定义符号 | 改动了它的导出属性（`static`/`TEXT64`） | 确认定义为 `TEXT64 u8 waitq_wake_all(...)`（非 static） |
| 行为与 Lesson 20 出现差异 | 调用点替换漏了一处（如 `kbd_wait_push` 残留） | `grep -n "kbd_wait_push\|waitq_push" kernel64.c` 应只剩 `waitq_push` |

---

## 7. 与 Linux 源码对照

- **`struct wait_queue_head`**：Linux `include/linux/wait.h` 的 `struct wait_queue_head`
  包含自旋锁 + `struct list_head`；TinyOS `struct wait_queue` 是它的有界数组教学版，
  单核下用关中断代替自旋锁。
- **`wake_up` / `wake_up_all`**：Linux `kernel/sched/wait.c::__wake_up` 按
  `wake_flags` 决定唤醒一个（`WQ_FLAG_EXCLUSIVE`）还是全部；TinyOS 的
  `waitq_wake_one`/`waitq_wake_all` 对应这两种语义的极简版。
- **`wait_event` 与唤醒重查**：Linux 的 `wait_event` 宏在唤醒后**重新检查条件**
  （`___wait_event` 的循环），正是 TinyOS「唤醒后重查 mailbox」的同一原理——
  唤醒只是一次提示，不是数据本身。
- **通用化设计**：Linux 用 `add_wait_queue`/`remove_wait_queue` 操作任意队列；
  TinyOS 用 `volatile struct wait_queue *` 第一参数实现同样「一套代码、多个队列」。
- **教学模型简化了什么**：固定容量、无链表、无自旋锁（靠关中断）、无超时/取消、
  无优先级排序、无 `wake_up_interruptible` 之类的变体。

---

## 8. 思考题与练习

1. （概念理解）为什么 `waitq_wake_one` 要校验出队线程的 `state`？如果去掉守卫，
   在什么场景下会出问题？
2. （源码定位）在 `kernel64.c` 中找出 `waitq_wake_all` 的定义与所有调用点；
   说明它为什么本课「有定义、无调用」，并解释 `nm -u`/`nm` 的区别。
3. （动手实验）在 `kbdwaittest` 启动后，想办法让 `waitq_wake_all` 被触发一次
   （例如临时在 `exec64` 加一个 `wqall` 分支），观察 `threadinfo` 的 `all` 计数与
   worker 的 `progress`：解释为什么「无投递则无进展」。
4. （动手实验）把 `WAIT_QUEUE_CAP` 改为 3 并启动三个 worker（需先扩展 `THREAD_COUNT`），
   观察队列与 `kbd_wait_char` 是否仍然正确；说明容量上界由谁保证。
5. （Linux 对照）Linux 的 `wake_up` 如何区分「唤醒一个 EXCLUSIVE 等待者」与
   「唤醒全部」？对比 TinyOS 的 `wake_one`/`wake_all` 的 FIFO 顺序保证。

---

## 9. 本课小结与下一课预告

**小结**：本课完成了「专用 → 通用」的抽象：`struct wait_queue` 封装容量、头尾、计数与
三种统计，`waitq_push/pop/wake_one/wake_all` 以队列为第一参数操作任意等待者集合；
`wake_one` 恰唤醒一个并回填 id，`wake_all` 清空队列并返回唤醒总数；两者都通过
`state` 参数通用化，并用守卫校验避免唤醒状态不符的线程；`kbd_wait_char` 与
`irq1_record` 只是换了调用点，无丢失唤醒协议与键盘行为保持不变；`waitq_wake_all`
以全局符号导出，留给下一课的生产者使用，本课命令表不暴露它（旧 README 的 `wqall`
命令与冻结源码不符，已如实标注）。

**下一课预告**：[`lesson-22-stable/README.md`](../lesson-22-stable/README.md) 将在这
个通用队列之上实现**固定 event 与计数信号量**：信号量持有计数，生产者 `signal` 用
`waitq_wake_all` 放行，消费者 `wait` 用 `waitq_push` 排队，完成「生产者—消费者」
演示——届时你会在一个命令里看到两个 worker 交替生产与消费同一个固定缓冲。

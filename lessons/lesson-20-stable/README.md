# Lesson 20: 有界键盘阻塞等待队列与 wake_one — 精讲文档

> **课号**：Lesson 20（可执行课）
> **主题**：把 Lesson 19 的「定时休眠」推广为「事件等待」：为键盘 IRQ1 建立有界 FIFO
> 等待队列，worker 在队列中 `blocked-kbd` 阻塞，IRQ1 把键码**直接投递**给最老的等待者
> （`wake_one`），实现无丢失唤醒（no-lost-wakeup）协议。
> **课程主线位置**：调度与同步阶段（Lesson 19 定时休眠 → **本课 键盘阻塞队列** →
> Lesson 21 通用等待队列）。上节课的唤醒者是「时钟」，本节课的唤醒者是「键盘中断」。
> **前置课程**：[`lesson-19-stable/README.md`](../lesson-19-stable/README.md)（PIT 定时休眠）
> **后续课程**：[`lesson-21-stable/README.md`](../lesson-21-stable/README.md)（有界通用等待队列 `wake_one`/`wake_all`）
> **一句话目标**：学完本课你能说出并演示：worker 如何「等一个键盘字符」——在同一临界区内
> 检查信箱并把自己发布到有界队列、以 `blocked-kbd` 状态 `sti; hlt` 阻塞，再由 IRQ1 的
> `wake_one` 从 FIFO 头部取出并直接投递字符，全程不丢键、shell 不吃走 worker 的键。

---

## 1. 课程定位（Mission）

**一句话目标**：实现一个「按键 → 唤醒一个 worker」的事件传递原语：有界静态 FIFO
（恰好两个槽）、每 worker 一个一字节信箱、IRQ1 直接投递路径与 shell 环形缓冲区回退路径。

- **在课程主线中的位置**：这是「等待队列」的第一课。Lesson 19 的 `wake_sleepers` 是
  「时钟驱动的有界扫描」，本课把它升级为「事件驱动的有界 FIFO + wake_one」；下一课
  （Lesson 21）把「键盘专用队列」抽象成「通用等待队列」，Lesson 22 再叠加
  event/semaphore 与生产者—消费者。位置顺序是：**先专用、后通用、再信号量**。
- **前置知识清单**：
  1. Lesson 19 的 `thread_sleep_ticks` 睡眠循环（`sti; hlt` + 状态重查）与临界区写法；
  2. IRQ1 键盘中断路径：`irq1_record`、环形缓冲区 `kbd_queue`、`scan64` 扫描码翻译；
  3. `THREAD_*` 状态机与 `start_threads` 的原子启动；
  4. 抢占调度下「改状态」为什么必须关中断（IRQ0/IRQ1 会插进来）。
- **本课交付**：`kbdwaittest` 命令启动两个 worker，各等 4 个键后退出；`ps` 新增
  `received`/`last` 列；`threadinfo` 新增 `kbd waiters`/`kbd enqueues/wake-one`；
  `kbdinfo` 新增 `waiters`/`wake-one`/`direct deliveries` 统计。

---

## 2. 核心概念精讲

### 2.1 从「等时间」到「等事件」

Lesson 19 的 worker 调 `thread_sleep_ticks(120)` 等一个**时间点**；本课 worker 调
`kbd_wait_char(&ch)` 等一个**事件**（按键）。两者共享同一个调度骨架：

```text
改变状态(阻塞) → sti; hlt → 某中断把状态改为 runnable → 下次调度 iretq 恢复 → 重查条件
```

区别只在「谁来改状态」：定时睡眠由 IRQ0 的 `wake_sleepers` 按 `wake_tick` 到期唤醒；
键盘等待由 IRQ1 的 `wake_one` 在**有键的瞬间**唤醒。这就带来一个时间睡眠没有的新问题：
**事件可能在「检查」和「阻塞」之间到达**——如果你先看信箱发现没键、再入队阻塞，那么
一个按键恰好在这两件事之间落下，就会永远没人消费。这就是「丢失唤醒」（lost wakeup）。

### 2.2 有界键盘等待队列：静态两槽环形 FIFO

```c
static volatile u8 kbd_waiters[THREAD_COUNT-1],kbd_wait_head,kbd_wait_tail,kbd_wait_count;
```

- 容量 `THREAD_COUNT-1 = 2`，恰好容纳两个 worker，**静态数组、零分配**。
- `head` 写端、`tail` 读端、`count` 占用数；`(x+1)%2` 回绕。
- 约束：一个 worker 只有在 `blocked-kbd` 状态才在队列里，入队/出队都发生在关中断
  临界区（`irq0_schedule` 或 IRQ1 中），因此不会被并发破坏。

```c
static TEXT64 int kbd_wait_push(u8 id){                       // 生产端：worker 入队
    if(kbd_wait_count>=THREAD_COUNT-1)return 0;               // 队列满则失败
    kbd_waiters[kbd_wait_head]=id;
    kbd_wait_head=(u8)((kbd_wait_head+1)%(THREAD_COUNT-1));   // 环形前进
    kbd_wait_count++; kbd_wait_enqueues++;                    // 统计入队次数
    return 1;
}
static TEXT64 int kbd_wait_pop(u8 *id){                       // 消费端：IRQ1 取最老等待者
    if(!kbd_wait_count)return 0;
    *id=kbd_waiters[kbd_wait_tail];
    kbd_wait_tail=(u8)((kbd_wait_tail+1)%(THREAD_COUNT-1));   // FIFO：永远先出最老的
    kbd_wait_count--;
    return 1;
}
```

### 2.3 无丢失唤醒协议（核心概念）

`kbd_wait_char` 用**一次关中断临界区**同时完成「查信箱」与「入队阻塞」，从根上堵住
丢失窗口：

```c
static TEXT64 void kbd_wait_char(u8 *out){
    u8 id=current_thread;
    for(;;){
        u64 flags=irq_save64();                       // 关中断，临界区开始
        if(id&&threads[id].mailbox_ready){            // 1) 信箱已有键 → 直接消费
            *out=threads[id].mailbox; threads[id].mailbox_ready=0;
            irq_restore64(flags); return;
        }
        if(id&&threads[id].state==THREAD_RUNNING
           &&kbd_wait_push(id))                       // 2) 没键 → 发布自己并阻塞
            threads[id].state=THREAD_BLOCKED_KBD;
        irq_restore64(flags);                         // 临界区结束
        while(threads[id].state==THREAD_BLOCKED_KBD)  // 3) 阻塞循环
            __asm__ volatile("sti; hlt");
    }
}
```

为什么这能保证不丢键？

- IRQ1 在键到达时若发现队列非空，会 `kbd_wait_pop` 取走等待者并投递。若 worker 已经
  在队列里（状态 `blocked-kbd`），键一定被投递；若 worker 还没入队，说明它还在临界区
  里「查信箱」或「入队」——IRQ1 中断被 `cli` 挡在外面，等 worker 一入队，紧接着的
  IRQ1 就能取到它。**观察与阻塞之间不再有中断插入的机会**。
- 首次进入循环时信箱为空、状态为 RUNNING，因此会走 `kbd_wait_push` 分支并阻塞；
  之后每次被唤醒（`sti; hlt` 的 IRQ1 返回点）都会重新查信箱——`wake_one` 置
  `mailbox_ready=1` 并改状态为 runnable，循环条件先因「状态已变」退出 `hlt`，
  下一轮 `for` 再消费信箱。

### 2.4 IRQ1 直接投递与 wake_one

```c
TEXT64 void irq1_record(void){
    u8 raw=inb64(0x60),ch,next,id;
    ...
    if(!(raw&0x80)){
        irq1_count++; ch=(u8)scan64(raw);
        if(ch){
            if(kbd_wait_pop(&id)&&id&&id<THREAD_COUNT
               &&threads[id].state==THREAD_BLOCKED_KBD){
                threads[id].mailbox=ch;              // 直接投递到 worker 信箱
                threads[id].mailbox_ready=1;
                threads[id].state=THREAD_RUNNABLE;   // wake_one：只唤醒这一个
                kbd_wake_one++; kbd_direct_deliveries++;
            } else {                                  // 无等待者 → 走 shell 环形缓冲区
                next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));
                if(next==kbd_tail)kbd_overflow_count++;
                else{kbd_queue[kbd_head]=ch;kbd_head=next;}
            }
        }
    }
    outb64(PIC1_COMMAND,PIC_EOI);                     // 恰好一次 EOI
}
```

关键设计：

1. **直接投递（direct delivery）**：键码写进 worker 的 `mailbox` 而非 shell 环形缓冲区，
   shell 的 `kbd_dequeue` 永远读不到它——被唤醒 worker 的数据不会被 shell 抢走。
2. **wake_one 语义**：`kbd_wait_pop` 取出 FIFO **最老**的等待者，且只改那一个 worker
   的状态；其他等待者继续阻塞。这就是「唤醒一个」。
3. **守护条件**：`id && id<THREAD_COUNT && state==BLOCKED_KBD`——出队的 id 若状态
   已不合法（理论不会发生，防御性检查），则回退到环形缓冲区，键不会丢。
4. **IRQ1 不做调度**：它不调用 `irq0_schedule`、不换帧、不分配内存、不打印；
   只发一个 EOI 后 `iretq`。帧替换与抢占仍然只由 IRQ0 完成。

### 2.5 信箱（mailbox）与「last」诊断

`struct thread` 新增 `mailbox`（一字节键值）与 `mailbox_ready`（信箱是否有货）：
`wake_one` 置位 `mailbox_ready` 并写 `mailbox`；`kbd_wait_char` 消费时读 `mailbox`
并清 `mailbox_ready`。`worker_run` 收到字符后又把值写回 `threads[id].mailbox` 作为
「最后收到的字节」展示在 `ps` 的 `last` 列，`received` 记录累计收到的键数。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-19-stable） |
|---|---|---|
| `boot.S` | Multiboot2 32 位入口、建页表、进 long mode、`.incbin` blob | 未变化 |
| `kernel.c` | 32 位引导阶段：解析 MBI、建页表、交接结构 | 未变化 |
| `kernel64.c` | 64 位内核本体：PMM、IDT、调度、键盘、shell | **全部本课增量** |
| `kernel64.ld` | 64 位裸 blob 布局 | 未变化 |
| `linker.ld` | 外层 ELF32 段布局 | 未变化 |
| `Makefile` | 双阶段构建、ISO、check/run | 未变化 |
| `grub.cfg` | GRUB 菜单 | 仅 `menuentry` 标题改为 "TinyOS lesson 20: keyboard wake-one blocking" |

### 3.2 kernel64.c 增量精讲

（说明：源码为单行风格，下列代码块按语句重排以便逐行注释，每条语句与源码逐字一致；
省略了与 Lesson 19 相同的 PMM/VM/IDT/定时部分。）

#### 3.2.1 新状态、新 TCB 字段与新全局

```c
enum thread_state { THREAD_EMPTY, THREAD_RUNNING, THREAD_RUNNABLE, THREAD_SLEEPING,
                    THREAD_BLOCKED_KBD, THREAD_FINISHED };   // 新增 THREAD_BLOCKED_KBD
struct thread { u64 frame,stack_phys,switches,progress,wake_tick,received;
                u8 state,id,mailbox,mailbox_ready; };        // 新增 received/mailbox/mailbox_ready
static u8 current_thread,round_robin,threads_started,sleep_test,kbd_wait_test;
static volatile u8 kbd_waiters[THREAD_COUNT-1],kbd_wait_head,kbd_wait_tail,kbd_wait_count;
static u64 ...,kbd_wait_enqueues,kbd_wake_one,kbd_direct_deliveries;
```

- `THREAD_BLOCKED_KBD` 与 `THREAD_SLEEPING` 是两种不同阻塞：前者等事件（IRQ1 唤醒），
  后者等时间（IRQ0 唤醒）。`thread_state_name` 对应输出 `blocked-kbd`。
- `kbd_wait_*` 是 `volatile`，因为 IRQ1 写端、worker 读端跨上下文。
- 统计三件套：`kbd_wait_enqueues`（入队总数）、`kbd_wake_one`（成功唤醒数）、
  `kbd_direct_deliveries`（直接投递数）——后两者在本课恒相等（每个直接投递都是
  wake_one）。

#### 3.2.2 kbd_wait_char：无丢失唤醒的消费者侧（本课核心）

（代码见 2.3 节。）逐层分析：

1. **第一步先查信箱**：`mailbox_ready` 可能为真——比如 worker 被唤醒后还没消费完，
   下一个字符又投递进来，或唤醒后重新被调度。必须先消费再决定是否再阻塞。
2. **入队与阻塞原子**：`kbd_wait_push` 成功才改状态；若队列满（两个 worker 都已阻塞，
   理论不会再调用），`push` 返回 0，状态不变，worker 继续忙等下一轮——作为防御，
   永不挂死。
3. **`sti; hlt` 循环**：与 Lesson 19 相同，`hlt` 等待 IRQ1；IRQ1 的 `iretq` 恢复点
   回到 `while` 重查。注意 `hlt` 也会被 IRQ0 打断（每 10ms），但 IRQ0 不改状态，
   只切换帧；若 IRQ0 把当前 worker 换下去，恢复时仍在循环里。
4. **边界**：`if(id&&...)` 保证 shell（id=0）永不进入该 API；`mailbox_ready` 消费后
   立即清零，保证每个键只被消费一次。

#### 3.2.3 worker_run 与 start_threads 的模式选择

```c
static TEXT64 void worker_run(u8 id){
    while(threads[id].progress<THREAD_STEPS){
        if(kbd_wait_test){u8 ch; kbd_wait_char(&ch);
            threads[id].mailbox=ch; threads[id].received++;}   // 记录 last byte 与计数
        threads[id].progress++;
        if(sleep_test) thread_sleep_ticks(id==1?SLEEP_A_TICKS:SLEEP_B_TICKS);
        else if(!kbd_wait_test) busy_delay();                  // 仅 preempttest 忙等
    }
    thread_exit();
}
static TEXT64 int start_threads(u8 mode){
    ...
    sleep_test=mode==1;  kbd_wait_test=mode==2;
    sleep_wakeups=idle_worker_ticks=kbd_wait_enqueues=kbd_wake_one=kbd_direct_deliveries=0;
    kbd_wait_head=kbd_wait_tail=kbd_wait_count=0;              // 队列清空
    ...
    threads[i].switches=threads[i].progress=threads[i].wake_tick=threads[i].received=0;
    threads[i].mailbox=threads[i].mailbox_ready=0;
    ...
}
```

- `start_threads` 的入参从 `int timed` 变成 `u8 mode` 三态：0=preempttest，
  1=sleeptest，2=kbdwaittest。
- 每次启动在临界区内**重置等待队列与全部统计**，避免上一次测试残留等待者。
- `worker_run` 里 `threads[id].mailbox=ch` 是「写回 last byte」技巧：`kbd_wait_char`
  已把信箱消费清零，这里重新写入用于 `ps` 的 `last` 列展示。

#### 3.2.4 irq1_record：直接投递与回退路径（本课核心）

（代码见 2.4 节。）IRQ1 的职责边界：

- 只读 `0x60` 端口取原始扫描码，`irq1_raw_count++`；仅对 make 码（`!(raw&0x80)`）
  翻译成字符并计数 `irq1_count`。
- 有字符且队列非空 → `kbd_wait_pop` 出队最老等待者，写信箱、置 `mailbox_ready`、
  改状态为 runnable，`kbd_wake_one++`、`kbd_direct_deliveries++`。
- 有字符但无等待者 → 原样入 shell 环形缓冲区（Lesson 18 以来的行为），shell 正常响应。
- 收尾：**恰好一次** `outb64(PIC1_COMMAND,PIC_EOI)`，无调度调用。

#### 3.2.5 ps / threadinfo / kbdinfo 输出增量

- `ps64` 表头新增 `received` 与 `last` 两列：
  ```c
  text64(c,"threads: id state frame stack-pa stack-high switches progress wake-tick received last\n");
  ```
  行尾依次打印 `threads[i].received` 与 `threads[i].mailbox`（last byte）。
- `threadinfo` 标题改为 `scheduler: PIT preemptive timer and keyboard wakeups`，
  新增三行：
  ```c
  text64(c,"\nmode: ");  text64(c,kbd_wait_test?"kbdwaittest":sleep_test?"sleeptest":"preempttest");
  text64(c,"\nkbd waiters: "); hex64(c,kbd_wait_count);
  text64(c,"\nkbd enqueues/wake-one: "); hex64(c,kbd_wait_enqueues); hex64(c,kbd_wake_one);
  ```
- `kbdinfo` 标题改为 `keyboard: IRQ1 ring producer plus direct worker wake-one`，
  `queue head`/`queue tail` 改为 `ring head/tail` 并新增：
  ```c
  text64(c,"\nwaiters: "); hex64(c,waiters);
  text64(c,"\nwake-one: "); hex64(c,kbd_wake_one);
  text64(c,"\ndirect deliveries: "); hex64(c,kbd_direct_deliveries);
  ```

#### 3.2.6 exec64 与 kbdwaittest 命令

```c
}else if(eq64(word,"kbdwaittest")){
    if(!noargs64(arg)) usage64(c,"kbdwaittest");
    else{ int r=start_threads(2);
        if(r>0) text64(c,"kbdwaittest: two FIFO keyboard waiters started\n");
        else if(!r) text64(c,"kbdwaittest: already started\n");
        else text64(c,"kbdwaittest: PMM allocation failed\n");
    }
}
```

- `help` 列表在 `sleeptest` 与 `threadstart` 之间插入 `kbdwaittest`；`about` 输出
  `TinyOS lesson 20: keyboard wake-one blocking on IRQ0 return frames`。
- `threads_started` 保证一次只能有一个测试拥有固定 worker TCB 集。

### 3.3 构建管线

与 Lesson 19 完全一致（无新增标志）。`make check` 仍校验 Multiboot2 header；
`kernel64.bin` 仍是 `objcopy -O binary` 产物；`.data` 段在 `kernel64.ld` 里
`PROGBITS` 保留持久状态（本课新增的 `kbd_waiters`/信箱字段都在其中）。

### 3.4 主控制流

```text
kernel_main64_binary → 横幅 → shell 循环（kbd_dequeue 读环形缓冲区）
  → 输入 kbdwaittest → exec64 → start_threads(2)（清队列、建两 worker）
  → worker A 首跑 → kbd_wait_char：
      查信箱(空) → kbd_wait_push(A) → THREAD_BLOCKED_KBD → sti;hlt
  → worker B 同理由 IRQ0 换入 → 入队 → blocked-kbd
  → 用户按 'a' → IRQ1：kbd_wait_pop→A → mailbox='a'，A→runnable
  → IRQ0 量子切换 → A 恢复 hlt 后循环 → 查信箱有货 → 返回 'a'
  → worker_run：last='a'，received=1，progress=1 → 再次 kbd_wait_char 入队
  → 4 个键后 A finished；B 同理；reap_finished_threads 归还栈页
```

---

## 4. 数据流与运行逻辑

1. `kbdwaittest` → `start_threads(2)`：输出 `kbdwaittest: two FIFO keyboard waiters started`，
   shell 回到 `tinyos> `。`ps` 显示两 worker `blocked-kbd`、`received 0`、`last 0`。
2. 用户敲一个非命令测试键（如 `a`）：IRQ1 走直接投递，**只有 A** 收到 `mailbox='a'`，
   `received=1`、`last=61`（0x61）；B 仍是 `blocked-kbd`。FIFO 保证 A 先于 B。
3. 第二个键：IRQ1 出队 B，B `received=1`；此后 A/B 各自重新入队（A 若已排到队首则
   先收第三个键）。
4. 每投递一次，`kbdinfo` 的 `wake-one` 与 `direct deliveries` 同步 +1，
   `kbd waiters` 随入队/出队在 0–2 间变化；`threadinfo` 的 `PIT ticks` 持续前进，
   证明「等键期间时钟调度照常」。
5. 两 worker 各收满 4 键后 `finished`；`meminfo` 里两栈页被 `reap_finished_threads`
   归还，`used` 回基线。之后 shell 键入恢复正常（等待队列空，键回环形缓冲区）。

---

## 5. 构建、运行与验证

### 5.1 依赖

与 Lesson 19 相同（gcc、binutils、grub-common、xorriso、mtools、qemu-system-x86_64），
无新增工具。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check                     # 期望输出: Multiboot2 header check passed.
readelf -rW build/kernel64.elf # 期望: 无内部重定位
nm -u build/kernel64.elf       # 期望: 无未定义符号
readelf -SW build/kernel64.elf # 期望: .data 为 PROGBITS
objdump -d -Mintel build/kernel64.elf  # 期望: IRQ0 唯一 iretq；IRQ1 无调度调用
readelf -lW build/kernel.elf   # 期望: 外层无 RWX LOAD 段
```

### 5.3 运行与 VGA 验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅逐字如下（来自
`kernel_main64_binary`）：

```text
TinyOS lesson 20: keyboard wake-one blocking
IRQ0 return-frame switching; high stacks, PMM, VM slot and keyboard enabled
tinyos> 
```

验证步骤（保留并扩充旧 README 的 QEMU VGA 流程）：

1. 跑 `kbdwaittest`，然后 `ps`、`threadinfo`：worker 1/2 必须 `blocked-kbd`，
   `kbd waiters` 为 2，PMM 栈页仍被占用（`pfree` 对栈页报 `cannot free: thread stack`）。
2. 按一个非命令测试键：恰好一个 worker 变为 runnable 并收到字节（`received=1`），
   另一个仍是 `blocked-kbd`；`ps` 的 `last` 列显示该字符的 ASCII 值。
3. 再按第二个键：另一个 worker 收到。继续按键直到两个 worker 各收满 4 键，
   观察 `kbdinfo` 的 `wake-one`/`direct deliveries` 与 `threadinfo` 的
   `kbd enqueues/wake-one` 计数与 FIFO 顺序一致。
4. 等待期间 `tickinfo`/`threadinfo` 的 PIT 计数继续增长。注意：直接投递测试模式下，
   你敲的键是测试输入而非 shell 命令；等待队列清空后 shell 键入恢复为命令。
5. 完成后 `meminfo`：finished 栈仅在另一个当前栈下被回收，`free` 回到初始值。
6. 回归 `sleeptest`、`preempttest`、`hhinfo`、`hhtest`、`vmtest`、`tickinfo`、`kbdinfo`、
   可恢复的 `bptest`。`vmfaulttest`、`pftest`、`udtest` 另起独立启动验证（预期
   CR2 `00000000003ff000`、CR2 `0000000000400000`、`#UD`）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 按了键但 worker 不醒，`wake-one` 不增长 | worker 未在队列中（状态不是 `blocked-kbd`），或 IRQ1 的 `kbd_wait_pop` 没被执行 | `ps`/`threadinfo` 看状态与 `kbd waiters`；断点 `irq1_record` |
| worker 收到的键被 shell 打印成命令 | 直接投递路径没走，键落入环形缓冲区 | 看 `kbdinfo` 的 `direct deliveries` 是否为 0；检查 `kbd_wait_pop` 分支条件 |
| 键丢失（两 worker 都没收到，shell 也没有） | 丢失唤醒：键落在「查信箱」与「入队」之间 | 确认 `kbd_wait_char` 中查信箱与 `kbd_wait_push` 在同一 `irq_save64` 临界区 |
| 第二个 worker 永远不醒 | FIFO 队首始终是 A（A 每步都重新入队排最前） | 这是 FIFO 语义；让 A 先 finished 或观察 `kbd_wait_head/tail` |
| `kbd waiters` 显示 3 或负数 | 队列头/尾回绕或 count 计数错 | 检查 `kbd_wait_push/pop` 的 `%2` 回绕与 `count` 增减是否配对 |
| `ps` 里没有 `received`/`last` 列 | 运行的是旧二进制 | `make clean && make` 后重试 |
| 两个 worker 启动后立即 finished | `kbd_wait_test` 未置 1（mode 传错） | `threadinfo` 看 `mode:` 是否为 `kbdwaittest` |
| shell 卡死、PIT 也不走 | worker 阻塞时 IRQ0 被长期关掉 | 查 `kbd_wait_char` 是否正确地 `sti; hlt` 释放中断 |

---

## 7. 与 Linux 源码对照

- **等待队列与 `wake_up`**：Linux `include/linux/wait.h` 的 `wait_event` /
  `wake_up` 用链表等待队列（`struct wait_queue_head`），`wake_up` 遍历并唤醒
  （`kernel/sched/wait.c::__wake_up`）。TinyOS 的 `kbd_wait_push/pop` 是它的
  有界数组版，且本课只有 `wake_one`（等价于 Linux 的 `EXCLUSIVE` 标志位之一）。
- **无丢失唤醒协议**：Linux 的 `wait_event` 展开为
  `prepare_to_wait_event()`（入队）与条件检查之间的原子性保证；TinyOS 用
  `irq_save64` 把「查信箱 + 入队」变成临界区，思想一致。
- **中断下半部与直接投递**：Linux 的 IRQ 处理器（如 `drivers/tty/serial/8250`）在
  中断里唤醒 `tty` 等待者；TinyOS 的 IRQ1 直接写 worker 信箱并置 runnable，正是
  「中断中唤醒」的教学版。
- **环形缓冲区**：Linux `drivers/tty/tty_buffer.c` 的 tty 缓冲区、`include/linux/
  circ_buf.h` 的 `CIRC_SPACE` 等宏；TinyOS 的 `kbd_waiters` 与 shell 的
  `kbd_queue` 都是环形 FIFO 的极简实现。
- **教学模型简化了什么**：固定两槽数组（无动态增长）、每 worker 一字节信箱（无
  数据队列）、单核无锁（靠关中断）、无超时等待（`kbd_wait_char` 没有 `wake_tick`
  兜底）、无优先级。

---

## 8. 思考题与练习

1. （概念理解）解释「丢失唤醒」的具体时间窗口，并说明 `kbd_wait_char` 的临界区
   如何封死这个窗口。
2. （源码定位）为什么 IRQ1 不做调度（不调用 `irq0_schedule`）？如果 IRQ1 也换帧，
   会出现什么问题？在 `irq1_entry` 汇编里找出证据。
3. （动手实验）把 `worker_run` 中 `if(kbd_wait_test)` 分支改成「先 `thread_sleep_ticks`
   再 `kbd_wait_char`」，观察 FIFO 顺序与统计计数有什么变化。
4. （动手实验）把 `kbd_wait_pop` 的 FIFO 取出改成 LIFO（取 `head-1`），重新运行，
   观察哪个 worker 先收到第二个键，验证队列语义。
5. （Linux 对照）Linux 的 `wake_up` 与 TinyOS 的 `kbd_wait_pop`+直接投递在
   「唤醒谁」和「数据怎么给」上有什么本质差异？TinyOS 的一字节信箱能承载多大的
   事件信息？

---

## 9. 本课小结与下一课预告

**小结**：本课把「等时间」推广为「等事件」：`THREAD_BLOCKED_KBD` 新状态、两槽环形
等待队列 `kbd_waiters`、每 worker 一字节信箱；`kbd_wait_char` 用一次关中断临界区
同时完成「查信箱」与「入队」，从根上实现无丢失唤醒；IRQ1 的 `wake_one` 按 FIFO 取出
最老等待者并直接投递键码（`direct deliveries`），无等待者时回退 shell 环形缓冲区；
IRQ1 保持「不调度、一次 EOI、返回」的职责边界，帧切换仍归 IRQ0。`kbdwaittest` 命令
与 `ps`/`threadinfo`/`kbdinfo` 的 `received`/`last`/`waiters`/`wake-one` 统计让整个
投递链可观测。

**下一课预告**：[`lesson-21-stable/README.md`](../lesson-21-stable/README.md) 会把
「键盘专用等待队列」抽象成**有界通用等待队列**：不区分等待原因，任意 worker 都能入队，
并提供 `wake_one` 与 `wake_all` 两种唤醒语义——为 Lesson 22 的 event/信号量
生产者—消费者铺路。

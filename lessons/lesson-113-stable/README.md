# Lesson 113: mutex 与 spinlock 竞争 — 精讲文档

> **课号**：Lesson 113（检查点课 / Checkpoint Lesson，可执行课）
> **主题**：mutex 与 spinlock 竞争（Mutex vs. Spinlock Contention）
> **课程主线位置**：第 5 阶段检查点主线（约 100–114）的并发站起点。109–112 都是
> VFS/设备/epoll/服务主题；本课起转入并发主题：mutex/spinlock 竞争（113）→ 原子操作与
> 内存序（114）。
> **前置课程**：[`lesson-112-stable/README.md`](../lesson-112-stable/README.md)
> **后续课程**：[`lesson-114-stable/README.md`](../lesson-114-stable/README.md)
> **一句话目标**：理解 spinlock 与 mutex 在「忙等 vs. 阻塞」上的本质区别，读懂继承自
> lesson-50 的 `raw_spin_lock_irqsave`/`atomic_exchange_acquire_u32` 如何用一条
> 原子交换指令实现互斥，并理解 `pctest`/`pcgo` 生产者-消费者用信号量表达的锁竞争。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能回答「为什么 spinlock 用 while(atomic_exchange(...)) 忙等、
mutex/信号量用 `sem_down` 让出 CPU」——前者适合短临界区且不能睡眠的上下文（中断/软中断），
后者适合可能长时间等待的进程上下文。`l113test` 输出
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed` 即通过。

- **在课程主线中的位置**：第 5 阶段检查点主线的并发站（113–114）。本课主题
  「mutex 与 spinlock 竞争」，机制来自 lesson-50 引入的
  `raw_spinlock_t`/`irq_save64`/`atomic_*` 原语与 lesson-57 起的
  `event`/`semaphore`/`wait_queue` 阻塞原语。
- **责任边界**：本课**不新增**并发机制。新增的只有 `struct lesson_106_model`、
  `lesson_106_state`、`l113test()`、成功串（换成 concurrency/SMP/RCU/diagnostics 措辞）
  与主题串。检查点模型从 VFS/设备/epoll/服务措辞切换为并发措辞。
- **前置知识清单**：① `raw_spinlock_t`/`raw_spin_lock_irqsave`/`raw_spin_unlock_
  irqrestore`（lesson-50）；② `atomic_exchange_acquire_u32`/`atomic_store_release_u32`/
  `atomic_fetch_or_relaxed_u8` 等 `__atomic_*` 内建（内存序 acquire/release/relaxed）；
  ③ `event`/`semaphore`/`wait_queue`（lesson-57，`pctest`/`pcgo`/`pcinfo`）；④
  `cpu_local`/`this_cpu` per-CPU 模型与 `softirq_model`。
- **本课交付**：命令 `l113test`（新增）与 `l105test`（由 112 课 `l112test` 更名）；
  `about`/横幅显示 `Lesson 113: mutex 与 spinlock 竞争`；
  `make check` 输出 `Multiboot2 and Lesson 113 checks passed.`。

---

## 2. 核心概念精讲

### 2.1 概念一：spinlock —— 忙等自旋锁

定义：spinlock 是一种「获取失败就原地循环重试」的锁。TinyOS 版本：
`raw_spinlock_t { volatile u32 locked; }`，获取用
`while(atomic_exchange_acquire_u32(&l->locked,1)){}`。

为什么需要：在中断处理、softirq、SMP 临界区里，当前执行上下文**不能睡眠**（睡眠需要
调度器，而调度器本身就依赖这些锁）。此时唯一安全的等待方式就是「自旋」——反复尝试用
一条原子指令把 `locked` 从 0 换成 1，谁成功谁进临界区。

工作机制：

```text
线程A: atomic_exchange(&locked,1) → 返回旧值0 → 拿到锁，进入临界区
线程B: atomic_exchange(&locked,1) → 返回旧值1 → 自旋重试（原地空转）
线程A: 临界区结束 → atomic_store_release(&locked,0) → 释放
线程B: 下一次 exchange 返回0 → 进入
```

关键性质：`xchg`/`exchange` 是**原子读-改-写**——两个线程同时执行也不会都拿到锁；
`acquire` 语义保证「拿锁后临界区的读写在锁获取之后可见」，`release` 语义保证「临界区
写入在解锁前对其他 CPU 可见」。这就是 113/114 两课反复强调的「锁 = 原子指令 + 内存序」。

### 2.2 概念二：mutex / 信号量 —— 阻塞式互斥

定义：mutex（互斥量）在获取失败时把调用者**挂起**（睡眠），等持有者释放后唤醒。TinyOS
用 `semaphore`（计数信号量）+ `wait_queue` 表达：`sem_down` 在 `count==0` 时把自己加入
等待队列并阻塞；`sem_up` 计数 +1 并唤醒一个等待者。

为什么需要：临界区可能持有锁很久（等待 I/O、等待子进程），此时自旋就是烧 CPU。
进程上下文可以睡眠，于是把「等待」从忙等变成「挂起-唤醒」。

工作机制（源码逐字见 §3.3）：
- `struct semaphore { u8 count,max; volatile struct wait_queue waitq; ... }`；
- `sem_down`：`count>0` 则 `count--` 直接进入；否则 `waitq_enqueue` 把自己放入队列并阻塞
  （`threads[id].state=THREAD_BLOCKED_SEM`），`sti; hlt` 让出 CPU；
- `sem_up`：`count<max` 则 `count++`；再 `waitq_wake_one` 唤醒队首阻塞者。

spinlock 与 mutex 的对比表：

| 维度 | spinlock（`raw_spin_lock_irqsave`） | mutex/semaphore（`sem_down/sem_up`） |
|---|---|---|
| 等待方式 | 忙等自旋（`while(exchange(1)){}`） | 阻塞挂起（加入 waitq，`sti; hlt`） |
| 适用上下文 | 中断/softirq/不可睡眠 | 进程上下文 |
| CPU 占用 | 高（自旋烧时间片） | 低（让出 CPU） |
| 临界区长度 | 短 | 可长 |
| TinyOS 载体 | `deferred_lock`、`lockatomictest` | `pc_spaces`/`pc_items`、`pctest` |

### 2.3 概念三：锁竞争与「原子性 + 内存序」

定义：竞争（contention）指多个执行流同时尝试进入同一临界区。竞争的胜负由原子指令决定；
可见性（谁先看到谁写的）由内存序决定。TinyOS 用五个 `__atomic_*` 内建覆盖
acquire/release/relaxed 三种序（lesson-50 引入，本课与 114 课是它的「主题课」）：

```c
static TEXT64 u8 atomic_load_relaxed_u8(volatile u8*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
static TEXT64 void atomic_store_release_u8(volatile u8*p,u8 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 u8 atomic_fetch_or_relaxed_u8(volatile u8*p,u8 v){return __atomic_fetch_or(p,v,__ATOMIC_RELAXED);}
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
```

- `relaxed`：只保证操作本身原子，不承诺顺序（用于计数器/标志位）；
- `acquire`：保证「其后的读写不会重排到它之前」（用于拿锁）；
- `release`：保证「其前的读写不会重排到它之后」（用于解锁/发布数据）。

`raw_spin_lock_irqsave` 是「关中断 + acquire 自旋」，`raw_spin_unlock_irqrestore` 是
「release 解锁 + 恢复中断」——既挡并发又挡中断抢占，是单核教学内核里最正统的互斥写法。

### 2.4 概念四：生产者-消费者（pctest/pcgo）——锁竞争的完整演示

定义：`pc_producer`/`pc_consumer` 两个线程共享一个 `pc_buffer[PC_BUFFER_CAP]`，用两个
信号量 `pc_spaces`（空位）与 `pc_items`（已占用）协调；缓冲区操作包在
`irq_save64/irq_restore64` 里。

为什么需要：这是「锁竞争 + 阻塞同步」最直观的场景——生产者拿空位信号量、写缓冲、释放
数据信号量；消费者对称操作。`pcinfo` 显示两边计数、等待者数、序号错误数。

工作机制（源码逐字见 §3.3）：`pctest` 启动两个 worker 并阻塞在 `pc_start_event` 上，
`pcgo` 用 `event_set` 广播放行；`pcinfo` 的 `P errors/ok:` 一行在
`prod==THREAD_STEPS&&cons==THREAD_STEPS&&!used&&!errors` 时输出 `yes`。

### 2.5 概念五：`lesson_106_model` 检查点模型

```c
struct lesson_106_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_106_model lesson_106_state;
```

与 105 模型同构；`l113test` 注入 `{106U,107U,108U,109U,1,1,1,1}` 并断言
`valid&&active&&ready&&accounted&&b==a+1U`。成功串从
`bounded VFS, devices, epoll, and service management checkpoint passed` 切换为
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`——这是主线主题切换的
**语义证据**（Makefile 依赖它区分阶段）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-112） |
|---|---|---|
| `boot.S` | Multiboot2 header + 长模式引导 | 未变化 |
| `kernel.c` | 32 位阶段 | 未变化 |
| `kernel64.c` | 64 位内核主体 | `l112test`→`l105test` 更名；新增 `struct lesson_106_model`/`lesson_106_state`/`l113test`（成功串改为 concurrency/SMP/RCU/diagnostics）；exec64 增 `l113test` 分支；`about` 与横幅改「Lesson 113: mutex 与 spinlock 竞争」 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 + check | check grep 更新为 `mutex 与 spinlock 竞争`/`l113test`/`Lesson 113` |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 本课增量（源码逐字）

更名命令 `l105test`（即 112 课的 `l112test`）：

```c
static TEXT64 void l105test(u16*c){lesson_105_state=(struct lesson_105_model){105U,106U,107U,108U,1,1,1,1};int ok=lesson_105_state.valid&&lesson_105_state.active&&lesson_105_state.ready&&lesson_105_state.accounted&&lesson_105_state.b==lesson_105_state.a+1U;text64(c,"l105test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 105 fallback reported");putc64(c,'\n');}
```

- 函数名改为 `l105test`（= 模型号 105），逻辑与成功串逐字未变。

新增结构与命令：

```c
struct lesson_106_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_106_model lesson_106_state;
static TEXT64 void l113test(u16*c){lesson_106_state=(struct lesson_106_model){106U,107U,108U,109U,1,1,1,1};int ok=lesson_106_state.valid&&lesson_106_state.active&&lesson_106_state.ready&&lesson_106_state.accounted&&lesson_106_state.b==lesson_106_state.a+1U;text64(c,"l113test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 106 fallback reported");putc64(c,'\n');}
```

逐行拆解：

1. 结构 `lesson_106_model`：与 105 同构，4 个 u32 计数 + 4 个 u8 状态位；
2. 全局 `lesson_106_state`：默认全零，仅 `l113test` 注入；
3. `l113test` 赋值 `{106U,107U,108U,109U,1,1,1,1}`：四状态位全 1；
4. 断言 `ok=`：五条件 AND（`valid/active/ready/accounted` + `b==a+1U`）；
5. **成功串切换**：`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`
   （不再是 VFS/设备/epoll/服务措辞）——标明检查点主线进入并发阶段；
6. 失败串 `Lesson 106 fallback reported` 不变的安全回退语义。

exec64 新增分支（源码逐字）：

```c
else if(eq64(word,"l105test")){if(!noargs64(arg))usage64(c,"l105test");else l105test(c);}else if(eq64(word,"l113test")){if(!noargs64(arg))usage64(c,"l113test");else l113test(c);}
```

主题横幅与 about（源码逐字）：

```c
text64(&c,"Lesson 113: mutex 与 spinlock 竞争\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

```c
text64(c,"Lesson 113: mutex 与 spinlock 竞争\n");
```

### 3.3 继承机制精讲——并发机制的源码基础（本课主题的实质内容）

锁类型与关中断原语（源码逐字）：

```c
typedef struct { volatile u32 locked; } raw_spinlock_t;
typedef unsigned long irqflags_t;
static raw_spinlock_t deferred_lock;
static TEXT64 u64 irq_save64(void){u64 flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");return flags;}
static TEXT64 void irq_restore64(u64 flags){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");}
```

- `raw_spinlock_t` 只有一个 `volatile u32 locked`；`irqflags_t` 是 unsigned long，存 RFLAGS
  快照；
- `irq_save64` 用 `pushfq; popq` 保存 IF 位再 `cli` 关中断；`irq_restore64` 按第 9 位
  （IF）决定是否 `sti`——「保存-关-恢复」成对使用，防止破坏调用者的中断状态；
- `deferred_lock` 是全局演示锁，`lockatomicinfo` 打印它的 `locked` 值。

互斥实现（源码逐字）：

```c
static TEXT64 void raw_spin_lock_irqsave(raw_spinlock_t*l,irqflags_t*f){*f=irq_save64();while(atomic_exchange_acquire_u32(&l->locked,1)){} }
static TEXT64 void raw_spin_unlock_irqrestore(raw_spinlock_t*l,irqflags_t f){atomic_store_release_u32(&l->locked,0);irq_restore64(f);}
```

- 拿锁：先 `irq_save64` 关中断（`*f` 带回原状态），再自旋 `atomic_exchange_acquire_u32
  (&l->locked,1)`——一次原子交换同时完成「测试并置位」，返回旧值；旧值非 0 说明被占，
  继续循环；
- 解锁：`atomic_store_release_u32(&l->locked,0)` 把锁清零（release 保证临界区写操作先
  可见），再 `irq_restore64(f)` 恢复中断；
- 对称性：`irq_save64` 与 `irq_restore64` 配对，`exchange_acquire` 与 `store_release`
  配对——这就是「锁协议」的全部要素。

测试命令（源码逐字）：

```c
static TEXT64 void lockatomictest(u16*c){irqflags_t f;u8 v=0;atomic_fetch_or_relaxed_u8(&this_cpu()->softirq_pending,0);raw_spin_lock_irqsave(&deferred_lock,&f);v=atomic_load_relaxed_u8(&this_cpu()->softirq_pending);atomic_store_release_u8(&this_cpu()->softirq_pending,(u8)(v|1));raw_spin_unlock_irqrestore(&deferred_lock,f);text64(c,"lockatomictest: ");text64(c,atomic_load_relaxed_u8(&this_cpu()->softirq_pending)==1&&deferred_lock.locked==0?"irq-safe lock, atomic publication, per-CPU ordering passed":"BROKEN");putc64(c,'\n');}
```

- 演示三步：拿锁 → 在锁内用 `atomic_store_release_u8` 发布 per-CPU 的
  `softirq_pending` 标志 → 解锁；
- 断言 `softirq_pending==1 && deferred_lock.locked==0`：发布生效且锁已释放；
- 这同时演示了「release 发布数据 + per-CPU 结构」——114 课内存序主题的伏笔。

阻塞同步（源码逐字）：

```c
static TEXT64 void sem_down(struct semaphore*s){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(s->count){s->count--;s->downs++;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&s->waitq,id)){s->blocks++;threads[id].state=THREAD_BLOCKED_SEM;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_SEM)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_up(struct semaphore*s){u64 flags=irq_save64();u8 id;if(s->count<s->max){s->count++;s->ups++;}else s->overflows++;if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id))s->wakes++;irq_restore64(flags);}
```

- `sem_down`：`count>0` 直接 `count--` 并返回（不阻塞）；否则入队 + 置
  `THREAD_BLOCKED_SEM`，然后 `sti; hlt` 真正睡眠——与 spinlock 的忙等形成鲜明对比；
- `sem_up`：`count<max` 则 `count++`（否则 `overflows++`），再 `waitq_wake_one` 唤醒
  队首阻塞线程；
- `pctest`/`pcgo` 用 `pc_spaces`（初始=PC_BUFFER_CAP=2）与 `pc_items`（初始=0）两个信号量
  实现有界缓冲区；`pcinfo` 展示 `S count/wait:`/`I count/wait:`/`R used/cap:`
  的完整竞争状态。

### 3.4 构建管线

与 lesson-112 完全一致（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse* -Werror`
编译，`ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary`，boot.S `.incbin`，
`grub-mkrescue`）。`check` 目标：`grub-file --is-x86-multiboot2` + grep
`mutex 与 spinlock 竞争`/`l113test`/`Lesson 113`，通过打印
`Multiboot2 and Lesson 113 checks passed.`。`run` 用 `-accel tcg -serial stdio` 的 QEMU。

### 3.5 主控制流

```text
GRUB → kernel_main32 → 长模式 → kernel_main64_binary
  ├─ module_init_model(); init_model_start(); wait_model_start();
  │  adoption_start(); resource_start(); pmm_init(h); vma_init();
  │  reclaim_init(); vfs_init(); address_space_init(...)
  ├─ ... framebuffer_init(h); install_idt(h); pit_init(); pic_init()
  ├─ 横幅 "Lesson 113: mutex 与 spinlock 竞争\nGETTICKS, ... bounded reclaim metadata\n"
  └─ 键盘循环 → exec64
        ├─ l105test → 复验 lesson_105 检查点（更名命令）
        ├─ l113test → 复验 lesson_106 检查点（本课新增，并发措辞）
        ├─ lockatomictest → 演示锁/原子/per-CPU
        └─ pctest → pcgo → pcinfo（生产者-消费者锁竞争演示）
```

---

## 4. 数据流与运行逻辑

```text
输入 "l113test" → exec64 → l113test(c)
  → lesson_106_state = {106,107,108,109, 1,1,1,1}
  → ok = valid(1)&&active(1)&&ready(1)&&accounted(1)&&b(107)==a(106)+1
  → "l113test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed"

锁竞争演示链：
  lockatomictest: raw_spin_lock_irqsave(&deferred_lock,&f)
                → atomic_store_release_u8(softirq_pending, 1)   （锁内发布）
                → raw_spin_unlock_irqrestore(&deferred_lock,f)
                → "lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed"
  pctest → 两个 worker 阻塞在 pc_start_event
  pcgo   → event_set 广播放行 → 生产者/消费者在 pc_spaces/pc_items 上竞争
  pcinfo → "P errors/ok: ... yes"（prod==cons==THREAD_STEPS 且无序号错误）
```

---

## 5. 构建、运行与验证

### 5.1 依赖

`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 113 checks passed.`（README 必须含
`mutex 与 spinlock 竞争` 与 `Lesson 113`，kernel64.c 必须含 `l113test`）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 113: mutex 与 spinlock 竞争\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字抄录）：

```bash
l113test
```

预期：

```text
l113test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed
```

```bash
about
```

预期：`Lesson 113: mutex 与 spinlock 竞争`

```bash
lockatomictest
```

预期：`lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`

```bash
pctest
pcgo
pcinfo
```

预期（`pctest`）：`pctest: producer and consumer blocked on start event; run pcgo`
预期（`pcgo`）：`pcgo: event set; broadcast wake-all issued`
预期（`pcinfo`）：末行 `P errors/ok: ... yes`（`prod==THREAD_STEPS &&
cons==THREAD_STEPS && !used && !errors`）。

继承回归：`l105test`、`softirqtest`、`sleeptest`、`preempttest`、`idletest` 行为不变。

### 5.4 课程实测记录（稳定快照）

旧 README 声明「Commands: `l106test`」——**命令名以源码为准勘误**：本课源码可执行命令是
`l105test`（更名）与 `l113test`（新增）；`l106test` 是模型编号（lesson_106），不是命令。
旧 README 已把检查点描述从「bounded VFS, devices, epoll, and service management」切换为
「bounded concurrency, SMP, RCU, and diagnostics」，与源码 `l113test` 成功串一致。
`make check` 复验输出 `Multiboot2 and Lesson 113 checks passed.`，`l113test` 显示
passed，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l113test` 输出 `Lesson 106 fallback reported` | `lesson_106_state` 未注入或断言不成立 | 核对 `l113test` 赋值 `{106U,107U,108U,109U,1,1,1,1}` 与 `ok` 五条件 |
| `lockatomictest` 输出 `BROKEN` | `deferred_lock` 未释放（`locked!=0`）或 `softirq_pending` 未置 1 | 检查 `raw_spin_unlock_irqrestore` 是否被调用；`atomic_store_release_u8` 目标是否 `this_cpu()->softirq_pending` |
| `pctest` 后 `pcgo` 无广播 | `pc_start_event.signaled` 已为 1 或 `pc_test` 未置 | 按顺序输入 `pctest` → `pcgo`；`pcgo` 会打印 `run pctest first` 或 `already set` |
| `pcinfo` 末行非 `yes` | 生产者/消费者未跑满 `THREAD_STEPS` 或有 `pc_sequence_errors` | 等 worker 完成再 `pcinfo`；检查 `pc_buffer` 容量与 `sem` 计数 |
| `make check` 报错 | README 缺 `mutex 与 spinlock 竞争`/`Lesson 113` 或 kernel64.c 缺 `l113test` | 对照 Makefile 三条 grep |
| `about` 显示旧课号 | 主题串未更新 | grep `Lesson 113: mutex 与 spinlock 竞争` kernel64.c |
| help 列表找不到 `l113test` | 已知小瑕疵：help 串未追加检查点命令 | 用 `about`/README 发现命令 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `raw_spinlock_t{volatile u32 locked}` + `while(exchange(1)){}` | `include/linux/spinlock.h`：`raw_spin_lock` → `arch_spin_lock`；x86 实现用 `lock xchg`/`cmpxchg`（`arch/x86/include/asm/spinlock.h`） | TinyOS 用 `__atomic_exchange_n` 编译出原子交换；Linux 用 `arch_spin_trylock` 等汇编原语；语义一致 |
| `raw_spin_lock_irqsave`（关中断 + 自旋） | `include/linux/spinlock.h` `raw_spin_lock_irqsave` → `local_irq_save` + `arch_spin_lock` | 「先保存 IF 再关中断」完全一致；TinyOS 的 `irq_save64` 就是 `local_irq_save` 的简化 |
| `sem_down`/`sem_up` + `wait_queue` 阻塞 | `include/linux/semaphore.h`：`down_interruptible`/`up`；`kernel/locking/semaphore.c` | 计数语义一致；TinyOS 的 waitq 是固定容量环形数组，Linux 是链表 |
| `event_set` 广播唤醒 | `kernel/sched/wait.c`：`wake_up_all`；`include/linux/wait.h` | TinyOS `waitq_wake_all` 遍历固定数组；Linux 遍历链表 |
| `pc_buffer` 生产者-消费者 | `kernel/locking/` + IPC 消息队列；POSIX 信号量 | TinyOS 用两个 `semaphore` 表达有界缓冲；Linux 有 `semaphore`/`mutex`/`rwlock` 全家桶 |
| `atomic_load_relaxed_u8`/`store_release_u8`/`exchange_acquire_u32` | `include/linux/atomic.h`、`include/asm-generic/atomic.h`：`atomic_read`/`atomic_set`/`atomic_xchg` 及 `smp_load_acquire`/`smp_store_release` | TinyOS 用 C11 `__atomic_*` 内建映射到同名语义；内存序概念与 Linux `smp_*` 一致 |

**权威来源**：Linux v6.x（`include/linux/spinlock.h`、`include/linux/mutex.h`、
`include/linux/semaphore.h`、`include/linux/wait.h`、`kernel/locking/semaphore.c`、
`arch/x86/include/asm/spinlock.h`）；C11 标准 §7.17.7（`__atomic` 内存序）；Intel SDM
（`XCHG`/`LOCK` 前缀）。

**教学模型简化了什么**：单核 + `cli` 关中断（无真实 SMP 内存屏障）；自旋循环无
`pause`/backoff（真实 x86 spinlock 会用 `REP NOP` 降低总线压力）；wait_queue 固定容量
环形数组（Linux 是动态链表）；无 `mutex_lock` 的 owner/debug 字段（真实 mutex 有
`owner`/`wait_lock`）；无 RCU（主题措辞提及但源码未建模真实 RCU）。

---

## 8. 思考题与练习

1. **概念理解**：spinlock 为什么不能用 `if(atomic_exchange(...))` 而必须 `while(...)`？
   如果只有一个线程竞争，`while` 和 `if` 有区别吗？
2. **源码定位**：`raw_spin_lock_irqsave` 的 `*f=irq_save64()` 与
   `raw_spin_unlock_irqrestore` 的 `irq_restore64(f)` 为什么必须成对？如果 unlock 时
   用无条件 `sti` 代替 `irq_restore64`，会破坏什么？
3. **动手实验**：把 `lockatomictest` 里 `atomic_fetch_or_relaxed_u8` 改成
   `atomic_fetch_or`（用 release 序）再 `make run` 输入 `lockatomictest`，观察是否仍
   passed，理解 relaxed 与 release 在无并发时的等价性，然后改回（勿提交）。
4. **Linux 对照**：读 `arch/x86/include/asm/spinlock.h` 的 `arch_spin_lock`，对照本课
   `while(atomic_exchange_acquire_u32(&l->locked,1)){}`，列出 Linux 版本为降低竞争而做的
   优化（如 `REP NOP`、ticket spinlock）。
5. **设计思考**：`sem_down` 用 `sti; hlt` 睡眠，而 `raw_spin_lock_irqsave` 保持 `cli`
   自旋。如果把 spinlock 改成「睡眠等待」，在 IRQ0 调度路径里会出什么问题？

---

## 9. 本课小结与下一课预告

**小结**：本课主题「mutex 与 spinlock 竞争」，是检查点主线转入并发的第一站。源码增量
仍是「更名 + 新模型 + 新命令 + 主题串」，但成功串切换为
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`，语义上标志主线
阶段切换。并发机制全部继承：`raw_spin_lock_irqsave`/`raw_spin_unlock_irqrestore` 用
「关中断 + `atomic_exchange_acquire_u32` 自旋」实现短临界区互斥；`sem_down`/`sem_up` 用
「挂起-唤醒」实现可睡眠的阻塞同步；`pctest`/`pcgo` 用两个信号量演示有界缓冲竞争；
`lockatomictest` 演示 release 发布 + per-CPU 结构。spinlock 忙等 vs. mutex 阻塞的本质
区别是本课核心。

**下一课预告**：[Lesson 114](../lesson-114-stable/README.md) 主题「原子操作与内存序」：
在锁的基础上把 `atomic_load_relaxed_u8`/`atomic_store_release_u8`/`atomic_exchange_
acquire_u32` 逐个展开，精讲 acquire/release/relaxed 三种序的语义与配对规则；
`l114test` 用 `lesson_107_model` 重申 concurrency/SMP/RCU/diagnostics 检查点，
`l106test` 由本课 `l113test` 更名收敛。

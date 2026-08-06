# Lesson 107: epoll wait/wake 集成 — 精讲文档

> **课号**：Lesson 107（主线源课编号 Lesson 100 线）
> **本课主题**：epoll wait/wake 集成——`epoll_wait` 没有就绪事件时如何把调用者挂进实例的 wait 队列睡下；fd 就绪回调如何通过 `wake_up(&ep->wq)` 唤醒它；教学模型里对应的正是继承的 `event_set`/`event_wait`/`waitq_wake_all` 与 `THREAD_BLOCKED_EVENT` 状态
> **课程主线位置**：VFS / 设备 / epoll / 服务管理教学模型阶段（Lesson 81 起的「checkpoint 课」系列）。103–106 讲了"报什么"（就绪位、watch 表、ET/LT），本课把"谁在等、怎么被叫醒"补上——这是 epoll 从"查询"走向"事件驱动"的最后一块拼图，也是阻塞语义与就绪语义在 epoll 里的合流。
> **前置课程**：[`../lesson-106-stable/README.md`](../lesson-106-stable/README.md)（epoll 水平触发：LT 的持续上报语义）
> **后续课程**：[`../lesson-108-stable/README.md`](../lesson-108-stable/README.md)（服务状态机：服务从启动到退出的一整套状态迁移）
> **本课一句话目标**：理解 epoll 的等待侧——`epoll_wait` = "查就绪列表，空了就把自己挂进实例的 wait 队列睡觉，被 `wake_up` 唤醒后重查"，并能把本课源码里 `event_wait`/`event_set`/`waitq_wake_all` 的"睡/叫"闭环映射到 `fs/eventpoll.c` 的 `ep_poll`/`ep_poll_callback` 上。
> **保留的原始快照信息**：This checkpoint models bounded VFS, devices, epoll, and service management metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l99test` + `l107test`（**勘误**：旧 README 标注的 `l100test` 在源码命令表中并不存在——源码 `exec64` 分派的是 `l99test` 与 `l107test`），plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能完整复述 epoll 的等待闭环——"`epoll_wait` → 无事件 → 入队 → 睡觉（`THREAD_BLOCKED_EVENT`）→ fd 就绪回调 → `waitq_wake_all` 广播 → 唤醒重查"，并能在本课源码里逐行指出这个闭环的每一环。
- **在课程主线中的位置**：checkpoint 课（Lesson 81 起）系列中的「epoll wait/wake 集成」检查点。本系列每课源码增量只有几行（上一课测试改名 + 新测试 + about/banner 文案），主题由课程标签承载；本课主题恰好落在**继承的 wait/event 机制**上——`event_wait`/`event_set`/`waitq_wake_all`/`THREAD_BLOCKED_EVENT` 都是既有代码，本课把它们作为"epoll 等待侧"的教学模型精讲，并在 Linux 对照里给出 `ep_poll`/`ep_poll_callback` 真身。
- **前置知识清单**（学本课之前必须掌握）：
  1. wait 队列结构：`struct wait_queue` 的环形表与 `waitq_enqueue`/`waitq_wake_one`/`waitq_wake_all`（Lesson 78 起）；
  2. 事件原语：`event_set`/`event_wait` 与 `struct event` 的 `signaled` 位（Lesson 78 起）；
  3. 线程状态机：`THREAD_BLOCKED_EVENT`/`THREAD_RUNNABLE`/`THREAD_SLEEPING` 与 `irq0_schedule` 的调度（Lesson 38 起）；
  4. epoll 实例、就绪位、ET/LT（Lesson 104–106）。
- **本课交付**：新增固定容量记录 `struct lesson_100_model` + `lesson_100_state` + `l107test`；把 `l106test` 改名为 `l99test`；`about` 与 banner 更新为「Lesson 107: epoll wait/wake 集成」。

---

## 2. 核心概念精讲

### 2.1 wait/wake 集成：epoll 的"等"

**直觉**：LT/ET 回答"事件什么时候报"，还没回答"没人报的时候调用者在干嘛"。答案：**睡在实例的 wait 队列里**。`epoll_wait` 先查就绪列表：有事件直接返回；没有事件就把当前任务挂进 `epoll` 实例的 wait 队列（对应教学模型的 `THREAD_BLOCKED_EVENT`），然后 `hlt` 睡下。等某个 fd 就绪，内核执行它的就绪回调，回调里 `wake_up(&ep->wq)` 把睡觉的任务叫醒，它醒来**重新查就绪列表**，有货就带着事件返回。

```
epoll_wait(epfd, events, N, timeout)
    │
    ├─ 就绪列表非空? ── 是 ──► 取事件，返回
    │
    └─ 否：把自己挂进 ep 的 wait 队列（入队 + THREAD_BLOCKED_EVENT）
         └─ 睡觉: sti; hlt
              ▲
              │ fd 就绪回调: wake_up(&ep->wq)（广播 wake_all）
              │
         └─ 被唤醒 ──► 重新查就绪列表 ──► 返回事件
```

### 2.2 三原语："入队、广播、重查"

教学模型用三个既有函数承担这个闭环，与 Linux 一一对应：
- **入队睡觉**：`event_wait(e)`——对应 `ep_poll` 里 `add_wait_queue(&ep->wq, ...)` + `schedule_timeout()`；
- **广播叫醒**：`event_set(e)` 内部调 `waitq_wake_all(&e->waitq, THREAD_BLOCKED_EVENT)`——对应 `ep_poll_callback` 里 `wake_up(&ep->wq)`；
- **醒来重查**：`event_wait` 的循环体——对应 `ep_poll` 的 `do { ... } while (ep_events_available(ep))` 式重查。

### 2.3 为什么是"wake_all 广播"而不是"wake_one"

Linux 里多个任务可以同时阻塞在同一个 epoll 实例上。fd 就绪回调无法知道"谁最需要这个事件"，所以 `ep_poll_callback` 调用 `wake_up(&ep->wq)`（唤醒**全部**阻塞者），每个人醒来各自重查就绪列表、各自取走自己的事件。教学模型的 `waitq_wake_all` 就是这条"广播"路径；`waitq_wake_one` 则对应管道读写那种"一对一"的唤醒（读端唤醒写端、写端唤醒读端）。

### 2.4 状态机视角：`THREAD_BLOCKED_EVENT`

线程状态枚举里 `THREAD_BLOCKED_EVENT` 就是"我在等一个事件（epoll 事件的抽象）"的状态：

```c
enum thread_state { THREAD_EMPTY, THREAD_RUNNING, THREAD_RUNNABLE, THREAD_SLEEPING,
                    THREAD_BLOCKED_KBD, THREAD_BLOCKED_EVENT, THREAD_BLOCKED_SEM, THREAD_FINISHED };
```

- 进入：`event_wait` 里 `state=THREAD_BLOCKED_EVENT` 后 `sti; hlt`；
- 离开：`waitq_wake_all` 里把匹配该状态的线程改成 `THREAD_RUNNABLE`，交给调度器。

### 2.5 checkpoint 固定元数据

`struct lesson_100_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担系列 checkpoint：`l107test` 断言 `b==a+1`（100+1=101）与四标志。本课号 107 首次对应被校验状态号 100，注意这里的编号巧合：`l107test` 校验 `lesson_100_state`，fallback 串是 `Lesson 100 fallback reported`——而旧 README 恰好把 `l100test` 误写成命令名。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-106） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：wait 队列/事件机制、线程状态机、`exec64` 分派 | **微增量（仅 3 行）**：新增 `struct lesson_100_model` + `lesson_100_state` + `l107test()`；把 `l106test` 改名为 `l99test`；`about`/banner/`exec64` 文案更新（wait/wake 集成机制继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成「epoll wait/wake 集成 / l107test / Lesson 107」关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的 checkpoint 元数据：`l107test`

```c
static TEXT64 void l107test(u16*c){lesson_100_state=(struct lesson_100_model){100U,101U,102U,103U,1,1,1,1};int ok=lesson_100_state.valid&&lesson_100_state.active&&lesson_100_state.ready&&lesson_100_state.accounted&&lesson_100_state.b==lesson_100_state.a+1U;text64(c,"l107test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 100 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_100_state=(struct lesson_100_model){100U,101U,102U,103U,1,1,1,1};`：`a=100,b=101,c=102,d=103`，四标志全 1。
- `int ok=...`：四标志与 `b==a+1U` 五条件与，`101==100+1` 必须成立。
- 成功串 `bounded VFS, devices, epoll, and service management checkpoint passed`、失败串 `Lesson 100 fallback reported` 逐字来自源码。
- **设计说明**：fallback 里的 100 是被校验状态号，**不是**命令名——旧 README 据此误写 `l100test`；本课真实命令是 `l99test` 与 `l107test`。

#### (b) 上一课测试改名为 `l99test`

```c
static TEXT64 void l99test(u16*c){lesson_99_state=(struct lesson_99_model){99U,100U,101U,102U,1,1,1,1};int ok=lesson_99_state.valid&&lesson_99_state.active&&lesson_99_state.ready&&lesson_99_state.accounted&&lesson_99_state.b==lesson_99_state.a+1U;text64(c,"l99test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 99 fallback reported");putc64(c,'\n');}
```

- lesson-106 里它叫 `l106test`；本课改名 `l99test`，测试名与被校验状态号对齐。
- `exec64` 分派把 `l106test` 分支换成 `l99test` 分支，并新增 `l107test` 分支：
```c
else if(eq64(word,"l99test")){if(!noargs64(arg))usage64(c,"l99test");else l99test(c);}
else if(eq64(word,"l107test")){if(!noargs64(arg))usage64(c,"l107test");else l107test(c);}
```

#### (c) 等待侧的"入队 + 睡"：`event_wait`

```c
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(e->signaled){irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}
```

逐行注释（对应 `epoll_wait` 的等待侧）：
- `for(;;)`：醒来后循环重查——对应 `ep_poll` 的"被唤醒后重新检查就绪列表"；
- `if(e->signaled){...return;}`：先查"事件是否已置位"——**就绪列表非空就先返回**，不等（对应 `epoll_wait` 的快速路径）；
- `waitq_enqueue(&e->waitq,id)`：把自己登记进 wait 队列（**对应把当前任务 `add_wait_queue` 进 `ep->wq`**），随后 `state=THREAD_BLOCKED_EVENT`；
- `while(...THREAD_BLOCKED_EVENT) sti; hlt`：置回中断、`hlt` 睡下；IRQ0 调度器会趁机运行别的线程；
- **边界与错误处理**：`id==0`（shell）不参与阻塞（shell 走自己的键盘循环），`waitq_enqueue` 失败（队列满）则不睡直接重试——保证不会死等。

#### (d) 唤醒侧的"广播"：`event_set` 与 `waitq_wake_all`

```c
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
```

逐行注释（对应 `ep_poll_callback` 的就绪回调路径）：
- `irq_save64()`/`irq_restore64()`：关中断保护临界区；
- `e->signaled=1`：置位事件（对应"就绪列表加入新事件"）；
- `e->sets++`：置位计数（`sets` 统计）；
- `waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT)`：**广播唤醒**所有在此 wait 队列里、状态为 `THREAD_BLOCKED_EVENT` 的线程，把它们的状态改成 `THREAD_RUNNABLE`（`wake_all` 统计累加）。

```c
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){u8 id,n=0;while(waitq_dequeue(q,&id))if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}q->wake_all+=n;return n;}
```

- `while(waitq_dequeue(q,&id))`：逐个取出登记者的 id（环形 FIFO）；
- `if(id&&id<THREAD_COUNT&&threads[id].state==state)`：校验 id 合法、且该线程**确实**阻塞在目标状态——不在该状态的跳过，防止误唤醒；
- `threads[id].state=THREAD_RUNNABLE`：状态机迁移，交给 `rr_pick_next` 调度；
- 返回 `n`：实际唤醒个数，由 `event_set` 累加进 `e->wakes`。
- **为什么设计**：这个"只唤醒匹配状态者"的保护是 wait/wake 集成正确性的关键——Linux 的 `wake_up` 同样要检查等待者的等待条件，否则会出现"唤醒错对象"的竞态。

#### (e) wait 队列的登记原语：`waitq_enqueue`

```c
static TEXT64 int waitq_enqueue(volatile struct wait_queue*q,u8 id){if(q->count>=WAIT_QUEUE_CAP)return 0;q->ids[q->head]=id;q->head=(u8)((q->head+1)%WAIT_QUEUE_CAP);q->count++;q->enqueues++;return 1;}
```

- 环形表写入：`head` 处登记、`head` 前进、`count++`；
- 满则返回 0（`event_wait` 据此不睡、重试）；
- `enqueues++` 统计入队次数——与 `waitq_wake_all` 的 `wake_all` 配合可诊断"入队了却没人被唤醒"的问题。

#### (f) `exec64` 增量、`about` 与 banner

`about` 分支逐字来自源码：
```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 107: epoll wait/wake 集成\n");}
```
banner 逐字来自 `kernel_main64_binary`：
```c
text64(&c,"Lesson 107: epoll wait/wake 集成\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'epoll wait/wake 集成' README.md
	@grep -q 'l107test' kernel64.c
	@grep -q 'Lesson 107' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 107 checks passed.'
```

- 与 lesson-106 的唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链与链接脚本均未变化（同前几课）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ 初始化（同前几课；threads[0] 为 shell，workers 由 start_threads 建）
    ├─ banner: "Lesson 107: epoll wait/wake 集成\nGETTICKS, ...\n"
    └─ for(;;) 键盘循环
        ├─ "l107test" ──► lesson_100_state 校验（a=100,b=101）──► "l107test: bounded VFS, ... checkpoint passed"
        ├─ "l99test"  ──► lesson_99_state 校验（上一课回归）
        ├─ "pctest"+"pcgo" ──► event_wait 阻塞（THREAD_BLOCKED_EVENT）→ event_set 广播 → waitq_wake_all 唤醒
        │                 （生产/消费者在 start 事件上汇合——wait/wake 闭环的完整演示）
        └─ "pcinfo"  ──► 查看 event 的 waits/wakes 计数
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打出「Lesson 107: epoll wait/wake 集成」；初始化链不变。
2. **checkpoint**：输入 `l107test` → `lesson_100_state` 新建并校验（`b==a+1`）→ VGA 打印 `l107test: bounded VFS, devices, epoll, and service management checkpoint passed`；`l99test` 回归校验 `lesson_99_state`。
3. **wait/wake 闭环演示**（用继承的 `pctest`/`pcgo` 命令）：
   - `pctest`：`start_threads(3)` → 生产者（线程 1）与消费者（线程 2）都执行 `event_wait(&pc_start_event)` → 各自 `waitq_enqueue` 进 `pc_start_event.waitq`、状态变 `THREAD_BLOCKED_EVENT`、`hlt` 睡下；
   - `pcgo`：`event_set(&pc_start_event)` → `signaled=1` → `waitq_wake_all(&waitq, THREAD_BLOCKED_EVENT)` 把两个 `THREAD_BLOCKED_EVENT` 线程置为 `THREAD_RUNNABLE` → 打印 `pcgo: event set; broadcast wake-all issued`；
   - 调度器 `irq0_schedule` 选中唤醒后的生产者/消费者，`pc_producer`/`pc_consumer` 用信号量（`sem_down`/`sem_up`）完成四步生产消费，最后 `thread_exit`。
4. **统计窗口**：`pcinfo` 打印 `E sig/wait`（`pc_start_event.signaled`/`.waitq.count`）与 `E waits/all`（`.waits`/`.wakes`）——正好是"入队等待次数"与"广播唤醒次数"的对照，即 wait/wake 集成是否闭环的诊断数据。
5. **epoll 映射**：`event_wait` = `epoll_wait` 的阻塞；`event_set` = fd 就绪回调；`waitq_wake_all` = `wake_up(&ep->wq)`。

输出串与源码逐字一致：`l107test: bounded VFS, devices, epoll, and service management checkpoint passed`；`pcgo: event set; broadcast wake-all issued`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致（`gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`，详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

**构建**：

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
   Lesson 107: epoll wait/wake 集成
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l107test`，预期输出：
   ```
   l107test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l107test: Lesson 100 fallback reported`。）
3. 输入 `l99test`（上一课回归），预期输出：
   ```
   l99test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
4. 输入 `pctest`，预期输出：
   ```
   pctest: producer and consumer blocked on start event; run pcgo
   tinyos>
   ```
5. 输入 `pcgo`，预期输出：
   ```
   pcgo: event set; broadcast wake-all issued
   tinyos>
   ```
   随后生产者/消费者被调度执行、四步完成并退出（`ps`/`pcinfo` 可观察）。
6. 输入 `pcinfo`，预期出现：
   ```
   E sig/wait: ...
   E waits/all: ...
   ...
   P errors/ok: 0 yes
   tinyos>
   ```
   （最后一行 `P errors/ok: 0 yes` 表示生产/消费无序列错误、缓冲清空——wait/wake 闭环正确。）
7. 输入 `about`，预期输出：
   ```
   Lesson 107: epoll wait/wake 集成
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 107 checks passed.`；QEMU 中 `l107test`/`l99test` 打印 `...checkpoint passed`，`pcgo` 打印 `...broadcast wake-all issued` 且 `pcinfo` 的 `P errors/ok: 0 yes`，即代表本课 checkpoint 与 wait/wake 集成主题的验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l107test` 打印 fallback 串 | `lesson_100_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=100,b=101`） |
| 输入 `l100test` 提示 unknown command | 旧 README 把 fallback 编号当成命令名；源码没有该命令 | 用 `l99test`/`l107test`；`help` 里的命令表为准 |
| `pcgo` 后生产/消费不推进 | 唤醒没发生：`event_set` 没被调，或 `waitq_wake_all` 状态不匹配 | `pcinfo` 看 `E waits/all`；确认两个 worker 处于 `THREAD_BLOCKED_EVENT`（`ps` 观察 blocked-event） |
| `pcgo` 打印"start event already set" | 之前已 `pcgo` 过，`pc_start_event.signaled==1` | 重新 `pctest`（`pc_reset` 会清 signaled）再 `pcgo` |
| `pcinfo` 的 `P errors/ok` 为 `no` | 生产者/消费者序列错乱或缓冲未清空 | 检查 `pc_sequence_errors` 是否非 0；必要时重启会话重新 `pctest` |
| `waitq_wake_all` 唤醒了错误的线程 | 状态参数写错（如把 `THREAD_BLOCKED_EVENT` 写成 `THREAD_BLOCKED_KBD`） | 核对调用处的第三个参数；用 `ps` 观察各线程状态 |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'epoll wait/wake 集成' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l107test` 符号 | `grep -q 'l107test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：`epoll_wait` 的阻塞路径 = `ep_poll`**
- TinyOS：`event_wait` 查 `signaled`、入队（`waitq_enqueue`）、置 `THREAD_BLOCKED_EVENT`、`sti; hlt` 睡下。
- Linux：`epoll_wait()` → `do_epoll_wait()` → `ep_poll()`（`fs/eventpoll.c`）：先 `ep_events_available()` 查 `rdllist`；空则 `add_wait_queue_exclusive(&ep->wq, &wait)` 把自己挂进实例 wait 队列，用 `schedule_hrtimeout_range`/`schedule_timeout` 睡眠（可被超时打断）。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_poll`、`do_epoll_wait`、`add_wait_queue_exclusive`）。
- 教学简化：无超时参数、无 `EXCLUSIVE` 唤醒优化（教学模型用 `wake_all` 简单广播）；`hlt` 是忙等式的停驻，靠 IRQ0 重新调度。

**对照点 2：就绪回调的唤醒 = `ep_poll_callback`**
- TinyOS：`event_set` 置 `signaled` 并 `waitq_wake_all(&waitq, THREAD_BLOCKED_EVENT)`。
- Linux：fd 就绪时调用 `ep_poll_callback()`（通过 `ep_item_poll` 建立的回调链）：把就绪的 `epitem` 挂进 `rdllist`（非 ET 或首个 ET），再 `wake_up(&ep->wq)` 唤醒 `ep_poll` 里睡觉的等待者。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_poll_callback`、`ep_item_poll`、`ep_pm_stay_awake`）。
- 教学简化：`signaled` 是"事件已置位"的单个标志（Linux 是就绪列表 + 回调参数），但"置位 → 广播唤醒 → 醒来重查"的骨架完全一致。

**对照点 3：广播 vs 独占唤醒**
- TinyOS：`waitq_wake_all` 唤醒全部 `THREAD_BLOCKED_EVENT` 者。
- Linux：`add_wait_queue_exclusive` 标记"独占等待"，`wake_up` 通常只唤醒第一个独占等待者（减少惊群）；epoll 的 `ep_poll` 有特殊处理避免等待者全部被唤醒空转。教学模型没有惊群问题，采用简单广播。
- 权威来源：Linux v6.x `kernel/sched/wait.c`（`__wake_up_common`、`WQ_FLAG_EXCLUSIVE`）；`fs/eventpoll.c`。
- 教学简化：单核、固定 3 线程，无惊群优化。

**对照点 4：等待条件的校验**
- TinyOS：`waitq_wake_all` 逐个检查 `threads[id].state==state` 才唤醒，避免误唤醒。
- Linux：`wake_up` 后每个唤醒任务都要在 `ep_poll` 里重新 `ep_events_available()` 检查——**唤醒 ≠ 条件满足**，必须重查（"wake 只是敲门，门里有没有货要自己看"）。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_events_available`）。
- 教学简化：教学模型在唤醒路径上校验状态、在等待循环里重查 `signaled`，两处都保留了"重查条件"的纪律。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `event_wait` 醒来后要 `for(;;)` 循环重查 `signaled` 而不是直接返回？这与 Linux `ep_poll` 醒来后调用 `ep_events_available` 重查是否同一道理？
2. **源码定位**：在本课 `kernel64.c` 中找出 wait/wake 闭环的五个环节（查就绪、入队、睡觉、广播、唤醒改状态）各自所在的函数与关键行。
3. **动手实验**：在 `event_set` 里临时把 `waitq_wake_all` 改成 `waitq_wake_one`，重新 `make run` 并跑 `pctest`+`pcgo`，观察只唤醒一个 worker 时生产/消费是否卡死（提示：另一个 worker 永远 `THREAD_BLOCKED_EVENT`）。改完请**恢复原值**。
4. **动手实验**：把 `waitq_wake_all` 里 `threads[id].state==state` 条件改成 `threads[id].state==THREAD_BLOCKED_KBD`，跑 `pctest`+`pcgo`，观察 `pcinfo` 的 `E wakes` 变为 0、两个 worker 永不被唤醒。改完请**恢复原值**。
5. **Linux 对照**：阅读 `fs/eventpoll.c` 的 `ep_poll_callback`，说明 `wake_up(&ep->wq)` 与教学模型 `waitq_wake_all(&e->waitq, THREAD_BLOCKED_EVENT)` 在"唤醒粒度"上的差别；`WQ_FLAG_EXCLUSIVE` 解决的是什么问题？

---

## 9. 本课小结与下一课预告

- 本课把 epoll 的等待侧讲透了：`epoll_wait` 空列表时"入队 + 睡觉 + 被唤醒重查"，闭环的每一环都能在本课源码里逐行指出。
- 你精读了 `event_wait`（入队 + `THREAD_BLOCKED_EVENT` + `hlt`）、`event_set`（置位 + 广播）与 `waitq_wake_all`（状态匹配才唤醒），理解了"唤醒只是敲门、醒来必须重查条件"的纪律。
- 你用 `pctest`/`pcgo` 亲手跑通了 wait/wake 闭环，并用 `pcinfo` 的 `waits/wakes` 与 `P errors/ok` 验证正确性。
- 你在 Linux 对照里见到了真身：`ep_poll` 的 `add_wait_queue_exclusive` + `ep_events_available`，`ep_poll_callback` 的 `wake_up(&ep->wq)`，以及独占唤醒对惊群问题的处理。
- 你完成了 checkpoint 验证：`l107test` 与回归 `l99test` 打印 `bounded VFS, devices, epoll, and service management checkpoint passed`，并把旧 README 的 `l100test` 命令勘误为源码真实的 `l99test`/`l107test`。

**下一课预告**：Lesson 108「服务状态机」。epoll 系列（103–107）到此收束，下一课把视角从"文件描述符的事件"转向"服务进程的一生"：服务从 stopped → starting → running → stopping → exited 的整条状态迁移，怎样用确定性状态机表达，以及如何与 wait/reap、会话（session_job）模型联动。见 [`../lesson-108-stable/README.md`](../lesson-108-stable/README.md)。

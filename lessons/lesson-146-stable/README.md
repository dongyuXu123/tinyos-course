# Lesson 146: socket poll/epoll 集成 — 精讲文档

> **课号**：Lesson 146 ｜ **主题**：socket poll/epoll 集成（socket poll/epoll integration）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课处于「网络三连」的收尾：上一课（145）讲连接状态机，本课把事件机制接到 socket 的 `poll`/`epoll` 集成，下一课（147）转向网络错误与超时
> **前置课程**：[`../lesson-145-stable/README.md`](../lesson-145-stable/README.md)（连接状态机）
> **后续课程**：[`../lesson-147-stable/README.md`](../lesson-147-stable/README.md)（网络错误与超时）
> **一句话目标**：讲清「为什么高并发服务器需要 poll/epoll 而不是逐个轮询」——`poll` 用一次调用监控多个 fd 的就绪位，`epoll` 用内核事件表 + 就绪链表把「扫描」变成「回调」，对照 Linux `fs/select.c`/`fs/eventpoll.c`/`include/linux/poll.h`，并把教学内核继承的 `pipe_poll` 就绪位与 `wait_queue` 唤醒机制按这一主题系统化复述，运行 `l146test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（socket poll/epoll 集成对象）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l145test` 恢复为历史命名 `l138test`（挂 `lesson_138_state`），新增 `lesson_139_model`/`lesson_139_state` 与 `l146test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l146test`（旧 README 所写 `l139test` 按源码勘误，源码中不存在 `l139test` 命令）；另保留历史检查点 `l100test`–`l138test`，以及 `polltest`/`pipeinfo`/`pipetest`/`pctest`/`pcgo`/`waitblocktest` 等与就绪位、事件、唤醒相关的回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「就绪位掩码」的直觉解释 `poll`（一次调用监控多个 fd）、`epoll`（内核事件表 + 就绪链表、水平/边缘触发）与阻塞 `select` 的关系；说出 Linux 中 `poll` 由 `fs/select.c` 的 `do_poll()` 驱动、`epoll` 由 `fs/eventpoll.c` 的 `eventpoll`/`epitem` 驱动、就绪位常量来自 `include/linux/poll.h`（`POLLIN/POLLOUT/POLLERR`）；在教学内核中沿 `pipe_poll` → `waitq_wake_one/wake_all` → `event_set/event_wait` → `polltest` 观察就绪位与唤醒机制；运行 `make check`/`make run` 验证本课稳定快照。

**在课程主线中的位置**：Lesson 140–147 构成「网络协议收敛带」——本课是网络三连（145 连接状态机 → **146 poll/epoll 集成** → 147 错误与超时）的第二课。上一课建立了「连接状态」概念，本课回答「服务器怎么同时盯着几万条连接」：事件驱动。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中与「就绪/事件/唤醒」相关的设施（`pipe_poll`、`wait_queue`、`event`、`semaphore`、`pipe_try_read/pipe_try_write`）按 poll/epoll 主题系统化复述。下一课（Lesson 147）转向网络错误与超时。

**前置知识清单**（学本课前必须掌握）：
1. 连接状态机概念：状态 × 事件二元组、`include/net/tcp_states.h`（Lesson 145）。
2. 就绪位：`pipe_poll(mask)` 的 `POLL_IN=1U`/`POLL_OUT=2U`、管道水位 `used`/`PIPE_CAP`（Lesson 43s/76s）。
3. 等待队列：`struct wait_queue`、`waitq_enqueue`/`waitq_wake_one`/`waitq_wake_all`、`THREAD_BLOCKED_*` 状态（Lesson 30s/70s）。
4. 事件与信号量：`event_set/event_wait`（`signaled` 位 + 广播唤醒）、`sem_down/sem_up`（`count` + 唤醒一个）（Lesson 70s/73s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–145）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 146: socket poll/epoll 集成`；
- 新命令 `l146test` 输出 `l146test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `polltest`/`pipeinfo`/`pctest`/`pcgo` 继续展示就绪位与事件唤醒元数据。

---

## 2. 核心概念精讲

### 2.1 poll/epoll：从「逐个问」到「等通知」

**直觉**：一个聊天服务器要同时看几万条 socket。如果每条连接一个线程去 `read()` 阻塞等待，线程会泛滥；如果每轮把所有 fd 问一遍「你有没有数据？」，就是 `poll`。`epoll` 更进一步：把关心的 fd 登记进内核的**事件表**，数据到来时内核主动把 fd 放进**就绪链表**，应用只需要取「谁就绪了」——从「轮询」变成「事件驱动」。

**准确定义**：
- **`poll`**：`poll(struct pollfd *fds, nfds_t nfds, int timeout)`，一次调用传入 `n` 个 `{fd, events}`，内核为每个 fd 查询当前就绪位（`revents`），返回就绪个数。复杂度 O(每次调用遍历全部 fd)。
- **`epoll`**（Linux 2.6+）：`epoll_create()` 建事件表 → `epoll_ctl(ADD/MOD/DEL)` 注册/修改/删除关注项（`epitem`）→ `epoll_wait()` 取就绪链表。就绪 fd 由回调（`ep_poll_callback`）放入就绪队列，复杂度与「就绪个数」成正比，与「总 fd 数」无关。

### 2.2 为什么需要 poll/epoll（动机）

1. **避免阻塞放大**：阻塞式 I/O 每 fd 一线程，连接数上万就崩溃；事件驱动用少量线程 + 内核就绪通知。
2. **避免空轮询**：`select`/`poll` 每次把 fd 集合整体传入内核，O(n) 扫描 + 用户/内核两次拷贝；`epoll` 把 fd 集合留在内核，事件回调直达就绪链表。
3. **触发模式**：水平触发（LT，默认）「就绪就一直报」，边缘触发（ET）「只在状态变化时报一次」——决定了应用要读到 EAGAIN 才算完，这是 epoll 高性能写法的基础。

### 2.3 Linux 中 poll/epoll 的工作机制

- **poll 实现**：`fs/select.c` 的 `do_poll()` 调用每个 fd 的 `f_op->poll()`（文件操作表回调，如 `net/socket.c` 的 `sock_poll()`），得到 `POLL*` 掩码；没有就绪且 `timeout>0` 时把当前任务挂到各等待队列（`poll_wait()`）再调度。
- **epoll 实现**：`fs/eventpoll.c` 定义 `struct eventpoll`（`rbr` 红黑树索引所有 `epitem`、`rdllist` 就绪链表、`wq` 等待队列）；`epoll_ctl` 向目标 fd 的等待队列注册 `ep_poll_callback`；fd 就绪时回调把 `epitem` 挂入 `rdllist` 并唤醒 `epoll_wait` 等待者。
- **就绪位常量**：`include/linux/poll.h` 的 `POLLIN=0x0001`、`POLLOUT=0x0004`、`POLLERR=0x0008`、`POLLHUP=0x0010`、`POLLNVAL=0x0020`。
- **教学简化**：教学内核没有文件操作表/红黑树/回调，但「就绪位掩码（`pipe_poll`）+ 唤醒（`waitq_wake_one/wake_all`）+ 事件（`event`）」三件套完整存在——`pipe_poll` 对应 `sock_poll` 的就绪查询，`event_set` 对应 `ep_poll_callback` 的「置位 + 唤醒」。

### 2.4 教学内核中与「poll/epoll 集成」有关的既有设施

本课主题机制（socket poll/epoll 集成）**未在源码中实现**，但「就绪位 + 唤醒」这个主题素材在内核里完整存在：

| 素材 | 源码 | poll/epoll 含义 |
|---|---|---|
| `pipe_poll(mask)` | 返回 `POLL_IN/POLL_OUT` 就绪位，统计 `poll_registrations` | `sock_poll()` 的就绪查询教学原型（对照 `include/linux/poll.h`） |
| `polltest` | `pipe_init` 后按「空→写→读→满→读」走就绪位迁移 | `poll()` 就绪位语义的可执行断言 |
| `waitq_wake_one/wake_all` | 唤醒 `THREAD_BLOCKED_*` 任务并置 `THREAD_RUNNABLE` | fd 就绪时的「唤醒等待者」机制（`ep_poll_callback` 的简化） |
| `event_set/event_wait` | `signaled` 位 + `waitq_wake_all` 广播唤醒 | epoll 事件回调「置就绪 + 唤醒 waiter」的教学对应 |
| `sem_down/sem_up` | `count` 减一/加一 + 唤醒一个阻塞者 | 生产者/消费者就绪槽位的互斥/同步（PC 模型） |
| `pipe_try_read/pipe_try_write` | 空/满时返回 0 并递增 `blocked_*` 计数 | 阻塞与就绪的分界：不可读/不可写即「未就绪」 |

### 2.5 检查点模型：lesson_139_model 与 l146test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `139→142` 标记 Origin 为 Lesson 139（`a=139,b=140,c=141,d=142`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「事件计数连续性」。本课同时把上一课新增的 `l145test` 恢复为历史命名 `l138test`（挂 `lesson_138_state`，计数 `138→141`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理（lesson-145 曾用 `l145test` 名字挂 138 号模型，本课将其名实重新对齐）。

### 2.6 机制继承 + 检查点增量

本课主题机制（socket poll/epoll 集成）**不是本课新写的代码**：管道就绪位、等待队列、事件、信号量全部来自早期课程（Lesson 30s–70s）。本课实际增量只有三处：`l145test`→`l138test` 更名、`lesson_139_model`+`l146test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「就绪位 + 唤醒」主题重新组织，并如实说明：**poll/epoll 结构（`struct eventpoll`/`struct epitem`）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l145test`→`l138test` 恢复命名；新增 `lesson_139_model`/`lesson_139_state`/`l146test`；`about` 与开机横幅更新。poll/epoll 主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化（md5 与上一课一致） |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`socket poll/epoll 集成`/`l146test`/`Lesson 146`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（就绪位/唤醒机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
static TEXT64 void l146test(u16*c){lesson_139_state=(struct lesson_139_model){139U,140U,141U,142U,1,1,1,1};int ok=lesson_139_state.valid&&lesson_139_state.active&&lesson_139_state.ready&&lesson_139_state.accounted&&lesson_139_state.b==lesson_139_state.a+1U;text64(c,"l146test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 139 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `139→142`（Origin Lesson 139），四布尔位全置 1，`b==a+1U` 校验计数连续——「就绪事件按顺序推进」的元数据隐喻。
2. **逻辑分析（≥3 行）**：赋值语句把整个结构体字面量写入 `lesson_139_state`，随后 `ok` 由五个条件合取而成：`valid/active/ready/accounted` 四个布尔位 + `b==a+1U` 连续性。由于字面量全为 1 且 `140==139+1`，`ok` 恒为真，输出必为成功串；fallback 分支（`Lesson 139 fallback reported`）在「计数被破坏或模型被错误初始化」时才可能命中，属于防御性兜底。
3. **输出串（逐字抄录）**：成功 `l146test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 139 fallback reported`。
4. **恢复的 `l138test`**：本课同时把上一课的 `l145test` 更名回 `l138test`（同为 `lesson_138_state`，计数 `138→141`），使检查点命令名与 Origin 对齐；`l100test`–`l137test` 历史检查点全部保留。

#### 3.2.2 就绪位查询：pipe_poll（sock_poll 的教学原型）

```c
#define POLL_IN 1U
#define POLL_OUT 2U
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

1. **就绪位语义**：调用方用 `mask` 声明「我关心可读还是可写」，函数按管道当前水位（`used` 与 `PIPE_CAP` 比较）计算实际就绪位——这是 `poll` 系统调用「查询就绪」的最小模型。
2. **边界（≥3 行）**：`used>0` 才可读、`used<PIPE_CAP` 才可写，满/空边界天然闭合；每次调用递增 `poll_registrations` 统计注册次数（对照 `epoll_ctl` 的注册概念）；返回掩码可直接与 `mask` 做 `&` 判定「是否命中关注的事件」。
3. **观察命令**：`polltest` 输出 `polltest: POLLIN/POLLOUT readiness transitions passed`；`pipeinfo` 打印 `pipe used/capacity`、`blocked r/w`、`wake r/w`。

```c
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,e=pipe_model.used==1;while(pipe_model.used<PIPE_CAP)pipe_try_write(8);int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),h=pipe_poll(POLL_OUT)==POLL_OUT;text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
```

1. **迁移序列（≥3 行）**：空管道 `POLL_IN` 不就绪（`a`）、`POLL_OUT` 就绪（`b`）；写入一字节后 `POLL_IN` 就绪（`d`）且 `used==1`（`e`）；灌满后 `POLL_OUT` 不就绪（`f`）——就绪位随水位双向翻转，正是 poll 的「水平触发」行为；读空后 `POLL_OUT` 恢复就绪（`h`）。
2. **对照 epoll**：`pipe_try_write`/`pipe_try_read` 每次都返回「是否成功」，相当于 fd 状态变化的驱动事件；`pipe_poll` 在任何时刻查询的都是一致的就绪位快照。

#### 3.2.3 唤醒机制：waitq_wake_one / waitq_wake_all（ep_poll_callback 的简化）

```c
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){u8 id;if(!waitq_dequeue(q,&id))return 0;if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0;threads[id].state=THREAD_RUNNABLE;q->wake_one++;*out=id;return 1;}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){u8 id,n=0;while(waitq_dequeue(q,&id))if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}q->wake_all+=n;return n;}
```

1. **唤醒一个 vs 唤醒全部**：`wake_one` 从 FIFO 队头取一个阻塞者置 `THREAD_RUNNABLE`（适合「一个就绪槽被一个消费者取走」）；`wake_all` 遍历队列唤醒所有匹配者（适合「广播事件」）——对应 Linux `wake_up`/`wake_up_all` 与 epoll 的 `ep_poll_callback` 唤醒一个 `epoll_wait` 等待者。
2. **状态守卫（≥3 行）**：两者都校验 `id` 合法（非 0、在 `THREAD_COUNT` 内）且当前状态与传入 `state` 匹配，才允许 `THREAD_RUNNABLE` 迁移——避免唤醒已退出/已就绪的线程，是「事件必须与等待状态匹配」的教学体现。
3. **统计**：`wake_one`/`wake_all` 计数器分别记录单播/广播唤醒次数，`kbdinfo`/`threadinfo` 可观察。

#### 3.2.4 事件对象：event_set / event_wait（epoll 事件回调的教学对应）

```c
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(e->signaled){irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}
```

1. **置位 + 广播**：`event_set` 在关中断下置 `signaled=1` 并 `waitq_wake_all` 唤醒所有 `THREAD_BLOCKED_EVENT` 等待者——与 `ep_poll_callback`「把就绪事件写入内核就绪链表并唤醒 `epoll_wait`」同构。
2. **等待循环（≥3 行）**：`event_wait` 先查 `signaled`（已就绪直接返回，避免错过唤醒）；未就绪则把自己入队并置 `THREAD_BLOCKED_EVENT`，然后 `sti; hlt` 自旋等待被唤醒；被唤醒后回到循环顶部，重新检查 `signaled`——丢失唤醒（lost wakeup）被「先检查后睡眠」的两步法挡住。
3. **观察**：`pctest` + `pcgo` 用事件做生产/消费起始闸门：`pctest` 输出 `pctest: producer and consumer blocked on start event; run pcgo`，`pcgo` 输出 `pcgo: event set; broadcast wake-all issued`。

#### 3.2.5 生产者/消费者槽位：sem_down / sem_up（就绪槽的计数信号量）

```c
static TEXT64 void sem_down(struct semaphore*s){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(s->count){s->count--;s->downs++;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&s->waitq,id)){s->blocks++;threads[id].state=THREAD_BLOCKED_SEM;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_SEM)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_up(struct semaphore*s){u64 flags=irq_save64();u8 id;if(s->count<s->max){s->count++;s->ups++;}else s->overflows++;if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id))s->wakes++;irq_restore64(flags);}
```

1. **计数槽位（≥3 行）**：`sem_down` 在 `count>0` 时减一成功返回（占用一个就绪槽），否则入队阻塞；`sem_up` 在 `count<max` 时加一并唤醒**一个**等待者（释放一个槽位），超过上限则 `overflows++` 计数溢出——这就是「有限容量生产者/消费者缓冲区」的同步内核。
2. **对照 epoll**：就绪链表容量、事件槽位分配都可用信号量建模；`pc_spaces`（初值 `PC_BUFFER_CAP`）与 `pc_items`（初值 0）分别表示「空槽数」与「满槽数」，正是「空闲容量 / 就绪事件」双信号量。
3. **观察**：`pcinfo` 打印 `S count/wait`、`I count/wait`、`R used/cap`、`P prod/cons`；`pctest` 全跑完后 `P errors/ok` 应显示 `ok=yes`。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 146: socket poll/epoll 集成\n`；检查点分支：
```c
else if(eq64(word,"l138test")){if(!noargs64(arg))usage64(c,"l138test");else l138test(c);}else if(eq64(word,"l146test")){if(!noargs64(arg))usage64(c,"l146test");else l146test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 146: socket poll/epoll 集成\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` 编译 `kernel64.c` → `ld -T kernel64.ld` → `objcopy` 出 raw bin → `boot.S` 以 `.incbin` 嵌入 → `grub-mkrescue` 出 ISO。`make check` 断言 README 含 `socket poll/epoll 集成`、`Lesson 146`，kernel64.c 含 `l146test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init / vma_init / reclaim_init / vfs_init / address_space_init
 ├─ 横幅 "Lesson 146: socket poll/epoll 集成"
 └─ 主循环：命令 → exec64
     ├─ l146test / l138test → 阶段检查点（lesson_139_state / lesson_138_state）
     ├─ polltest / pipeinfo → 就绪位查询（POLL_IN/POLL_OUT）
     ├─ pctest / pcgo / pcinfo → 事件 + 信号量 + 就绪槽（PC 模型）
     ├─ pipetest → 管道阻塞/唤醒元数据
     ├─ waitblocktest → 等待队列唤醒机制
     └─ about → "Lesson 146: socket poll/epoll 集成"
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 完成 PMM/VM/VFS 初始化，打印横幅 `Lesson 146: socket poll/epoll 集成`。
2. **`l146test`** → `l146test(c)` → 初始化 `lesson_139_state` → 五条件断言 → `l146test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`l138test`**（恢复名） → `l138test(c)` → `lesson_138_state` 断言 → `l138test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`polltest`** → `pipe_init` → `pipe_poll`/`pipe_try_write`/`pipe_try_read` 就绪位迁移 → `polltest: POLLIN/POLLOUT readiness transitions passed`。
5. **`pctest`** → `start_threads(3)` → 生产者/消费者阻塞在 `pc_start_event` → `pctest: producer and consumer blocked on start event; run pcgo`。
6. **`pcgo`** → `event_set(&pc_start_event)` → 广播唤醒 → `pcgo: event set; broadcast wake-all issued`。
7. **`about`** → `Lesson 146: socket poll/epoll 集成`。

---

## 5. 构建、运行与验证

**依赖**：同前几课（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 146 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 146: socket poll/epoll 集成` 横幅 |
| `l146test` | `l146test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l138test` | `l138test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `polltest` | `polltest: POLLIN/POLLOUT readiness transitions passed` |
| `pctest` | `pctest: producer and consumer blocked on start event; run pcgo` |
| `pcgo` | `pcgo: event set; broadcast wake-all issued` |
| `about` | `Lesson 146: socket poll/epoll 集成` |

判定成功：`l146test` 输出 passed、无 fallback，`polltest` 输出 passed、无 `BROKEN`，`pctest`/`pcgo` 序列无异常，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l146test` 输出 `Lesson 139 fallback reported` | `lesson_139_state` 初始化/断言失败（stale 镜像） | `grep -n "l146test" kernel64.c`；确认初始化串 `{139U,140U,141U,142U,1,1,1,1}` 与 `b==a+1U` |
| `polltest` 输出 `BROKEN` | 管道就绪位与水位不一致 | 检查 `pipe_poll` 的 `used`/`PIPE_CAP` 比较；`pipe_try_write` 满时返回 0、`pipe_try_read` 空时返回 0 |
| `pcgo` 提示 `pcgo: run pctest first` | 未先执行 `pctest` | 先 `pctest` 再 `pcgo`；`pc_test` 标志由 `start_threads(3)` 置位 |
| `pcinfo` 的 `P errors/ok` 显示 no | 生产者/消费者序列错乱或仍有残留 | 检查 `pc_sequence_errors`、`pc_used`、`pc_spaces.count`/`pc_items.count` 是否归位 |
| `event_wait` 后线程卡死 | 丢失唤醒（等待者入队前事件已 set） | 确认 `event_wait` 先查 `signaled` 再入队；`event_set` 用 `irq_save64/restore64` 保护置位与唤醒 |
| `l146test` 与 `l138test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l146test` 只操作 `lesson_139_state`、`l138test` 只操作 `lesson_138_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 146' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `socket poll/epoll 集成` 与 `Lesson 146` |
| 输入 `l146test` 提示 `unknown command` | exec64 分支未命中（命令表陈旧） | `grep -o 'l146test' kernel64.c` 应同时命中函数定义与 `eq64` 分支 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `pipe_poll(mask)` 返回 `POLL_IN/POLL_OUT` | `net/socket.c` 的 `sock_poll()`；`include/linux/poll.h`（`POLLIN=0x0001` 等） | 模型只有管道水位两条就绪位，无 `POLLERR/POLLHUP/POLLNVAL` 与 `sk_buff` 队列 |
| `polltest` 的就绪位迁移 | `fs/select.c` 的 `do_poll()` + `poll_schedule_timeout()` | 模型同步调用、无超时参数，只断言就绪位翻转 |
| `waitq_wake_one/wake_all` | `kernel/sched/wait.c` 的 `__wake_up`/`wake_up_all` | 模型是定长 FIFO（`WAIT_QUEUE_CAP`），无 `wait_queue_entry` 链表 |
| `event_set/event_wait` | `fs/eventpoll.c` 的 `ep_poll_callback()` + `epoll_wait` | 模型无 `struct eventpoll`/`epitem`/红黑树，只保留「置位 + 广播唤醒」 |
| `sem_down/sem_up`（PC 槽位） | `kernel/sched/semaphore.c`；`fs/eventpoll.c` 的就绪链表计数 | 模型是单核 `count` 计数，无 `wait_list` 与 CAS |
| `l146test` 断言 | 无直接对应（LTP `poll`/`epoll` 测试） | 模型把 poll/epoll 主题的可验证状态固化进内核 |

**权威来源**：Linux `fs/select.c`、`fs/eventpoll.c`、`include/linux/poll.h`、`net/socket.c` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有** `struct pollfd`/`struct eventpoll`/`ep_poll_callback` 的等价实现——socket poll/epoll 集成是「主题宣告」，教学内核停留在「用管道就绪位 + 等待队列唤醒 + 事件对象承载就绪/事件概念」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `epoll` 在连接数大时比 `select`/`poll` 快？请用「每次调用 O(n) 扫描」vs「就绪链表 + 回调」说明复杂度差异。
2. **源码定位**：在 `kernel64.c` 中找出所有「置位后唤醒等待者」的函数（提示：`event_set`、`pipe_try_write`/`pipe_try_read`、`sem_up`），说明每处的唤醒目标。
3. **动手实验**：给 `pipe_poll` 增加 `POLL_ERR` 位（当管道 `writes>reads` 溢出标记时置位），在 `polltest` 里验证并重新构建运行。
4. **动手实验**：把 `event_set` 里的 `waitq_wake_all` 改成 `waitq_wake_one`，运行 `pctest`/`pcgo` 观察第二个消费者是否被漏唤醒，随后恢复并解释原因。
5. **Linux 对照**：阅读 `fs/eventpoll.c` 的 `ep_poll_callback`，说明它为什么要在持有 `ep->lock` 时把 `epitem` 挂入 `rdllist`；对照教学模型 `event_set` 的关中断保护。

---

## 9. 本课小结与下一课预告

1. `poll` 一次监控多个 fd 的就绪位（O(n) 扫描），`epoll` 用内核事件表 + 就绪链表 + 回调做到 O(就绪数)（`fs/select.c`、`fs/eventpoll.c`、`include/linux/poll.h`）。
2. 就绪位常量（`POLLIN/POLLOUT/POLLERR`）与水平/边缘触发是 poll/epoll 的核心语义。
3. 教学内核没有 poll/epoll 对象，但就绪/唤醒素材完整：`pipe_poll` 就绪位、`waitq_wake_one/wake_all` 唤醒、`event_set/event_wait` 事件、`sem_down/sem_up` 槽位。
4. 检查点增量：`l145test`→`l138test` 更名、新增 `lesson_139_model`+`l146test`、横幅与 `about` 更新为 `Lesson 146: socket poll/epoll 集成`。
5. `pipe_poll` 的「空/满边界即未就绪」语义为「超时 = 一直未就绪」铺路。
6. 下一课（Lesson 147）主题为 **网络错误与超时**（对照 `net/ipv4/tcp.c` 的 RTO/`tcp_time_wait`、`include/uapi/asm-generic/errno.h`）：从「就绪/未就绪」两态转到「错误码 + 超时定时器」，教学内核将以 `pmm_error` 错误串、`timer_model` 定时器设施承接该主题。

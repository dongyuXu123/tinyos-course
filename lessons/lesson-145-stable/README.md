# Lesson 145: 连接状态机 — 精讲文档

> **课号**：Lesson 145 ｜ **主题**：连接状态机（connection state machine）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课处于「网络三连」的中间：上一课（144）讲 socket 端口分配，本课讲连接状态机，下一课（146）讲 socket poll/epoll 集成，之后转入 namespace 三连（148–150）
> **前置课程**：[`../lesson-144-stable/README.md`](../lesson-144-stable/README.md)（socket 端口分配）
> **后续课程**：[`../lesson-146-stable/README.md`](../lesson-146-stable/README.md)（socket poll/epoll 集成）
> **一句话目标**：讲清「连接状态机」为什么是网络协议的核心抽象（一个 socket 从出生到关闭要经过哪些状态、由什么事件驱动迁移），对照 Linux `net/ipv4/tcp.c` 的 TCP 状态集合，并把教学内核继承的 `wait_model`/`task_transition`/`pipe_poll` 等既有状态设施按这一主题系统化复述，运行 `l145test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（连接状态机对象）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l144test` 恢复为历史命名 `l137test`（挂 `lesson_137_state`），新增 `lesson_138_model`/`lesson_138_state` 与 `l145test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l145test`（旧 README 所写 `l138test` 按源码勘误，源码中不存在 `l138test` 命令）；另保留历史检查点 `l100test`–`l137test`，以及 `waittest`/`waitblocktest`/`taskvalidate`/`pipe_poll`/`polltest`/`timertest` 等与状态迁移相关的回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「状态 + 事件」的二元组描述任何连接的生命周期（`LISTEN → ESTABLISHED → CLOSE_WAIT → …`）；说出 Linux 里 TCP 状态机以 `tcp_set_state()` + `include/net/tcp_states.h` 状态枚举实现，状态迁移由 `tcp_*` 事件函数触发；在教学内核中沿 `wait_model`（`WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD`）→ `task_transition`（`TASK_*`）→ `pipe_poll`（`POLL_IN/POLL_OUT`）→ `l145test` 观察「状态机」的教学形态；运行 `make check`/`make run` 验证本课稳定快照。

**在课程主线中的位置**：Lesson 140–147 构成「网络协议收敛带」——140/141 讲收发队列与 loopback 接口、142 讲 IPv4 地址元数据、143/144 讲 UDP socket 状态与端口分配，本课把「状态」泛化为一等概念（**连接状态机**），146 接续到 poll/epoll 事件集成，147 处理网络错误与超时。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中所有「状态迁移」相关的设施（`wait_model`、`task_transition`、`pipe_poll`、`timer_model`、`thread_state`）按「连接状态机」主题系统化复述。下一课（Lesson 146）转向 socket poll/epoll 集成。

**前置知识清单**（学本课前必须掌握）：
1. socket 端口分配与 UDP socket 状态：`lesson_137_model`/`lesson_138_model` 检查点范式（四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言）（Lesson 143/144）。
2. 等待队列与唤醒：`struct wait_queue`、`waitq_enqueue`/`waitq_wake_one`/`waitq_wake_all`、`event`/`semaphore`（Lesson 30s/70s）。
3. 任务状态机：`enum task_state`（`TASK_RUNNING`/`TASK_INTERRUPTIBLE`/…）、`task_transition`、`task_table_validate`（Lesson 37/66s）。
4. 管道与就绪位：`struct pipe_model`、`pipe_try_read`/`pipe_try_write`/`pipe_poll`、`POLL_IN`/`POLL_OUT`（Lesson 43s/76s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–144）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 145: 连接状态机`；
- 新命令 `l145test` 输出 `l145test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `waittest`/`polltest`/`taskvalidate`/`timertest` 继续展示状态迁移与就绪位元数据。

---

## 2. 核心概念精讲

### 2.1 连接状态机：把「连接」拆成「状态 × 事件」

**直觉**：打电话要经历「拨号 → 振铃 → 通话 → 挂断」；网络连接也一样，一个 TCP 连接不是「一直在」的，而是一台**状态机**——任何时刻处于某个**状态**，某个**事件**（收到报文、超时、用户调用）到来时迁移到下一个状态。

**准确定义**：连接状态机是一个五元组 `(S, Σ, δ, s0, F)`——`S` 是状态集合，`Σ` 是事件（输入）集合，`δ: S×Σ→S` 是迁移函数，`s0` 是初始状态（`CLOSED`），`F` 是终态集合。网络协议用状态机建模连接是因为**网络不可靠**：报文可能丢失、乱序、重复，协议栈必须知道「我期望什么、对方期望什么」，状态机把这种期望精确编码成可判定的枚举。

### 2.2 为什么需要连接状态机（动机）

1. **有序推进**：握手必须严格按 `SYN → SYN+ACK → ACK` 推进，状态机保证非法迁移（如未握手就传数据）不可达。
2. **资源归属**：缓冲区、计时器、seq 号都挂在连接上，状态决定了哪些资源活跃、哪些可回收（`TIME_WAIT` 保留资源等对端 FIN）。
3. **确定性验证**：教学内核需要「可断言」的元数据——状态是枚举值，`==` 即可判错，这正是检查点模型 `valid/active/ready/accounted` 四位布尔能工作的前提。

### 2.3 Linux 中 TCP 连接状态机的工作机制

- **状态枚举**：`include/net/tcp_states.h` 定义 `TCP_ESTABLISHED=1`、`TCP_SYN_SENT`、`TCP_SYN_RECV`、`TCP_FIN_WAIT1`、`TCP_FIN_WAIT2`、`TCP_TIME_WAIT`、`TCP_CLOSE`、`TCP_CLOSE_WAIT`、`TCP_LAST_ACK`、`TCP_LISTEN`、`TCP_CLOSING`、`TCP_NEW_SYN_RECV`。
- **状态迁移**：`net/ipv4/tcp.c` 的 `tcp_set_state()` 是唯一改状态入口（带 `TCP_DEBUG_STATE` 打印）；`tcp_rcv_state_process()`、`tcp_fin()`、`tcp_time_wait()` 等事件函数在报文/定时器/系统调用驱动下调用 `tcp_set_state()`。
- **典型路径**：`CLOSED →(listen)→ LISTEN →(SYN)→ SYN_RECV →(ACK)→ ESTABLISHED →(FIN 对端)→ CLOSE_WAIT →(本地 close)→ LAST_ACK →(ACK)→ CLOSED`；主动方走 `ESTABLISHED →(FIN)→ FIN_WAIT_1 →(ACK)→ FIN_WAIT_2 →(FIN)→ TIME_WAIT →(2MSL)→ CLOSED`。
- **教学简化**：教学内核没有 socket/`tcp_set_state` 等价实现，但「状态枚举 + 迁移函数 + 合法性断言」的三件套在 `wait_model`/`task_transition` 中完整存在——本课以此复述状态机主题。

### 2.4 教学内核中与「状态机」有关的既有设施

本课主题机制（连接状态机）**未在源码中实现**，但「状态」这个主题素材在内核里完整存在：

| 素材 | 源码 | 状态机含义 |
|---|---|---|
| `wait_model` | `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD` + `wait_model_exit/wait_model_wait/wait_model_reap` | 「进程等待」状态机：运行→僵尸→回收，迁移带前置条件 |
| `task_transition` | `enum task_state`（`TASK_RUNNING=0`…`EXIT_DEAD=16`）+ `task_transition(i,next)` | Linux `task_struct.state` 迁移的教学模型，非法迁移直接拒绝 |
| `pipe_poll` | `pipe_poll(mask)` 返回 `POLL_IN/POLL_OUT` 就绪位 | socket poll 的就绪位模型（下一课 146 的核心） |
| `timer_model` | `timer_arm/timer_poll/timer_cancel` + `armed/readable/periodic/canceled` | 定时器状态：未臂→臂上→可读→取消 |
| `thread_state` | `THREAD_RUNNING/THREAD_RUNNABLE/THREAD_SLEEPING/…` | 线程调度状态机 |
| 检查点模型 | `lesson_138_state` 的 `valid/active/ready/accounted` | 四个布尔位就是一台四态机 |

### 2.5 检查点模型：lesson_138_model 与 l145test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `138→141` 标记 Origin 为 Lesson 138（`a=138,b=139,c=140,d=141`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「状态计数连续性」。本课同时把上一课新增的 `l144test` 恢复为历史命名 `l137test`（挂 `lesson_137_state`，计数 `137→140`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理（lesson-144 曾用 `l144test` 名字挂 137 号模型，本课将其名实重新对齐）。

### 2.6 机制继承 + 检查点增量

本课主题机制（连接状态机对象）**不是本课新写的代码**：等待队列、任务状态机、管道就绪位、定时器状态全部来自早期课程（Lesson 30s–70s）。本课实际增量只有三处：`l144test`→`l137test` 更名、`lesson_138_model`+`l145test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「状态机」主题重新组织，并如实说明：**连接状态机结构（`struct sock` 式状态对象）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l144test`→`l137test` 恢复命名；新增 `lesson_138_model`/`lesson_138_state`/`l145test`；`about` 与开机横幅更新。连接状态机主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化（md5 与上一课一致） |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`连接状态机`/`l145test`/`Lesson 145`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（状态机机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
static TEXT64 void l145test(u16*c){lesson_138_state=(struct lesson_138_model){138U,139U,140U,141U,1,1,1,1};int ok=lesson_138_state.valid&&lesson_138_state.active&&lesson_138_state.ready&&lesson_138_state.accounted&&lesson_138_state.b==lesson_138_state.a+1U;text64(c,"l145test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 138 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `138→141`（Origin Lesson 138），四布尔位全置 1，`b==a+1U` 校验计数连续——「连接状态按顺序推进」的元数据隐喻。
2. **逻辑分析（≥3 行）**：赋值语句把整个结构体字面量写入 `lesson_138_state`，随后 `ok` 由五个条件合取而成：`valid/active/ready/accounted` 四个布尔位 + `b==a+1U` 连续性。由于字面量全为 1 且 `139==138+1`，`ok` 恒为真，输出必为成功串；fallback 分支（`Lesson 138 fallback reported`）在「计数被破坏或模型被错误初始化」时才可能命中，属于防御性兜底。
3. **输出串（逐字抄录）**：成功 `l145test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 138 fallback reported`。
4. **恢复的 `l137test`**：本课同时把上一课的 `l144test` 更名回 `l137test`（同为 `lesson_137_state`，计数 `137→140`），使检查点命令名与 Origin 对齐；`l100test`–`l136test` 历史检查点全部保留。

#### 3.2.2 等待状态机：wait_model（连接状态机的教学原型）

```c
#define WAIT_RUNNING 1U
#define WAIT_ZOMBIE 2U
#define WAIT_DEAD 3U
struct wait_model { u64 parent_pid,child_pid,exit_code,wait_calls,reaps,statuses; u8 state,waited; };
```

1. **状态三态**：`WAIT_RUNNING → WAIT_ZOMBIE → WAIT_DEAD` 是教学内核最像连接状态机的对象——子进程「出生 → 退出（留状态）→ 被回收」。
2. **迁移函数**（每个 ≥3 行实质分析）：
   - `wait_model_exit(code)`：仅当 `state==WAIT_RUNNING` 才允许进入僵尸态，同时记录 `exit_code` 并递增 `statuses`——**迁移前置条件**，非法迁移（运行中重复退出）被拒绝。
   - `wait_model_wait()`：递增 `wait_calls`，仅当 `state==WAIT_ZOMBIE` 才置 `waited=1`——等待者在「连接就绪」前不会成功，对应 TCP 状态机里「事件必须与状态匹配」。
   - `wait_model_reap()`：要求 `waited` 且 `WAIT_ZOMBIE`，然后置 `WAIT_DEAD`——最终态不可逆，正是状态机的终态语义。
3. **观察命令**：`waittest` 输出 `waittest: bounded wait, exit status, zombie selection, and reap passed`；`waitinfo` 打印 `wait parent/child/state/code/calls/reaps`。

```c
static TEXT64 int wait_model_exit(u64 code){if(wait_model.state!=WAIT_RUNNING)return 0;wait_model.exit_code=code;wait_model.state=WAIT_ZOMBIE;wait_model.statuses++;return 1;}
static TEXT64 int wait_model_wait(void){wait_model.wait_calls++;if(wait_model.state!=WAIT_ZOMBIE)return 0;wait_model.waited=1;return 1;}
static TEXT64 int wait_model_reap(void){if(!wait_model.waited||wait_model.state!=WAIT_ZOMBIE)return 0;wait_model.state=WAIT_DEAD;wait_model.reaps++;return 1;}
```

#### 3.2.3 任务状态机：task_transition / task_table_validate

```c
enum task_state { TASK_RUNNING=0, TASK_INTERRUPTIBLE=1, TASK_UNINTERRUPTIBLE=2,
                  TASK_STOPPED=4, TASK_TRACED=8, EXIT_DEAD=16, EXIT_ZOMBIE=32 };
static TEXT64 int task_transition(u32 i,u8 next){u8 old;if(i>=TASK_TABLE_CAP||!task_table[i].valid||!task_state_valid(next))return 0;old=task_table[i].state;if(old==next)return 1;if(old==EXIT_DEAD||old==EXIT_ZOMBIE)return 0;if(next==EXIT_DEAD)return 0;task_table[i].state=next;task_table[i].transitions++;return 1;}
```

1. **状态集合**：`TASK_*` 枚举与 Linux `include/linux/sched.h` 的 `task_state` 位值一一对应（`0/1/2/4/8/16/32`），连数值都保留——这是「教学内核对照 Linux」的直接证据。
2. **迁移守卫（≥3 行）**：先查索引与 `valid`，再查 `task_state_valid(next)`（迁移目标必须合法）；然后拒绝从终态（`EXIT_DEAD/EXIT_ZOMBIE`）继续迁移，也拒绝直接进入 `EXIT_DEAD`（必须先僵尸后回收）——与 TCP 状态机「从 `TIME_WAIT` 只能回 `CLOSED`」同构。
3. **观察命令**：`taskvalidate` 输出 `task validation: passed (bounded table, unique PID/TID, valid parent/state)`；`tasklist`/`schedinfo` 打印任务状态汇总。

#### 3.2.4 就绪位状态机：pipe_poll（下一课 poll/epoll 的伏笔）

```c
#define POLL_IN 1U
#define POLL_OUT 2U
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

1. **就绪位语义**：调用方用 `mask` 声明「我关心可读（`POLL_IN`）还是可写（`POLL_OUT`）」，函数按管道当前水位（`used` 与 `PIPE_CAP` 比较）计算实际就绪位——「事件 × 状态 → 就绪掩码」。
2. **边界（≥3 行）**：`used>0` 才可读、`used<PIPE_CAP` 才可写，满/空边界天然闭合；每次调用递增 `poll_registrations` 统计注册次数；返回掩码可直接与 `mask` 做 `&` 判定「是否命中关注的事件」。
3. **与连接状态机对照**：连接的「可读 = 收到数据」「可写 = 发送缓冲区有空间」正是 TCP socket 的 `sk_wmem_alloc`/接收队列状态；本课为下一课（146 poll/epoll 集成）埋下就绪位机制。
4. **观察命令**：`polltest` 输出 `polltest: POLLIN/POLLOUT readiness transitions passed`；`pipeinfo` 打印 `pipe used/capacity`、`blocked r/w`、`wake r/w`。

#### 3.2.5 定时器状态机：timer_model（连接超时的伏笔）

```c
static TEXT64 void timer_poll(void){if(!timer_model.armed||timer_model.canceled)return;if(!tick_due(ticks,timer_model.deadline_tick))return;timer_model.expirations++;timer_model.readable=1;if(timer_model.periodic)timer_model.deadline_tick+=timer_model.interval_ticks;else timer_model.armed=0;}
static TEXT64 void timer_arm(u64 delay,u64 interval){timer_model.deadline_tick=ticks+delay;timer_model.interval_ticks=interval;timer_model.periodic=interval!=0;timer_model.armed=1;timer_model.readable=0;timer_model.canceled=0;timer_model.arms++;}
```

1. **状态位**：`armed/readable/periodic/canceled` 四个布尔位描述定时器生命周期；`timer_arm` 一次性完成「设 deadline、定周期、清可读、复位取消」。
2. **到期判定（≥3 行）**：`tick_due(ticks,deadline_tick)` 用无符号回绕安全比较判断是否到期；到期后 `expirations++`、`readable=1`；周期定时器自动滚动 deadline，非周期定时器自动 `armed=0`——「单发/周期」两种模式由状态位区分。
3. **对照连接状态机**：TCP 的 `RTO`/`keepalive` 计时器（`net/ipv4/tcp_timer.c`）就是这类「臂上→到期→动作」状态机；本课为 147「网络错误与超时」埋下定时器设施。`timertest` 输出 `timertest: deadline, expiration, periodic, and cancel passed`。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 145: 连接状态机\n`；检查点分支：
```c
else if(eq64(word,"l137test")){if(!noargs64(arg))usage64(c,"l137test");else l137test(c);}else if(eq64(word,"l145test")){if(!noargs64(arg))usage64(c,"l145test");else l145test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 145: 连接状态机\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` 编译 `kernel64.c` → `ld -T kernel64.ld` → `objcopy` 出 raw bin → `boot.S` 以 `.incbin` 嵌入 → `grub-mkrescue` 出 ISO。`make check` 断言 README 含 `连接状态机`、`Lesson 145`，kernel64.c 含 `l145test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init / vma_init / reclaim_init / vfs_init / address_space_init
 ├─ 横幅 "Lesson 145: 连接状态机"
 └─ 主循环：命令 → exec64
     ├─ l145test / l137test → 阶段检查点（lesson_138_state / lesson_137_state）
     ├─ waittest / waitblocktest / waitpidtest → 等待状态机（RUNNING/ZOMBIE/DEAD）
     ├─ taskvalidate / tasklist → 任务状态机（TASK_* 枚举）
     ├─ polltest / pipeinfo → 就绪位状态机（POLL_IN/POLL_OUT）
     ├─ timertest / timerinfo → 定时器状态机（armed/readable/canceled）
     └─ about → "Lesson 145: 连接状态机"
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 完成 PMM/VM/VFS 初始化，打印横幅 `Lesson 145: 连接状态机`。
2. **`l145test`** → `l145test(c)` → 初始化 `lesson_138_state` → 五条件断言 → `l145test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`l137test`**（恢复名） → `l137test(c)` → `lesson_137_state` 断言 → `l137test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`waittest`** → `wait_model_start` → `wait_model_exit(42)` → `wait_model_wait` → `wait_model_reap` → 三态迁移全通过 → `waittest: bounded wait, exit status, zombie selection, and reap passed`。
5. **`polltest`** → `pipe_init` → `pipe_poll`/`pipe_try_write`/`pipe_try_read` 就绪位迁移 → `polltest: POLLIN/POLLOUT readiness transitions passed`。
6. **`timertest`** → `timer_arm(2,0)` → 拨动 `ticks` → `timer_poll` 到期 → `timer_cancel` → `timertest: deadline, expiration, periodic, and cancel passed`。
7. **`about`** → `Lesson 145: 连接状态机`。

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
Multiboot2 and Lesson 145 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 145: 连接状态机` 横幅 |
| `l145test` | `l145test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l137test` | `l137test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `waittest` | `waittest: bounded wait, exit status, zombie selection, and reap passed` |
| `polltest` | `polltest: POLLIN/POLLOUT readiness transitions passed` |
| `timertest` | `timertest: deadline, expiration, periodic, and cancel passed` |
| `taskvalidate` | `task validation: passed (bounded table, unique PID/TID, valid parent/state)` |
| `about` | `Lesson 145: 连接状态机` |

判定成功：`l145test` 输出 passed、无 fallback，`waittest`/`polltest`/`timertest` 均 passed、无 `BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l145test` 输出 `Lesson 138 fallback reported` | `lesson_138_state` 初始化/断言失败（stale 镜像） | `grep -n "l145test" kernel64.c`；确认初始化串 `{138U,139U,140U,141U,1,1,1,1}` 与 `b==a+1U` |
| `waittest` 输出 `BROKEN` | 等待状态机迁移顺序被破坏 | 对照 `wait_model_exit/wait/wait_reap`：必须依次 `RUNNING→ZOMBIE→DEAD`，且 `reap` 要求 `waited` |
| `polltest` 输出 `BROKEN` | 管道就绪位与水位不一致 | 检查 `pipe_poll` 的 `used`/`PIPE_CAP` 比较；`pipe_try_write` 满时返回 0、`pipe_try_read` 空时返回 0 |
| `timertest` 输出 `BROKEN` | 定时器状态位被提前复位 | 对照 `timer_arm/timer_poll/timer_cancel` 的状态位转换；`tick_due` 用无符号回绕比较 |
| `l145test` 与 `l137test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l145test` 只操作 `lesson_138_state`、`l137test` 只操作 `lesson_137_state` |
| `taskvalidate` 输出 `BROKEN` | `task_table` 的 PID/TID 重复、父进程号不小于子进程号、或槽位状态非法 | 对照 `task_table_validate` 的三个循环：根槽检查、O(n²) 唯一性、`parent_pid>=pid` 判负 |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 145' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `连接状态机` 与 `Lesson 145` |
| 输入 `l145test` 提示 `unknown command` | exec64 分支未命中（命令表陈旧） | `grep -o 'l145test' kernel64.c` 应同时命中函数定义与 `eq64` 分支 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `wait_model` 三态（RUNNING/ZOMBIE/DEAD） | `kernel/exit.c` 的 `TASK_ZOMBIE`/`release_task()`；`include/linux/sched.h` | 模型无 PCB/僵尸链表，只保留状态枚举与迁移守卫 |
| `task_transition` + `enum task_state` | `include/linux/sched.h` 的 `task_state` 位值；`kernel/sched/core.c` `set_task_state()` | 模型没有抢占/睡眠挂起，只做合法迁移校验 |
| `pipe_poll` 的 POLL_IN/POLL_OUT | `net/socket.c` `sock_poll()`；`include/linux/poll.h` | 模型只有管道水位，无 socket 缓冲区/sk_buff |
| TCP 连接状态机（主题概念） | `net/ipv4/tcp.c` 的 `tcp_set_state()`；`include/net/tcp_states.h` | 教学内核**没有** `struct sock`/`tcp_set_state`，只在 README/概念层对照状态集合 |
| `timer_model` 的 armed/readable/canceled | `net/ipv4/tcp_timer.c`（RTO/keepalive）；`kernel/time/timer.c` | 模型无真实硬件定时器中断驱动，仅 PIT tick 驱动元数据 |
| `l145test` 断言 | 无直接对应（LTP/网络协议栈测试） | 模型把「状态机」主题的可验证状态固化进内核 |

**权威来源**：Linux `net/ipv4/tcp.c`、`include/net/tcp_states.h`、`include/linux/sched.h` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有**连接状态机结构或 `tcp_set_state` 的等价实现——连接状态机是「主题宣告」，教学内核停留在「用等待队列/任务/管道/定时器的既有状态位承载状态机概念」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么 TCP 握手必须用状态机而不能用「收到 SYN 就回 ACK」的简单逻辑？举一个非法迁移的例子（如未握手就传数据）说明状态机如何挡住它。
2. **源码定位**：在 `kernel64.c` 中找出所有「状态迁移带前置条件」的函数（提示：`wait_model_exit`、`task_transition`、`timer_poll`、`pipe_poll`），说明每处的前置条件是什么。
3. **动手实验**：把 `wait_model_reap` 的 `waited` 前置条件去掉，运行 `waittest` 观察输出变化；随后恢复并说明这条检查保护了什么不变式。
4. **动手实验**：给 `pipe_poll` 增加一个 `POLL_ERR` 位（仅当管道「出错」时置位），在 `polltest` 里验证并重新构建运行。
5. **Linux 对照**：阅读 `include/net/tcp_states.h` 的状态枚举，画出从 `LISTEN` 到 `TIME_WAIT` 的完整迁移图，并对照教学模型 `wait_model` 的三态迁移，说明两者在「迁移守卫」上的异同。

---

## 9. 本课小结与下一课预告

1. 连接状态机 =「状态 × 事件」的二元组，TCP 用 `tcp_set_state()` + `include/net/tcp_states.h` 实现。
2. Linux 的状态迁移由报文/定时器/系统调用事件触发，非法迁移被状态守卫拒绝（`net/ipv4/tcp.c`）。
3. 教学内核没有 socket 状态对象，但「状态枚举 + 迁移守卫 + 合法性断言」三件套完整存在：`wait_model` 三态、`task_transition`、`pipe_poll` 就绪位、`timer_model` 定时器位。
4. 检查点增量：`l144test`→`l137test` 更名、新增 `lesson_138_model`+`l145test`、横幅与 `about` 更新为 `Lesson 145: 连接状态机`。
5. `pipe_poll` 的 `POLL_IN/POLL_OUT` 就绪位机制是下一课的直接伏笔。
6. 下一课（Lesson 146）主题为 **socket poll/epoll 集成**（对照 `fs/eventpoll.c`、`include/linux/poll.h`）：从「单连接状态」转到「多描述符批量就绪监控」，教学内核将以 `pipe_poll`/等待队列设施承接该主题。

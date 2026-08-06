# Lesson 147: 网络错误与超时 — 精讲文档

> **课号**：Lesson 147 ｜ **主题**：网络错误与超时（network errors and timeouts）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课是「网络三连」（145 连接状态机 → 146 poll/epoll 集成 → **147 错误与超时**）的收尾，之后进入 namespace 三连（148–150）
> **前置课程**：[`../lesson-146-stable/README.md`](../lesson-146-stable/README.md)（socket poll/epoll 集成）
> **后续课程**：[`../lesson-148-stable/README.md`](../lesson-148-stable/README.md)（进程 namespace）
> **一句话目标**：讲清「网络为什么需要错误码与超时」——协议栈用错误码（`ECONNREFUSED`/`ETIMEDOUT`/`ENOSYS` 等）报告失败原因、用超时定时器（RTO/重传）应对不可靠网络，对照 Linux `include/uapi/asm-generic/errno*.h` 与 `net/ipv4/tcp_timer.c`，并把教学内核继承的 `pmm_error` 错误串、`timer_model`/`sleep_model` 定时器设施按这一主题系统化复述，运行 `l147test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（网络错误/超时对象）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l146test` 恢复为历史命名 `l139test`（挂 `lesson_139_state`），新增 `lesson_140_model`/`lesson_140_state` 与 `l147test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l147test`（旧 README 所写 `l140test` 按源码勘误，源码中不存在 `l140test` 命令）；另保留历史检查点 `l100test`–`l139test`，以及 `timertest`/`timerinfo`/`sleeptimetest`/`uptime`/`tickinfo`/`meminfo`（`pmm_error`）/`syscallinfo`（`ENOSYS`）等与错误与超时相关的回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「错误码报告原因、超时定时器兜底」的框架解释网络失败处理；说出 Linux 中错误码来自 `include/uapi/asm-generic/errno*.h`（`ECONNREFUSED=111`、`ETIMEDOUT=110`、`ENOSYS=38`），TCP 的 RTO 重传/`TIME_WAIT` 由 `net/ipv4/tcp_timer.c` 驱动；在教学内核中沿 `pmm_error`（字符串错误）→ `timer_model`（臂上/到期/取消）→ `sleep_model`（请求/截止/唤醒）→ `l147test` 观察「错误与超时」的教学形态；运行 `make check`/`make run` 验证本课稳定快照。

**在课程主线中的位置**：Lesson 140–147 构成「网络协议收敛带」，本课为网络段的**收尾课**：145 建立连接状态机、146 解决多连接就绪监控，本课解决「状态机卡住时怎么办」——错误码与超时把「不确定的等待」变成「确定的失败」。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中所有「错误报告 + 定时/超时」相关的设施（`pmm_error` 错误串、`timer_model`/`timer_poll`/`timer_cancel`、`sleep_model`/`sleeptimetest`、`ENOSYS`/`tick_due`）按「网络错误与超时」主题系统化复述。下一课（Lesson 148）转向进程 namespace。

**前置知识清单**（学本课前必须掌握）：
1. 连接状态机与就绪位：状态 × 事件、`pipe_poll` 的 `POLL_IN/POLL_OUT`（Lesson 145/146）。
2. 定时器模型：`struct timer_model`（`deadline_tick`/`interval_ticks`/`armed`/`readable`/`periodic`/`canceled`）、`timer_arm/timer_poll/timer_read/timer_cancel`（Lesson 76s）。
3. 睡眠模型：`struct sleep_model`（`requested_ticks`/`deadline_tick`/`wake_tick`/`remaining_ticks`）、`thread_sleep_ticks`（Lesson 76s/73s）。
4. 错误处理范例：`pmm_error` 字符串、`mmap_tag64` 的错误分支、`syscall_dispatch` 的 `ENOSYS`（Lesson 34/44s）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–146）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 147: 网络错误与超时`；
- 新命令 `l147test` 输出 `l147test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `timertest`/`sleeptimetest`/`timerinfo`/`uptime` 继续展示定时与超时元数据。

---

## 2. 核心概念精讲

### 2.1 网络错误码：把「为什么失败」编码成数字

**直觉**：连接被拒、路由不可达、对端无响应——原因五花八门。协议栈用**错误码**把失败原因压缩成一个整数，应用拿到 `errno` 就能知道「重试、换端口还是报错」。

**准确定义**：错误码是内核返回给应用层的 `errno` 值（负值在 `syscall` 返回时取反）。Linux 在 `include/uapi/asm-generic/errno-base.h` 与 `errno.h` 定义：`EPERM=1`、`ECONNREFUSED=111`、`ETIMEDOUT=110`、`ENOSYS=38`。网络路径典型错误：`connect()` 被拒 → `ECONNREFUSED`；发包后长时间无响应 → `ETIMEDOUT`；路由不可达 → `EHOSTUNREACH`；地址已被占 → `EADDRINUSE`。

### 2.2 超时定时器：把「不确定的等待」变成「确定的失败」

**直觉**：网络会丢包。如果发一个包就永远等下去，进程会挂死。TCP 的**重传超时（RTO）**给每个报文一个「最后期限」，超时未 ACK 就重传，重传次数超限则放弃并报告错误——这就是「超时 = 一种兜底错误」。

**准确定义**：超时是「在截止时间前未等到期望事件 → 触发既定动作（重传/取消/报错）」的机制。TCP 用 `inet_csk_reqsk_queue_prune`/`tcp_retransmit_timer`（`net/ipv4/tcp_timer.c`）驱动重传与建连超时；应用层用 `SO_RCVTIMEO`/`SO_SNDTIMEO` 设置接收/发送超时，`poll(..., timeout)` 让「等待就绪」也有上限。

### 2.3 为什么需要错误与超时（动机）

1. **错误码**：失败原因不可枚举就无法决策——`ECONNREFUSED`（重试可能没用）与 `ETIMEDOUT`（可能是网络拥塞，值得重试）要区分。
2. **超时**：协议不能无限等待——丢包不可避，必须靠「截止时间 + 重传」让链路最终收敛到成功或失败。
3. **确定性验证**：教学内核的一切都必须可断言——错误用字符串（`pmm_error`）或错误号（`ENOSYS`）明确报告，超时用 `deadline_tick` 比较判定，这正是检查点模型能工作的前提。

### 2.4 Linux 中错误与超时的工作机制

- **错误路径**：`net/ipv4/tcp.c` 在握手被拒/数据超时等路径设置 `sk->sk_err`，唤醒等待者；`tcp_recvmsg`/`tcp_write_err` 把 `sk_err` 转成 `errno` 返回。`include/uapi/asm-generic/errno.h` 是数值权威来源。
- **超时路径**：`net/ipv4/tcp_timer.c` 的 `tcp_write_timer`（重传 RTO）、`tcp_keepalive_timer`（保活）、`inet_csk_accept` 的 `accept` 超时；每轮 `tcp_time_wait`/`tcp_delack_timer` 等。计时单位是 `jiffies`（内核时钟节拍），教学内核用 `ticks`（PIT 100Hz）对应。
- **教学简化**：教学内核没有 sk_buff/sk_err，但「错误串（`pmm_error`）+ 截止时间（`deadline_tick`）+ 到期判定（`tick_due`）+ 取消（`timer_cancel`）」四件套完整存在。

### 2.5 教学内核中与「错误与超时」有关的既有设施

本课主题机制（网络错误/超时对象）**未在源码中实现**，但「错误报告 + 截止时间」这个主题素材在内核里完整存在：

| 素材 | 源码 | 错误/超时含义 |
|---|---|---|
| `pmm_error` | `static const char *pmm_error;`，`pmm_init`/`mmap_tag64` 写入 `"MBI too small"`/`"ready"` 等 | 内核错误串（对照 `errno` 的数字错误码） |
| `ENOSYS` | `#define ENOSYS 38`，`syscall_dispatch` 默认分支 `return (u64)(-(s64)ENOSYS)` | 未知系统调用报 `-ENOSYS`，与应用层 errno 数值一致 |
| `timer_model` | `deadline_tick`/`armed`/`readable`/`periodic`/`canceled` + `timer_poll`/`timer_arm`/`timer_cancel` | 超时定时器：臂上→到期→可读/取消 |
| `tick_due` | `return (u64)(now-deadline)<(1ULL<<63);` | 无符号回绕安全到期比较（RTO 判定的教学原型） |
| `sleep_model` | `requested_ticks`/`deadline_tick`/`wake_tick`/`remaining_ticks` + `sleeptimetest` | 睡眠超时：请求→截止→唤醒 |
| `pmm_free_page/page_state` | 返回 `"free"/"allocated"/"fixed/reserved"/"mapped"/"thread stack"/"freed"` | 多分支错误/状态串 |

### 2.6 检查点模型：lesson_140_model 与 l147test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `140→143` 标记 Origin 为 Lesson 140（`a=140,b=141,c=142,d=143`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「超时计数连续性」。本课同时把上一课新增的 `l146test` 恢复为历史命名 `l139test`（挂 `lesson_139_state`，计数 `139→142`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理（lesson-146 曾用 `l146test` 名字挂 139 号模型，本课将其名实重新对齐）。

### 2.7 机制继承 + 检查点增量

本课主题机制（网络错误与超时）**不是本课新写的代码**：错误串、定时器、睡眠模型全部来自早期课程（Lesson 34/44s/76s）。本课实际增量只有三处：`l146test`→`l139test` 更名、`lesson_140_model`+`l147test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「错误报告 + 截止时间」主题重新组织，并如实说明：**网络错误/超时结构（`struct sk_err`/`tcp_retransmit_timer` 等价物）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l146test`→`l139test` 恢复命名；新增 `lesson_140_model`/`lesson_140_state`/`l147test`；`about` 与开机横幅更新。错误/超时主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化（md5 与上一课一致） |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`网络错误与超时`/`l147test`/`Lesson 147`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（错误/超时机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
static TEXT64 void l147test(u16*c){lesson_140_state=(struct lesson_140_model){140U,141U,142U,143U,1,1,1,1};int ok=lesson_140_state.valid&&lesson_140_state.active&&lesson_140_state.ready&&lesson_140_state.accounted&&lesson_140_state.b==lesson_140_state.a+1U;text64(c,"l147test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 140 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `140→143`（Origin Lesson 140），四布尔位全置 1，`b==a+1U` 校验计数连续——「错误/超时状态按顺序收敛」的元数据隐喻。
2. **逻辑分析（≥3 行）**：赋值语句把整个结构体字面量写入 `lesson_140_state`，随后 `ok` 由五个条件合取而成：`valid/active/ready/accounted` 四个布尔位 + `b==a+1U` 连续性。由于字面量全为 1 且 `141==140+1`，`ok` 恒为真，输出必为成功串；fallback 分支（`Lesson 140 fallback reported`）在「计数被破坏或模型被错误初始化」时才可能命中，属于防御性兜底。
3. **输出串（逐字抄录）**：成功 `l147test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 140 fallback reported`。
4. **恢复的 `l139test`**：本课同时把上一课的 `l146test` 更名回 `l139test`（同为 `lesson_139_state`，计数 `139→142`），使检查点命令名与 Origin 对齐；`l100test`–`l138test` 历史检查点全部保留。

#### 3.2.2 错误串机制：pmm_error 与 ENOSYS（errno 的教学原型）

```c
static const char *pmm_error;
static TEXT64 const char *page_state(u64 p){u32 i;if(!pmm_ready)return "PMM unavailable";if((p&(PAGE_SIZE-1))||p>=PMM_MAX_PHYS)return "invalid";i=(u32)(p/PAGE_SIZE);if(!bit(i))return "free";if(fixed(i))return "fixed/reserved";return "allocated";}
```

1. **错误报告（≥3 行）**：`pmm_error` 是全局错误串指针，`mmap_tag64` 在 MBI 解析失败时写入 `"MBI too small"`/`"truncated MBI tag"`/`"bad MBI tag size"`/`"mmap tag missing"` 等，`pmm_init` 成功时置 `"ready"`——教学内核用字符串而非数字 errno 报告失败原因，`meminfo` 命令把它打印出来（`status: ...`）。
2. **多分支状态串**：`page_state` 按「未就绪/地址非法/空闲/固定保留/已分配」返回五类串，`pmm_free_page` 再叠加 `"mapped"`/`"thread stack"`/`"freed"`——这是「错误码分派」的字符串版。
3. **数字错误码**：`syscall_dispatch` 的默认分支 `return (u64)(-(s64)ENOSYS);` 用 `#define ENOSYS 38` 返回负 38——与应用层 errno 数值一致（对照 `include/uapi/asm-generic/errno.h`）。`syscallinfo` 输出 `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS`。

```c
#define ENOSYS 38
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){case SYS_GETTICKS:return ticks;case SYS_GETPID:return FIXED_PID;case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;case SYS_EXIT:return 0;default:return (u64)(-(s64)ENOSYS);}}
```

#### 3.2.3 超时定时器：timer_model / timer_poll / timer_cancel（RTO 的教学原型）

```c
static TEXT64 int tick_due(u64 now,u64 deadline){return (u64)(now-deadline)<(1ULL<<63);}
static TEXT64 void timer_poll(void){if(!timer_model.armed||timer_model.canceled)return;if(!tick_due(ticks,timer_model.deadline_tick))return;timer_model.expirations++;timer_model.readable=1;if(timer_model.periodic)timer_model.deadline_tick+=timer_model.interval_ticks;else timer_model.armed=0;}
static TEXT64 void timer_arm(u64 delay,u64 interval){timer_model.deadline_tick=ticks+delay;timer_model.interval_ticks=interval;timer_model.periodic=interval!=0;timer_model.armed=1;timer_model.readable=0;timer_model.canceled=0;timer_model.arms++;}
static TEXT64 void timer_cancel(void){timer_model.armed=0;timer_model.canceled=1;timer_model.readable=0;timer_model.expirations=0;}
```

1. **到期判定（≥3 行）**：`tick_due(now,deadline)` 用无符号减法回绕比较——`now>=deadline` 时 `now-deadline` 落在 `[0,2^63)`，否则落在上半个区间，从而避免「ticks 回绕」误判。这是超时判定的正确性根基。
2. **臂上/到期/取消（≥3 行）**：`timer_arm` 一次性设置 `deadline_tick`、周期（`interval!=0` 则 `periodic=1`）、并复位 `readable/canceled`；`timer_poll` 先查 `armed` 与 `canceled`（取消即失效），到期后 `expirations++`、`readable=1`，周期定时器自动滚动 deadline，单发定时器自动 `armed=0`；`timer_cancel` 把四态全部复位——「超时后动作」与「取消后不再触发」都精确建模。
3. **对照 TCP**：单发定时器对应 TCP 的一次性 RTO 定时器，周期定时器对应 keepalive/延迟 ACK 定时器（`net/ipv4/tcp_timer.c`）；`tick_due` 对应 Linux 的 `time_after_eq`/`time_before` 宏。
4. **观察命令**：`timertest` 输出 `timertest: deadline, expiration, periodic, and cancel passed`；`timerinfo` 打印 `timer armed/readable/periodic: ... deadline/expirations: ... arms/reads: ...`。

#### 3.2.4 睡眠超时：sleep_model / sleeptimetest（应用层超时的教学原型）

```c
struct sleep_model { u64 requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes; u8 active,interrupted; };
static TEXT64 void sleeptimetest(u16*c){u64 old=ticks;u64 delay=3;sleep_model.requested_ticks=delay;sleep_model.deadline_tick=old+delay;sleep_model.wake_tick=0;sleep_model.remaining_ticks=delay;sleep_model.active=1;sleep_model.interrupted=0;sleep_model.sleeps++;ticks=old+delay-1;sleep_model.remaining_ticks=sleep_model.deadline_tick-ticks;int a=sleep_model.remaining_ticks&&sleep_model.active;ticks=old+delay;sleep_model.remaining_ticks=0;sleep_model.wake_tick=ticks;sleep_model.active=0;sleep_model.wakes++;int b=!sleep_model.active&&!sleep_model.remaining_ticks;text64(c,"sleeptimetest: ");text64(c,a&&b?"deadline sleep and wake accounting passed":"BROKEN");putc64(c,'\n');}
```

1. **请求/截止/唤醒（≥3 行）**：睡眠请求把 `requested_ticks=delay` 与 `deadline_tick=old+delay` 绑定；`ticks=old+delay-1` 时（未到截止）`remaining_ticks>0` 且 `active=1`（断言 `a`），到截止后 `remaining_ticks=0`、`wake_tick=ticks`、`active=0`（断言 `b`）——「未超时仍在等」vs「超时后唤醒」被精确区分。
2. **对照应用层**：`sleep_model` 对应 `nanosleep`/`SO_RCVTIMEO` 的「到期唤醒」；真实线程路径由 `thread_sleep_ticks` 实现（置 `wake_tick=ticks+delta`、`THREAD_SLEEPING`，IRQ0 的 `wake_sleepers` 扫描唤醒）。
3. **观察命令**：`sleeptimetest` 输出 `sleeptimetest: deadline sleep and wake accounting passed`；`threadinfo` 打印 `sleep wakeups` 与 worker 步骤。

#### 3.2.5 错误与超时的观察面：meminfo / uptime / syscallinfo

- `meminfo` 输出 `PMM: 4 KiB physical frames...\nstatus:  <pmm_error>`——错误串的第一观察点；
- `tickinfo`/`uptime` 输出 `PIT channel 0: 0000000000000064 Hz\nticks: ...\nuptime (centiseconds): ...`——时钟即超时的「时间源」；
- `syscallinfo` 输出 `unknown=-ENOSYS`——数字错误码的观察点。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 147: 网络错误与超时\n`；检查点分支：
```c
else if(eq64(word,"l139test")){if(!noargs64(arg))usage64(c,"l139test");else l139test(c);}else if(eq64(word,"l147test")){if(!noargs64(arg))usage64(c,"l147test");else l147test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 147: 网络错误与超时\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` 编译 `kernel64.c` → `ld -T kernel64.ld` → `objcopy` 出 raw bin → `boot.S` 以 `.incbin` 嵌入 → `grub-mkrescue` 出 ISO。`make check` 断言 README 含 `网络错误与超时`、`Lesson 147`，kernel64.c 含 `l147test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init（写 pmm_error）/ vma_init / reclaim_init / vfs_init / address_space_init
 ├─ 横幅 "Lesson 147: 网络错误与超时"
 └─ 主循环：命令 → exec64
     ├─ l147test / l139test → 阶段检查点（lesson_140_state / lesson_139_state）
     ├─ timertest / timerinfo → 超时定时器（armed/deadline/expirations/cancel）
     ├─ sleeptimetest / threadinfo → 睡眠超时（requested/deadline/wake）
     ├─ meminfo → pmm_error 错误串
     ├─ uptime / tickinfo → PIT 时钟时间源
     ├─ syscallinfo → -ENOSYS 数字错误码
     └─ about → "Lesson 147: 网络错误与超时"
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 完成 PMM/VM/VFS 初始化，`pmm_init` 把 `pmm_error` 置为 `"ready"`，打印横幅 `Lesson 147: 网络错误与超时`。
2. **`l147test`** → `l147test(c)` → 初始化 `lesson_140_state` → 五条件断言 → `l147test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`l139test`**（恢复名） → `l139test(c)` → `lesson_139_state` 断言 → `l139test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`timertest`** → `timer_arm(2,0)` → 拨动 `ticks` → `timer_poll` 到期 → `timer_cancel` → `timertest: deadline, expiration, periodic, and cancel passed`。
5. **`sleeptimetest`** → 构造 `sleep_model` 请求 → 截止前后两段断言 → `sleeptimetest: deadline sleep and wake accounting passed`。
6. **`meminfo`** → `PMM: 4 KiB physical frames in 16 MiB mapped window` + `status: ready`（成功时）。
7. **`about`** → `Lesson 147: 网络错误与超时`。

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
Multiboot2 and Lesson 147 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 147: 网络错误与超时` 横幅 |
| `l147test` | `l147test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l139test` | `l139test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `timertest` | `timertest: deadline, expiration, periodic, and cancel passed` |
| `sleeptimetest` | `sleeptimetest: deadline sleep and wake accounting passed` |
| `syscallinfo` | 末行 `... unknown=-ENOSYS`（含 `3=EXIT; unknown=-ENOSYS`） |
| `about` | `Lesson 147: 网络错误与超时` |

判定成功：`l147test` 输出 passed、无 fallback，`timertest`/`sleeptimetest` 均 passed、无 `BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l147test` 输出 `Lesson 140 fallback reported` | `lesson_140_state` 初始化/断言失败（stale 镜像） | `grep -n "l147test" kernel64.c`；确认初始化串 `{140U,141U,142U,143U,1,1,1,1}` 与 `b==a+1U` |
| `timertest` 输出 `BROKEN` | 定时器状态位被提前复位 | 对照 `timer_arm/timer_poll/timer_cancel` 的状态位转换；`tick_due` 用无符号回绕比较 |
| `sleeptimetest` 输出 `BROKEN` | 睡眠截止/唤醒记账被破坏 | 对照 `sleep_model` 的 `deadline_tick`/`remaining_ticks`/`active` 三段赋值；`ticks=old+delay-1` 与 `ticks=old+delay` 两分支 |
| `meminfo` 显示 `status: not initialized` | `pmm_init` 未执行或 MBI 解析失败 | `grep -o 'pmm_error' kernel64.c`；检查 `mmap_tag64` 错误分支写入的字符串 |
| 系统调用返回错误号非 `-ENOSYS` | `syscall_dispatch` 默认分支被绕过 | 确认 `SYS_GETTICKS/GETPID/WRITE_CONSOLE/EXIT` 四分支之外都走 `return (u64)(-(s64)ENOSYS)` |
| `l147test` 与 `l139test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l147test` 只操作 `lesson_140_state`、`l139test` 只操作 `lesson_139_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 147' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `网络错误与超时` 与 `Lesson 147` |
| 输入 `l147test` 提示 `unknown command` | exec64 分支未命中（命令表陈旧） | `grep -o 'l147test' kernel64.c` 应同时命中函数定义与 `eq64` 分支 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `pmm_error` 错误串（`"MBI too small"` 等） | `include/uapi/asm-generic/errno*.h` 的数字 errno | 模型用字符串不用数字；无 `sk_err`/`task->err` |
| `ENOSYS=38` + `syscall_dispatch` 默认分支 | `include/uapi/asm-generic/errno-base.h`（`ENOSYS=38`）；`kernel/` 各 syscall 表 | 模型只有 4 个系统调用，`-ENOSYS` 即全部未知号 |
| `timer_model`（armed/deadline/readable/canceled） | `net/ipv4/tcp_timer.c`（RTO/keepalive）；`kernel/time/timer.h` | 模型无真实硬件定时器中断队列，仅 PIT tick 驱动元数据 |
| `tick_due` 无符号回绕比较 | `include/linux/jiffies.h` 的 `time_after_eq`/`time_before` | 模型只有单一 64 位 `ticks`，无 `jiffies` 周期处理 |
| `sleep_model`（requested/deadline/wake） | `kernel/time/hrtimer.c`；`kernel/sched/core.c` 的睡眠调度 | 模型无高精度定时器，睡眠由 IRQ0 扫描唤醒 |
| `page_state` 多分支状态串 | `mm/page_alloc.c` 的页状态与 `-ENOMEM` | 模型不真实分配物理页语义，只返回状态串 |
| `l147test` 断言 | 无直接对应（网络错误注入/超时测试套件） | 模型把错误与超时主题的可验证状态固化进内核 |

**权威来源**：Linux `net/ipv4/tcp_timer.c`、`include/uapi/asm-generic/errno*.h`、`include/linux/jiffies.h` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有**网络错误/超时结构或 `tcp_retransmit_timer` 的等价实现——网络错误与超时是「主题宣告」，教学内核停留在「用 `pmm_error` 错误串 + `timer_model`/`sleep_model` 定时设施承载错误/超时概念」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：`ECONNREFUSED` 与 `ETIMEDOUT` 对应用的决策有什么区别？为什么「超时」可以被看作一种特殊的错误？
2. **源码定位**：在 `kernel64.c` 中找出所有「设置错误/失败标记」的代码位置（提示：`mmap_tag64` 写 `pmm_error`、`syscall_dispatch` 返回 `-ENOSYS`、`pmm_free_page` 返回 `"mapped"`），说明每处的错误语义。
3. **动手实验**：把 `tick_due` 改成 `return now>=deadline;`（去掉回绕安全），运行 `timertest` 观察在 ticks 回绕场景下的潜在差异，随后恢复并解释为什么无符号回绕比较是必要的。
4. **动手实验**：给 `timer_model` 增加一个 `missed_expirations` 计数（当 `timer_poll` 被调用时 `ticks` 已越过 deadline 多个 tick 时累加），在 `timerinfo` 里打印并重新构建运行。
5. **Linux 对照**：阅读 `net/ipv4/tcp_timer.c` 的 `tcp_write_timer`，说明 RTO 重传定时器与教学模型 `timer_poll` 的「到期动作」有何异同；教学模型的周期定时器对应 TCP 的哪个定时器？

---

## 9. 本课小结与下一课预告

1. 网络失败处理 = 错误码（报告原因）+ 超时定时器（兜底等待）：`include/uapi/asm-generic/errno*.h`、`net/ipv4/tcp_timer.c`。
2. Linux 用 `sk_err` 携带错误、用 RTO/keepalive/延迟 ACK 定时器驱动超时动作。
3. 教学内核没有网络错误对象，但错误/超时素材完整：`pmm_error` 错误串、`ENOSYS=38`、`timer_model` 超时定时器、`tick_due` 回绕安全比较、`sleep_model` 睡眠超时。
4. 检查点增量：`l146test`→`l139test` 更名、新增 `lesson_140_model`+`l147test`、横幅与 `about` 更新为 `Lesson 147: 网络错误与超时`。
5. 「截止时间」概念（`deadline_tick`）是连接状态机、poll 超时与 namespace 生命周期共用的时间基元。
6. 下一课（Lesson 148）主题为 **进程 namespace**（对照 `kernel/nsproxy.c`、`include/linux/nsproxy.h`）：网络段收尾后转入 namespace 三连，教学内核将以 `task_table`/`task_struct`/`FIXED_PID`/`SECOND_PID` 进程号设施承接「进程号世界隔离」主题。

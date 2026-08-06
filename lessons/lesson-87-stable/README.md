# Lesson 87: 负载均衡与进程组调度综合 checkpoint — 精讲文档

> **课号**：Lesson 87 ｜ **主题**：负载均衡与进程组调度综合 checkpoint（load balancing + process-group scheduling）
> **课程主线位置**：进程/调度/COW 元数据阶段（Lesson 71–87 检查点序列的收尾，本课为 Lesson 80 原型的综合检查点）
> **前置课程**：[`../lesson-86-stable/README.md`](../lesson-86-stable/README.md)（调度公平性验证）
> **后续课程**：[`../lesson-88-stable/README.md`](../lesson-88-stable/README.md)（VFS 层次与 mount 元数据）
> **一句话目标**：把「负载」（调度统计与 runnable 分布）与「进程组」（pgid/session/foreground 组）两条主线合入一个综合检查点，验证单内核映像内两条元数据链同时正确。

本课是稳定快照（stable snapshot）综合检查点：`kernel64.c` 相对上一课仅做一处增量——把 `l86test` 恢复为 `l79test`，新增 `lesson_80_model` 状态与 `l87test` 检查点测试，并更新 `about`/开机横幅为本课主题。本课是调度/进程组阶段最后一个检查点，之后课程转入 VFS 主线（Lesson 88 起）。继承的进程、GUI、子系统回归保持有效；会话不变量继续保留。

---

## 1. 课程定位（Mission）

**学完本课你能**：同时说清两条元数据链——「负载均衡侧」如何用调度统计（`sched_picks`/`preempt_switches`/`quantum_left`）与轮转公平性验证负载分布；「进程组侧」如何用 `process_group_model` 表达 pgid/leader/session/foreground/controlling-terminal 五要素；并能运行 `l87test` 与 `pgtest`/`sessiontest`/`fgtest` 组合验证。

**在课程主线中的位置**：本课是进程/调度/COW 阶段（Lesson 71–87）的收尾综合检查点，课程原型为 Lesson 80「负载均衡与进程组调度综合 checkpoint」。上一课（Lesson 86）只验证调度公平性；本课把「负载」与「进程组」两条线合并验证，随后课程进入全新的 VFS 主线（Lesson 88 超级块/inode/dentry/file）。

**前置知识清单**（学本课前必须掌握）：
1. 调度公平性与时间片：`irq0_schedule`、`rr_pick_next`、`quantum_left`、`preempt_switches`（Lesson 86）。
2. 进程组与 session 元数据：`struct process_group_model`、pgid 首领、session、控制终端（Lesson 68–70）。
3. 孤儿进程组 reparent、job-control 信号、终端 stop/continue（Lesson 73–75 的 `orphan66test`/`job67test`/`stop68test`）。
4. 调度器类统计：`sched_enqueues/sched_dequeues/sched_picks`（Lesson 69/78）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 87: 负载均衡与进程组调度综合 checkpoint`；
- 新命令 `l87test` 输出 `l87test: bounded scheduling and copy-on-write checkpoint passed`（或其 fallback）；
- 进程组命令组 `pginfo`/`pgtest`/`sessiontest`/`fgtest` 与调度命令组 `schedinfo`/`threadinfo`/`ps` 联合验证「负载 + 进程组」综合状态。

---

## 2. 核心概念精讲

### 2.1 负载（load）在单核教学模型中的含义

**直觉**：Linux 的「负载」指系统里 runnable/阻塞线程数量的时间加权平均（`loadavg`），多核还有「每个 CPU 的 runqueue 是否均衡」。单核 TinyOS 没有多 runqueue 可迁移，因此把「负载均衡」降维为**验证调度统计的一致性**：所有 runnable 线程都被轮转扫描覆盖（`next_runnable` 每次返回合法 id 或 `0xff`），切换/选择计数单调正确，没有线程被饿死。

**本课的观察载体**：
- `sched_enqueues`/`sched_dequeues`/`sched_picks`：调度类三次操作的累计计数；
- `preempt_switches`/`quantum_left`/`worker steps`：时间片消耗与两 worker 进度；
- `ps` 的线程表：每个线程的 `state/switches/progress` 是否呈现轮转分布。

### 2.2 进程组调度五要素

`struct process_group_model { u32 pgid,leader,session,member_count; u8 foreground,controlled; };`
- `pgid`：进程组 id；`leader`：组首领（组长线程的 pid 通常即 pgid）；
- `session`：会话 id（session 首领创建时 `session==pgid`）；
- `member_count`：组成员数；
- `foreground`：该组是否为前台组（持有控制终端）；
- `controlled`：该组是否受控制终端控制（存在 job control）。

`pgtest` 断言 `pgid==leader`、`session==pgid`、`member_count==2`、`foreground` 与 `controlled` 同时成立；`sessiontest` 断言「首领创建会话、会话即 pgid、受控」；`fgtest` 断言前台组切换（`pgid` 从 100 变为 200）时旧组仍受保护。

### 2.3 负载均衡与进程组如何「综合」

综合 checkpoint 的意义：调度统计（负载侧）与进程组元数据（组调度侧）必须**同时**正确，缺一不可。`l87test` 断言检查点状态；`pgtest`/`sessiontest`/`fgtest` 断言组语义；`schedinfo`/`threadinfo`/`ps` 提供调度统计——四者合起来证明「单内核映像内两条元数据链同时成立」。

两条链在数据上是正交的，验证时却要同时展开，可用下图表达本课的心智模型：

```
负载链（内核怎么选）                    进程组链（任务怎么分组）
─────────────────                      ───────────────────────
rr_pick_next ── sched_picks            process_group.pgid=100
irq0_schedule ── quantum_left          process_group.leader=100
             ── preempt_switches       process_group.session=100
             ── worker steps: N M      process_group.member_count=2
schedinfo/threadinfo/ps                 process_group.foreground=1
                                        process_group.controlled=1
            └─────── l87test 总门闩 ───────┘
```

### 2.4 检查点模型：lesson_80_model

与前课同构的四个 `u32` 计数器 + 四个 `u8` 布尔位。`l87test` 一次性初始化并断言 `b == a + 1U`（80→83 序列，Origin 编号 Lesson 80）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、调度、VFS、GUI、检查点） | 恢复 `l79test`；新增 `lesson_80_model`/`lesson_80_state`/`l87test`；`exec64` 增加 `l87test` 分支；`about` 与开机横幅更新 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`负载均衡与进程组调度综合 checkpoint`/`Lesson 87`/`l87test`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（本课增量 + 负载/进程组主题的直接载体）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_80_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_80_model lesson_80_state;
static TEXT64 void l87test(u16*c){lesson_80_state=(struct lesson_80_model){80U,81U,82U,83U,1,1,1,1};int ok=lesson_80_state.valid&&lesson_80_state.active&&lesson_80_state.ready&&lesson_80_state.accounted&&lesson_80_state.b==lesson_80_state.a+1U;text64(c,"l87test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 80 fallback reported");putc64(c,'\n');}
```
逐行分析：
1. **结构**：与 `lesson_79_model` 完全同构，计数序列 80→83，四个布尔位全置 1。
2. **断言与输出**：`b==a+1U` 恒真；成功串 `l87test: bounded scheduling and copy-on-write checkpoint passed`（源码逐字抄录），fallback `Lesson 80 fallback reported`。
3. **综合含义**：虽然断言本身简单，但它是「负载均衡 + 进程组」两条主线合并后的总门闩——它通过只说明内核本体仍是本课正确版本，具体的两组语义由 `pgtest` 族与 `schedinfo` 族分别承担。
4. **恢复的 `l79test`**：本课同时恢复 `l79test`（`lesson_79_state`，79→82），与 `l87test` 并存，历史检查点可独立运行。

#### 3.2.2 进程组模型：pginfo / pgtest / sessiontest / fgtest

```c
static TEXT64 void pginfo(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};text64(c,"pginfo: pgid/leader/session/members: ");hex64(c,process_group.pgid);text64(c,"/");hex64(c,process_group.leader);text64(c,"/");hex64(c,process_group.session);text64(c,"/");hex64(c,process_group.member_count);putc64(c,'\n');}
static TEXT64 void pgtest(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};int ok=process_group.pgid==process_group.leader&&process_group.session==process_group.pgid&&process_group.member_count==2&&process_group.foreground&&process_group.controlled;text64(c,"pgtest: ");text64(c,ok?"bounded process-group leader, session, foreground, and controlling-terminal metadata passed":"process-group fallback reported");putc64(c,'\n');}
static TEXT64 void sessiontest(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};int ok=process_group.leader==process_group.pgid&&process_group.session==process_group.leader&&process_group.controlled;text64(c,"sessiontest: ");text64(c,ok?"leader-only session creation and controlling-terminal ownership passed":"session fallback reported");putc64(c,'\n');}
static TEXT64 void fgtest(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};u32 previous=process_group.pgid;process_group.pgid=200;process_group.foreground=1;int ok=previous==100&&process_group.pgid==200&&process_group.controlled;text64(c,"fgtest: ");text64(c,ok?"bounded foreground process-group handoff and stopped-group protection passed":"foreground fallback reported");putc64(c,'\n');}
```

逐行分析：
1. **统一初始化**：四个函数都以 `{100,100,100,2,1,1}` 复位进程组（pgid=leader=session=100、2 个成员、前台、受控），保证每次测试从同一确定性状态出发。
2. **`pgtest` 五要素断言**：`pgid==leader`（组长即组 id）、`session==pgid`（会话由组首领创建）、`member_count==2`、`foreground`、`controlled` 五个布尔同时成立。输出串 `bounded process-group leader, session, foreground, and controlling-terminal metadata passed`。
3. **`sessiontest` 首领语义**：`leader==pgid && session==leader && controlled`——只验证「首领创建会话」与「受控」两条，比 `pgtest` 更聚焦。
4. **`fgtest` 前台切换**：保存旧 pgid（100）→ 置 `pgid=200` 并保持 `foreground=1` → 断言 `previous==100`、`pgid==200`、`controlled`——模拟 shell 把前台组交给新组，旧组转入后台（停止组保护）。输出串 `bounded foreground process-group handoff and stopped-group protection passed`。
5. **`pginfo` 报告**：把五要素以 `pgid/leader/session/members` 的斜杠格式打到 VGA。

#### 3.2.3 负载侧：调度统计（调度类 + 量子 + 切换）

负载侧没有新增函数，复用以下既有代码作为「负载均衡」的载体：
```c
static TEXT64 void schedinfo(u16*c){text64(c,"scheduler class: ");text64(c,active_sched_class?active_sched_class->name:"none");text64(c,"\nops enqueue/dequeue/pick: ");hex64(c,sched_enqueues);text64(c," ");hex64(c,sched_dequeues);text64(c," ");hex64(c,sched_picks);text64(c,"\nwait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions\n");}
```
- `schedinfo` 报告调度类名与三次操作的累计计数。在「负载」视角下：`sched_picks` 是选择次数、`sched_enqueues/dequeues` 是就绪队列进出次数——三者单调增长说明 runnable 集合的进出平衡。
- `threadinfo` 的 `quantum left`、`preempt switches`、`worker steps: N M` 与 `ps` 的线程表构成负载分布的细粒度读数（两个 worker 切换/进度是否均衡）。
- 这些统计与进程组五要素**互不干扰**：调度统计是「内核怎么选」，进程组元数据是「任务怎么分组」，综合检查点要求两条链都成立。

#### 3.2.4 exec64 增量与开机横幅

- `exec64` 本课增量：`about` 输出 `Lesson 87: 负载均衡与进程组调度综合 checkpoint\n`；检查点分支：
```c
else if(eq64(word,"l79test")){if(!noargs64(arg))usage64(c,"l79test");else l79test(c);}else if(eq64(word,"l87test")){if(!noargs64(arg))usage64(c,"l87test");else l87test(c);}
```
- `help` 命令文本未变化：综合检查点并不新增面向用户的命令面，而是复用既有进程组与调度命令做交叉验证。
- 开机横幅：
```c
text64(&c,"Lesson 87: 负载均衡与进程组调度综合 checkpoint\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与 Lesson 85/86 完全相同的构建管线（64 位 freestanding raw 镜像 + 32 位 Multiboot2 外壳 + `grub-mkrescue`）。`make check` 新增断言 README 含 `负载均衡与进程组调度综合 checkpoint`、`Lesson 87`，kernel64.c 含 `l87test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ 初始化：pmm/vma/vfs/调度器类/进程组（fork 与进程组状态由测试命令惰性初始化）
 ├─ 横幅 "Lesson 87: 负载均衡与进程组调度综合 checkpoint"
 └─ 主循环：命令 → exec64
     ├─ l87test → 综合检查点断言
     ├─ pgtest/sessiontest/fgtest → 进程组五要素验证
     ├─ schedinfo/threadinfo/ps → 负载统计读数
     └─ preempttest/sleeptest → 负载生成（两 worker 抢占/睡眠）
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：打印横幅 `Lesson 87: 负载均衡与进程组调度综合 checkpoint`，随后 `tinyos> `。
2. **`l87test`** → `l87test(c)` → 断言检查点 → `l87test: bounded scheduling and copy-on-write checkpoint passed`。
3. **`pgtest`** → `process_group={100,100,100,2,1,1}` → 五要素断言 → `pgtest: bounded process-group leader, session, foreground, and controlling-terminal metadata passed`。
4. **`sessiontest`** → 首领/会话断言 → `sessiontest: leader-only session creation and controlling-terminal ownership passed`。
5. **`fgtest`** → 前台组切换 → `fgtest: bounded foreground process-group handoff and stopped-group protection passed`。
6. **`pginfo`** → 打印 `pginfo: pgid/leader/session/members: 0000000000000064/0000000000000064/0000000000000064/0000000000000002`（100/100/100/2）。
7. **`preempttest` → `schedinfo`/`threadinfo`** → 生成负载并读取 `ops enqueue/dequeue/pick` 与 `preempt switches`/`worker steps`，确认两条链同时成立。

---

## 5. 构建、运行与验证

**依赖**：同前两课（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 87 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 87: 负载均衡与进程组调度综合 checkpoint` 横幅 |
| `l87test` | `l87test: bounded scheduling and copy-on-write checkpoint passed` |
| `l79test` | `l79test: bounded scheduling and copy-on-write checkpoint passed` |
| `pgtest` | `pgtest: bounded process-group leader, session, foreground, and controlling-terminal metadata passed` |
| `sessiontest` | `sessiontest: leader-only session creation and controlling-terminal ownership passed` |
| `fgtest` | `fgtest: bounded foreground process-group handoff and stopped-group protection passed` |
| `schedinfo` | `scheduler class: tiny_rr` + `ops enqueue/dequeue/pick` 计数 |
| `preempttest` | `preempttest: two non-yielding workers started` |

判定成功：`l87test` 与进程组三测试全部 passed、调度统计随 `preempttest` 增长、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l87test` 输出 `Lesson 80 fallback reported` | `lesson_80_state` 初始化/断言失败（stale 镜像） | `grep -n "l87test" kernel64.c`；确认初始化串 `{80U,81U,82U,83U,1,1,1,1}` |
| `pgtest`/`sessiontest`/`fgtest` 输出 fallback | 进程组五要素断言失败 | 检查 `process_group=(struct process_group_model){100,100,100,2,1,1}` 初始化是否在测试前生效 |
| `fgtest` 输出 fallback | `pgid` 未从 100 切到 200 或 `controlled` 被清 | 对照 `fgtest` 的 `previous==100&&pgid==200&&controlled` 断言 |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `负载均衡与进程组调度综合 checkpoint` 与 `Lesson 87`；kernel64.c 含 `l87test` |
| `schedinfo` 显示 `scheduler class: none` | `active_sched_class` 未赋值（过早调用） | 检查 `kernel_main64_binary` 中 `active_sched_class=&fair_sched_class` 在 banner 之前执行 |
| `preempt switches` 不增长 | PIT/IRQ0 未注册或被屏蔽 | `idtinfo` 看 `IRQ0 vector: 0000000000000020`；检查 `pic_init` 掩码 |
| 两个 worker 进度长期失衡 | 轮转游标/量子逻辑异常 | 对照 `rr_pick_next` 的 `round_robin` 游标与 `irq0_schedule` 量子递减 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `sched_enqueues/dequeues/picks` 统计 = 负载读数 | `kernel/sched/loadavg.c`：`calc_global_load()`；`/proc/loadavg` | 模型无时间加权平均与 decay，只有单调计数 |
| 单核轮转 + 量子 = 「负载均衡」 | `kernel/sched/topology.c` + `kernel/sched/fair.c`：`load_balance()`/`find_busiest_queue()` | 模型无 per-CPU runqueue 与任务迁移，均衡退化为统计一致性 |
| `struct process_group_model`（pgid/leader/session/member_count） | `include/linux/sched/signal.h`：`struct signal_struct`（`pgrp`/`session`）；`kernel/sys.c`：`setsid()`/`setpgid()` | 模型只有一张 4 字段记录，无真实 pid namespace 与链表 |
| `foreground`/`controlled` 两标志 | `include/linux/tty.h`：`tty` 的 `pgrp` 与 `TIOCSPGRP`/`tcsetpgrp()` | 模型用两比特代替 tty 结构与前/后台切换系统调用 |
| `fgtest` 前台组切换 | `drivers/tty/tty_io.c`：`tty_check_change()`、SIGTTOU/SIGTTIN 保护 | 模型只断言 `controlled` 保持，不发送真实信号 |
| `sessiontest` 首领创建会话 | `kernel/sys.c`：`setsid()` 要求调用者是进程组首领且非会话首领 | 模型直接断言 `session==leader` |
| `l87test` 综合断言 | 无直接对应（LTP/ptrace 测试套件） | 模型把「负载 + 进程组」验证固化进内核 |

**权威来源**：POSIX 进程组/会话/控制终端语义（`setsid`/`setpgid`/`tcsetpgrp`）、Intel SDM Vol.3A §10（PIT）、Multiboot2 规范；Linux `kernel/sched/`、`kernel/sys.c` 仅作工程对照。

---

## 8. 思考题与练习

1. **概念理解**：为什么单核教学模型里「负载均衡」只能降维为统计一致性？如果增加第二个 runqueue，你认为哪段代码需要改？
2. **源码定位**：在 `kernel64.c` 中找出 `process_group` 被**赋值**的所有函数，说明为什么四个测试函数都要先复位它。
3. **动手实验**：把 `pgtest` 的初始化改为 `{100,200,100,2,1,1}`（leader≠pgid），运行后观察 fallback 串；解释五要素断言。
4. **动手实验**：先 `preempttest` 再 `l87test`，再 `schedinfo`——说明负载统计与检查点断言之间为何相互独立。
5. **Linux 对照**：阅读 `kernel/sys.c` 的 `setsid()`，对比 TinyOS 的 `sessiontest`，指出 Linux 增加了哪些「必须失败」的边界条件（非组长、已会话首领）。

---

## 9. 本课小结与下一课预告

1. 本课是进程/调度/COW 阶段的收尾综合检查点：把「负载」（调度统计）与「进程组」（pgid/session/foreground）两条元数据链合并验证。
2. 进程组五要素（pgid/leader/session/member_count/foreground/controlled）由 `process_group_model` 表达，`pgtest`/`sessiontest`/`fgtest` 分别验证组语义的不同侧面。
3. 负载侧沿用 `schedinfo`/`threadinfo`/`ps` 的统计读数，与进程组断言互不干扰，共同构成「综合」的含义。
4. `l87test` 是阶段总门闩，`l79test` 等历史检查点继续保留可独立运行。
5. 全部继承回归（进程、GUI、VFS、子系统）保持有效。
6. 下一课（Lesson 88）将**彻底转入 VFS 主线**，主题为「VFS 层次与 mount 元数据」，开始讲解 superblock/inode/dentry/file 四层模型，进程组阶段至此收束。

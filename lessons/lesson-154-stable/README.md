# Lesson 154: cgroup CPU 统计 — 精讲文档

> **课号**：Lesson 154 ｜ **主题**：cgroup CPU 统计（cgroup CPU accounting）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课为 **cgroup 系列四连**（层级 153 → **CPU 统计 154** → 内存限制 155 → 设备策略 156）的第二课
> **前置课程**：[`../lesson-153-stable/README.md`](../lesson-153-stable/README.md)（cgroup 层级）
> **后续课程**：[`../lesson-155-stable/README.md`](../lesson-155-stable/README.md)（cgroup 内存限制）
> **一句话目标**：讲清 cgroup 的 CPU 控制器如何计量与限额——`cpu.max`（配额/周期）、`cpu.weight`（权重）、`cpuacct.usage`（累计 CPU 时间），对照 Linux `kernel/sched/core.c`、`kernel/cgroup/cpu.c`、`kernel/sched/cputime.c`，并把教学内核中继承的**CPU 计量设施**（PIT `ticks` 计数器、`irq0_schedule` 的每次 tick 记账、`clock_model` 的 tick→ns 换算、`timer_model`/`sleep_model` 的 deadline 计量、`softirq_model` 的预算计数）按 cgroup CPU 统计主题系统化复述，运行 `l154test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（cgroup CPU 控制器）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l153test` 恢复为历史命名 `l146test`（挂 `lesson_146_state`），新增 `lesson_147_model`/`lesson_147_state` 与 `l154test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l154test`（旧 README 所写 `l147test` 按源码勘误）；另保留历史检查点 `l100test`–`l146test`，以及 `tickinfo`/`uptime`/`clockinfo`/`clocktest`/`timerinfo`/`timertest`/`sleeptimetest`/`schedinfo`/`softirqinfo`/`softirqtest` 等 CPU 计量回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「时间片 = 记账单位」解释 CPU 统计（每个 tick 把 CPU 时间记到谁头上）；说出 cgroup 的 CPU 控制器三件套（`cpu.max` 配额、`cpu.weight` 权重、`cpuacct.usage` 累计量）及对照的 Linux 文件（`kernel/cgroup/cpu.c`、`kernel/sched/cputime.c`）；在教学内核中沿 `ticks` → `irq0_schedule` → `clock_update` → `timer_poll` → `softirq_run_budget` 观察「每次 tick 如何被计量并分配到对象上」；运行 `l154test`/`clocktest`/`timertest` 验证。

**在课程主线中的位置**：Lesson 153 讲完 cgroup 层级（树形结构），本课在树上挂第一个控制器——**CPU 计量**：层级决定「谁在哪个组」，计量决定「组里用了多少 CPU」。作为检查点课，源码 diff 极小，任务是把继承机制中与「CPU 时间计量」相关的设施（PIT tick、调度记账、时钟/定时器/软中断的各类计数器）按 cgroup CPU 统计主题系统化复述。下一课（Lesson 155）讲 cgroup 内存限制。

**前置知识清单**（学本课前必须掌握）：
1. PIT 与 tick：`PIT_RATE_HZ 100`、`PIT_DIVISOR 11932`、`static volatile u64 ticks`（Lesson 22s/50s）。
2. 调度记账：`irq0_schedule` 每次 IRQ0 的 `ticks++`、`quantum_left`/`TIME_SLICE_TICKS`、`preempt_switches`/`idle_worker_ticks`（Lesson 50s/59s）。
3. 时钟模型：`clock_model{monotonic_ticks,monotonic_ns,realtime_ns,reads}` 与 `clock_update` 的 tick→ns 换算（Lesson 40s）。
4. 定时器/睡眠模型：`timer_arm`/`timer_poll`/`timer_read`/`timer_cancel`、`sleep_model{requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes}`（Lesson 40s）。
5. 软中断预算：`softirq_model{raises,runs,drops,budget_exhaustions}` 与 `SOFTIRQ_BUDGET`（Lesson 50s）。
6. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–153）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 154: cgroup CPU 统计`；
- 新命令 `l154test` 输出 `l154test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `tickinfo`/`clockinfo`/`timerinfo`/`softirqinfo` 继续展示 CPU 计量元数据。

---

## 2. 核心概念精讲

### 2.1 cgroup CPU 统计：把时间片记到组头上

**直觉**：内核每过一个 tick（时钟中断一次），就回答一个问题：「刚过去的这个 tick，CPU 花在哪个任务上？」任务属于哪个 cgroup，这个 tick 就记到那个组。累计起来，`cpuacct.usage` 就是该组（含后代）用掉的 CPU 纳秒数。

```
每个 IRQ0 tick
   └─ 查当前任务 → 任务所属 cgroup → 该组 usage += 一个 tick
   └─ 若任务超出 cpu.max 配额 → 限流（throttle）
   └─ cpu.weight 决定多个竞争组之间的时间片分配比例
```

**准确定义**：cgroup CPU 统计 = **CPU 时间的计量（accounting）+ 分配（scheduling）+ 限制（throttling）** 三件事在 cgroup 层级上的落点。计量是基础：没有「每个组用了多少 CPU」的账本，权重与配额都无从谈起。

### 2.2 为什么需要 cgroup CPU 统计（动机）

1. **配额（cpu.max）**：`100000 200000` 表示「每 200ms 周期内最多用 100ms」——防止容器吃掉整机 CPU；超过配额则 throttle 该组。
2. **权重（cpu.weight）**：`cpu.weight` 决定 CPU 繁忙时组间按权重分时间片（CFS `sched_entity` 的 `load`），实现公平竞争而非平均分配。
3. **计量（cpuacct.usage）**：没有计量就没有「这容器到底用了多少」——计费、监控、`docker stats` 都靠它。

### 2.3 Linux 中 cgroup CPU 统计的工作机制

- **计量**：`kernel/sched/cputime.c` 的 `account_user_time`/`account_system_time` 在每次 tick 把 CPU 时间记到 `struct task_struct` 的 `utime`/`stime`；`kernel/cgroup/cpuacct.c`（`CONFIG_CGROUP_CPUACCT`）按 cgroup 聚合出 `cpuacct.usage`（单位纳秒）。
- **配额与权重**：`kernel/cgroup/cpu.c`（cpu 控制器）提供 `cpu.max`（quota/period）与 `cpu.weight`；`kernel/sched/fair.c` 的 CFS 调度实体通过 `sched_entity->load` 实现权重分配，quota 耗尽走 `throttle_cfs_rq`。
- **统计视图**：`/sys/fs/cgroup/cpu.stat` 输出 `usage_usec`/`user_usec`/`system_usec`/`nr_periods`/`nr_throttled`/`throttled_usec`。
- **教学简化**：教学内核没有 cgroup cpu 控制器，但「每次 tick 计量一次 CPU 使用」的记账逻辑完整存在于 `irq0_schedule`：`ticks++`、时间片递减、各种切换计数——这正是一个「单组、无配额」的 CPU 计量模型。

### 2.4 教学内核中与「CPU 计量」有关的既有设施

本课主题机制（cgroup CPU 控制器）**未在源码中实现**，但「CPU 时间计量」素材完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| tick 计数器 | `static volatile u64 ticks`，`irq0_schedule` 里 `ticks++` | 计量单位 = 一次 PIT 中断（对照 cpuacct 的 ns 计量） |
| 时间片记账 | `quantum_left`/`TIME_SLICE_TICKS`、`if(quantum_left){quantum_left--;...}` | 任务级配额（对照 cpu.max 的 quota） |
| 切换计数 | `preempt_switches`、`idle_switches`、`idle_ticks`、`idle_worker_ticks` | 调度的统计账本 |
| 时钟换算 | `clock_update`：`monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ)` | tick → ns 的单位换算（对照 cpuacct.usage 的纳秒） |
| 定时器计量 | `timer_model{deadline_tick,interval_ticks,expirations,arms,reads}` | deadline 式 CPU 时间计量 |
| 睡眠计量 | `sleep_model{requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes}` | 阻塞期间的唤醒记账 |
| 软中断预算 | `softirq_model{raises,runs,drops,budget_exhaustions}`、`SOFTIRQ_BUDGET` | 每 tick 的处理预算（对照 CPU 限额） |
| 调度统计 | `sched_enqueues/sched_dequeues/sched_picks` | 调度操作的计数 |

### 2.5 检查点模型：lesson_147_model 与 l154test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `147→150` 标记 Origin 为 Lesson 147（`a=147,b=148,c=149,d=150`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「计量连续性」。本课同时把上一课新增的 `l153test` 恢复为历史命名 `l146test`（挂 `lesson_146_state`，计数 `146→149`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.6 机制继承 + 检查点增量

本课主题机制（cgroup CPU 计量与限额）**不是本课新写的代码**：tick 计数器与调度记账来自调度阶段，时钟/定时器/睡眠/软中断模型来自更早阶段。本课实际增量只有三处：`l153test`→`l146test` 更名、`lesson_147_model`+`l154test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「CPU 计量」主题重新组织，并如实说明：**cgroup CPU 控制器（`cpu.max`/`cpu.weight`/`cpuacct.usage` 式对象）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l153test`→`l146test` 恢复命名；新增 `lesson_147_model`/`lesson_147_state`/`l154test`；`about` 与开机横幅更新。cgroup CPU 统计主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`cgroup CPU 统计`/`l154test`/`Lesson 154`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（CPU 计量机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_147_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_147_model lesson_147_state;
static TEXT64 void l154test(u16*c){lesson_147_state=(struct lesson_147_model){147U,148U,149U,150U,1,1,1,1};int ok=lesson_147_state.valid&&lesson_147_state.active&&lesson_147_state.ready&&lesson_147_state.accounted&&lesson_147_state.b==lesson_147_state.a+1U;text64(c,"l154test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 147 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `147→150`（Origin Lesson 147），四布尔位全置 1，`b==a+1U` 校验计数连续。
2. **逻辑分析（≥3 行）**：结构体字面量一次性写入 `lesson_147_state`，`ok` 由四布尔位 + `b==a+1U` 合取；字面量全 1 使断言恒真，成功串必输出；`Lesson 147 fallback reported` 是防御性兜底，仅在模型计数被破坏时命中。
3. **输出串（逐字抄录）**：成功 `l154test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 147 fallback reported`。
4. **恢复的 `l146test`**：本课把上一课 `l153test` 更名回 `l146test`（同为 `lesson_146_state`，计数 `146→149`）；`l100test`–`l145test` 历史检查点全部保留。

#### 3.2.2 计量源头：ticks 与 irq0_schedule

```c
static volatile u64 ticks;
```

1. **计量单位**：`ticks` 是 PIT 通道 0 的中断次数（`PIT_RATE_HZ 100`，即每秒 100 次），全内核唯一的「CPU 时间钟摆」——对应 cgroup cpuacct 的纳秒计量，只是单位从 ns 换成 tick。
2. **volatile**：IRQ0 中断上下文写、shell 读，`volatile` 防止编译器缓存优化。
3. **读法**：`tickinfo` 以 `cli`/`sti` 夹住 `t=ticks` 原子读——计量读出的「快照」不能与中断写交错。

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;ticks++;softirq_run_budget();outb64(PIC1_COMMAND,PIC_EOI);if(f&&f->cs==USER_CS){user_irq0_save_restore(f);return f;}if(idle_running){idle_frame=f;idle_ticks++;}else threads[current_thread].frame=(u64)(unsigned long)f;wake_sleepers();reap_finished_threads();if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}old=current_thread;next=next_runnable();quantum_left=TIME_SLICE_TICKS;if(next==0xff){if(idle_running)return f;if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}idle_running=1;idle_switches++;return idle_frame;}if(idle_running){idle_running=0;threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}if(next==old){if(old==0)idle_worker_ticks++;return f;}if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}rr_dequeue(next);threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}
```

1. **每次 tick 的记账序列**：`ticks++`（计量推进）→ `softirq_run_budget()`（软中断消耗本 tick 的处理预算）→ `outb64(PIC1_COMMAND,PIC_EOI)`（发 EOI）——每次中断先记账再处理。
2. **时间片配额**：`if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}`——任务未用完 `TIME_SLICE_TICKS` 就继续运行，用完才进入选下一个（对照 `cpu.max` 的 quota 语义：配额内运行、超配额换人）。
3. **切换计量**：`preempt_switches++`（抢占次数）、`idle_ticks++`（idle 占用 tick）、`idle_worker_ticks++`（shell 保持运行的时间片）——每次切换都被计入统计，正是「tick 记到谁头上」的账本。

#### 3.2.3 计量换算：clock_update（tick → ns）

```c
static TEXT64 void clock_update(void){clock_model.monotonic_ticks=ticks;clock_model.monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ);clock_model.realtime_ns=clock_model.monotonic_ns;}
```

1. **单位换算**：`monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ)`——每 tick 折合 10ms（`100Hz`），把 tick 计数换算成纳秒——正是 cgroup `cpuacct.usage` 的计量单位（ns）。
2. **单调时钟**：`monotonic_ticks` 直接取自 `ticks`，只增不减——对应 Linux 的 CLOCK_MONOTONIC。
3. **realtime 简化**：`realtime_ns=monotonic_ns`——教学模型省略 wall clock 与 NTP 校正，只保留单调语义。
4. **验证**：`clocktest` 断言 `b==ticks*(1000000000ULL/PIT_RATE_HZ)`，成功串 `clocktest: monotonic PIT clock conversion passed`——计量换算的确定性回归。

#### 3.2.4 deadline 式计量：timer_model 与 sleep_model

```c
struct timer_model { u64 deadline_tick,interval_ticks,expirations,arms,reads; u8 armed,readable,periodic,canceled; };
struct sleep_model { u64 requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes; u8 active,interrupted; };
```

1. **timer_model 的 CPU 计量**：`deadline_tick`（到期 tick）、`interval_ticks`（周期）、`expirations`（到期次数）——「过了多少个 tick」的计量；`timertest` 成功串 `timertest: deadline, expiration, periodic, and cancel passed`。
2. **sleep_model 的唤醒记账**：`requested_ticks`/`deadline_tick`/`remaining_ticks`/`wake_tick`——阻塞期按 tick 计量剩余时间；`sleeptimetest` 成功串 `sleeptimetest: deadline sleep and wake accounting passed`。
3. **共同点**：两者都以 `ticks` 为参照系做 deadline 判断——与 cgroup CPU 控制器一样，计量都锚定在同一个时钟源上。

#### 3.2.5 预算计量：softirq_model

```c
struct softirq_model { u8 pending; u64 raises,runs,drops,budget_exhaustions; };
```

1. **处理预算**：`softirq_run_budget` 每次 tick 最多跑 `SOFTIRQ_BUDGET`（2）次软中断，超预算记 `budget_exhaustions`——「每个 tick 的 CPU 处理配额」，与 cgroup 的 `cpu.max` 限额同构。
2. **计量字段**：`raises/runs/drops/budget_exhaustions` 分别统计触发、执行、丢弃、预算耗尽——一套完整的「CPU 处理量」账本。
3. **观察**：`softirqinfo` 输出 `softirq pending/raises/runs/drops/budget: ...`；`softirqtest` 成功串 `softirqtest: tasklet coalescing, FIFO work, and budget carry-over passed`。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 154: cgroup CPU 统计\n`；检查点分支：
```c
else if(eq64(word,"l146test")){if(!noargs64(arg))usage64(c,"l146test");else l146test(c);}else if(eq64(word,"l154test")){if(!noargs64(arg))usage64(c,"l154test");else l154test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 154: cgroup CPU 统计\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```
`GETTICKS`（`SYS_GETTICKS 0U`）正是「用户态读 CPU 计量」的系统调用——cgroup CPU 统计面向应用的最小接口。

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` → `kernel64.ld` → `objcopy` → `boot.S` 嵌入 → `grub-mkrescue`）。`make check` 断言 README 含 `cgroup CPU 统计`、`Lesson 154`，kernel64.c 含 `l154test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pit_init()（PIT 100Hz 计时起点）/ 横幅 "Lesson 154: cgroup CPU 统计"
 └─ 主循环：命令 → exec64
     ├─ l154test / l146test → 阶段检查点（lesson_147_state / lesson_146_state）
     ├─ tickinfo / uptime → ticks 快照与开机时长（centiseconds）
     ├─ clockinfo / clocktest → tick→ns 换算与单调时钟断言
     ├─ timerinfo / timertest → deadline/periodic/cancel 计量
     ├─ sleeptimetest → 唤醒记账（wake_tick/remaining_ticks）
     ├─ schedinfo → enqueue/dequeue/pick 计数
     └─ softirqinfo / softirqtest → 软中断处理预算计量
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`pit_init()` 启动 PIT，`ticks` 开始按 100Hz 递增，打印横幅 `Lesson 154: cgroup CPU 统计`。
2. **每次 IRQ0**：`irq0_schedule` 执行 `ticks++` → `softirq_run_budget()` → 时间片递减 → 需要时切换并 `preempt_switches++`。
3. **`l154test`** → `l154test(c)` → 初始化 `lesson_147_state` → 五条件断言 → `l154test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`clockinfo`** → `clock_update` 换算 → `clock ticks/ns: <t>/<t*10ms> reads: N PIT Hz: 64`（PIT Hz 打印为 16 进制 0x64=100）。
5. **`timertest`** → `timer_arm(2,0)` → 推进 `ticks` → `timer_poll` → `timer_read` → 周期定时 → `timer_cancel` → `timertest: deadline, expiration, periodic, and cancel passed`。
6. **`about`** → `Lesson 154: cgroup CPU 统计`。

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
Multiboot2 and Lesson 154 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 154: cgroup CPU 统计` 横幅 |
| `l154test` | `l154test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l146test` | `l146test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `clocktest` | `clocktest: monotonic PIT clock conversion passed` |
| `timertest` | `timertest: deadline, expiration, periodic, and cancel passed` |
| `sleeptimetest` | `sleeptimetest: deadline sleep and wake accounting passed` |
| `tickinfo` | `PIT channel 0: 0000000000000064 Hz` 与 `ticks: <t>`、`uptime (centiseconds): <t>` 行 |
| `about` | `Lesson 154: cgroup CPU 统计` |

判定成功：`l154test`/`clocktest`/`timertest`/`sleeptimetest` 全部 passed、无 fallback/`BROKEN`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l154test` 输出 `Lesson 147 fallback reported` | `lesson_147_state` 初始化/断言失败（stale 镜像） | `grep -n "l154test" kernel64.c`；确认初始化串 `{147U,148U,149U,150U,1,1,1,1}` 与 `b==a+1U` |
| `clocktest` 输出 `BROKEN` | tick→ns 换算与 `PIT_RATE_HZ` 不符 | 对照 `clock_update`：`monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ)` |
| `timertest` 输出 `BROKEN` | `timer_poll` 的 deadline 判断异常 | 对照 `timer_poll` 与 `timer_model.deadline_tick`；先跑 `timerinfo` 看 armed/readable/expirations |
| `tickinfo` 的 ticks 不增长 | IRQ0 未触发或 EOI 缺失 | 确认 `pit_init()` 与 `irq0_schedule` 的 `outb64(PIC1_COMMAND,PIC_EOI)` |
| `sleeptimetest` 输出 `BROKEN` | `remaining_ticks`/`wake_tick` 记账错误 | 对照 `sleep_model` 的 `deadline_tick-ticks` 与 `wakes++` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 154' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `cgroup CPU 统计` 与 `Lesson 154` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `ticks` + `ticks++`（每次 IRQ0） | `kernel/sched/core.c` 的 `scheduler_tick()`；`kernel/sched/cputime.c` 的 `account_process_tick` | 模型只有一个全局 tick 计数器，无 per-task `utime/stime` 与 `cputime` 结构 |
| `quantum_left` 时间片递减 | `kernel/sched/core.c` 的 CFS `vruntime`/`sched_slice`；cpu 控制器 `cpu.max` 的 quota/period | 模型是固定 2-tick 轮转，无公平调度与周期限额 |
| `preempt_switches`/`idle_ticks` 计数 | `kernel/sched/cputime.c` 的 `account_user_time`/`account_system_time`；`/proc/stat` | 模型只累计整数计数，无用户/系统态细分 |
| `clock_update` 的 tick→ns | `kernel/cgroup/cpuacct.c` 的 `cpuacct.usage`（ns 累计）；`kernel/time/timekeeping.c` | 模型用固定 `1000000000ULL/PIT_RATE_HZ` 换算，无 NTP/timer_list |
| `timer_model.deadline_tick` | `kernel/time/hrtimer.c` 的高精度定时器；`cpuacct` 的 `usage_usec` | 模型以 tick 为粒度，无纳秒级 hrtimer |
| `SOFTIRQ_BUDGET` 处理预算 | `kernel/softirq.c` 的 `__do_softirq` 预算循环；cpu 控制器 quota throttle | 模型无 `pending` 位图细节与中断嵌套，只计数 |
| `schedinfo` 的 enqueue/dequeue/pick | `kernel/sched/fair.c` 的 `enqueue_entity`/`dequeue_entity`；`kernel/cgroup/cpu.c` 的 `cpu.weight` | 模型无 `sched_entity` 与 `load` 权重 |

**权威来源**：Linux `kernel/sched/core.c`、`kernel/sched/cputime.c`、`kernel/cgroup/cpu.c`、`kernel/cgroup/cpuacct.c`、`kernel/softirq.c` 为对照；Intel SDM 的 8254 PIT 规范与 Multiboot2 规范仍为硬件/引导权威来源。

**如实说明**：本课**没有** cpu 控制器（`cpu.max`/`cpu.weight`/`cpuacct.usage`）的等价实现——cgroup CPU 统计是「主题宣告」，教学内核停留在「单组、每次 tick 全局计量」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么计量（accounting）是配额与权重的前提？`cpu.max` 的 quota/period 与 `quantum_left` 的时间片有何异同？
2. **源码定位**：在 `kernel64.c` 中找出 `ticks++` 的全部执行点（提示：`irq0_schedule`、`timer_poll` 相关测试），并说明哪些函数直接读 `ticks`。
3. **动手实验**：把 `irq0_schedule` 的 `TIME_SLICE_TICKS` 从 2 改为 4，运行 `schedinfo`/`ps` 观察 `preempt_switches` 与线程切换频率变化，说明时间片对计量统计的影响。
4. **动手实验**：仿照 `clock_update`，给 `clock_model` 增加 `usage_ns` 字段（模拟 `cpuacct.usage`），每次 `clock_update` 时累加，并在 `clockinfo` 打印。
5. **Linux 对照**：阅读 `kernel/cgroup/cpuacct.c`，说明 `cpuacct.usage` 是如何按 cgroup 聚合的；对比教学模型「全局单组 ticks」的简化。

---

## 9. 本课小结与下一课预告

1. cgroup CPU 统计 = 计量 + 分配 + 限制三件事在层级上的落点，计量是基础。
2. Linux 用 `account_process_tick`（`kernel/sched/cputime.c`）记时间、`cpuacct` 聚合纳秒、`cpu.max`/`cpu.weight` 做配额与权重。
3. 教学内核的计量素材完整：`ticks`（PIT 计数）、`irq0_schedule` 的每次 tick 记账、`clock_update` 的 tick→ns 换算、`timer_model`/`sleep_model` 的 deadline 计量、`softirq_model` 的预算计数。
4. `quantum_left` 时间片是「任务级配额」的教学对应，`SOFTIRQ_BUDGET` 是「每 tick 处理限额」的教学对应。
5. 检查点增量：`l153test`→`l146test` 更名、新增 `lesson_147_model`+`l154test`、横幅与 `about` 更新为 `Lesson 154: cgroup CPU 统计`。
6. 下一课（Lesson 155）主题为 **cgroup 内存限制**（对照 `mm/memcontrol.c` 的 memory 控制器）：从 CPU 计量转到内存账本，教学内核将以 PMM 统计（`pmm_total/pmm_free/pmm_used`）与回收模型承接该主题。

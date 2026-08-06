# Lesson 48: Linux 风格时间系统、timerfd-like 模型、睡眠与时钟抽象 — 精讲文档

> **课号**：Lesson 48（可执行课）
> **主题**：Linux 风格时间系统（clock）、timerfd-like 模型、睡眠（sleep）
> 与时钟抽象
> **课程主线位置**：第 6 阶段「Linux 风格 I/O 与文件抽象」收官课。前课（47）
> 完成信号与用户态返回；本课把 PIT 的 `ticks` 计数升华为三套可观察的
> 教学模型：单调时钟换算、`fs/timerfd.c` 风格的可读性翻转定时器、
> 基于截止点的睡眠记账。
> **前置课程**：[`lesson-47-stable/README.md`](../lesson-47-stable/README.md)
> **后续课程**：第 6 阶段结束；可在 `lessons/` 下按编号继续学习后续课程。
> **一句话目标**：能讲清 Linux 为什么把「时间」拆成单调时钟/实时时钟、
> timerfd 为什么是「到期就变可读的文件描述符」、睡眠为什么按截止点
> 记账而不是「睡多少 tick」，并在 TinyOS 里复刻全部**判定与记账**——
> 不新增真实 fd/系统调用，不动用户指针。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——Linux `kernel/time/timekeeping.c`
维护单调/实时两套时钟；`fs/timerfd.c` 把定时器包装成 fd，到期后
`poll` 变可读、`read` 读出到期次数；`kernel/time/sleep_timeout.c` 的
`schedule_timeout` 按绝对截止点唤醒。TinyOS 用
`clock_model`/`timer_model`/`sleep_model` 三个结构 + 六个函数实现
「ticks → 时间 → 定时 → 睡眠」的时间子系统教学切片。

- **在课程主线中的位置**：47 的「事件 → 通知」在这里换成「时间 → 到期」。
  48 收官后，第 6 阶段的 I/O 抽象（页、VFS、路径、管道、信号、时间）
  全部以「固定容量元数据 + 确定性记账」的教学模型完成。
- **前置知识清单**：
  1. PIT 100 Hz 中断源（`PIT_RATE_HZ=100`）与全局 `ticks` 计数
     （lesson-27/28 起，IRQ0 每 10 ms 递增一次）；
  2. lesson-43 的 `tick_due(now, deadline)`（无符号回绕安全比较）；
  3. lesson-46 的 `poll`「可读性」语义——timerfd 的可读性与管道同构；
  4. Linux 时钟词汇：`CLOCK_MONOTONIC`/`CLOCK_REALTIME`、timerfd、
     `sleep_timeout`。
- **本课交付**：`clockinfo`/`clocktest`/`timerinfo`/`timertest`/
  `sleeptimetest` 五条命令；`clock_model`/`timer_model`/`sleep_model`
  三个结构；`clock_update`/`timer_poll`/`timer_arm`/`timer_read`/
  `timer_cancel` 五个函数。

---

## 2. 核心概念精讲

### 2.1 概念一：单调时钟与实时时钟

**定义**：`CLOCK_MONOTONIC` 只增不减、不受 `date` 调整影响，适合测量
间隔；`CLOCK_REALTIME` 是墙上时钟，可被 NTP 拨动。**为什么需要**：
「过了多久」与「现在几点」是两种问题，混用一个时钟会导致定时器错乱。
**TinyOS 对应**：`clock_model.monotonic_ticks=ticks`、
`monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ)`（100 Hz 每 tick
10,000,000 ns），`realtime_ns=monotonic_ns`（无 RTC 校正）。

### 2.2 概念二：timerfd——定时器伪装成文件

**定义**：timerfd 是 Linux 把定时器包装成文件描述符的机制：
`timerfd_create` 得一个 fd，`timerfd_settime` 设定一次性/周期截止，
到期后该 fd 在 `poll` 里变可读，`read` 返回到期次数。**为什么需要**：
进程已有统一的「可读 fd」事件循环，定时器混进来无需新机制。
**TinyOS 对应**：`timer_model` 即「timerfd 元数据」——`deadline_tick`
（绝对截止 tick）、`interval_ticks`（周期间隔，0=一次性）、`armed`、
`readable`（到期翻转，对应 poll 可读）、`periodic`、`canceled`；
`timer_poll` 每 tick 检查截止，`timer_read` 读出并清零到期次数。

### 2.3 概念三：到期即“可读”——poll 语义复用

**定义**：timerfd 的 `readable` 位和管道「有数据」一样，都是 poll 的
就绪条件：到期前 `poll` 不可读，到期后 `poll` 可读，读走计数后又
不可读。**为什么需要**：这正是 §2.2「定时器伪装成文件」能成立的
关键——可读性抽象与数据内容解耦。**TinyOS 对应**：`timer_poll`
到期时 `expirations++; readable=1`；`timer_read` 返回
`readable?expirations:0` 后清零两者——「可读→读走→复位」状态机，
与 lesson-46 管道/`POLLIN` 同一套直觉。

### 2.4 概念四：睡眠——按绝对截止点记账

**定义**：`schedule_timeout` 不是「睡 n tick」，而是算出
`expires = jiffies + n` 后把线程挂到定时器，到期唤醒——**用绝对
截止点**避免多睡/少睡的累计误差。**TinyOS 对应**：
`sleep_model` 记录 `deadline_tick=old+delay`、`remaining_ticks`、
`active`；`sleeptimetest` 演示「截止前 remaining>0 且 active、截止时
归零并唤醒」两个判定；真实唤醒路径是 lesson-38 的 `wake_sleepers`
（`tick_due(ticks, wake_tick)`）。

### 2.5 概念五：教学模型的“可控时间”

**为什么测试要直接改 `ticks`**：QEMU 里 100 Hz 中断会让 `ticks` 不断
增长，测试无法等待精确的 tick 数。因此 `timertest`/`sleeptimetest`
保存 `old=ticks` 后**直接赋值推进**（`ticks=old+2` 等），把时钟当可控
变量使用——代价是测试结束后全局 `ticks` 已被改写（见 §6）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-47） |
|---|---|---|
| `boot.S` | Multiboot2 引导、进入 long mode | 未变化 |
| `kernel.c` | 32 位入口、低内存页表、user image 装载 | 未变化 |
| `kernel64.c` | 64 位内核主体（累积） | **核心**：三个 `*_model` 结构 + `clock_update`/`timer_poll`/`timer_arm`/`timer_read`/`timer_cancel`/`clockinfo`/`clocktest`/`timerinfo`/`timertest`/`sleeptimetest` |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `clock`、`timer`、`sleep`；kernel64.c 含 `clockinfo`、`clocktest`、`timerinfo`、`timertest`、`sleeptimetest` |
| `grub.cfg` | 装载 | **menuentry 标题更新**为 `TinyOS lesson 48: clock, timerfd-like model, and sleep` |

### 3.2 结构 / 全局变量精讲

```c
struct clock_model { u64 monotonic_ticks,monotonic_ns,realtime_ns,reads; };
struct timer_model { u64 deadline_tick,interval_ticks,expirations,arms,reads; u8 armed,readable,periodic,canceled; };
struct sleep_model { u64 requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes; u8 active,interrupted; };
static struct clock_model clock_model;
static struct timer_model timer_model;
static struct sleep_model sleep_model;
```

逐行注释：
- `clock_model`：单调/实时两套读数 + 读取计数。`monotonic_ns` 由
  `ticks*(1000000000/PIT_RATE_HZ)` 导出，不做累加、无漂移——对应 Linux
  `jiffies_to_nsecs`/`ktime_get_mono_fast_ns`；
- `timer_model`：timerfd-like 对象——`deadline_tick` 绝对截止、
  `interval_ticks` 周期（0=一次性）、`expirations` 到期累计、
  `armed`/`readable`/`periodic`/`canceled` 四个状态位；
- `sleep_model`：一次睡眠档案——`requested_ticks` 请求时长、
  `deadline_tick` 绝对唤醒点、`wake_tick` 实际唤醒 tick、
  `remaining_ticks` 剩余、`sleeps`/`wakes` 计数、`active`/`interrupted`
  状态位；三个全局单实例 boot 时零初始化（`.bss`），无需 init 函数。

### 3.3 函数精讲：clock_update —— 从 ticks 派生时钟读数

```c
static TEXT64 void clock_update(void){clock_model.monotonic_ticks=ticks;
clock_model.monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ);
clock_model.realtime_ns=clock_model.monotonic_ns;}
```

逐行分析：
- **tick 快照**：`monotonic_ticks=ticks` 直接取全局 IRQ0 计数；
- **纳秒换算**：`ticks*(1000000000ULL/PIT_RATE_HZ)`——PIT 100 Hz 时
  每 tick 10,000,000 ns。用**预计算除数**而非 `ticks*10^9/100`，
  避免 64 位中间值截断；
- **实时时钟**：`realtime_ns=monotonic_ns`——本课不接 RTC/HPET，
  实时即单调（Linux `ktime_get_real_ns` 另有墙钟偏移）；
- 本函数被 `clockinfo`/`clocktest` 调用，每次调用重新派生，
  不存在「过期的缓存读数」。

### 3.4 函数精讲：timer_poll / timer_arm / timer_read / timer_cancel —— timerfd 状态机

```c
static TEXT64 void timer_poll(void){if(!timer_model.armed||timer_model.canceled)return;
if(!tick_due(ticks,timer_model.deadline_tick))return;
timer_model.expirations++;timer_model.readable=1;
if(timer_model.periodic)timer_model.deadline_tick+=timer_model.interval_ticks;
else timer_model.armed=0;}
static TEXT64 void timer_arm(u64 delay,u64 interval){timer_model.deadline_tick=ticks+delay;
timer_model.interval_ticks=interval;timer_model.periodic=interval!=0;
timer_model.armed=1;timer_model.readable=0;timer_model.canceled=0;timer_model.arms++;}
static TEXT64 u64 timer_read(void){u64 n=timer_model.readable?timer_model.expirations:0;
timer_model.expirations=0;timer_model.readable=0;timer_model.reads++;return n;}
static TEXT64 void timer_cancel(void){timer_model.armed=0;timer_model.canceled=1;
timer_model.readable=0;timer_model.expirations=0;}
```

`timer_poll` 逐行分析：
1. **前提守卫**（第一行）：未 arm 或已 canceled 直接返回；未到截止
   （`!tick_due`）也返回——每 tick 调用的轮询式检查；
2. **到期记账**（第三行）：`expirations++`、`readable=1`——「到期即
   可读」，对应 timerfd 的 poll 翻转；
3. **周期 vs 一次性**（第四、五行）：`periodic` 时把 `deadline_tick +=
   interval_ticks` 滑到下一周期（Linux `hrtimer_forward` 的简化）；
   一次性则 `armed=0` 自然熄灭；`tick_due` 用无符号回绕安全比较
   `(u64)(now-deadline)<(1ULL<<63)`，不怕 ticks 溢出。

`timer_arm`：设定 `deadline_tick=ticks+delay`（绝对截止）、
`periodic=interval!=0`（interval 为 0 即一次性）、复位
armed/readable/canceled，`arms++`——对应 `timerfd_settime`。
`timer_read`：`readable` 时才返回 `expirations`，然后清零到期计数与
可读位，`reads++`——对应 timerfd 的 `read` 消耗到期次数；
**不可读时返回 0**，与「读空管道会阻塞/返回 0」同构。
`timer_cancel`：`armed=0; canceled=1` 并清 readable/expirations——
对应 `timerfd_settime(0)` 取消；`canceled` 位让 `timer_poll` 后续调用
直接短路。

### 3.5 函数精讲：clockinfo / clocktest / timerinfo

```c
static TEXT64 void clockinfo(u16*c){clock_update();clock_model.reads++;
text64(c,"clock ticks/ns: ");hex64(c,clock_model.monotonic_ticks);text64(c,"/");
hex64(c,clock_model.monotonic_ns);text64(c," reads: ");hex64(c,clock_model.reads);
text64(c," PIT Hz: ");hex64(c,PIT_RATE_HZ);putc64(c,'\n');}
static TEXT64 void timerinfo(u16*c){text64(c,"timer armed/readable/periodic: ");
hex64(c,timer_model.armed);text64(c,"/");hex64(c,timer_model.readable);text64(c,"/");
hex64(c,timer_model.periodic);text64(c," deadline/expirations: ");
hex64(c,timer_model.deadline_tick);text64(c,"/");hex64(c,timer_model.expirations);
text64(c," arms/reads: ");hex64(c,timer_model.arms);text64(c,"/");
hex64(c,timer_model.reads);putc64(c,'\n');}
static TEXT64 void clocktest(u16*c){u64 a,b;clock_update();a=clock_model.monotonic_ns;
clock_model.reads++;clock_update();b=clock_model.monotonic_ns;
text64(c,"clocktest: ");text64(c,b>=a&&b==ticks*(1000000000ULL/PIT_RATE_HZ)?
"monotonic PIT clock conversion passed":"BROKEN");putc64(c,'\n');}
```

- `clockinfo`：先 `clock_update` 再 `reads++`，输出
  `clock ticks/ns: <ticks>/<ns> reads: <n> PIT Hz: 64`（`PIT_RATE_HZ=100`
  经 `hex64` 打印为 `0000000000000064`）；
- `timerinfo`：输出六个字段——armed/readable/periodic、
  deadline/expirations、arms/reads；
- `clocktest`：两次 `clock_update` 取 `a`、`b`，断言
  `b>=a`（单调不倒退）且 `b==ticks*10^7`（换算公式逐字一致）。

### 3.6 函数精讲：timertest / sleeptimetest —— 可控时间的确定性验证

```c
static TEXT64 void timertest(u16*c){u64 old=ticks;
timer_arm(2,0);timer_poll();int a=!timer_model.readable;
ticks=old+2;timer_poll();int b=timer_model.readable&&timer_read()==1&&!timer_model.readable;
timer_arm(1,1);ticks=old+3;timer_poll();ticks++;timer_poll();
int d=timer_model.expirations>=2;
timer_cancel();int e=!timer_model.armed&&!timer_model.readable;
text64(c,"timertest: ");text64(c,a&&b&&d&&e?
"deadline, expiration, periodic, and cancel passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void sleeptimetest(u16*c){u64 old=ticks;u64 delay=3;
sleep_model.requested_ticks=delay;sleep_model.deadline_tick=old+delay;
sleep_model.wake_tick=0;sleep_model.remaining_ticks=delay;
sleep_model.active=1;sleep_model.interrupted=0;sleep_model.sleeps++;
ticks=old+delay-1;sleep_model.remaining_ticks=sleep_model.deadline_tick-ticks;
int a=sleep_model.remaining_ticks&&sleep_model.active;
ticks=old+delay;sleep_model.remaining_ticks=0;sleep_model.wake_tick=ticks;
sleep_model.active=0;sleep_model.wakes++;
int b=!sleep_model.active&&!sleep_model.remaining_ticks;
text64(c,"sleeptimetest: ");text64(c,a&&b?"deadline sleep and wake accounting passed":"BROKEN");putc64(c,'\n');}
```

`timertest` 逐步追踪（`old=T`）：
1. `timer_arm(2,0)` 一次性、截止 `T+2`；`ticks==T` 时 `timer_poll` 未
   到期 → `a=!readable=1`（**未到截止不可读**）；
2. `ticks=old+2` 推进到截止 → `timer_poll` 到期 `readable=1`、
   `timer_read()==1` 并把 readable 清 0 → `b=1`（**到期可读、读走复位**）；
3. `timer_arm(1,1)` 周期（interval=1）；`ticks=old+3` 到期一次、
   `ticks++` 再到期一次 → `d=expirations>=2`（**周期自动续期**）；
   `timer_cancel()` 后 `e=!armed&&!readable`（**取消即复位**）；
4. 四断言全真 → `timertest: deadline, expiration, periodic, and cancel passed`。

`sleeptimetest` 逐步追踪（`old=T`）：
1. 手工登记一次 3-tick 睡眠：`requested=3`、`deadline=T+3`、
   `remaining=3`、`active=1`、`sleeps=1`；
2. 推进到 `ticks=T+2`（截止前）：`remaining=deadline-ticks=1`，
   `a=remaining&&active`（**尚未到期：还有剩余、仍在睡**）；
3. 推进到 `ticks=T+3`（截止）：`remaining=0`、`wake_tick=ticks`、
   `active=0`、`wakes=1`；`b=!active&&!remaining`（**到期即醒、剩余
   归零**）；
4. 两断言全真 → `sleeptimetest: deadline sleep and wake accounting passed`。

**注意**：两个测试都直接改写全局 `ticks`（`timertest` 结束在 `T+4`、
`sleeptimetest` 结束在 `T+3`）且不还原——`uptime`/`tickinfo` 会因此
跳变（见 §6）。

### 3.7 exec64 分支、grub.cfg 与 Makefile

`exec64` 新增五个分支（源码逐字）：

```c
else if(eq64(word,"clockinfo")){if(!noargs64(arg))usage64(c,"clockinfo");else clockinfo(c);}
else if(eq64(word,"clocktest")){if(!noargs64(arg))usage64(c,"clocktest");else clocktest(c);}
else if(eq64(word,"timerinfo")){if(!noargs64(arg))usage64(c,"timerinfo");else timerinfo(c);}
else if(eq64(word,"timertest")){if(!noargs64(arg))usage64(c,"timertest");else timertest(c);}
else if(eq64(word,"sleeptimetest")){if(!noargs64(arg))usage64(c,"sleeptimetest");else sleeptimetest(c);}
```

**源码事实（必须知悉）**：
- 开机横幅与 `about` **仍是 lesson-43 文案**（源码逐字）：
  `TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model`
  / `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata`
  ——本课横幅/`about`/help 均未同步，grub.cfg 已更新为 lesson-48；
- Makefile `check` 目标新增 grep（README 三关键词 + kernel64.c 五符号）：

```make
@grep -q 'clock' README.md
@grep -q 'timer' README.md
@grep -q 'sleep' README.md
@grep -q 'clockinfo' kernel64.c
@grep -q 'clocktest' kernel64.c
@grep -q 'timerinfo' kernel64.c
@grep -q 'timertest' kernel64.c
@grep -q 'sleeptimetest' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 48 checks passed.'
```

### 3.8 主控制流

```text
kernel_main64_binary
  ├─ pmm_init → vma_init → reclaim_init → vfs_init（ramfs/pipe）
  ├─ 横幅（源码仍为 lesson-43 文案，见 §3.7 源码事实）
  └─ 键盘循环 → exec64：clockinfo / clocktest / timerinfo / timertest /
       sleeptimetest / signalinfo / pipeinfo / ...（旧命令回归）
```

---

## 4. 数据流与运行逻辑

```text
输入 "clocktest"
  → clock_update()×2 → b>=a 且 b==ticks*10000000
  → "clocktest: monotonic PIT clock conversion passed"

输入 "clockinfo"
  → "clock ticks/ns: <ticks>/<ticks*10000000> reads: <n> PIT Hz: 0000000000000064"

输入 "timertest"（old=T）
  ├─ timer_arm(2,0) 截止 T+2；ticks=T 未到期 → a=1；T+2 到期 → b=1
  ├─ timer_arm(1,1) 周期；T+3 到期、再 ++ 到期 → d=1；timer_cancel() → e=1
  └─ → "timertest: deadline, expiration, periodic, and cancel passed"

输入 "sleeptimetest"（old=T）
  ├─ 登记 3-tick 睡眠：deadline=T+3；T+2 → remaining=1、active → a=1
  ├─ T+3 → remaining=0、wake_tick=T+3、active=0 → b=1
  └─ → "sleeptimetest: deadline sleep and wake accounting passed"
```

（两个测试结束后全局 `ticks` 被改写：timertest 停在 `old+4`、
sleeptimetest 停在 `old+3`；建议先跑 `clockinfo` 再看 `uptime`。）

---

## 5. 构建、运行与验证

### 5.1 依赖

同旧课：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and lesson 48 checks passed.`（要求 README 含
`clock`、`timer`、`sleep`，kernel64.c 含 `clockinfo`、`clocktest`、
`timerinfo`、`timertest`、`sleeptimetest`，缺一即失败；旧 README 里的
`kernel/time/timekeeping.c`/`kernel/time/hrtimer.c`/`fs/timerfd.c`/
`kernel/time/sleep_timeout.c` 引用在 §7 中保留）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字，
仍是 lesson-43 文案——未同步，见 §3.7）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

验证步骤（输出串从源码逐字）：

```bash
clocktest
```

预期：`clocktest: monotonic PIT clock conversion passed`

```bash
clockinfo
```

预期（格式串从源码逐字，数字随运行时间增长）：

```text
clock ticks/ns: 0000000000000000/0000000000000000 reads: 0000000000000001 PIT Hz: 0000000000000064
```

```bash
timertest
```

预期：`timertest: deadline, expiration, periodic, and cancel passed`

```bash
sleeptimetest
```

预期：`sleeptimetest: deadline sleep and wake accounting passed`

```bash
timerinfo
```

预期（`timertest` 之后，timer 已被 cancel）：

```text
timer armed/readable/periodic: 0000000000000000/0000000000000000/0000000000000001 deadline/expirations: 0000000000000000/0000000000000000 arms/reads: 0000000000000002/0000000000000001
```

（cancel 后 expirations 清 0；`periodic` 保留最后一次 arm 值，arms=2、
reads=1 记账在。）

继承回归：`signalinfo`/`signaltest`/`userreturntest`（lesson-47）、
`pipeinfo`/`pipetest`（lesson-46）、`ramfsinfo`（lesson-45）行为一致；
真实 `#PF`/`#UD`/`#BP` 命令 `pftest`/`udtest`/`isttest`/`bptest` 保持
原行为。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 48 checks passed.`；`clocktest`
单调换算、`timertest` 四断言、`sleeptimetest` 两断言均通过；
`timerinfo` 显示 arms=2/reads=1 记账；旧命令回归正常，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `clock`/`timer`/`sleep`，或 kernel64.c 缺五个命令符号 | 对照 Makefile check 的 grep 列表 |
| `timertest` 后 `uptime`/`tickinfo` 跳变 | `timertest`/`sleeptimetest` 直接改写全局 `ticks`（源码事实） | 属预期；先 `clockinfo` 再 `timertest`，勿把 uptime 当真实秒数 |
| `timerinfo` 的 expirations 恒为 0 | `timertest` 末尾 `timer_cancel()` 清零了 expirations（源码事实） | 属预期；`canceled` 后 `timer_poll` 短路返回 |
| `clocktest` 输出 `BROKEN` | 换算公式不匹配或时钟倒退 | 核对 `b==ticks*(1000000000ULL/PIT_RATE_HZ)` 与 `b>=a` |
| `timertest` 输出 `BROKEN` | 四断言中一个不成立 | 逐项核对：`ticks=T` 未到期；`T+2` 到期且 `timer_read()==1`；周期 `expirations>=2`；cancel 后 `!armed&&!readable` |
| `sleeptimetest` 输出 `BROKEN` | 截止前/后判定不符 | 核对 `T+2` 时 `remaining=1&&active=1`；`T+3` 时 `remaining=0&&!active` |
| 横幅/`about`/help 仍显示 lesson-43 文案 | 本课三处文案未同步（源码事实），grub.cfg 已同步 | 对照 lesson-48 kernel64.c 字符串；`timer_*` 只改 `timer_model` 字段，无真实 fd/syscall |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `kernel/time/timekeeping.c`、
`kernel/time/hrtimer.c`、`fs/timerfd.c`、`kernel/time/sleep_timeout.c`**
（延续 lesson-47 的 signal 对照线，收官第 6 阶段）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `clock_update` 从 ticks 派生 ns | `kernel/time/timekeeping.c` 的 `ktime_get_mono_fast_ns`/`jiffies_to_nsecs` | 教学模型直接 `ticks*10^7`；Linux 走 `tk->tkr_mono` 累加器 |
| `realtime_ns=monotonic_ns` | `ktime_get_real_ns`（带墙钟偏移） | 教学模型无 RTC/NTP，实时=单调 |
| `timer_model` 的 deadline/interval/periodic | `kernel/time/hrtimer.c` 的 `hrtimer_start`/`hrtimer_forward` | 教学模型无红黑树/时钟基准切换，单定时器 |
| `timer_poll` 到期置 `readable` | `fs/timerfd.c` 的 `timerfd_tmrproc` 置 `TFD_TIMER_CANCELED`/`hrtimer` 到期后 `wake_up` | 教学模型用轮询替代软中断回调 |
| `timer_read` 返回并清零 expirations | `fs/timerfd.c` 的 `timerfd_read` 读出 64 位到期次数（`hrtimer_forward_now` 差值） | 语义一致，教学模型 1 次记 1 |
| `timer_cancel`/`canceled` 位 | `fs/timerfd.c` 的 `timerfd_settime(0)` 与 `TFD_TIMER_CANCELED` | 教学模型 `canceled` 让 poll 短路 |
| `sleep_model.deadline_tick` 绝对截止 | `kernel/time/sleep_timeout.c` 的 `schedule_timeout`（`expires=jiffies+timeout` + `setup_timer`） | 教学模型只记账不挂线程 |
| `tick_due` 回绕安全比较 | `time_after_eq`（`include/linux/jiffies.h`） | 同构：`(long)(now-deadline)>=0` 风格 |
| 固定 `PIT_RATE_HZ=100` | `HZ` 配置（100/250/1000） | 教学模型单一频率 |

**权威来源**：Intel SDM Vol.3A（PIT 8254 通道 0 计数）、
POSIX `clock_gettime`/`timerfd` 语义、Linux `man 7 time`。

**教学模型简化了什么**：
1. 无 RTC/HPET/TSC：时间全部来自 PIT 100 Hz ticks；
2. 无真实 timerfd fd/syscall：定时器只是内存结构；
3. 无 `hrtimer` 红黑树与时钟基准切换：单定时器线性判定；
4. 无 `schedule_timeout` 挂起/唤醒：睡眠只记账；
5. 测试直接改写全局 `ticks`：牺牲真实感换取确定性。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `clock_update` 用 `ticks*(1000000000ULL/PIT_RATE_HZ)`
   而不是 `ticks*1000000000ULL/PIT_RATE_HZ`？（提示：64 位中间值溢出。）
2. **源码定位**：`timer_read` 返回 0 的两种情形分别是什么？为什么
   「不可读时返回 0」与管道「空读」语义一致？
3. **动手实验**：把 `PIT_RATE_HZ` 临时改成 200，重建后运行 `clocktest`
   观察断言是否仍成立（`ticks*10^9/200` 与公式一致则成立），然后改回
   （不提交）。
4. **Linux 对照**：打开 `fs/timerfd.c` 的 `timerfd_read`，对照本课
   `timer_read`，列出 Linux 版本在读出到期次数外还做的至少 3 件事
   （提示：`ktime_get`、`hrtimer_forward_now`、`spin_lock`、拷贝到
   user buffer）。
5. **设计思考**：`timertest`/`sleeptimetest` 为什么可以直接改写全局
   `ticks`？真实时钟每 10 ms 跳一次时测试要等多久？这种「可控时间」
   手法在哪些场景是合理的取舍？

---

## 9. 本课小结与下一课预告

**小结**：本课用三个 `*_model` 结构把「时间」变成可观察的教学对象：
`clock_model` 从 ticks 派生单调/实时纳秒（100 Hz → 每 tick 10 ms），
`clocktest` 验证单调与换算；`timer_model` 复刻 timerfd 语义
（arm→未到期不可读→到期可读→read 清零→periodic 续期→cancel 复位），
`timertest` 用可控 ticks 四断言收官；`sleep_model` 按绝对截止点记账，
`sleeptimetest` 验证「不早醒、到期归零」。第 6 阶段就此收官：页缓存
（43）→ VFS 对象（44）→ ramfs/路径（45）→ 管道/poll（46）→ 信号（47）
→ 时间（48），全部以固定容量元数据 + 确定性记账完成，从未触碰真实
磁盘、用户指针或无限阻塞。本课横幅/`about`/help 仍是 lesson-43 文案
（源码未同步），grub.cfg 已更新为 lesson-48。

**下一课预告**：第 6 阶段结束。后续课程（可在 `lessons/` 目录按编号
继续学习）将把「I/O 与时间」的元数据模型向更完整的内核抽象推进——
届时 `clock_model` 的时间基准与 `timer_model` 的到期通知会成为真实
调度/等待的基础，「绝对截止点 + 回绕安全比较」会在任何需要超时处复用。

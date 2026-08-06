# Lesson 46: Linux 风格管道、阻塞 I/O 与 poll/wait 机制 — 精讲文档

> **课号**：Lesson 46（可执行课）
> **主题**：Linux 风格管道（pipe）、阻塞 I/O（blocking I/O）与
> poll/wait 机制
> **课程主线位置**：第 6 阶段「Linux 风格 I/O 与文件抽象」第四课。前课（45）
> 完成 ramfs 树与路径查找；本课在「文件/节点」语义之上实现**固定容量环形
> 管道**：空读阻塞、满写阻塞的状态转移记账，以及 Linux `POLLIN`/`POLLOUT`
> 就绪判定。
> **前置课程**：[`lesson-45-stable/README.md`](../lesson-45-stable/README.md)
> **后续课程**：[`lesson-47-stable/README.md`](../lesson-47-stable/README.md)
> （信号、异常通知与用户态返回语义）
> **一句话目标**：能讲清 Linux 管道为什么是「带阻塞语义的环形缓冲」、
> 读端空/写端满时进程如何挂到等待队列、poll 如何不问 I/O 就预判就绪，
> 并在 TinyOS 里复刻全部**状态转移与记账**——固定 4 字节环、不拷贝用户
> 内存、不在命令里无限期阻塞。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——管道 `fs/pipe.c` 的环形缓冲
（`pipe_buffer` 数组 + head/tail）、`pipe_read`/`pipe_write` 在空/满时的
阻塞语义（挂 `pipe_wait` 等待队列），以及 `poll`（`fs/select.c`）如何
对每个 fd 调用 `poll_mask` 得到就绪位。TinyOS 用 `pipe_model`
（`data[4]`/`head`/`tail`/`used`）实现 FIFO 环形管道，
`pipe_try_write`/`pipe_try_read` 在满/空时记账「阻塞」而不真正睡眠，
`pipe_poll` 用 `POLL_IN`/`POLL_OUT` 位报告就绪。

- **在课程主线中的位置**：45 给了「名字→对象」，46 给对象加上「行为」。
  「可读/可写」判定（poll）会被 47 的信号、48 的 timerfd 复用——timerfd
  正是「可读性随定时器到期而翻转」的对象。
- **前置知识清单**：
  1. lesson-45 的 ramfs 节点/路径模型与 lesson-44 的 inode/file 对象；
  2. lesson-38 的 `struct wait_queue` 与 `waitq_wake_one`/`waitq_reset`
     （FIFO 队列 + wake_one/wake_all 记账）；
  3. 环形缓冲 head/tail 语义（`(head+1)%CAP` 回绕）；
  4. Linux `include/linux/poll.h` 的 `POLLIN`/`POLLOUT` 直觉。
- **本课交付**：`pipeinfo`/`pipetest`/`polltest` 三条命令；
  `struct pipe_model` 与 `pipe_init`/`pipe_try_write`/`pipe_try_read`/
  `pipe_poll` 四个函数；读写/阻塞/唤醒/注册六类计数器；`vfs_init`
  尾部挂接 `pipe_init()`。

---

## 2. 核心概念精讲

### 2.1 概念一：管道——内核里的环形 FIFO

**定义**：管道是内核提供的一段**无名的、单向的环形字节缓冲**：写端
`write` 把字节放入环尾，读端 `read` 从环头取走，先进先出（FIFO）。
**为什么需要**：进程间通信的最原始形态——`ls | grep` 就是把 ls 的
stdout 接到 grep 的 stdin。**TinyOS 对应**：`pipe_model.data[PIPE_CAP=4]`
固定 4 字节环，`head` 指向下一个写入位、`tail` 指向下一个读出位，
`used` 记录当前字节数；满时 `head` 与 `tail` 相差整个环、空时相等，
写入/读出后用取模回绕。

### 2.2 概念二：阻塞 I/O——空读与满写的「等一下」

**定义**：阻塞 I/O 是同步 I/O 的默认语义：读一个空管道时，调用者不会
拿到「没有数据」的错误，而是被挂起，直到数据到来；写一个满管道时，
调用者被挂起，直到有人腾出空间。**为什么需要**：生产者/消费者节奏
天然不同步，阻塞让内核把「等待」管理起来而不是让用户忙轮询。
**TinyOS 对应**：`pipe_try_read` 在 `used==0` 时 `blocked_readers++`、
`pipe_try_write` 在 `used>=PIPE_CAP` 时 `blocked_writers++`，都返回 0
模拟「本应阻塞」。**它们不会真正挂起线程**——用计数表示一次阻塞，
教学模型刻意避免在 shell 命令里无限睡眠。

### 2.3 概念三：等待队列——被阻塞者的归宿

**定义**：等待队列（`struct wait_queue`）是挂在某个条件上的线程名单：
条件不满足就入队挂起，条件满足时 `wake_up` 把队里的线程唤醒。
**为什么需要**：阻塞不是「忙等」，而是「登记 + 挂起 + 被唤醒」三段式。
**TinyOS 对应**：本课为管道准备了两条等待队列——`pipe_read_wait`
（等数据的读者）与 `pipe_write_wait`（等空间的写者）。`pipe_try_write`
成功放入数据后 `waitq_wake_one(&pipe_read_wait,...)` 唤醒一个读者；
`pipe_try_read` 成功取走后唤醒一个写者；队列由 `pipe_init` 的
`waitq_reset` 在启动时清空。

### 2.4 概念四：poll——不读不写也能问“就绪了吗”

**定义**：`poll` 让调用者一次性问多个 fd：「现在可读吗？可写吗？」
内核按每个 fd 的类型计算就绪掩码。`POLLIN`=可读（读不会阻塞）、
`POLLOUT`=可写（写不会阻塞）。**为什么需要**：进程不想阻塞在某个 fd
上，而想在所有 fd 里挑一个就绪的先处理（I/O 多路复用）。
**TinyOS 对应**：`pipe_poll(mask)` 按请求的 mask 与当前 `used` 计算
就绪位——`used>0` 置 `POLL_IN`，`used<PIPE_CAP` 置 `POLL_OUT`；
空管道不可读、满管道不可写，与 Linux 语义一致。每次调用
`poll_registrations++` 记账。

### 2.5 概念五：poll 与阻塞的关系——先查后等

**为什么 poll 有用**：阻塞 I/O 是「无条件等」，poll 是「先查就绪、不行
才决定等不等」。Linux `poll` 内部 `do_poll` 就是「查一遍 mask → 无就绪
就 sleep 到超时 → 再查」的循环。TinyOS 本课把两件事分开演示：
`pipetest` 验证阻塞转移（空读/满写记账），`polltest` 验证就绪判定
（空/有数据/满/再读空的状态机）——这是 `fs/select.c` 与 `fs/pipe.c`
各管一段的教学切片。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-45） |
|---|---|---|
| `boot.S` | Multiboot2 引导、进入 long mode | 未变化 |
| `kernel.c` | 32 位入口、低内存页表、user image 装载 | 未变化 |
| `kernel64.c` | 64 位内核主体（累积） | **核心**：`pipe_model` 结构 + `pipe_init`/`pipe_try_write`/`pipe_try_read`/`pipe_poll`/`pipeinfo`/`pipetest`/`polltest`；`vfs_init` 尾部挂 `pipe_init()`；help 更新 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `pipe`、`blocking`、`poll`；kernel64.c 含 `pipeinfo`、`pipetest`、`polltest` |
| `grub.cfg` | 装载 | **menuentry 标题更新**为 `TinyOS lesson 46: pipes, blocking I/O, and poll readiness` |

### 3.2 结构 / 宏 / 全局变量精讲

```c
#define PIPE_CAP 4U
#define PIPE_WAIT_CAP 4U
#define POLL_IN 1U
#define POLL_OUT 2U
struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };
static struct pipe_model pipe_model;
static struct wait_queue pipe_read_wait,pipe_write_wait;
```

逐行注释：
- `PIPE_CAP=4`：环形缓冲容量——「固定 4 字节环」是本课最醒目的有界；
- `PIPE_WAIT_CAP=4`：等待队列容量上限（声明备用；实际队列用继承的
  `WAIT_QUEUE_CAP`）；本课未直接使用该宏，属预留；
- `POLL_IN=1`/`POLL_OUT=2`：就绪掩码位——与 Linux
  `include/uapi/linux/eventpoll.h` 的 `POLLIN(0x1)`/`POLLOUT(0x4)` 同构
  （值不同，语义一致）；
- `struct pipe_model`：字节环 `data[4]` + `head`（写位）/`tail`（读位）/
  `used`（存量）+ 7 个 u64 计数器（reads/writes/blocked_readers/
  blocked_writers/wake_readers/wake_writers/poll_registrations）；
- 全局量：`pipe_model` 单实例 + 两条等待队列 `pipe_read_wait`（读者等待
  数据）、`pipe_write_wait`（写者等待空间）。

### 3.3 函数精讲：pipe_init —— 清零与队列复位

```c
static TEXT64 void pipe_init(void){pipe_model=(struct pipe_model){0};
waitq_reset(&pipe_read_wait);waitq_reset(&pipe_write_wait);}
```

逐行分析：
- `(struct pipe_model){0}` 复合字面量整体清零：环、head/tail/used 与
  全部计数器一次性归零，比逐字段赋值更不易漏；
- `waitq_reset` 把两条等待队列的 head/tail/count 与 enqueues/wake_one/
  wake_all 记账归零（`fs/pipe.c` 里管道的 waitqueue 在 `alloc_pipe_info`
  时初始化，对应这里）；
- `pipe_init` 在 `vfs_init` 尾部被调用，随后 `polltest` 也会再调一次
  以建立干净基线。

### 3.4 函数精讲：pipe_try_write / pipe_try_read —— 满写/空读的阻塞记账

```c
static TEXT64 int pipe_try_write(u8 value){u8 id;
if(pipe_model.used>=PIPE_CAP){pipe_model.blocked_writers++;return 0;}
pipe_model.data[pipe_model.head]=value;
pipe_model.head=(u8)((pipe_model.head+1)%PIPE_CAP);
pipe_model.used++;pipe_model.writes++;
if(waitq_wake_one(&pipe_read_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_readers++;
return 1;}
```

`pipe_try_write` 逐行分析：
1. **满管判定**（第二行）：`used>=PIPE_CAP` 时写会阻塞——`blocked_writers++`
   记账并返回 0；这正是 Linux `pipe_write` 里「管道满则
   `pipe_wait(wpipe)`」的计数化；
2. **入环**（第三行）：`data[head]=value` 写入写位；
3. **回绕推进**（第四行）：`head=(head+1)%PIPE_CAP`——环尾写到第 4 槽
   后回到 0，实现环形；
4. **记账**（第五行）：`used++`、`writes++`；
5. **唤醒读者**（第六行）：`waitq_wake_one(&pipe_read_wait,
   THREAD_BLOCKED_EVENT,&id)` 从读者等待队列取出一个 `THREAD_BLOCKED_EVENT`
   状态的线程置为 RUNNABLE；成功则 `wake_readers++`。对应 Linux 写完数据
   后 `wake_up_interruptible(&pipe->rd_wait)`；
6. 返回 1 表示「本次写入不会阻塞」。

```c
static TEXT64 int pipe_try_read(u8*out){u8 id;
if(!pipe_model.used){pipe_model.blocked_readers++;return 0;}
*out=pipe_model.data[pipe_model.tail];
pipe_model.tail=(u8)((pipe_model.tail+1)%PIPE_CAP);
pipe_model.used--;pipe_model.reads++;
if(waitq_wake_one(&pipe_write_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_writers++;
return 1;}
```

`pipe_try_read` 逐行分析：
1. **空管判定**（第二行）：`used==0` 时读会阻塞——`blocked_readers++`
   返回 0，对应 Linux `pipe_read` 的空管道 `pipe_wait(rpipe)`；
2. **出环**（第三行）：`*out=data[tail]` 从读位取出——注意这里
   `out` 是内核栈上的局部缓冲指针（测试函数传入），不是任意用户指针；
3. **回绕推进**（第四行）：`tail=(tail+1)%PIPE_CAP`；
4. **记账**（第五行）：`used--`、`reads++`；
5. **唤醒写者**（第六行）：读走数据腾出空间后
   `waitq_wake_one(&pipe_write_wait,...)`，成功 `wake_writers++`——对应
   Linux 读完数据后 `wake_up_interruptible(&pipe->wr_wait)`。

### 3.5 函数精讲：pipe_poll —— POLLIN/POLLOUT 就绪判定

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;
pipe_model.poll_registrations++;
if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;
if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;
return ready;}
```

逐行分析：
- **注册记账**（第二行）：每次 poll 询问 `poll_registrations++`
  （Linux `do_poll` 的 `poll_table` 注册次数类比）；
- **可读判定**（第三行）：请求了 `POLL_IN` 且 `used>0` → 置 `POLL_IN`；
  空管道（`used==0`）读会阻塞，所以**不**可读；
- **可写判定**（第四行）：请求了 `POLL_OUT` 且 `used<PIPE_CAP` →
  置 `POLL_OUT`；满管道写会阻塞，所以**不**可写；
- **按位或返回**：`ready` 可能同时含两个位（半满管道即可读又可写），
  与 Linux `ep_ptable_queue_proc` 之后算出的 `revents` 同构；
- 本函数**只读 `used`、不改环**——poll 不会消费数据，这是它的语义核心。

### 3.6 函数精讲：pipeinfo / pipetest / polltest

```c
static TEXT64 void pipeinfo(u16*c){text64(c,"pipe used/capacity: ");
hex64(c,pipe_model.used);text64(c,"/");hex64(c,PIPE_CAP);
text64(c," reads/writes: ");hex64(c,pipe_model.reads);text64(c,"/");
hex64(c,pipe_model.writes);text64(c," blocked r/w: ");
hex64(c,pipe_model.blocked_readers);text64(c,"/");
hex64(c,pipe_model.blocked_writers);text64(c," wake r/w: ");
hex64(c,pipe_model.wake_readers);text64(c,"/");
hex64(c,pipe_model.wake_writers);putc64(c,'\n');}
static TEXT64 void pipetest(u16*c){u8 v=0,out=0;
int a=!pipe_try_read(&out),b=pipe_try_write(0x41),d=pipe_try_read(&v),
e=v==0x41,f=pipe_model.used==0;
pipe_model.used=PIPE_CAP;int g=!pipe_try_write(0x42);pipe_model.used=0;
text64(c,"pipetest: ");text64(c,a&&b&&d&&e&&f&&g?
"bounded FIFO empty/full blocking transitions passed":"BROKEN");
putc64(c,'\n');}
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();
int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,
w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,
e=pipe_model.used==PIPE_CAP;
while(pipe_model.used<PIPE_CAP)pipe_try_write(8);
int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),
h=pipe_poll(POLL_OUT)==POLL_OUT;
text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?
"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
```

- `pipeinfo`：三组比例——`used/capacity`、`reads/writes`、
  `blocked r/w`、`wake r/w`（注意 `poll_registrations` 被 pipe_poll 递增，
  但 pipeinfo **不打印**它——源码如此）；
- `pipetest` 六步（假定 boot 后首次运行，`used=0`）：
  ① `!pipe_try_read`：空读阻塞记账 → 1；② 写 `0x41`('A') → 1；
  ③ 读回 → 1；④ `v=='A'` → 1；⑤ `used==0`（读完空）→ 1；
  ⑥ 强制 `used=PIPE_CAP` 后 `!pipe_try_write`：满写阻塞 → 1，
  再恢复 `used=0`。六项全真 → passed；
- `polltest` 的状态机：空→（poll 不可读、可写）→写 7（变可读）→
  填满（不可写）→读一个（又可写）。**源码缺陷（如实记录）**：
  `e=pipe_model.used==PIPE_CAP` 在 `while` 填充循环**之前**求值，
  此时 `used=1`，故 `e=0`，断言恒假 → **`polltest` 必然输出
  `polltest: BROKEN`**。修复方式见思考题 3（把 `e` 移到 while 之后，
  或改为 `e=pipe_model.used<PIPE_CAP`）。

### 3.7 exec64 分支、kernel_main、grub.cfg 与 Makefile

`exec64` 新增三个分支（源码逐字）：

```c
else if(eq64(word,"pipeinfo")){if(!noargs64(arg))usage64(c,"pipeinfo");else pipeinfo(c);}
else if(eq64(word,"pipetest")){if(!noargs64(arg))usage64(c,"pipetest");else pipetest(c);}
else if(eq64(word,"polltest")){if(!noargs64(arg))usage64(c,"polltest");else polltest(c);}
```

`vfs_init` 尾部新增 `pipe_init()`（源码逐字，仅列尾部）：
`... fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}`
——管道在 VFS/ramfs 之后初始化，无需改 `kernel_main64_binary`。

**源码事实（必须知悉）**：
- **help 文案已更新**（本课是六连课中首个补全 help 列表的）：开头为
  `commands: help about pipeinfo pipetest polltest ptrinfo ptrtest ...`；
- 开机横幅与 `about` **仍是 lesson-43 文案**（源码逐字）：
  `TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model`
  / `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata`
  ——本课横幅/`about` 未同步，grub.cfg 已更新为
  `TinyOS lesson 46: pipes, blocking I/O, and poll readiness`；
- Makefile `check` 目标新增 grep（README 三关键词 + kernel64.c 三符号）：

```make
@grep -q 'pipe' README.md
@grep -q 'blocking' README.md
@grep -q 'poll' README.md
@grep -q 'pipeinfo' kernel64.c
@grep -q 'pipetest' kernel64.c
@grep -q 'polltest' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 46 checks passed.'
```

### 3.8 主控制流

```text
kernel_main64_binary
  ├─ pmm_init → vma_init → reclaim_init → vfs_init ──┐
  │      vfs_init 尾部：ramfs_init() + pipe_init() ◄───┘
  ├─ 横幅（源码仍为 lesson-43 文案，见 §3.7 源码事实）
  └─ 键盘循环 → exec64：
        pipeinfo / pipetest / polltest / ramfsinfo / fdtest / ...（旧命令回归）
```

---

## 4. 数据流与运行逻辑

```text
输入 "pipetest"（boot 后首次，used=0）
  ├─ 空读 → blocked_readers=1，返回 0 → a=1
  ├─ 写 'A'(0x41) → head=1，used=1，writes=1；读回 → v='A'，used=0 → d/e/f=1
  ├─ 强制 used=4 → 满写 → blocked_writers=1，返回 0 → g=1；恢复 used=0
  └─ → "pipetest: bounded FIFO empty/full blocking transitions passed"

输入 "pipeinfo"（pipetest 之后）
  → "pipe used/capacity: 0000000000000000/0000000000000004
     reads/writes: 0000000000000001/0000000000000001
     blocked r/w: 0000000000000001/0000000000000001
     wake r/w: 0000000000000000/0000000000000000"

输入 "polltest"（注意：先 pipe_init() 清零）
  a=空不可读(1) b=空可写(1) w=写7→used=1(1) d=可读(1)
  e=used==PIPE_CAP → 1==4 → 0   ← 源码求值顺序缺陷
  while 填满(used=4)；f=满不可写(1)；g=读走一个→used=3(1)；h=可写(1)
  → 断言含 e=0 → "polltest: BROKEN"（源码逻辑如此，见 §3.6）
```

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

`make check` 输出：`Multiboot2 and lesson 46 checks passed.`（要求 README 含
`pipe`、`blocking`、`poll`，kernel64.c 含 `pipeinfo`、`pipetest`、`polltest`，
缺一即失败；旧 README 里的 `fs/pipe.c`/`fs/select.c`/`fs/eventpoll.c`/
`include/linux/poll.h` 引用在 §7 中保留）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字，
显示的是 lesson-43 文案——源码未同步，见 §3.7）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

验证步骤（输出串从源码逐字）：

```bash
pipetest
```

预期：`pipetest: bounded FIFO empty/full blocking transitions passed`

```bash
pipeinfo
```

预期（一次 `pipetest` 之后）：

```text
pipe used/capacity: 0000000000000000/0000000000000004 reads/writes: 0000000000000001/0000000000000001 blocked r/w: 0000000000000001/0000000000000001 wake r/w: 0000000000000000/0000000000000000
```

```bash
polltest
```

预期（源码求值顺序缺陷导致的既定结果，详见 §3.6/§6）：

```text
polltest: BROKEN
```

（不是环境问题——`e` 在填充循环前求值为 0，断言恒不成立；思考题 3
给出修复，修改后应输出 `polltest: POLLIN/POLLOUT readiness transitions
passed`。）

继承回归：`ramfsinfo`/`pathtest`（lesson-45）、`fdtest`（lesson-44）、
`anoninfo`（lesson-43）行为一致；真实 `#PF` 命令 `pftest`/`isttest`/
`stackguardtest` 保持致命停机。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 46 checks passed.`；`pipetest`
六断言通过，`pipeinfo` 显示 used=0、reads/writes=1/1、
blocked r/w=1/1；`polltest` 按静态求值必然输出 `polltest: BROKEN`
（源码 `e` 求值顺序缺陷，见 §3.6，如实记录）；旧命令回归正常。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `pipe`/`blocking`/`poll`，或 kernel64.c 缺 `pipeinfo`/`pipetest`/`polltest` | 对照 Makefile check 的 grep 列表 |
| `polltest` 输出 `BROKEN` | **源码求值顺序缺陷**：`e=pipe_model.used==PIPE_CAP` 在 `while` 填充前求值（used=1≠4） | 静态追踪声明求值顺序；把 `e` 移到 while 之后或改为 `used<PIPE_CAP` 验证 |
| `pipetest` 输出 `BROKEN` | 六断言中一个不成立 | 逐项核对空读阻塞、写读 FIFO、`v=='A'`、used 归 0、满写阻塞 |
| `pipeinfo` 的 wake r/w 恒为 0 | 测试中没有真实线程入队，`waitq_wake_one` 无 `THREAD_BLOCKED_EVENT` 可唤醒 | 属预期；`blocked_*` 是「本应阻塞」的计数，wake 需真实阻塞线程才会递增 |
| `pipeinfo` 不显示 poll 注册数 | `poll_registrations` 被 `pipe_poll` 递增但 `pipeinfo` 不打印（源码事实） | 对照 `pipeinfo` 输出串与 `pipe_poll` |
| 横幅/`about` 显示 lesson-43 | 本课横幅/`about` 未同步（源码事实），help 与 grub.cfg 已同步 | 对照 lesson-46 kernel64.c 字符串 |
| 担心「命令会真的无限阻塞」 | 设计保证非阻塞记账 | `pipe_try_read`/`pipe_try_write` 满/空时只 `++` 计数并返回 0，无 `hlt` 循环 |
| `pipe_poll` 会不会消费数据 | 不会 | 函数只读 `used` 与 `mask`，不触碰 `head`/`tail` |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `fs/pipe.c`、`fs/select.c`、`fs/eventpoll.c`、
`include/linux/poll.h`**（延续 lesson-45 的 VFS/namei 对照线）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `pipe_model.data[4]` + head/tail/used 环形缓冲 | `fs/pipe.c` 的 `struct pipe_inode_info`（`bufs`/`head`/`tail` 的 `pipe_buffer` 数组，页粒度） | 教学模型 4 字节字节环；Linux 是页池 + 分批记账 |
| `pipe_try_write` 满管 `blocked_writers++` | `fs/pipe.c` 的 `pipe_write` 在 `pipe_full` 时 `pipe_wait(wpipe)` 挂起 | 教学模型记账代替挂起，绝不在命令里无限期等待 |
| `pipe_try_read` 空管 `blocked_readers++` | `fs/pipe.c` 的 `pipe_read` 空时 `pipe_wait(rpipe)` | 同上 |
| `waitq_wake_one(&pipe_read_wait, ...)` | `fs/pipe.c` 写后 `wake_up_interruptible_sync(&pipe->rd_wait)` | 教学模型复用 lesson-38 的 wait_queue |
| `pipe_poll` 的 `POLL_IN`/`POLL_OUT` 计算 | `fs/pipe.c` 的 `pipe_poll`：`mask |= EPOLLIN`（有数据）/`EPOLLOUT`（有空间） | 教学模型用 `include/linux/poll.h` 词汇，取值不同 |
| `poll_registrations++` | `fs/select.c` 的 `do_poll` + `poll_table` 注册（`pollwake` 回调） | 教学模型只计数，无回调/无 fd 数组遍历 |
| `polltest` 的「空→非空→满→可读」状态机 | `fs/select.c` 多路复用的轮询循环 | 教学模型单管道状态机，语义对齐 `POLLIN/POLLOUT` |
| 固定 `PIPE_CAP=4` | 管道容量默认 16 页（`/proc/sys/fs/pipe-max-size`） | 有界是教学刻意简化 |

**权威来源**：POSIX 管道语义（`pipe(2)`/`read`/`write` 阻塞行为）、
Linux `include/uapi/linux/eventpoll.h` 的 `EPOLLIN`/`EPOLLOUT`、
`man 7 pipe`。

**教学模型简化了什么**：
1. 不拷贝用户缓冲：`pipe_try_*` 每次只搬运 1 字节到内核栈局部变量；
2. 不真正阻塞：阻塞用计数器表达，无 `schedule()`/线程挂起；
3. 无 fd 绑定：管道不是 file/inode 对象，无 `pipe_rdwr_fops`；
4. 无半关闭/EOF：`pipe_read` 的「写端全关返回 0」语义未实现；
5. 无多路复用：`pipe_poll` 只判单管道就绪，不遍历 fd 集合。

---

## 8. 思考题与练习

1. **概念理解**：为什么空管道读「阻塞」而 `poll(POLLIN)` 返回「不可读」？
   这两者描述的是不是同一件事的两面？
2. **源码定位**：`pipe_try_write` 里 `head=(head+1)%PIPE_CAP` 与
   `pipe_try_read` 里 `tail=(tail+1)%PIPE_CAP` 各自保证什么？
   如果写满 4 字节后再写 2 字节，head/tail 分别是什么？
3. **动手实验（修复 polltest 源码缺陷）**：把
   `e=pipe_model.used==PIPE_CAP` 移到 `while` 填充循环**之后**（或改成
   `e=pipe_model.used<PIPE_CAP`），重建后运行 `polltest`，应输出
   `polltest: POLLIN/POLLOUT readiness transitions passed`；确认后还原
   （不提交）。
4. **Linux 对照**：打开 `fs/pipe.c` 的 `pipe_write`，列出它在本课
   `pipe_try_write` 基础上多做的至少 4 件事
   （提示：`pipe_buf_confirm`、页分配、`copy_page_from_iter`、超时/非阻塞
   分支）。
5. **设计思考**：`pipe_read_wait`/`pipe_write_wait` 用的是
   `THREAD_BLOCKED_EVENT` 状态。如果要让 `pipe_try_write` 真正把当前线程
   挂起（阻塞）而不是记账，需要复用 lesson-38 的哪些原语？
   （提示：`event_wait` 的 `sti; hlt` 循环 + `waitq_enqueue`。）

---

## 9. 本课小结与下一课预告

**小结**：本课用 `pipe_model` 4 字节环形缓冲复刻了 Linux 管道核心：
`pipe_try_write` 满管记账阻塞、写后唤醒读者，`pipe_try_read` 空管记账
阻塞、读后唤醒写者，`pipe_poll` 按 `used` 计算 `POLL_IN`/`POLL_OUT`
就绪位且不消费数据；`pipetest` 验证 FIFO 与空/满阻塞转移，
`polltest` 演示就绪状态机但**因源码求值顺序缺陷（`e` 在填充前求值）必然
输出 BROKEN**——这是如实记录的源码问题，修复方式见思考题 3。
help 文案本课补齐了 `pipeinfo pipetest polltest`；横幅与 `about` 仍是
lesson-43 文案；grub.cfg 菜单已更新为 lesson-46。阻塞语义用计数表达、
不真正睡眠，这是「确定性、非阻塞的教学模型」的又一例。

**下一课预告**：lesson-47 将实现 Linux 风格**信号、异常通知与用户态返回
语义**（对照 `kernel/signal.c`、`arch/x86/kernel/traps.c`）：`#BP`/`#UD`/
`#PF` 怎么映射成 SIGILL/SIGSEGV、信号怎么入队、为何必须在返回用户态前
送达、默认动作如何终止被模拟的用户进程。届时本课的 wait/唤醒记账思想
会延续到「异常即事件」的模型里。

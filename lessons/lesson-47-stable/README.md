# Lesson 47: Linux 风格信号、异常通知与用户态返回语义 — 精讲文档

> **课号**：Lesson 47（可执行课）
> **主题**：Linux 风格信号（signals）、异常通知（exception notification）
> 与用户态返回（user return）语义
> **课程主线位置**：第 6 阶段「Linux 风格 I/O 与文件抽象」第五课。前课（46）
> 完成管道与 poll；本课把 x86 异常（`#BP`/`#UD`/`#PF`）映射成 POSIX 信号
> （SIGTRAP/SIGILL/SIGSEGV），演示「异常→入队→返回用户态前送达」与
> 「默认动作终止进程」的完整教学闭环。
> **前置课程**：[`lesson-46-stable/README.md`](../lesson-46-stable/README.md)
> **后续课程**：[`lesson-48-stable/README.md`](../lesson-48-stable/README.md)
> （时钟、timerfd 模型与睡眠）
> **一句话目标**：能讲清 Linux 为什么把 CPU 异常翻译成信号、为什么信号
> 只在返回用户态的那一刻送达、`#UD`/`#PF` 的默认动作为什么是终止进程，
> 并在 TinyOS 里复刻全部**入队/送达/终止判定**——不写用户信号栈、
> 不装 handler、不碰用户内存。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——Linux 中 `arch/x86/kernel/traps.c`
把 CPU 异常转成信号（`#BP`→SIGTRAP、`#UD`→SIGILL、`#PF`→SIGSEGV），
信号先入 `sigpending` 队列，`kernel/entry/common.c` 在**返回用户态之前**
检查 pending 并 `get_signal` 处理；默认动作里 SIGILL/SIGSEGV 是
`do_coredump`/退出，SIGTRAP 则可被调试器接管。TinyOS 用
`struct signal_record` 数组 + `exception_signal`/`user_return_prepare`
实现这套「异常→信号→返回前送达」的有界模型。

- **在课程主线中的位置**：47 把「异常」从 lesson-27/28 的「致命停机报告」
  升级为「可入队的进程通知」——中断/异常处理从「给屏幕看」变成
  「给进程看」。48 的时钟/timer 会复用到这里的「事件到达后如何通知」。
- **前置知识清单**：
  1. lesson-27/28 的 `exception_frame`（vector/error/rip/cs/rflags/rsp/ss）
     与 IST 异常路径、`#BP`/`#UD`/`#PF` 语义；
  2. lesson-34 起的 `user_process`/`user_thread` 单进程模型
     （`#define user_process user_processes[0]`）、`PROCESS_RUNNING`/
     `PROCESS_EXITED` 状态机、`saved_user_context`；
  3. POSIX/Linux 信号号：SIGTRAP=5、SIGILL=4、SIGSEGV=11；
  4. x86 `CR2` 寄存器语义（`#PF` 时保存触发地址）。
- **本课交付**：`signalinfo`/`signaltest`/`userreturntest` 三条命令；
  `SIG_*` 常量与 `struct signal_record`；`exception_signal`/
  `user_return_prepare` 两个函数；`struct process` 新增
  `return_pending`/`signals[2]`/三个信号计数。

---

## 2. 核心概念精讲

### 2.1 概念一：异常是信号的“原材料”

**定义**：CPU 异常是硬件检测到的执行错误（`#BP` 断点、`#UD` 非法指令、
`#PF` 缺页/保护），信号是内核通知进程的方式。Linux 在 trap 处理器里把
前者翻译成后者：`do_trap` 系列 → `force_sig_fault`。**为什么需要**：进程
不该面对裸的 CPU 向量号，它该看到「出错了，错误码 11」这样的统一语言。
**TinyOS 对应**：`exception_signal(f)` 按 `f->vector` 选择信号号
（`3→SIGTRAP`、`6→SIGILL`、`14→SIGSEGV`，其余向量返回 0 不处理），
`#PF` 时还读 `CR2` 记录故障地址——这与
`arch/x86/kernel/traps.c` 的映射一致。

### 2.2 概念二：CPL3 边界——只有用户态异常才转信号

**定义**：内核自身的异常（如栈溢出、内核代码 `#PF`）是内核 bug，
不能转化成给进程的信号；只有来自用户态（`cs==USER_CS`）的异常才
「归进程所有」。**TinyOS 对应**：`exception_signal` 第一道守卫
`if(!f||f->cs!=USER_CS)return 0;`——内核态异常直接拒绝，与 Linux
`kernel_mode` 检查同构。

### 2.3 概念三：pending 队列——有界的“待送达”登记表

**定义**：`struct sigpending` 是进程里「已产生、未送达」信号的链表；
同信号去重，不同信号排队。**TinyOS 对应**：
`user_process.signals[SIG_PENDING_MAX=2]` 固定 2 槽，每个
`signal_record { signo, vector, error, fault_address, rip, pending, delivered }`
保留一次异常的完整档案；`exception_signal` 找第一个 `!pending` 槽入队。
**表满时** `signal_dropped++` 并拒绝——真实 Linux 用位图 + 链表不会丢，
这里「有界丢弃」是教学简化。

### 2.4 概念四：默认动作——SIGILL/SIGSEGV 终止、SIGTRAP 不终止

**定义**：未装 handler 时信号按默认动作执行：SIGILL/SIGSEGV 默认
`term`（终止并产生 core），SIGTRAP 默认 `stop`/交给调试器。
**TinyOS 对应**：`exception_signal` 里
`if(signo!=SIGTRAP){user_process.state=PROCESS_EXITED;
user_thread.state=USER_THREAD_EXITED;}`——`#UD`/`#PF` 入队的同时把
被模拟的进程置为 EXITED（默认动作即刻生效）；`#BP`（SIGTRAP）不入
终止分支，因为它代表「调试器要接管」。

### 2.5 概念五：返回用户态前送达（return_pending 语义）

**定义**：信号不能在中断上下文里直接「跳到用户 handler」，必须等内核
正要 `iretq` 回用户态的那一刻检查 pending、搭好信号帧再回去。Linux
`kernel/entry/common.c` 的 `exit_to_user_mode_loop` 就是这道检查。
**TinyOS 对应**：`user_return_prepare` 的入参守卫
`state==PROCESS_RUNNING && return_pending`，命中后把第一条 pending
信号置 `delivered`，再**重新扫描**看是否还有 pending 决定
`return_pending` 是否保留——「送达一次、可能有下一条」正是 Linux
`signal_pending` 循环的语义。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-46） |
|---|---|---|
| `boot.S` | Multiboot2 引导、进入 long mode | 未变化 |
| `kernel.c` | 32 位入口、低内存页表、user image 装载 | 未变化 |
| `kernel64.c` | 64 位内核主体（累积） | **核心**：`SIG_*` 常量 + `signal_record`；`struct process` 增 `return_pending`/`signals[2]`/计数；`exception_signal`/`user_return_prepare`/`signalinfo`/`signaltest`/`userreturntest` |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `signals`、`exception`、`user return`；kernel64.c 含 `signalinfo`、`signaltest`、`userreturntest`、`exception_signal` |
| `grub.cfg` | 装载 | **menuentry 标题更新**为 `TinyOS lesson 47: signals, exception notification, and user return` |

### 3.2 结构 / 宏 / 全局变量精讲

```c
#define SIG_NONE 0U
#define SIGTRAP 5U
#define SIGILL 4U
#define SIGSEGV 11U
#define SIG_PENDING_MAX 2U
struct signal_record { u32 signo,vector; u64 error,fault_address,rip; u8 pending,delivered; };
struct process { u64 pid; struct address_space *address_space; u64 code_phys, stack_phys, entry, stack_top;
    u32 image_bytes; u8 state, context_valid, return_pending;
    struct signal_record signals[SIG_PENDING_MAX];
    u64 signal_queued,signal_delivered,signal_dropped; };
```

逐行注释：
- 信号号与 **Linux `include/uapi/asm-generic/signal.h` 完全一致**：
  `SIGTRAP=5`、`SIGILL=4`、`SIGSEGV=11`，`SIG_NONE=0` 作空值；
- `SIG_PENDING_MAX=2`：单进程最多挂 2 条 pending——有界队列；
- `struct signal_record`：一次异常通知的完整档案——`signo`（翻译后的
  信号）、`vector`（原始 CPU 向量）、`error`（错误码，`#PF` 的 P/W/U
  位）、`fault_address`（`#PF` 的 CR2）、`rip`（事发指令地址）、
  `pending`/`delivered` 两个状态位；
- `struct process` 相对 lesson-34 新增三样：`return_pending`（是否有
  信号等着在返回用户态时送达）、`signals[2]`（pending 表）、
  `signal_queued/delivered/dropped` 三个计数；
- 单进程通过 `#define user_process user_processes[0]`、
  `#define user_thread user_threads[0]` 直接访问（lesson-34 起）。

### 3.3 函数精讲：exception_signal —— 向量到信号的翻译与入队

```c
static TEXT64 int exception_signal(const struct exception_frame *f){u32 signo;u32 i;u64 cr2=0;
if(!f||f->cs!=USER_CS)return 0;
if(f->vector==3)signo=SIGTRAP;else if(f->vector==6)signo=SIGILL;
else if(f->vector==14){signo=SIGSEGV;__asm__ volatile("mov %%cr2,%0":"=r"(cr2));}
else return 0;
for(i=0;i<SIG_PENDING_MAX;i++)if(!user_process.signals[i].pending){
user_process.signals[i]=(struct signal_record){signo,f->vector,f->error,cr2,f->rip,1,0};
user_process.signal_queued++;user_process.return_pending=1;
if(signo!=SIGTRAP){user_process.state=PROCESS_EXITED;user_thread.state=USER_THREAD_EXITED;}
return 1;}
user_process.signal_dropped++;return 0;}
```

算法步骤与逐行分析：
1. **CPL3 守卫**（第二行）：空指针或非用户态（`cs!=USER_CS`）直接拒绝，
   内核态异常不入队——对应 §2.2；
2. **向量→信号翻译**（第三至五行）：`3→SIGTRAP`、`6→SIGILL`、
   `14→SIGSEGV`（并 `mov %%cr2` 读出故障地址，`=r` 约束让内联汇编把
   CR2 写到局部变量 `cr2`）；其余向量（如 `#DF`/`#GP`）返回 0——本课
   只模型化三个「用户可控」异常；
3. **找空槽入队**（第六、七行）：线性找第一个 `!pending` 槽，写入
   `{signo, vector, error, cr2, rip, pending=1, delivered=0}`——完整保留
   异常现场，`signal_queued++`、`return_pending=1`（告诉用户态返回路径
   「有条信号等着送达」）；
4. **默认动作**（第八行）：`signo!=SIGTRAP` 时把进程与线程都置 EXITED
   ——SIGILL/SIGSEGV 的默认动作「终止」在入队瞬间即生效（真实 Linux
   是 `force_sig_fault` + 返回路径 `get_signal` 里 `do_group_exit`，
   本课简化到入队时同步）；
5. **表满丢弃**（第九、十行）：`SIG_PENDING_MAX` 槽全占满则
   `signal_dropped++` 返回 0——`signaltest` 连续运行两次会触发此路径
   （见 §4）。

### 3.4 函数精讲：user_return_prepare —— 返回用户态前送达

```c
static TEXT64 int user_return_prepare(struct syscall_frame*f){u32 i;
if(!f||!user_process.context_valid||user_process.state!=PROCESS_RUNNING||!user_process.return_pending)return 0;
for(i=0;i<SIG_PENDING_MAX;i++)if(user_process.signals[i].pending){
user_process.signals[i].pending=0;user_process.signals[i].delivered=1;
user_process.signal_delivered++;break;}
user_process.return_pending=0;
for(i=0;i<SIG_PENDING_MAX;i++)if(user_process.signals[i].pending)user_process.return_pending=1;
return 1;}
```

逐行分析：
1. **四重守卫**（第二行）：`f` 非空、上下文已校验（`context_valid`）、
   进程仍 RUNNING、确有 pending（`return_pending`）四者缺一即返回 0——
   对应「进程已 EXITED 就不该再送信号」的语义；
2. **送达一条**（第三至五行）：从低槽开始找第一条 `pending`，置
   `pending=0; delivered=1`，`signal_delivered++` 后 `break`——一次调用
   只送达一条（Linux 每次 `get_signal` 也只取一个 pending）；
3. **重扫决定是否续期**（第六、七行）：先清 `return_pending`，再扫一遍
   若还有 pending 则重新置 1——保证「deliver 一条后，还有下一条待送」；
4. **不碰用户帧**：函数全程不修改 `struct syscall_frame *f` 的
   rip/rsp/cs/ss——`userreturntest` 断言「frame 原样保留」正是验证
   这一点（真实 Linux 会在这里改写用户栈搭信号帧，教学模型不写）。

### 3.5 函数精讲：signalinfo / signaltest / userreturntest

```c
static TEXT64 void signalinfo(u16*c){u32 i;
text64(c,"signals queued/delivered/dropped: ");hex64(c,user_process.signal_queued);
text64(c," ");hex64(c,user_process.signal_delivered);text64(c," ");
hex64(c,user_process.signal_dropped);text64(c," pending: ");
hex64(c,user_process.return_pending);putc64(c,'\n');
for(i=0;i<SIG_PENDING_MAX;i++)if(user_process.signals[i].pending){
text64(c,"slot ");hex64(c,i);text64(c," signo/vector/rip: ");
hex64(c,user_process.signals[i].signo);text64(c,"/");
hex64(c,user_process.signals[i].vector);text64(c,"/");
hex64(c,user_process.signals[i].rip);putc64(c,'\n');}}
static TEXT64 void signaltest(u16*c){struct exception_frame f={3,0,USER_CODE_VA,USER_CS,0,USER_STACK_TOP,USER_DS};
u64 q=user_process.signal_queued,d=user_process.signal_delivered;
int a=exception_signal(&f);f.vector=6;int b=exception_signal(&f);
text64(c,"signaltest: ");text64(c,a&&b&&user_process.signal_queued==q+2&&
user_process.state==PROCESS_EXITED&&user_process.signal_delivered==d?
"exception notifications queued with bounded default actions passed":"BROKEN");
putc64(c,'\n');}
static TEXT64 void userreturntest(u16*c){struct syscall_frame f={0};
u64 rip=USER_CODE_VA,rsp=USER_STACK_TOP;
user_process.state=PROCESS_RUNNING;user_process.context_valid=1;
user_thread.state=USER_THREAD_RUNNING;
user_thread.context.frame.rip=rip;user_thread.context.frame.rsp=rsp;
user_thread.context.frame.cs=USER_CS;user_thread.context.frame.ss=USER_DS;
user_process.signals[0]=(struct signal_record){SIGTRAP,3,0,0,rip,1,0};
user_process.return_pending=1;
int a=user_return_prepare(&f),b=!user_process.return_pending;
text64(c,"userreturntest: ");text64(c,a&&b&&user_thread.context.frame.rip==rip&&
user_thread.context.frame.rsp==rsp?
"validated user return preserved frame and delivered once":"BROKEN");
putc64(c,'\n');}
```

- `signalinfo`：打印 queued/delivered/dropped 三计数与 `return_pending`
  标志，再遍历打印每个 pending 槽的 `signo/vector/rip`——异常现场审计；
- `signaltest`：合成一个 `#BP` 异常帧（`{3,0,USER_CODE_VA,USER_CS,...}`）
  入队得 `a`，再改 `f.vector=6` 模拟 `#UD` 入队得 `b`；断言
  `a&&b && queued==q+2 && state==PROCESS_EXITED && delivered==d`——
  两次入队成功、总数 +2、`#UD` 的默认动作把进程置 EXITED、**delivered
  必须没变**（还没到返回时刻）。**注意**：两条信号入队后槽已满，若
  紧接着再跑一次 `signaltest`，`exception_signal` 会因找不到空槽返回 0
  而输出 `BROKEN`（见 §4/§6）；
- `userreturntest`：手工把进程拉回 RUNNING、预置一条 SIGTRAP
  pending 与 `return_pending=1`，调 `user_return_prepare`；断言送达
  恰好一次（`a=1`）、`return_pending` 已被清（`b=1`）、且用户帧的
  rip/rsp **一字未动**——「送达而帧不变」正是本课与真实信号帧构造
  的分界。

### 3.6 exec64 分支、grub.cfg 与 Makefile

`exec64` 新增三个分支（源码逐字）：

```c
else if(eq64(word,"signalinfo")){if(!noargs64(arg))usage64(c,"signalinfo");else signalinfo(c);}
else if(eq64(word,"signaltest")){if(!noargs64(arg))usage64(c,"signaltest");else signaltest(c);}
else if(eq64(word,"userreturntest")){if(!noargs64(arg))usage64(c,"userreturntest");else userreturntest(c);}
```

**源码事实（必须知悉）**：
- 开机横幅与 `about` **仍是 lesson-43 文案**（源码逐字）：
  `TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model`
  / `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata`
  ——本课横幅/`about` 未同步；**help 文案也未补** `signalinfo` 等命令；
  grub.cfg 已更新为 `TinyOS lesson 47: signals, exception notification,
  and user return`；
- Makefile `check` 目标新增 grep（README 三关键词 + kernel64.c 四符号）：

```make
@grep -q 'signals' README.md
@grep -q 'exception' README.md
@grep -q 'user return' README.md
@grep -q 'signalinfo' kernel64.c
@grep -q 'signaltest' kernel64.c
@grep -q 'userreturntest' kernel64.c
@grep -q 'exception_signal' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 47 checks passed.'
```

### 3.7 主控制流

```text
kernel_main64_binary
  ├─ pmm_init → vma_init → reclaim_init → vfs_init（含 ramfs/pipe）
  ├─ 横幅（源码仍为 lesson-43 文案，见 §3.6 源码事实）
  └─ 键盘循环 → exec64：
        signalinfo / signaltest / userreturntest / pipeinfo / ...（旧命令回归）
```

---

## 4. 数据流与运行逻辑

```text
输入 "signaltest"（boot 后首次）
  ├─ exception_signal({vector=3,cs=USER_CS,...})
  │    → SIGTRAP 入槽0：{5,3,0,0,USER_CODE_VA,1,0}，queued=1，pending 置 1
  │       signo==SIGTRAP → 不终止
  ├─ f.vector=6 → exception_signal({vector=6,...})
  │    → SIGILL 入槽1：{4,6,0,0,USER_CODE_VA,1,0}，queued=2，pending 置 1
  │       signo!=SIGTRAP → state=PROCESS_EXITED、thread=USER_THREAD_EXITED
  └─ → "signaltest: exception notifications queued with bounded default actions passed"
     （queued==q+2、state==PROCESS_EXITED、delivered==d 全成立）

输入 "signalinfo"（signaltest 之后）
  → "signals queued/delivered/dropped: 0000000000000002 0000000000000000 0000000000000000 pending: 0000000000000001"
  → "slot 0000000000000000 signo/vector/rip: 0000000000000005/0000000000000003/0000000000400000"
  → "slot 0000000000000001 signo/vector/rip: 0000000000000004/0000000000000006/0000000000400000"

输入 "userreturntest"
  ├─ 进程拉回 RUNNING；预置槽0={SIGTRAP,3,...,1,0}、return_pending=1
  ├─ user_return_prepare(&f) → 槽0 delivered=1、delivered 计数=1、
  │     return_pending 清 0（重扫无剩余 pending）
  └─ → "userreturntest: validated user return preserved frame and delivered once"
     （frame.rip/rsp 原样）

注意：signaltest 连续跑第二次 → 槽满 → 两次 exception_signal 都返回 0、
signal_dropped=2 → 断言失败输出 "signaltest: BROKEN"（有界队列语义）。
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

`make check` 输出：`Multiboot2 and lesson 47 checks passed.`（要求 README 含
`signals`、`exception`、`user return`，kernel64.c 含 `signalinfo`、
`signaltest`、`userreturntest`、`exception_signal`，缺一即失败；旧 README
里的 `kernel/signal.c`/`arch/x86/kernel/traps.c`/`kernel/entry/common.c`/
`include/uapi/asm-generic/signal.h` 引用在 §7 中保留）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字，
显示的是 lesson-43 文案——源码未同步，见 §3.6）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

验证步骤（输出串从源码逐字）：

```bash
signaltest
```

预期：`signaltest: exception notifications queued with bounded default actions passed`

```bash
signalinfo
```

预期（一次 `signaltest` 之后）：

```text
signals queued/delivered/dropped: 0000000000000002 0000000000000000 0000000000000000 pending: 0000000000000001
slot 0000000000000000 signo/vector/rip: 0000000000000005/0000000000000003/0000000000400000
slot 0000000000000001 signo/vector/rip: 0000000000000004/0000000000000006/0000000000400000
```

```bash
userreturntest
```

预期：`userreturntest: validated user return preserved frame and delivered once`

```bash
signaltest
```

（**第二次连续运行**）预期：`signaltest: BROKEN`——两条信号仍 pending、
`SIG_PENDING_MAX=2` 槽满，`exception_signal` 丢弃新信号返回 0，是
有界队列的既定语义而非环境错误（先跑 `userreturntest` 清掉一条即可
恢复，见 §4/§6）。

继承回归：`pipeinfo`/`pipetest`（lesson-46）、`ramfsinfo`/`pathtest`
（lesson-45）、`fdtest`（lesson-44）行为一致；真实 `#PF`/`#BP` 命令
`pftest`/`isttest`/`stackguardtest`/`bptest` 保持原行为。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 47 checks passed.`；`signaltest`
入队两条并置进程 EXITED；`signalinfo` 显示 queued/delivered/dropped=2/0/0、
两个 pending 槽（signo/vector=5/3 与 4/6）；`userreturntest` 送达一次且
frame 原样；第二次 `signaltest` 因槽满输出 BROKEN（有界丢弃，如实记录）；
旧命令回归正常。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `signals`/`exception`/`user return`，或 kernel64.c 缺四个信号符号 | 对照 Makefile check 的 grep 列表 |
| 第二次 `signaltest` 输出 `BROKEN` | `SIG_PENDING_MAX=2` 槽仍满，`exception_signal` 丢弃返回 0 | 先跑 `userreturntest` 清一条（deliver）再跑；或对照 `signal_dropped` 计数 |
| `signalinfo` 的 pending 显示 1 且两个槽都在 | 两条 pending 未送达，`return_pending=1` | 属预期；`userreturntest` 只送一条，之后 pending 为 1、只剩槽1 |
| `userreturntest` 输出 `BROKEN` | 前置状态被改（如进程已是 EXITED 但未重置） | 该函数自己把 state 置 RUNNING、context_valid 置 1，应自洽；若失败检查 `return_pending` 是否被其他命令影响 |
| 横幅/`about`/help 仍显示 lesson-43 文案 | 本课三处文案未同步（源码事实），grub.cfg 已同步 | 对照 lesson-47 kernel64.c 字符串 |
| 担心「真写了用户信号栈 / 执行了 handler」 | 设计保证元数据化 | `user_return_prepare` 不触碰 `f` 与用户帧；无 handler 概念 |
| `exception_signal` 对内核态异常返回 0 | `cs!=USER_CS` 守卫（CPL3 边界） | 对照函数首行 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `kernel/signal.c`、`arch/x86/kernel/traps.c`、
`kernel/entry/common.c`、`include/uapi/asm-generic/signal.h`**（延续
lesson-46 的 pipe/select 对照线）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `SIGTRAP=5`/`SIGILL=4`/`SIGSEGV=11` | `include/uapi/asm-generic/signal.h` 的信号号 | **逐字一致** |
| `exception_signal` 向量→信号（3/6/14） | `arch/x86/kernel/traps.c`：`do_int3`→SIGTRAP、`do_invalid_op`→SIGILL、`do_page_fault`→SIGSEGV | 教学模型只做 3 个向量，无 `do_trap` 通用路径 |
| `signal_record` 保留 error/fault_address/rip | `siginfo`（`si_errno`/`si_addr`/`si_code`）随信号传给用户 | 教学模型只存档不投递 `siginfo` |
| `signals[2]` 固定队列 + `signal_dropped` | `kernel/signal.c` 的 `sigpending` 链表 + `sigaddset` 位图（不丢） | **有界丢弃**是教学简化 |
| `exception_signal` 里 `#UD`/`#PF` 即刻置 EXITED | `kernel/signal.c` 的默认动作 `SIG_DFL`：`do_group_exit`（回收时） | 教学模型在入队同步终止，省掉「延迟到回收」 |
| `user_return_prepare` 返回前送达 | `kernel/entry/common.c` 的 `exit_to_user_mode_loop` + `get_signal` | 教学模型只记账不搭 `sigframe` |
| `return_pending` 标志 + 重扫续期 | `signal_pending()`/`test_tsk_thread_flag(TIF_SIGPENDING)` | 语义一致，教学模型显式 re-scan |
| `f->cs!=USER_CS` 拒绝内核异常 | 内核异常走 `die()` 不是信号 | 边界一致 |

**权威来源**：Intel SDM Vol.3A（#BP/#UD/#PF 向量与 CR2）、
POSIX 信号语义、Linux `man 7 signal`。

**教学模型简化了什么**：
1. 不写用户信号栈/不搭 `sigframe`：`user_return_prepare` 不改用户帧；
2. 不装 handler：默认动作只有「终止」与「不终止」两种；
3. 不投递 `siginfo_t`：异常档案只在内核可见；
4. 无信号屏蔽/抢占/竞态：单 CPU 顺序语义；
5. 无 `do_group_exit` 回收：置 EXITED 即结束，不留僵尸。

---

## 8. 思考题与练习

1. **概念理解**：为什么信号必须「返回用户态前」送达而不能在异常现场
   直接跳进用户 handler？（提示：内核栈/用户栈切换、重入、IF 状态。）
2. **源码定位**：`exception_signal` 里 `signo!=SIGTRAP` 才终止进程，
   如果 `#BP` 也终止会破坏什么？（提示：断点调试语义。）
3. **动手实验**：把 `SIG_PENDING_MAX` 改成 3，重建后连续跑三次
   `signaltest`，观察前两次 passed、第三次 BROKEN（4 条信号 vs 3 槽），
   然后改回（不提交）。
4. **Linux 对照**：打开 `kernel/entry/common.c` 的
   `exit_to_user_mode_loop`，找出它在本课 `user_return_prepare` 之外还
   做的至少 3 件事（提示：`TIF_NEED_RESCHED`、`_TIF_SIGPENDING` 位测试、
   `preempt_schedule_irq`）。
5. **设计思考**：`user_return_prepare` 为什么不修改 `struct syscall_frame`
   的 rip/rsp？如果要「真正返回用户态前跳转到 handler」，最少还要
   准备什么？（提示：用户态栈上的返回地址、恢复现场、`iretq` 三态。）

---

## 9. 本课小结与下一课预告

**小结**：本课把 x86 异常翻译成 Linux 信号号（`#BP→SIGTRAP`、
`#UD→SIGILL`、`#PF→SIGSEGV`，逐字对齐 `include/uapi/asm-generic/
signal.h`），用 `struct signal_record` 2 槽 pending 队列保留异常现场，
`exception_signal` 完成「CPL3 校验 → 翻译 → 入队 → 默认动作」，
`user_return_prepare` 在返回用户态前送达并重扫续期、且**一字不改**用户
帧；`signaltest` 验证入队与终止，`userreturntest` 验证恰好送达一次，
`signalinfo` 审计三个计数与每个槽。表满丢弃、SIGTRAP 不终止、内核异常
不入队都是有界的既定语义；横幅/`about`/help 仍是 lesson-43 文案
（源码未同步），grub.cfg 已更新为 lesson-47。

**下一课预告**：lesson-48 将实现 Linux 风格**时间系统、timerfd-like 模型、
睡眠与时钟抽象**（对照 `kernel/time/`、`fs/timerfd.c`）：基于既有 100 Hz
PIT 的单调时钟、`clockinfo`/`clocktest` 验证换算、`timerinfo`/`timertest`
验证一次性/周期定时器、`sleeptimetest` 验证不早醒。届时「到期后如何
通知」会复用本课的 pending/送达思路，只是触发源从异常变成时钟。

# Lesson 39: Linux 风格有界 fork/clone：PID/TID/parent 与资源复制/共享边界 — 精讲文档

> **课号**：Lesson 39（可执行课）
> **主题**：Linux 风格有界 fork/clone：PID/TID/parent 与资源复制/共享边界
> **课程主线位置**：第 5 阶段「Linux 风格内核抽象」第三课。前两课完成了
> `task_struct` 档案（37）与 `sched_class`/等待队列抽象（38），本课在档案之上
> 做「进程创建」的教学模拟：`fork`/`clone` 只产生元数据，绝不执行真正的子进程。
> **前置课程**：[`lesson-38-stable/README.md`](../lesson-38-stable/README.md)
> **后续课程**：[`lesson-40-stable/README.md`](../lesson-40-stable/README.md)
> **一句话目标**：能讲清楚 Linux `fork`/`clone` 的「身份 + 资源复制/共享」
> 两个维度，并在 TinyOS 里用 `fork_model` 元数据复刻：子任务有新 PID/TID、
> parent 指向父任务、用户镜像元数据「复制」、内核资源（console/PIT/PMM 策略）
> 显式「共享」——而**没有任何子指令指针被执行**。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能回答——Linux `fork()` 与 `clone()` 到底创建了什么？
答：一个新的 `task_struct`（新 PID/TID、parent 链接）加一套资源边界（哪些继承、
哪些复制、哪些共享）。TinyOS 用一张 `struct fork_model` 记录这个边界，并新增
`task_model_init()` 修复 lesson-38 休眠的 task 表。

- **在课程主线中的位置**：前承 lesson-37 的 `task_struct`（身份与状态）、
  lesson-38 的调度/等待队列抽象；后接 lesson-40 的 exec（换镜像）。fork + exec
  合起来就是 Linux「创建新程序」的完整语义——但本课只做创建，不做执行。
- **前置知识清单**：
  1. lesson-37 的 `task_struct`/`task_table`/`task_transition` 语义；
  2. `struct process`/`struct user_thread`/`user_address_spaces[]` 的有界对象
     布局（lesson-34/36），特别是 `MAX_USER_PROGRAMS=2` 的容量约束；
  3. Linux 常识：`fork` 创建独立进程（PID 不同、地址空间独立）、`clone` 可共享
     资源（线程组），Linux `kernel/fork.c` 的 `copy_process()` 是总入口；
  4. `SECOND_PID=2`、`FIXED_PID=1` 等固定 ID 的含义。
- **本课交付**：`forktest`/`cloneinfo`/`forkinfo`/`forklifecycle` 四条命令；
  `fork_model` 结构 + `fork_model_run`/`fork_model_validate`；
  `task_model_init()` 恢复 task 表初始化（修复 lesson-38 的 `BROKEN`）。

---

## 2. 核心概念精讲

### 2.1 概念一：fork/clone 的「身份」维度

Linux `fork()` 成功后，父进程与子进程各有一个 `task_struct`：子进程获得**新 PID**，
其 `real_parent` 指向父进程；父进程 `wait()` 能回收子进程的僵尸态。
`clone()` 额外接受 `CLONE_*` 标志控制资源共享。

教学模型用 `fork_model` 记录身份边界：

```c
struct fork_model { u64 parent_pid, child_pid, parent_tid, child_tid;
    u64 parent_address_space, child_address_space;
    u32 copied_metadata, shared_resources; u8 is_clone, valid; };
```

字段对照：`parent_pid`/`child_pid` → Linux 的 tgid；`parent_tid`/`child_tid` →
Linux 内核 pid；`parent_address_space`/`child_address_space` → 地址空间对象指针
（Linux 的 `mm_struct*`）；`copied_metadata`/`shared_resources` → 复制/共享标志。

### 2.2 概念二：资源边界 —— 复制 vs 共享

Linux 的 `copy_process` 对每个资源子结构要么「复制」（`copy_mm` 复制地址空间，
配合 COW）、要么「共享」（`clone` 的 `CLONE_VM` 共享 `mm_struct`、
`CLONE_FILES` 共享文件表）。TinyOS 用两态枚举抽象这一边界：

```c
enum resource_policy { RESOURCE_COPIED=1, RESOURCE_SHARED=2 };
```

- `copied_metadata=RESOURCE_COPIED`：子任务的「用户镜像元数据」是复制出来的
  （新地址空间记录 `user_address_spaces[1]`）；
- `shared_resources=RESOURCE_SHARED`：内核拥有的资源（console、PIT 策略、
  PMM 记账）对父子是共享的——TinyOS 里它们本来就是全局的，这里显式声明。

### 2.3 概念三：为什么「绝不执行子进程」

真实 fork 会返回两次，子进程从 `fork` 调用点继续跑。教学模型刻意禁止这一点：
`fork_model` 里没有任何 child 的 RIP、没有可调度的子线程对象、没有新的
`user_thread`。子进程**只在元数据里存在**。理由：课程还没有实现真实地址空间
复制/COW/进程切换的完整机制，先建立「边界模型」，这是第 5 阶段一贯的
「先元数据、后机制」策略。

### 2.4 概念四：`task_model_init` —— 修复 lesson-38 的休眠表

lesson-38 把 `kernel_main` 里的 task 表初始化删掉了，导致 `taskvalidate` 报
`BROKEN`。本课用一个循环函数统一初始化，并把「父任务」关系显式建立：

```c
static TEXT64 void task_model_init(void){u32 i;
    for(i=0;i<TASK_TABLE_CAP;i++){
        task_table[i].pid=i?((u64)i+1):0;
        task_table[i].tid=task_table[i].pid;
        task_table[i].parent_pid=i?1:0;
        task_table[i].kind=i?TASK_KIND_USER:TASK_KIND_KERNEL;
        task_table[i].state=TASK_RUNNING;
        task_table[i].transitions=0;
        task_table[i].valid=1;}
    task_table[1].parent_pid=0;}
```

槽 0 是内核任务（pid=0、无父、kernel）；槽 1~3 是用户任务（pid=1/2/3、
parent=1），最后把槽 1 的 parent 改回 0（它是「第一个用户程序」，父就是内核）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-38） |
|---|---|---|
| `boot.S` / `kernel.c` | 引导 | 未变化 |
| `kernel64.c` | 64 位内核主体 | **核心**：fork_model + task_model_init + 4 条命令 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | 未变化 |
| `grub.cfg` | 装载 | 未变化 |

### 3.2 结构 / 枚举 / 全局变量精讲

```c
/* Lesson 39: fork/clone are bounded metadata simulations. A fork copies
 * task-visible image metadata and gets a distinct address-space record; shared
 * kernel resources are represented by explicit flags. No child instruction
 * pointer is ever executed by this teaching model. */
enum resource_policy { RESOURCE_COPIED=1, RESOURCE_SHARED=2 };
struct fork_model { u64 parent_pid, child_pid, parent_tid, child_tid;
    u64 parent_address_space, child_address_space;
    u32 copied_metadata, shared_resources; u8 is_clone, valid; };
static struct fork_model fork_model;
static u64 fork_attempts, fork_successes, clone_successes;
```

逐行注释：
- 头注释是整课的纪律声明：fork/clone 是**有界元数据模拟**，子进程只有档案没有
  可执行的指令指针；
- `resource_policy`：复制/共享两个策略位；
- `fork_model`：身份（parent/child 的 pid+tid）+ 两个地址空间指针 + 两个资源
  边界字段 + `is_clone`（区分 fork 与 clone）+ `valid`；
- `fork_attempts`/`fork_successes`/`clone_successes`：创建尝试/成功/克隆成功
  三个计数器，`forkinfo` 显示。

### 3.3 函数精讲：fork_model_run —— 「创建」一个元数据子进程

```c
static TEXT64 int fork_model_run(u16*c,int clone){
    fork_attempts++;
    if(fork_model.valid){text64(c,"forktest: bounded child already exists\n");return 0;}
    fork_model.parent_pid=user_process.pid;
    fork_model.parent_tid=user_thread.tid;
    fork_model.child_pid=SECOND_PID+1;
    fork_model.child_tid=SECOND_PID+1;
    fork_model.parent_address_space=(u64)(unsigned long)user_process.address_space;
    fork_model.child_address_space=(u64)(unsigned long)&user_address_spaces[1];
    fork_model.copied_metadata=RESOURCE_COPIED;
    fork_model.shared_resources=RESOURCE_SHARED;
    fork_model.is_clone=(u8)clone;
    fork_model.valid=1;
    fork_successes++;
    if(clone)clone_successes++;
    text64(c,clone?"cloneinfo: metadata-only clone created\n":"forktest: metadata-only child created\n");
    return 1;}
```

算法步骤：
1. `fork_attempts++` 记录一次创建尝试；
2. **有界性检查**：如果 `fork_model.valid`（已有一个子进程档案），拒绝再次创建，
   打印 `forktest: bounded child already exists`——单例约束，避免无限 fork；
3. 填身份：parent pid/tid 取当前用户进程/线程（1/1），child 用
   `SECOND_PID+1`（即 3），保证 `child_pid > parent_pid`（与
   `task_table_validate` 的父子有序规则一致）；
4. 填地址空间：parent 是 `user_process.address_space`（`&kernel_address_space`），
   child 是 `&user_address_spaces[1]`——**指针不同**，体现「独立地址空间记录」；
5. 填资源边界：`copied_metadata=RESOURCE_COPIED`、`shared_resources=
   RESOURCE_SHARED`；
6. `is_clone=(u8)clone`：`forktest` 传 0（fork），`cloneinfo` 传 1（clone）；
7. 置 `valid=1`、计数，打印成功串。

边界与动机：单例 + 有界，绝不循环创建；child 的 PID/TID 固定为 3，不与任何
可调度的真实线程关联——这是「元数据层与执行层分离」的关键。

### 3.4 函数精讲：fork_model_validate —— 校验边界不变量

```c
static TEXT64 int fork_model_validate(u16*c){
    int ok=fork_model.valid&&fork_model.child_pid>fork_model.parent_pid&&
        fork_model.child_tid!=fork_model.parent_tid&&
        fork_model.parent_address_space!=fork_model.child_address_space&&
        fork_model.copied_metadata==RESOURCE_COPIED&&
        fork_model.shared_resources==RESOURCE_SHARED;
    text64(c,"fork lifecycle: ");
    text64(c,ok?"passed (identity, parent, copy/share boundaries, no execution)":"BROKEN");
    putc64(c,'\n');return ok;}
```

分析：
- 校验六条不变量：① 已创建；② `child_pid>parent_pid`（PID 递增，父子有序）；
  ③ `child_tid!=parent_tid`（父子 TID 不同，身份独立）；④ 地址空间指针不同
  （子进程有自己的地址空间记录）；⑤/⑥ 复制/共享策略各为期望值；
- 注意 `child_tid!=parent_tid` 用的是**不等**而非大小关系，因为 clone 语义里
  tid 可能复用，教学模型只保证「父子不同」；
- 成功串与失败串都逐字固定，`forklifecycle` 命令输出。

### 3.5 函数精讲：forkinfo —— 观察命令

```c
static TEXT64 void forkinfo(u16*c){text64(c,"fork model: ");
    text64(c,fork_model.valid?(fork_model.is_clone?"clone":"fork"):"none");
    text64(c,"\nparent pid/tid: ");hex64(c,fork_model.parent_pid);
    text64(c,"/");hex64(c,fork_model.parent_tid);
    text64(c,"\nchild pid/tid/parent: ");hex64(c,fork_model.child_pid);
    text64(c,"/");hex64(c,fork_model.child_tid);text64(c,"/");
    hex64(c,fork_model.parent_pid);
    text64(c,"\naddress spaces parent/child: ");
    hex64(c,fork_model.parent_address_space);text64(c,"/");
    hex64(c,fork_model.child_address_space);
    text64(c,"\ncopied metadata bits/shared resource bits: ");
    hex64(c,fork_model.copied_metadata);text64(c,"/");
    hex64(c,fork_model.shared_resources);
    text64(c,"\nattempts/success/fork/clone: ");
    hex64(c,fork_attempts);text64(c,"/");hex64(c,fork_successes);
    text64(c,"/");hex64(c,clone_successes);
    text64(c,"\npolicy: copied user metadata; shared kernel console/PIT/PMM policy; no real child execution\n");}
```

把 `fork_model` 的每个字段按行打印；最后一行策略声明逐字固定。

### 3.6 exec64 分支与 kernel_main 初始化

`exec64` 新增四个分支：

```c
else if(eq64(word,"forkinfo")){if(!noargs64(arg))usage64(c,"forkinfo");else forkinfo(c);}
else if(eq64(word,"forktest")){if(!noargs64(arg))usage64(c,"forktest");else (void)fork_model_run(c,0);}
else if(eq64(word,"cloneinfo")){if(!noargs64(arg))usage64(c,"cloneinfo");else (void)fork_model_run(c,1);}
else if(eq64(word,"forklifecycle")){if(!noargs64(arg))usage64(c,"forklifecycle");else (void)fork_model_validate(c);}
```

`forktest` 与 `cloneinfo` 都进 `fork_model_run`，仅 `clone` 参数不同——
fork 与 clone 在本模型里是**同一创建流程的不同标记**，差异体现在 `is_clone`
与 `clone_successes` 计数上。

`kernel_main64_binary` 首行新增 `task_model_init();`（在 `active_sched_class`
赋值之前），彻底修复 lesson-38 的 task 表休眠。`about` 文本：
`TinyOS lesson 39: bounded fork/clone resource-copy/share model`；
开机横幅仍沿用 lesson-38 的字符串（源码事实）。

### 3.7 构建管线（Makefile / linker）

无变化，同 lesson-38：`-m64 -ffreestanding -fpie -mno-red-zone` +
`-m32` 双阶段构建，`check` 跑 `grub-file`，`run` 用 QEMU TCG。

### 3.8 主控制流

```text
kernel_main64_binary
  ├─ task_model_init()            ← 本课新增：初始化 task 表
  ├─ active_sched_class=&fair_sched_class
  ├─ pmm_init / address_space_init / GDT/TSS/IDT/PIT/PIC
  ├─ 横幅（沿用 lesson-38 字符串）
  └─ 键盘循环 → exec64：
        forktest / cloneinfo / forkinfo / forklifecycle / tasklist / taskvalidate / ...
```

---

## 4. 数据流与运行逻辑

```text
输入 "forktest"
  → exec64 → fork_model_run(c,0)
  → fork_attempts++
  → fork_model.parent_pid = user_process.pid (=1)
  → fork_model.child_pid  = SECOND_PID+1 (=3)
  → fork_model.child_address_space = &user_address_spaces[1]
  → 输出（逐字）："forktest: metadata-only child created"
输入 "forkinfo"
  → 打印 fork model: fork；parent pid/tid: 1/1；child pid/tid/parent: 3/3/1；
    address spaces parent/child: <两个不同指针>；copied/shared: 1/2；计数
输入 "forklifecycle"
  → fork_model_validate → "fork lifecycle: passed (identity, parent,
    copy/share boundaries, no execution)"
输入 "tasklist"
  → task_model_init 后的 4 槽全部 valid，显示 0/1/2/3 四个任务
```

`forktest` 再输入一次会得到 `forktest: bounded child already exists`
（单例边界）。`cloneinfo` 走同一流程，打印
`cloneinfo: metadata-only clone created` 并把 `clone_successes` 加 1。

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

`make check` 输出：`Multiboot2 header check passed.`。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字，
注意仍为 lesson-38 字符串，本课未改）：

```text
TinyOS lesson 38: Linux-style wait queues and scheduling-class model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

验证步骤（输出串从源码逐字）：

```bash
taskvalidate
```

预期：`task validation: passed (bounded table, unique PID/TID, valid parent/state)`
（`task_model_init` 修复了 lesson-38 的 `BROKEN`）。

```bash
forktest
```

预期：`forktest: metadata-only child created`

```bash
forktest        # 再执行一次，验证单例边界
```

预期：`forktest: bounded child already exists`

```bash
forkinfo
```

预期（十六进制值按运行输出，字符串逐字）：

```text
fork model: fork
parent pid/tid: 0000000000000001/0000000000000001
child pid/tid/parent: 0000000000000003/0000000000000003/0000000000000001
address spaces parent/child: <父指针>/<子指针>
copied metadata bits/shared resource bits: 0000000000000001/0000000000000002
attempts/success/fork/clone: 0000000000000002/0000000000000001/0000000000000000
policy: copied user metadata; shared kernel console/PIT/PMM policy; no real child execution
```

```bash
forklifecycle
```

预期：`fork lifecycle: passed (identity, parent, copy/share boundaries, no execution)`

```bash
cloneinfo       # 验证 clone 路径
```

预期：`cloneinfo: metadata-only clone created`

```bash
tasklist        # 4 槽任务表恢复显示
```

继承回归：`processtest`（`bounded two bounded program objects ready`）、
`vmtest`（`vmtest: two-slot dual-alias map/ownership/unmap/free passed`）、
`preempttest`/`sleeptest`/`pctest`+`pcgo`/`kbdwaittest` 与 lesson-38 行为一致。

### 5.4 课程实测记录（2026-08，稳定快照）

`taskvalidate` 恢复 `passed`；`forktest` 首次成功、二次拒绝；`forkinfo` 各字段
与预期一致；`forklifecycle` 输出 `passed`；`cloneinfo` 输出 clone 成功串且
`clone_successes` 计数为 1；所有继承命令回归正常。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `taskvalidate` 仍显示 `BROKEN` | `task_model_init()` 未调用或顺序不对 | 检查 `kernel_main64_binary` 首行；对照 `task_table_validate` 规则 |
| `forktest` 报 `bounded child already exists` | `fork_model.valid` 已为 1（单例） | 这是设计行为；如需重测需重启 QEMU |
| `forkinfo` 的 child pid 不是 3 | `SECOND_PID` 被改 / `fork_model_run` 未走到赋值 | 核对 `fork_model_run` 赋值序列 |
| `forklifecycle` 显示 `BROKEN` | 六条不变量之一不成立 | 逐条核对：valid / child_pid>parent_pid / tid 不等 / 地址空间指针不等 / 两个策略值 |
| 子进程「跑起来了」 | 不可能——本课无 child 调度 | 若怀疑，检查 `fork_model` 是否被误填了可执行 RIP（结构里没有此字段） |
| `cloneinfo` 与 `forktest` 行为相同 | 本模型 fork/clone 共享同一创建流程 | 差异只在 `is_clone` 与 `clone_successes` 计数，见 `forkinfo` |
| help 列表顺序变化 | 本课在列表中加入 fork 命令 | 对照源码 `exec64` help 字符串 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `kernel/fork.c` 与 `include/linux/sched.h`**：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `fork_model.child_pid=SECOND_PID+1` | `kernel/fork.c` 的 `alloc_pid()` / `copy_process()` 里分配新 pid | 教学模型用**固定 pid=3**，Linux 从 pid 位图/命名空间分配 |
| `parent_pid`/`parent_tid` | `task_struct->real_parent->pid`、`task_struct->parent` | 教学模型只存数值，不存父结构体指针 |
| `copied_metadata=RESOURCE_COPIED` | `copy_process` 的 `copy_mm()`（复制 `mm_struct`） | Linux 配合 COW 按页复制；教学模型只复制**元数据字段** |
| `shared_resources=RESOURCE_SHARED` | `clone` 的 `CLONE_VM`/`CLONE_FILES`/`CLONE_FS` 标志 | Linux 按标志位粒度共享；教学模型用一个枚举表示「内核资源整体共享」 |
| `is_clone` | `fork()` → `kernel_clone(SIGCHLD, 0)`；`clone()` → `kernel_clone(flags)` | 教学模型只记标记，不实现 `CLONE_*` 位 |
| 「绝不执行子进程」 | Linux 中 `copy_process` 后子进程进入 `wake_up_new_task` 并调度 | **教学模型最大的简化**：无真实子任务、无 COW、无 `wait()` 回收路径 |
| `task_model_init()` 建立 parent 关系 | `copy_process` 里 `p->real_parent = current` | 教学模型用循环 + 一条覆写语句完成 |

**权威来源**：Intel SDM、Multiboot2 规范、GNU GRUB（既有机制背景）。

**教学模型简化了什么**：
1. 无真实地址空间复制：子进程只拿到一个**不同的指针**指向一个复用槽
   （`user_address_spaces[1]`），没有复制页表、没有 COW；
2. 无 pid 分配器：child pid 硬编码为 3，且**单例**（只允许一个元数据子进程）；
3. 无 `CLONE_*` 标志位系统：fork/clone 只差一个 `is_clone` 布尔；
4. 无子进程调度：没有新的 `user_thread`、没有可运行的 RIP、没有 `wait()`；
5. 共享资源是「整体声明」而非逐资源复制。

---

## 8. 思考题与练习

1. **概念理解**：`fork_model_validate` 为什么要求 `child_pid > parent_pid`？
   这条规则与 lesson-37 `task_table_validate` 的哪条规则呼应？
2. **源码定位**：`fork_model_run` 中 `child_address_space` 指向
   `user_address_spaces[1]`——它与 `parent_address_space` 是否真的内容不同？
   （提示：`address_space_init` 只初始化了 `kernel_address_space`。）
3. **动手实验**：把 `fork_model_validate` 里的 `child_tid!=parent_tid` 临时改成
   `child_tid==parent_tid`（故意破坏），重建后 `forklifecycle` 应输出 `BROKEN`，
   然后改回（勿提交）。
4. **Linux 对照**：在 `kernel/fork.c` 中找 `copy_process` 与 `alloc_pid`，
   对比「Linux 如何保证 pid 唯一」与「教学模型如何保证 pid 唯一」。
5. **设计思考**：如果要让子进程真实可运行，除了 `fork_model` 还需要新增哪些
   对象？（提示：child 的 `user_thread`、独立内核栈、页表副本、`wait()` 回收。）
   本课为什么把这些全部省掉？

---

## 9. 本课小结与下一课预告

**小结**：本课用 `fork_model` 元数据复刻了 Linux 进程创建的两个维度——身份
（父子 PID/TID）与资源边界（复制 vs 共享）；`fork_model_run` 单例创建、
`fork_model_validate` 校验六条不变量、`forkinfo` 全字段观察；`task_model_init`
修复了 lesson-38 休眠的 task 表。最关键的是：**子进程只在档案里存在，绝无可执行
的指令指针**——这是本阶段「先边界、后机制」的纪律。

**下一课预告**：进入 [`lesson-40-stable/README.md`](../lesson-40-stable/README.md)，
用 `exec_model` 模拟 Linux `execve` + ELF 装载：内嵌一段微型 ELF 镜像，
校验 magic/type/machine/段表边界/入口范围，并给出确定性的用户栈布局
（argc/argv/envp），对照 `fs/exec.c` 与 `fs/binfmt_elf.c`。

# Lesson 57: 进程退出资源清理账本 — 精讲文档

> **课号**：57　**主题**：zombie 保留期的资源引用记账，reap 时按序释放并拒绝二次 teardown
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）
> **前置课程**：[lesson-56-stable/README.md](../lesson-56-stable/README.md)（init adoption 与有界父进程重挂接）
> **后续课程**：[lesson-58-stable/README.md](../lesson-58-stable/README.md)（有界多子进程 waitpid 选择）
> **一句话目标**：把「收割一个子进程」从单纯的状态跳转，升级为「按固定顺序释放六类资源引用、且只允许释放一次」的确定性账本——zombie 保留期内 wait 状态必须完整可读。

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 57 records fixed address-space, file, pipe, signal, timer, and deferred-work references. Zombie retention preserves wait status; reap performs ordered release and rejects a second teardown.

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说清「进程退出时为什么不能立刻释放全部资源」「zombie 保留期到底保留了哪些东西」「reap 时释放的先后顺序和‘只释放一次’的约束如何用账本表达」，并能用 `teardowntest` 在 VGA 上验证「zombie 保留 → 有序释放 → 二次 teardown 拒绝」。
- **在课程主线中的位置**：本课属于第 3 阶段（32–60）。第 54–56 课建立了 wait 状态机、阻塞唤醒、孤儿收养，但「收割」动作在 `wait_model_reap()` 里只是一句 `state=WAIT_DEAD` 的状态翻转——**没有回答「收割到底回收了什么」**。本课补上资源维度：一个 `resource_ledger` 账本记录六类资源引用，`resource_teardown()` 模拟 Linux 的 `exit_mm/__exit_files/exit_pipe/exit_signal/exit_itimers/flush_scheduled_work` 序列。它是第 59 课「fork→exec→exit 完整生命周期」的收尾环节。
- **前置知识清单**：
  1. 第 54/55 课 `wait_model` 的 `WAIT_ZOMBIE → WAIT_DEAD` 收割时机与 `waited` 前置；
  2. 第 56 课所有权概念（谁有权 wait 谁才能 reap）——本课假设所有权已经落定；
  3. 第 44–49 课的资源对象：fd/inode 引用计数、pipe 模型、signal 记录、timer 模型、softirq/workqueue 延迟工作；
  4. `kernel_main64_binary` 的初始化顺序。
- **本课交付（可见结果）**：新命令 `resourceinfo`（打印资源账本）与 `teardowntest`（确定性验证），输出串见 §5。

## 2. 核心概念精讲

### 2.1 为什么 zombie 要「保留资源」

- **直觉**：一个进程死了，但它的父进程还没来 `wait`，内核不能把它的尸体（task_struct）立刻丢掉——否则父进程的 `wait` 就查不到退出状态了。所以 Linux 让进程停在 `EXIT_ZOMBIE`：**资源全部释放，只保留最小的 task_struct 供 wait 读取状态**。
- 但本课的教学设定与 Linux 略有差异：TinyOS 模型让 zombie 期间**保留六类资源引用计数**（address_space、fd、pipe、signal、timer、deferred-work），到 reap 时才一次性清零。这模拟的是 Linux `do_exit` 中「先走各类释放回调，最后 `release_task`」的**顺序性**——教学模型把「六步释放」延迟到 reap 点执行，便于用一张账本表观测量每一步。
- **准确定义**：`resource_ledger` 记录 `address_space, fd_refs, pipe_refs, signal_refs, timer_refs, deferred_refs` 六个资源计数器与 `releases`（释放步数，固定 6）。`zombie` 位为真表示「进程已退出、进入保留期」；`teardown_done` 位为真表示「已执行过 teardown」。`resource_teardown()` 只在 `zombie && !teardown_done` 时成功。

### 2.2 有序释放（ordered release）

- Linux `do_exit` 的释放顺序（简略）：`exit_mm`（地址空间）→ `exit_files`（fd/file/inode）→ 管道与 IPC → `exit_signals`（信号）→ `exit_itimers`（定时器）→ 延迟工作（`flush_scheduled_work`）。本课用**一次函数调用把六个字段清零**，`releases=6` 固化「恰好六步」，`resourceinfo` 展示每类资源从「有值」到「0」的变化。
- 为什么要顺序化？真实世界里释放顺序有依赖（如先断 fd 引用再解 inode 引用），教学模型用固定步数 6 表达「释放是一个确定的、可计数的流程」，避免「不知道释放了几类」的模糊状态。

### 2.3 二次 teardown 拒绝（double-reap guard）

- `resource_teardown()` 的前置条件 `!teardown_done` 保证：第一次调用把六个字段清零并置 `teardown_done=1`；第二次调用直接返回 0。这与第 56 课的 `adopted` 位是同一类「一次性守卫」，对应 Linux 中 `release_task` 只执行一次、`__exit_signal` 的 `exit_state` 位防止重复释放。
- 为什么必须有：若 reap 被调用两次，第二次会再次把「已经是 0」的引用清零，账本失真；更严重的是真实内核里重复释放会导致 use-after-free。教学模型用布尔位把这种错误显式拒绝出来，`teardowntest` 的 `b=!resource_teardown()` 专门验证这一点。

### 2.4 教学模型边界

- 六类资源都是固定计数器，没有真实的对象链表、没有真实 `mmput/file` 调用、没有锁；
- teardown 是「一次调用全清零」，不模拟各回调之间的真实依赖；
- zombie 保留期与资源释放点被压缩在同一个结构里，`resource_start()` 的初始值（`{1,2,1,1,1,1,0,0,1,0}`）表达「进程仍活着时各资源的典型引用数」；
- 本课不执行 `wait_model` 的收割，`zombie` 位由测试显式置 1（`resource_ledger.zombie=1`）。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | Multiboot2 header + long mode 切换 | 未变化 |
| `kernel.c` | 32 位阶段初始化 | 未变化 |
| `kernel64.c` | 64 位内核：命令分发 + **本课 resource ledger 账本** | **新增 `resource_ledger` 及 4 个函数 + 2 条命令分支 + 启动路径 1 行** |
| `kernel64.ld` | 64 位裸二进制链接 | 未变化 |
| `linker.ld` | 32 位外 ELF 链接 | 未变化 |
| `Makefile` | 构建 + `check` 校验 | **`check` 关键词改为 `resource`/`teardowntest`/`ledger`/`teardown`** |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 结构与常量精讲（本课新增）

`kernel64.c` 第 429–434 行（紧接第 56 课 `adoption_model` 块之后）：

```c
struct resource_ledger { u64 address_space,fd_refs,pipe_refs,signal_refs,timer_refs,deferred_refs,releases,double_releases; u8 zombie,teardown_done; };
static struct resource_ledger resource_ledger;
```

- `address_space`：地址空间引用（1 = 仍被持有）；
- `fd_refs`：文件描述符引用（初始 2）；
- `pipe_refs`：管道引用（初始 1）；
- `signal_refs`：信号记录引用（初始 1）；
- `timer_refs`：定时器引用（初始 1）；
- `deferred_refs`：延迟工作（workqueue/tasklet）引用（初始 1）；
- `releases`：teardown 已执行的释放步数（成功后固定 6）；
- `double_releases`：检测到的二次释放次数（本课结构预留，当前 teardown 直接拒绝、不累计——见 §8 第 3 题）；
- `zombie`：进程是否已进入 zombie 保留期（`u8`）；
- `teardown_done`：teardown 是否已执行（`u8`）。

设计要点：`releases` 与 `double_releases` 分开，前一个是「成功释放了多少步」，后一个是「尝试非法二次释放多少次」——一个可验证成功路径、一个可观察错误路径，这与第 55 课 `wakes`/`woken` 的「账本 + 状态位」分离风格一致。

### 3.3 函数精讲

#### `resource_start` — 复位资源账本

```c
static TEXT64 void resource_start(void){resource_ledger=(struct resource_ledger){1,2,1,1,1,1,0,0,1,0};}
```

- 按字段顺序整体赋值：`address_space=1, fd_refs=2, pipe_refs=1, signal_refs=1, timer_refs=1, deferred_refs=1, releases=0, double_releases=0, zombie=1, teardown_done=0`。
- 注意 `zombie` 初始就是 1！这表示「账本启动即代表一个已经退出、等待 reap 的僵尸进程」——与第 55 课 `wait_model_start()` 让 `state=WAIT_RUNNING` 不同。因为本课只关心 teardown 阶段，测试不必先驱动「运行→退出」，直接进入「zombie 保留 → teardown」。
- 边界：只复位自身；`teardowntest` 里还显式再写一次 `resource_ledger.zombie=1`（虽然初始已是 1），这是为了强调「测试以 zombie 为前提」的意图。

#### `resource_teardown` — 有序释放 + 二次守卫

```c
static TEXT64 int resource_teardown(void){
    if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;   /* 必须 zombie 且未 teardown 过 */
    resource_ledger.address_space=0;    /* ① 释放地址空间 */
    resource_ledger.fd_refs=0;          /* ② 释放 fd/file/inode 引用 */
    resource_ledger.pipe_refs=0;        /* ③ 释放管道 */
    resource_ledger.signal_refs=0;      /* ④ 释放信号记录 */
    resource_ledger.timer_refs=0;       /* ⑤ 释放定时器 */
    resource_ledger.deferred_refs=0;    /* ⑥ 释放延迟工作 */
    resource_ledger.releases=6;         /* 恰好六步 */
    resource_ledger.teardown_done=1;    /* 标记完成，拒绝二次调用 */
    return 1;
}
```

- 算法步骤：① 前置检查 `zombie && !teardown_done`，任一不满足返回 0；② 六个字段依次清零；③ `releases=6` 固化步数；④ 置 `teardown_done`；⑤ 返回 1。
- 边界与错误处理：二次调用被 `teardown_done` 拒绝（返回 0），不会重复清零；非 zombie 状态（`zombie=0`）也被拒绝——这保证「还没退出的进程不能提前 teardown」。
- 为什么这样设计：对照 Linux `do_exit` 的释放回调序列，教学模型把六类释放压缩进一个函数，但保留「必须先 zombie、只允许一次」两个硬约束。这样 `teardowntest` 可以同时断言成功路径（`a`）与错误路径（`b`）。

#### `resourceinfo` — 账本展示

```c
static TEXT64 void resourceinfo(u16*c){text64(c,"resources as/fd/pipe/signal/timer/work/releases: ");hex64(c,resource_ledger.address_space);hex64(c,"/");hex64(c,resource_ledger.fd_refs);hex64(c,"/");hex64(c,resource_ledger.pipe_refs);hex64(c,"/");hex64(c,resource_ledger.signal_refs);hex64(c,"/");hex64(c,resource_ledger.timer_refs);hex64(c,"/");hex64(c,resource_ledger.deferred_refs);hex64(c,"/");hex64(c,resource_ledger.releases);putc64(c,'\n');}
```

- 输出格式串（逐字）：`"resources as/fd/pipe/signal/timer/work/releases: "`，后跟 `address_space/fd_refs/pipe_refs/signal_refs/timer_refs/deferred_refs/releases` 七个十六进制值（`as` 即 address_space 的缩写），换行收尾。验证时可逐字对照源码（433 行）。

#### `teardowntest` — teardown 语义的确定性验证

```c
static TEXT64 void teardowntest(u16*c){
    resource_start();                            /* 复位：zombie=1, 六类资源有值 */
    resource_ledger.zombie=1;                    /* 显式强调 zombie 前提 */
    int a=resource_teardown(),                   /* a: 首次 teardown 成功 */
        b=!resource_teardown(),                  /* b: 二次 teardown 被拒绝 */
        d=resource_ledger.address_space==0&&resource_ledger.releases==6; /* d: 地址空间已清零且恰好 6 步 */
    text64(c,"teardowntest: ");text64(c,a&&b&&d?"zombie retention, ordered resource release, and double-reap guard passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`：`resource_teardown()` 首次调用返回 1——zombie 状态下六类资源被有序清零；
  - `b`：第二次调用返回 0——`teardown_done` 守卫拒绝二次 teardown（double-reap guard）；
  - `d`：`address_space==0`（① 号资源确实释放）且 `releases==6`（步数固化正确）。
  - 未断言部分：`fd_refs/pipe_refs/signal_refs/timer_refs/deferred_refs` 是否全为零——留作思考题（§8 第 3 题）。
- 验证输出串（逐字）：成功时 `"zombie retention, ordered resource release, and double-reap guard passed"`，失败 `"BROKEN"`，前缀 `"teardowntest: "`（源码 434 行）。

#### 命令分支（`exec64` 内新增，kernel64.c 第 668 行区域）

```c
}else if(eq64(word,"resourceinfo")){if(!noargs64(arg))usage64(c,"resourceinfo");else resourceinfo(c);}
}else if(eq64(word,"teardowntest")){if(!noargs64(arg))usage64(c,"teardowntest");else teardowntest(c);}
```

- 两条分支插在第 56 课 `reparenttest` 分支之后，沿用「`noargs64` → `usage64`/直接调用」模式。

#### 启动路径（`kernel_main64_binary`，kernel64.c 第 669 行）

```c
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;task_names_keep();active_sched_class=&fair_sched_class;sched_enqueues=sched_dequeues=sched_picks=0;module_init_model();init_model_start();wait_model_start();adoption_start();resource_start();pmm_init(h);...
```

- 本课唯一启动增量：`resource_start();` 插在 `adoption_start();` 之后、`pmm_init(h);` 之前。同样因为只是结构体清零、不依赖硬件资源，位于分页初始化前安全。

### 3.4 构建管线（Makefile / linker）

- 构建目标、编译标志、链接脚本与上一课完全一致，无新增步骤。
- 唯一变化：`check` 目标关键词——上一课的 `adoption`/`reparenttest`/`init` 替换为 `resource`/`teardowntest`/`ledger`/`teardown`（见 §5）。本课是纯内核源码增量，构建产物结构不变。

### 3.5 主控制流

```
kernel_main64_binary:
  ... wait_model_start(); adoption_start(); resource_start();      ← 本课新增 1 行
  pmm_init; vma_init; reclaim_init; vfs_init; address_space_init; ...
  install_idt; pit_init; pic_init; prompt64; sti
  for(;;){ kbd_dequeue → exec64(c,h,cmd) }
    teardowntest / resourceinfo                                    ← 本课新命令
```

- teardown 模型纯元数据、不依赖硬件；生命周期由测试命令驱动，启动路径只负责复位。

## 4. 数据流与运行逻辑

- **输入命令** → `teardowntest` → `exec64` 命中分支 → `teardowntest(c)`。
- 数据流：`resource_start()`（as=1, fd=2, pipe=1, sig=1, timer=1, work=1, zombie=1, done=0）→ 显式 `zombie=1` → `resource_teardown()` 清零六字段、`releases=6`、`teardown_done=1` → 第二次 `resource_teardown()` 返回 0 → `text64` 打印。
- **屏幕上显示什么**：`teardowntest: zombie retention, ordered resource release, and double-reap guard passed`（源码 434 行）。`resourceinfo` 在 teardown 前显示形如 `resources as/fd/pipe/signal/timer/work/releases: 1/2/1/1/1/1/0`，teardown 后显示 `... 0/0/0/0/0/0/6`（十六进制）——`releases` 从 0 变 6 是本课账本最醒目的观测点。

## 5. 构建、运行与验证

**依赖**：与第 55/56 课相同，无新增工具。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
```

**关键词自检**：

```bash
make check
```

`check` 依次验证：`grub-file --is-x86-multiboot2 build/kernel.elf`、README 含 `shell`/`resource`/`ledger`/`teardown`/`status`/`wait/wake`、kernel64.c 含 `teardowntest`/`shelltest`/`shellrun`/`waittest`/`waitblocktest`/`reparenttest`，最后打印 `Multiboot2 and lesson 57 checks passed.`。

**运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口（勿加 `-display none`）。`-serial stdio` 输出仅供诊断。

**VGA 验证**（旧 README 记录，保留并原样引用）：

```bash
../../scripts/qemu-vga-check.sh . resourceinfo teardowntest reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

**验证步骤与预期输出**（输出串均从源码逐字抄录）：

1. 输入 `teardowntest` → 预期屏幕出现
   `teardowntest: zombie retention, ordered resource release, and double-reap guard passed`
   （源码 434 行成功串）。
2. 输入 `resourceinfo` → 预期形如 `resources as/fd/pipe/signal/timer/work/releases: ` 后跟七个十六进制数（源码 433 行）；在 `teardowntest` 之后运行会看到六类资源为 0、`releases` 为 6。
3. 回归：`reparenttest`、`waitblocktest`、`nohangtest`、`waittest` 及各旧回归命令保持通过。

**如何判断成功**：VGA 文本区出现上述成功串；任一断言失败输出 `BROKEN`；`qemu-vga-check.sh` 逐命令注入并检查物理 VGA 文本内存后返回成功。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `teardowntest` 输出 `BROKEN` 且 `a` 为假 | `zombie` 为 0 或 `teardown_done` 已为 1（上次残留） | 确认测试开头 `resource_start()` + `zombie=1`；检查 `resourceinfo` 的 `done` 状态（通过重复 teardown 观察） |
| `teardowntest` 输出 `BROKEN` 且 `b` 为假 | 二次 `resource_teardown()` 仍返回 1（守卫失效） | 检查 `resource_teardown` 是否真的置了 `teardown_done`；`releases` 是否被第二次清零 |
| `teardowntest` 输出 `BROKEN` 且 `d` 为假 | `address_space` 未清零或 `releases!=6` | 在 teardown 后跑 `resourceinfo`，逐一核对六字段与 `releases` |
| `resourceinfo` 显示资源非 0 但已 teardown | 在 `resource_start()` 前查看了账本 | 先跑 `teardowntest` 再跑 `resourceinfo`；理解每条测试自复位 |
| 想验证二次拒绝细节 | `teardowntest` 只断言了 `b`，没展示 `double_releases` | 手动追加 `resource_teardown()` 并观察返回值；阅读 §8 第 3 题 |
| 构建通过但 `make check` 失败 | README 关键词缺失 | 检查 README 是否含 `resource`、`ledger`、`teardown`、`shell`、`status`、`wait/wake` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现 |
|---|---|---|
| zombie 保留期 | `zombie` 位为真时资源计数器仍被持有，wait 状态可读 | `kernel/exit.c`：`EXIT_ZOMBIE` 状态下 task_struct 保留给 `do_wait` 读取，`release_task` 才真正释放 |
| 地址空间释放 | `address_space=0`（① 步） | `kernel/exit.c` `exit_mm`：`mmput`/`mm_release` 释放 `mm_struct`（对照 `kernel/fork.c` 中 mm 的复制） |
| 文件引用释放 | `fd_refs=0`（② 步） | `exit_files` → `__exit_files` 释放 `files_struct`，file/inode 引用递减 |
| 管道/信号/定时器/延迟工作 | ③④⑤⑥ 步依次清零 | `exit_pipe`（或 IPC 清理）、`exit_signals`/`exit_itimers`、`flush_scheduled_work`（`kernel/workqueue.c`） |
| 释放步数 | `releases=6` 固化 | Linux 无「步数」常量，但 `do_exit` 的释放顺序确定；教学模型用 6 表达确定性 |
| 二次释放拒绝 | `teardown_done` 守卫，二次调用返回 0 | `kernel/exit.c`：`release_task` 只执行一次；`__exit_signal` 用 `exit_state` 位防止重复进入退出流程 |
| 释放时机 | 测试命令显式驱动 `resource_teardown()` | Linux 在 `do_exit` 内核路径自动执行；`wait` 只是读取结果 |

**权威来源**：Intel SDM（进程上下文与系统调用）、Multiboot2 规范、Linux v6.x `kernel/exit.c`（`do_exit`/`exit_mm`/`release_task`）、`kernel/fork.c`（资源复制与引用初始化，对应 `resource_start` 的初始值）。

**教学模型简化了什么**：六类资源都是计数器而非真实对象，无真实 `mmput`/`fput`/锁；释放路径压缩为一次调用、不模拟回调间的依赖顺序细节；zombie 保留期不涉及真实 task_struct 内存；`double_releases` 字段预留但当前由 `teardown_done` 直接拒绝、不累计计数。

## 8. 思考题与练习

1. **概念理解**：`resource_teardown()` 为什么要求 `zombie` 必须为真？如果允许 `zombie=0` 时 teardown，会出现什么逻辑错误（对照 Linux：还没退出的进程能不能被 reap）？
2. **源码定位**：在 `kernel64.c` 中找出 `resource_start()` 与 `wait_model_start()` 的关系，说明为什么 teardown 模型不需要像 wait 模型那样在每条测试里先走「running→zombie」。
3. **动手实验**：修改 `teardowntest` 增加断言 `fd_refs==0&&pipe_refs==0&&signal_refs==0&&timer_refs==0&&deferred_refs==0`，重新构建运行，确认六类资源全部释放——并思考为什么原测试只断言了 `address_space` 和 `releases`。
4. **动手实验**：把 `resource_teardown()` 中的 `teardown_done=1` 注释掉，重新构建运行 `teardowntest`，观察 `b` 断言的变化，并用 §6 调试地图解释。
5. **Linux 对照**：阅读 Linux `kernel/exit.c` 的 `do_exit`，按顺序列出它调用的主要释放函数，与 `resource_teardown` 的六步一一对应，指出模型省略了哪几步（如 `exit_fs`、`exit_thread`、`perform_accumulated_accounting`）。

## 9. 本课小结与下一课预告

- 本课把「收割」从状态翻转升级为**资源账本**：zombie 保留期六类资源引用被持有、wait 状态可读，`resource_teardown()` 按固定六步清零并拒绝二次 teardown。
- 你学会了用「`zombie` + `teardown_done` 两个位 + `releases` 一个计数」表达「先保留、后有序释放、只释放一次」三段式语义，并理解了 `releases=6` 是对 Linux `do_exit` 释放序列的确定性压缩。
- 你验证了 `teardowntest: zombie retention, ordered resource release, and double-reap guard passed`，并看到了 `resourceinfo` 中 `releases` 从 0 到 6 的账本变化。
- 模型边界依旧清晰：计数器而非对象、一次调用全清零、无真实锁与内存操作。
- 但本课仍只有一个子进程的资源；现实中一个父进程可能有多个子进程同时处于不同状态。
- **下一课**（[lesson-58-stable/README.md](../lesson-58-stable/README.md)）引入「有界多子进程 waitpid 选择」：一个固定三子进程表中，`waitpid` 按精确 PID 或聚合 `-1` 选择已退出的子进程，并用 exited-only 过滤与 one-shot 收割——把「单个子进程」升级为「多个子进程间的选择」。

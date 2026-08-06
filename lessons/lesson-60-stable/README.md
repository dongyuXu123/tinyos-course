# Lesson 60: 受控用户空间 job/session 模型 — 精讲文档

> **课号**：60　**主题**：固定 init/shell 会话管理至多两个 job，组合 argv/env、描述符、管道、信号、定时器、延迟工作与 wait/reap/teardown 元数据
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）**收官课**；第 5 阶段（68–87）进程组/session 的前置
> **前置课程**：[lesson-59-stable/README.md](../lesson-59-stable/README.md)（fork → exec → exit 完整元数据生命周期）
> **后续课程**：[lesson-61-stable/README.md](../lesson-61-stable/README.md)（第 4 阶段 GUI 主线：framebuffer 与图形输出）
> **一句话目标**：建立「session（会话）拥有 shell，shell 拥有至多两个 job（作业）」的层级模型，让每个 job 同时挂载 argv/env、fd、pipe、signal、timer、deferred-work 与 wait/reap/teardown 元数据——为第 5 阶段真正的进程组/session/job control 铺路。

> **Course status: stable snapshot (validated; verified build artifacts included).**

A fixed init/shell session owns at most two child jobs and composes argv/env, descriptors, pipes, signals, timers, deferred work, wait/reap, and resource teardown metadata without executing arbitrary user code.

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能画出「session → shell → job」的层级，说清一个 job 同时要携带哪些类别的元数据（程序参数、文件、管道、信号、定时器、延迟工作、退出状态），以及 job 退出后资源如何按序清零、第二次 reap 如何被拒绝——并用 `jobtest`/`sessioninfo` 在 VGA 上验证。
- **在课程主线中的位置**：本课是**第 3 阶段（32–60）的收官课**。此前各课把用户空间逐个零件建模：进程/线程（34）、任务模型（37）、fork（39）、exec（40）、wait/zombie（54）、阻塞/非阻塞 wait（55）、收养（56）、资源账本（57）、多子进程选择（58）、生命周期闭环（59）。第 60 课把这些零件**横向组合**成「一个会话里跑两个 job」的受控模型——这是阶段 3 验收（运行一条命令 → 输出 → 退出 → 父进程收割）的最终形态，也是阶段末（60）直接衔接第 5 阶段（68–87，进程组/session/调度/COW）的桥：`session_job` 的 `pid` 字段将升级为 Linux 的 `task_struct->pid/tgid`，session 将升级为 `session->pgrp`。
- **前置知识清单**：
  1. 第 59 课 `fork_exec_lifecycle`：fork→exec→exit→wait/reap 的阶段顺序约束；
  2. 第 57 课 `resource_ledger`：zombie 保留 + 有序 teardown + 二次拒绝；
  3. 第 58 课 `multiwait`：多子进程的精确/聚合选择与 one-shot 收割；
  4. 第 44–49 课资源对象：fd 表、pipe、signal 记录、timer、workqueue/tasklet 的元数据语义；
  5. `FIXED_PID=1`（init）、`SECOND_PID=2`（第一个 job）、`SECOND_PID+1`（第二个 job）。
- **本课交付（可见结果）**：新命令 `sessioninfo`（打印 session/job 账本）与 `jobtest`（确定性验证），输出串见 §5。

## 2. 核心概念精讲

### 2.1 session（会话）与 job（作业）

- **直觉**：你把几个命令同时「后台跑起来」，Shell 把它们当作一组「作业」（jobs）；你登录终端建立的整个交互环境就是一个「会话」（session）。Linux 中 `setsid` 创建新会话、会话首进程成为会话 leader，会话里可以有多个前台/后台作业组。
- **准确定义**（本课教学模型）：
  - **session**：一个固定 init/shell 会话，`sessioninfo` 打印 `init/shell/jobs/commands/waits/reaps`；
  - **shell**：会话内的命令解释器（继承第 52/53 课 `shell_runtime` 概念）；
  - **job**：会话拥有的至多两个子作业，用 `session_jobs[2]` 表示，job[0] 的 PID 为 `SECOND_PID`（2）、job[1] 为 `SECOND_PID+1`（3）。
- 为什么是「至多两个」：固定容量元数据模型的一贯边界——`session_jobs[2]` 与第 38 课 `THREAD_COUNT`、第 44 课 `FD_MAX`、第 58 课 `children[3]` 一样，都是「够教学、不失控」的小数组。

### 2.2 job 的九类元数据（`session_job` 结构）

每个 job 把第 40–58 课的所有资源类别收进一个结构：

```c
struct session_job { u64 pid,argv,env,fd,pipe,signals,timers,deferred,status; u8 active,execed,zombie,reaped; };
```

- `pid`：job 身份（2 或 3）；`argv`/`env`：程序参数与环境元数据；
- `fd`/`pipe`/`signals`/`timers`/`deferred`：描述符、管道、信号、定时器、延迟工作引用；
- `status`：退出码；`active/execed/zombie/reaped`：四个 `u8` 生命周期位。

这与 Linux `struct task_struct` 的设计哲学一致：**一个进程记录同时指向它的内存（mm）、文件（files）、信号（sighand/blocked）、定时器（posix_cputimers）、工作（task_work）**。教学模型用简单的 `u64` 计数代替指针，把「一个 job 挂载多类资源」的关系表达清楚。

### 2.3 job 的退出与收割（exit → reap）

- `session_job_exit(i, code)`：要求 job `active` 且尚未 `zombie`；成功后 `status=code`、`zombie=1`（进入可收割状态）。
- `session_job_reap(i)`：要求 job `zombie` 且未 `reaped`；成功后把 `fd/pipe/signals/timers/deferred` **五个资源字段清零**（对应第 57 课 teardown 的资源维度）、`reaped=1`、`active=0`、`session_reaps++`。
- **与第 57 课的区别**：第 57 课 `resource_teardown` 清的是**单进程**的六类资源（含 address_space）；本课 `session_job_reap` 清的是**每个 job** 的五类资源（fd/pipe/signals/timers/deferred）。address_space 被省略是因为本课 job 模型聚焦「用户空间作业视图」，地址空间归属在第 59 课生命周期中已表达。
- **二次拒绝**：`reaped` 位保证一个 job 只能 reap 一次；`zombie==0` 时也不能 reap（没退出不能收割）。这与第 54/57/58/59 课的全部 one-shot 守卫一致，是全课程统一的「确定性」承诺。

### 2.4 教学模型边界

- 固定两个 job，无真实进程组/会话 ID 分配、无 `setsid`/`setpgid` 系统调用、无前台/后台切换（`fg`/`bg`）；
- job 的 `argv/env/fd/pipe/...` 都是计数元数据，不真正装载、不执行、不打开文件；
- 本课不接入第 58 课 `multiwait` 选择算法——`jobtest` 按索引直接操作 job[0]/job[1]，wait/reap 语义用 `session_waits`/`session_reaps` 记账；
- 第 5 阶段（68–87）才把这些元数据升级为真正的进程组/session 内核对象。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | Multiboot2 header + long mode 切换 | 未变化 |
| `kernel.c` | 32 位阶段初始化 | 未变化 |
| `kernel64.c` | 64 位内核：命令分发 + **本课 session/job 组合模型** | **新增 `session_job`/`session_jobs[2]`/3 个计数器 + 4 个函数 + 2 条命令分支** |
| `kernel64.ld` | 64 位裸二进制链接 | 未变化 |
| `linker.ld` | 32 位外 ELF 链接 | 未变化 |
| `Makefile` | 构建 + `check` 校验 | **`check` 关键词改为 `session`/`job`/`jobtest`/`sessioninfo`/`forkexecwaittest`/`lifecycleinfo`/`shellrun`** |
| `grub.cfg` | GRUB 菜单 | 未变化 |

注意：本课**未**在 `kernel_main64_binary` 启动路径新增调用（`session_jobs` 零初始化 + `session_start()` 测试内复位），延续第 58/59 课的「纯命令层增量」模式。

### 3.2 结构与常量精讲（本课新增）

`kernel64.c` 第 454–457 行（位于第 59 课 `lifecycle_model` 块之前、`waitinfo` 之前）：

```c
struct session_job { u64 pid,argv,env,fd,pipe,signals,timers,deferred,status; u8 active,execed,zombie,reaped; };
static struct session_job session_jobs[2];
static u64 session_commands,session_waits,session_reaps;
```

- `session_jobs[2]`：固定两个 job 槽位，job[0] 与 job[1]；
- `session_commands`：会话累计命令数（jobtest 置 2）；
- `session_waits`：会话累计 wait 次数（jobtest 置 2）；
- `session_reaps`：成功收割次数（每次 `session_job_reap` +1）；
- 结构与 `resource_ledger`/`fork_exec_lifecycle` 一样是纯静态元数据，无动态分配。

### 3.3 函数精讲

#### `session_start` — 复位 session 与两个 job

```c
static TEXT64 void session_start(void){
    session_jobs[0]=(struct session_job){SECOND_PID,2,1,2,1,1,1,1,0,1,1,0,0};   /* job[0]: pid=2, argv=2, env=1, fd=2, pipe/sig/timer/deferred=1, active=1, execed=1 */
    session_jobs[1]=(struct session_job){SECOND_PID+1,2,1,1,1,1,1,1,0,1,1,0,0}; /* job[1]: pid=3, fd=1（与 job[0] 的 fd=2 形成差异） */
    session_commands=0;session_waits=0;session_reaps=0;
}
```

- 字段顺序映射（`session_job` 声明顺序）：`pid, argv, env, fd, pipe, signals, timers, deferred, status, active, execed, zombie, reaped`。
- job[0]：`pid=2, argv=2, env=1, fd=2, pipe=1, signals=1, timers=1, deferred=1, status=0, active=1, execed=1, zombie=0, reaped=0`；
- job[1]：与 job[0] 唯一差异是 `pid=3`、`fd=1`——用 `fd` 的差异（2 vs 1）体现两个 job 的独立资源视图，也为验证「每个 job 独立 teardown」提供可观测点。
- 设计原因：`execed=1` 表示「job 已通过 exec 装载元数据」（呼应第 59 课 `image_replaced`）；两个 job 都 `active=1`（都在运行中）、`zombie=0`（都未退出），等待 `jobtest` 驱动它们退出与收割。

#### `session_job_exit` — job 退出发布

```c
static TEXT64 int session_job_exit(u32 i,u64 code){
    if(i>=2||!session_jobs[i].active||session_jobs[i].zombie)return 0;   /* 槽位合法 + 必须 active + 未 zombie */
    session_jobs[i].status=code;                                          /* 记录退出码 */
    session_jobs[i].zombie=1;                                             /* 进入可收割状态 */
    return 1;
}
```

- 算法步骤：① 边界 `i<2`；② 要求 `active==1`（job 必须还活着）；③ 要求 `zombie==0`（不能重复退出）；④ `status=code`；⑤ `zombie=1`；⑥ 返回 1。
- 边界与错误处理：`i>=2`、非 active、已 zombie 均返回 0 零副作用——与第 54/59 课「只允许 running→zombie 一次」同源。
- 为什么这样设计：每个 job 独立发布自己的退出码（`jobtest` 里 job[1] 退 9、job[0] 退 7），对应 Linux 每个子进程独立 `do_exit` 且只发布一次。

#### `session_job_reap` — job 收割 + 资源清零（本课核心）

```c
static TEXT64 int session_job_reap(u32 i){
    if(i>=2||!session_jobs[i].zombie||session_jobs[i].reaped)return 0;    /* 槽位合法 + 必须 zombie + 未 reaped */
    session_jobs[i].fd=session_jobs[i].pipe=session_jobs[i].signals=session_jobs[i].timers=session_jobs[i].deferred=0; /* 五类资源清零 */
    session_jobs[i].reaped=1;                                             /* 标记已收割 */
    session_jobs[i].active=0;                                             /* job 结束 */
    session_reaps++;                                                      /* 会话收割账本 */
    return 1;
}
```

- 算法步骤：① 前置：`i<2` ∧ `zombie==1` ∧ `reaped==0`；② 五个资源字段（fd/pipe/signals/timers/deferred）清零——这是第 57 课 teardown 的 job 化版本；③ `reaped=1`；④ `active=0`；⑤ `session_reaps++`；⑥ 返回 1。
- 边界与错误处理：二次 reap 被 `reaped` 拒绝；未 exit 的 job（`zombie==0`）被拒绝。`session_reaps` 是会话级聚合账本（区别于 per-job 的 `reaped` 位），供 `jobtest` 汇总断言与 `sessioninfo` 展示。
- 为什么这样设计：收割既要做**状态终结**（`reaped=1, active=0`），又要做**资源清理**（五字段清零）——这正是第 57 课「zombie 保留期资源被持有、reap 时释放」的语义在一个 job 上的落地。对照 Linux `wait_task_zombie → release_task`：取状态后 `release_task` 递减 mm/files 等引用，job 的资源字段清零即这一动作的元数据版。

#### `sessioninfo` — session 账本展示

```c
static TEXT64 void sessioninfo(u16*c){text64(c,"session init/shell/jobs/commands/waits/reaps: ");hex64(c,FIXED_PID);hex64(c,"/");hex64(c,FIXED_PID);hex64(c,"/");hex64(c,2);hex64(c,"/");hex64(c,session_commands);hex64(c,"/");hex64(c,session_waits);hex64(c,"/");hex64(c,session_reaps);putc64(c,'\n');}
```

- 输出格式串（逐字）：`"session init/shell/jobs/commands/waits/reaps: "`，随后打印 `FIXED_PID/FIXED_PID/2/session_commands/session_waits/session_reaps`（init 与 shell 都显示 1，jobs 固定 2），换行收尾。注意 `jobs` 是常量 `2` 直接打印，不是数组长度表达式——验证时可逐字对照源码（460 行）。
- 设计原因：`init`/`shell` 用 `FIXED_PID`（1）展示「会话由 init 拥有的 shell 驱动」，`jobs` 固定 2 是本模型的容量声明；三个计数器（commands/waits/reaps）体现会话级累计观测。

#### `jobtest` — 双 job 生命周期验证

```c
static TEXT64 void jobtest(u16*c){
    session_start();                                   /* 复位：两个 active job 就绪 */
    session_commands=2;                                /* 会话执行了 2 条命令 */
    int a=session_job_exit(1,9),                       /* a: job[1]（pid 3）退出，code=9 */
        b=session_job_exit(0,7),                       /* b: job[0]（pid 2）退出，code=7 */
        d=session_job_reap(1),                         /* d: 收割 job[1]，五资源清零 */
        e=session_job_reap(0),                         /* e: 收割 job[0]，五资源清零 */
        f=!session_jobs[0].active&&!session_jobs[1].active&&session_reaps==2; /* f: 两个 job 均 inactive 且收割 2 次 */
    text64(c,"jobtest: ");text64(c,a&&b&&d&&e&&f?"init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`/`b`：两个 job 先后退出（job[1] code=9、job[0] code=7）——验证 per-job 状态独立；
  - `d`/`e`：两个 job 先后收割——验证 per-job teardown（五资源清零）；
  - `f`：两个 job 都 `active==0` 且 `session_reaps==2`——会话级聚合账本与 per-job 状态一致；
  - 注意 `session_waits=2` 在 `a` 之前被设置，模拟「会话对两个 job 都发起过 wait」（wait 语义由第 54/55 课承担，这里只记账）。
- 验证输出串（逐字）：成功时 `"init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed"`，失败 `"BROKEN"`，前缀 `"jobtest: "`（源码 461 行）。

#### 命令分支（`exec64` 内新增，kernel64.c 第 695 行区域）

```c
}else if(eq64(word,"jobtest")){if(!noargs64(arg))usage64(c,"jobtest");else jobtest(c);}
}else if(eq64(word,"sessioninfo")){if(!noargs64(arg))usage64(c,"sessioninfo");else sessioninfo(c);}
```

- 两条分支插在第 59 课 `forkexecwaittest`/`lifecycleinfo` 分支之后，沿用「`noargs64` → `usage64`/直接调用」模式。

### 3.4 构建管线（Makefile / linker）

- 构建目标、编译标志、链接脚本与上一课完全一致，无新增步骤。
- 唯一变化：`check` 目标关键词（见 §5）——新增 `session`/`job`/`jobtest`/`sessioninfo` 四项，并保留 `forkexecwaittest`/`lifecycleinfo`/`shellrun`。README 需同时包含这些关键词。
- 本课是纯内核源码增量（`session_job` 模型 + 命令），构建产物结构不变。

### 3.5 主控制流

```
kernel_main64_binary:   ← 本课无启动路径增量
  ... resource_start(); pmm_init; ...; install_idt; pit_init; pic_init; prompt64; sti
  for(;;){ kbd_dequeue → exec64(c,h,cmd) }
    jobtest / sessioninfo                               ← 本课新命令
```

- session/job 模型由测试命令驱动；`session_start()` 在测试内复位。启动路径保持第 58/59 课形态（零新增调用）。

## 4. 数据流与运行逻辑

- **输入命令** → `jobtest` → `exec64` 命中分支 → `jobtest(c)`。
- 数据流：`session_start()`（job[0]: pid=2,fd=2,…；job[1]: pid=3,fd=1,…；计数全 0）→ `session_commands=2`、`session_waits=2` → `session_job_exit(1,9)`（job[1] zombie, status=9）→ `session_job_exit(0,7)`（job[0] zombie, status=7）→ `session_job_reap(1)`（job[1] 五资源清零, reaped=1, active=0, session_reaps=1）→ `session_job_reap(0)`（job[0] 同样处理, session_reaps=2）→ `text64` 打印。
- **屏幕上显示什么**：`jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`（源码 461 行）。`sessioninfo` 显示形如 `session init/shell/jobs/commands/waits/reaps: 1/1/2/2/2/2`（十六进制）——commands/waits/reaps 在 `jobtest` 后都是 2。

## 5. 构建、运行与验证

**依赖**：与第 55–59 课相同，无新增工具。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
```

**关键词自检**：

```bash
make check
```

`check` 依次验证：`grub-file --is-x86-multiboot2 build/kernel.elf`、README 含 `session`/`job`、kernel64.c 含 `jobtest`/`sessioninfo`/`forkexecwaittest`/`lifecycleinfo`/`shellrun`，最后打印 `Multiboot2 and lesson 60 checks passed.`。

**运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口（勿加 `-display none`）。`-serial stdio` 输出仅供诊断。

**VGA 验证**（延续旧 README 命令集，含全部回归）：

```bash
../../scripts/qemu-vga-check.sh . jobtest sessioninfo forkexecwaittest lifecycleinfo waitpidtest multichildtest resourceinfo teardowntest adoptioninfo reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

**验证步骤与预期输出**（输出串均从源码逐字抄录）：

1. 输入 `jobtest` → 预期屏幕出现
   `jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`
   （源码 461 行成功串）。
2. 输入 `sessioninfo` → 预期形如 `session init/shell/jobs/commands/waits/reaps: ` 后跟六个十六进制数（源码 460 行）。
3. 回归：`forkexecwaittest`、`lifecycleinfo`、`waitpidtest`、`multichildtest`、`resourceinfo`、`teardowntest`、`adoptioninfo`、`reparenttest`、`waitblocktest`、`nohangtest`、`waittest`、`shellrun` 及各旧回归命令保持通过。

**如何判断成功**：VGA 文本区出现上述成功串；任一断言失败输出 `BROKEN`；`qemu-vga-check.sh` 逐命令注入并检查物理 VGA 文本内存后返回成功。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `jobtest` 输出 `BROKEN` 且 `a` 或 `b` 为假 | job 非 active 或已 zombie（残留状态） | 确认测试开头 `session_start()`；`sessioninfo` 看 reaps/waits；检查 `active`/`zombie` 位 |
| `jobtest` 输出 `BROKEN` 且 `d` 或 `e` 为假 | job 未 zombie 或已 reaped | 确认 `session_job_exit` 先于 `session_job_reap`；`reaped` 守卫是否被前次执行置位 |
| `jobtest` 输出 `BROKEN` 且 `f` 为假 | 某 job 仍 active 或 `session_reaps!=2` | 检查两次 reap 是否都成功；`sessioninfo` 看 `reaps` 字段 |
| `sessioninfo` 数值与预期不符 | `jobtest` 未执行或复位未生效 | 在 `jobtest` 前后各跑一次 `sessioninfo`，对比 `commands/waits/reaps` |
| 想观察 job 资源清零 | `jobtest` 只断言 active/reaps | 在 `session_job_reap` 前后打印 `session_jobs[i].fd/pipe/...`（临时加 text64）确认五字段归零 |
| 二次 reap 行为未验证 | `jobtest` 只走成功路径 | 手动追加 `session_job_reap(0)` 观察返回值与 `session_reaps` 是否保持 2 |
| 构建通过但 `make check` 失败 | README 关键词缺失 | 检查 README 是否含 `session`/`job`；kernel64.c 含 `jobtest`/`sessioninfo`/`forkexecwaittest`/`lifecycleinfo`/`shellrun` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现 |
|---|---|---|
| job 对象 | `session_job`：pid + argv/env + fd/pipe/signals/timers/deferred 引用 + 状态位 | `include/linux/sched.h` `struct task_struct`：`pid/tgid`、`mm`、`files`、`sighand`、`blocked`、`posix_cputimers`、`task_work` 等字段 |
| session | `sessioninfo` 打印 init/shell/jobs，固定一个会话 | Linux `struct signal_struct->__session`（会话 ID）；`kernel/sys.c` `setsid()` 创建新会话 |
| job 退出 | `session_job_exit`：`status=code`、`zombie=1` | `kernel/exit.c` `do_exit`：设置 `exit_code`，`exit_notify` 通知父进程 |
| job 收割 + 资源清零 | `session_job_reap`：五类资源字段清零、`reaped=1`、`active=0` | `kernel/exit.c` `wait_task_zombie` → `release_task`：递减 mm/files 引用并释放 task_struct |
| 二次收割拒绝 | `reaped` 位守卫 | `EXIT_DEAD` 状态 + `release_task` 单次执行 |
| 会话级账本 | `session_commands/waits/reaps` 计数 | Linux 无此聚合计数器；`kernel/signal.c` 维护进程组/会话信号分发（`kill_pgrp` 等） |
| 多 job | `session_jobs[2]` 固定两槽 | Linux 任意多子进程由 `children` 链表组织；第 5 阶段（68–87）才引入真实进程组 |

**权威来源**：Intel SDM、Multiboot2 规范、Linux v6.x `include/linux/sched.h`（task_struct/会话字段）、`kernel/exit.c`（退出与收割）、`kernel/fork.c`、`kernel/sys.c`/`kernel/signal.c`（session/进程组，第 5 阶段将展开）。

**教学模型简化了什么**：无真实 PID 分配器与进程组 ID；无 `setsid`/`setpgid`/`kill_pgrp`；job 的各类引用是计数器而非指针；无前台/后台作业控制；session 与 job 的层级用固定数组表达，不建进程树。真正的进程组/session 内核对象在第 5 阶段（68–87）实现。

## 8. 思考题与练习

1. **概念理解**：`session_job_reap` 为什么只清 `fd/pipe/signals/timers/deferred` 五类而不清 `argv/env/status`？`status` 为什么必须在收割后仍可读（对比第 54 课 wait 语义）？
2. **源码定位**：在 `kernel64.c` 中找出 `session_jobs` 数组与 `session_commands` 等计数器的声明位置（第 455–456 行），说明它们为何是 `static`，以及 `session_start()` 复位了哪两类状态。
3. **动手实验**：修改 `session_start()` 中 job[1] 的 `fd=1` 为 `fd=5`，在 `session_job_reap(1)` 前后验证 job[1] 的 fd 从 5 归零，并确认 job[0] 的 fd 不受影响——验证 per-job 资源独立性。
4. **动手实验**：在 `jobtest` 的 `e` 断言后追加 `session_job_reap(1)`，观察返回值与 `session_reaps` 是否仍为 2，验证二次 reap 守卫。
5. **Linux 对照**：阅读 Linux `include/linux/sched.h` 中 `struct signal_struct` 的 `__session`/`__pgrp` 字段与 `kernel/sys.c` 的 `setsid()`，说明真实会话与作业组的区别，并预测第 5 阶段（68–87）将把 `session_job` 的哪些字段升级为内核对象。

## 9. 本课小结与下一课预告

- 本课建立了「session → shell → job」的受控层级：一个固定会话拥有两个 job，`session_job` 把 argv/env、fd、pipe、signal、timer、deferred-work、status 与 active/execed/zombie/reaped 四个生命周期位组合在一起。
- 你学会了 per-job 的退出发布（`session_job_exit`）与收割 + 资源清零（`session_job_reap`），理解了 `reaped` 位的二次拒绝与 `session_reaps` 会话级账本的配合。
- 你验证了 `jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`，并用 `sessioninfo` 看到了会话级 commands/waits/reaps 账本。
- 这一课把第 40–59 课的全部用户空间零件横切成「一个 job 的多类元数据视图」，是阶段 3 的收官；模型边界依旧清晰：固定两槽、无真实进程组、无前台/后台切换。
- 第 3 阶段至此完成：从裸机启动到受控用户空间 job/session 模型，你已经能叙述「用户敲命令 → fork/exec/exit/wait → 收割 → 资源清零」的完整链路。
- **下一课**（[lesson-61-stable/README.md](../lesson-61-stable/README.md)）进入第 4 阶段 GUI 主线：可靠的 framebuffer handoff 与图形输出前置——从文本终端切到像素世界；而阶段 3 末的 job/session 模型将作为阶段 5（68–87）进程组/session 的元数据基础，在图形主线之后回归深化。

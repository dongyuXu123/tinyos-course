# Lesson 109: 服务依赖拓扑 — 精讲文档

> **课号**：Lesson 109（检查点课 / Checkpoint Lesson，可执行课）
> **主题**：服务依赖拓扑（Service Dependency Topology）
> **课程主线位置**：第 5 阶段「VFS/设备/epoll/服务管理检查点」主线的中间课（100–114）。
> 前课 Lesson 108（服务状态机）建立了服务状态流转；本课把「服务之间谁依赖谁、按什么
> 顺序启动」的拓扑关系作为检查点主题再次确认；后续 Lesson 110（服务启动与失败回滚）、
> 111（守护进程生命周期）、112（综合验证）继续同一条主线，113/114 转入并发主题。
> **前置课程**：[`lesson-108-stable/README.md`](../lesson-108-stable/README.md)
> **后续课程**：[`lesson-110-stable/README.md`](../lesson-110-stable/README.md)
> **一句话目标**：理解「机制继承 + 检查点增量」的课程结构，读懂 `l109test` 检查点如何
> 用固定的 `lesson_102_model` 结构重申 bounded VFS、设备、epoll 与服务管理元数据不变量，
> 并说清 init→shell→job 的服务依赖拓扑在源码中的体现。

---

## 1. 课程定位（Mission）

**一句话目标**：能独立解释「为什么 109 与 108 的 kernel64.c 只有几行不同」——因为本课是
检查点课：内核机制全部继承自早期课程（lesson-50 引入的 `raw_spin_lock_irqsave`/`atomic_*`，
lesson-88 起的 VFS/设备/epoll 元数据，lesson-101 起的服务管理模型），本课只新增一个
`struct lesson_102_model` 检查点结构、一个 `l109test()` 命令和主题横幅。运行 `l109test`
输出 `bounded VFS, devices, epoll, and service management checkpoint passed` 即为通过。

- **在课程主线中的位置**：第 5 阶段检查点主线（约 100–114）中段。100–108 已把 VFS
  （inode/dentry/file/fd/ramfs）、设备/模块、poll/epoll、wait、adoption、resource teardown、
  服务状态机逐个建模；本课以「服务依赖拓扑」为主题对整套元数据做一次确定性复验。
- **责任边界**：本课**不引入新机制**（没有新锁、新表、新调度）。新增的 `lesson_102_model`
  与 `l109test` 是「声明式检查点」——往固定结构里塞已知常量再校验，输出统一通过串。
- **前置知识清单**：① `struct init_model`/`struct shell_runtime_model`/`struct session_job`
  的服务对象模型（108 课）；② `struct module_model`/`symbol_model` 的模块初始化顺序与导出
  符号查找（`moduletest`）；③ exec64 命令分发的 `eq64`/`noargs64` 模式；④
  `raw_spin_lock_irqsave`/`atomic_store_release_u32` 等并发原语的存在与用途（lesson-50 起）。
- **本课交付**：命令 `l109test`（新增）与 `l101test`（由 `l108test` 更名）；
  `about`/横幅显示 `Lesson 109: 服务依赖拓扑`；一个占 2 行源码的 `lesson_102_model`
  检查点结构；`make check` 验证串 `Multiboot2 and Lesson 109 checks passed.`。

---

## 2. 核心概念精讲

### 2.1 概念一：检查点课（Checkpoint Lesson）——机制继承 + 检查点增量

定义：一类在主线中定期出现的课程。它**不再造新轮子**，而是把累积的内核元数据机制当作
「已交付资产」，用一个小而确定的结构 + 一条命令，重新验证「这些机制仍然自洽」。

为什么需要：TinyOS 的 kernel64.c 累积到本课约 850 行，命令多达百余条。若每一课都重写
全部机制，代码会失控；但若完全不验证，回归无从谈起。检查点课用「往 `struct lesson_K_model`
里塞固定常量，再逐字段断言」的方式，把「机制没坏」变成一个可以 grep、可以自动检查的结论。

工作机制：`lesson_101`/`lesson_102` 模型都是同一形状
`{ u32 a,b,c,d; u8 valid,active,ready,accounted; }`。测试函数先整体赋值
`{N, N+1, N+2, N+3, 1,1,1,1}`，然后断言 `valid && active && ready && accounted &&
b == a+1`。所有字段都为真且 `b=a+1`，才打印通过串。因为结构是全局 `static` 且赋值是
编译期常量，结果确定、无副作用。

### 2.2 概念二：服务依赖拓扑（Service Dependency Topology）

定义：把系统中各服务（init、shell、session job、模块）看成节点，把「A 必须先于 B 可用」
看成有向边，形成的图就是服务依赖拓扑。

为什么需要：真实系统（systemd/launchd）里服务之间有 `After=`/`Requires=` 依赖；启动顺序
错误会导致「服务进程活着但依赖缺失」。TinyOS 用元数据记录这些依赖关系：
`struct shell_runtime_model` 的 `pipe_links/signal_links/timer_links/deferred_links` 就是
「shell 与文件/管道/信号/定时器/延迟工作之间的链接计数」；`struct module_model` 的
`init_calls` 代表模块初始化顺序；`struct session_job` 记录每个 job 的 fd/pipe/signal 引用。

工作机制（以代码为准）：
- `init_model_start()` 先启动 init（pid=1, ready=1）；
- `shell_runtime_start()` 把 shell_runtime 置为 ready；
- `shell_exec_path("/bin/sh",2,1)` 走 `ramfs_lookup` → `fd_open_model` → 计数
  `commands/execs/argv_words/env_words/pipe_links/signal_links/timer_links/deferred_links`；
- `session_start()` 建立两个 session job，各自持有 fd/pipe/signal/timer/deferred。

拓扑关系图：

```text
      init (pid=1, init_model.ready=1)
        │ 启动
        ▼
      shell_runtime (ready=1, 计数 links)
        │ 解析 /bin/sh → ramfs_lookup → fd_open_model
        ▼
   session job[0] ── fd/pipe/signal/timer/deferred
   session job[1] ── fd/pipe/signal/timer/deferred
        │ 退出 → zombie → wait → reap
        ▼
   resource_ledger（地址空间/fd/pipe/signal/timer 引用逐一释放）
```

### 2.3 概念三：`lesson_102_model` 检查点结构

定义：本课新增的固定形状结构，语义是「一个服务管理元数据快照」：

```c
struct lesson_102_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_102_model lesson_102_state;
```

- `a,b,c,d`：四个 u32 计数，本课固定为 `102,103,104,105`（延续 101→102 的序列）；
- `valid`：模型合法（非零乱状态）；
- `active`：服务处于激活态；
- `ready`：服务已就绪；
- `accounted`：资源已被记账。

为什么这样设计：四个布尔位对应服务生命周期里「合法、激活、就绪、记账完成」四个属性，
`b==a+1` 表达「依赖链步进 1」的拓扑不变量。全部成立才输出 passed，否则输出
`Lesson 102 fallback reported`（fallback 串说明检查点失败时仍保持安全回退语义）。

### 2.4 概念四：更名与命名收敛（`l108test` → `l101test`）

定义：108 课里用 `lesson_101_state` 的命令叫 `l108test`（函数名与模型号不一致）；本课把
它更名为 `l101test`，使「命令名 = 模型号」，新增的 `l109test` 则对应 `lesson_102_state`。

为什么需要：检查点命令大量累积（l64test…l101test），若命名不收敛，读者无法从命令名猜到
它验证哪个模型。更名不改变行为：`l101test` 仍写 `lesson_101_state`、仍打印
`l101test: bounded VFS, devices, epoll, and service management checkpoint passed`。
注意这是源码事实：`help` 输出串**没有**列出 `l101test`/`l109test`（与 lesson-61 的
`guiinfo`/`drawtest` 同款小瑕疵），命令靠 README 与 `about` 发现。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-108） |
|---|---|---|
| `boot.S` | Multiboot2 header + 32→64 位引导 | 未变化 |
| `kernel.c` | 32 位阶段：MBI 解析、页表、handoff | 未变化 |
| `kernel64.c` | 64 位内核主体（累积文件，约 886 行） | `l108test`→`l101test` 更名；新增 `struct lesson_102_model`/`lesson_102_state`/`l109test`；exec64 增 `l109test` 分支；`about` 与横幅改「Lesson 109: 服务依赖拓扑」 |
| `kernel64.ld` | 64 位二进制布局（`.text64`/三栈 ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF 布局（`.multiboot`/段分离） | 未变化 |
| `Makefile` | 构建 + `check` 静态验证 | `check` 三条 grep 更新为 `服务依赖拓扑`/`l109test`/`Lesson 109`；`run` 不变 |
| `grub.cfg` | GRUB 菜单（lesson-52 菜单名） | 未变化 |

### 3.2 kernel64.c —— 本课增量（全部变更都集中在此文件）

#### 3.2.1 更名的既有检查点（源码逐字）

```c
static TEXT64 void l101test(u16*c){lesson_101_state=(struct lesson_101_model){101U,102U,103U,104U,1,1,1,1};int ok=lesson_101_state.valid&&lesson_101_state.active&&lesson_101_state.ready&&lesson_101_state.accounted&&lesson_101_state.b==lesson_101_state.a+1U;text64(c,"l101test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 101 fallback reported");putc64(c,'\n');}
```

- 函数名由 `l108test` 改为 `l101test`，逻辑逐字未动：仍是往 `lesson_101_state` 填
  `{101U,102U,103U,104U,1,1,1,1}`；
- `int ok=` 一行做五重断言：`valid/active/ready/accounted` 全真且 `b==a+1U`；
- 输出前缀 `l101test: `，成功串
  `bounded VFS, devices, epoll, and service management checkpoint passed`，失败串
  `Lesson 101 fallback reported`——「更名不换语义」是本课最直观的增量证据。

#### 3.2.2 新增检查点结构（源码逐字）

```c
struct lesson_102_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_102_model lesson_102_state;
```

- 第一行：结构定义，4 个 u32 计数 + 4 个 u8 状态位；与 101 模型同构，编号前进一位；
- 第二行：全局静态实例 `lesson_102_state`，默认全零（`valid=0`），只有 `l109test` 会给它
  赋确定值，避免任何未初始化读；
- 结构本身不申请内存、不挂指针，是纯元数据快照，符合「bounded/freestanding」纪律。

#### 3.2.3 新增检查点命令 `l109test`（源码逐字）

```c
static TEXT64 void l109test(u16*c){lesson_102_state=(struct lesson_102_model){102U,103U,104U,105U,1,1,1,1};int ok=lesson_102_state.valid&&lesson_102_state.active&&lesson_102_state.ready&&lesson_102_state.accounted&&lesson_102_state.b==lesson_102_state.a+1U;text64(c,"l109test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 102 fallback reported");putc64(c,'\n');}
```

逐行拆解：

1. 签名 `static TEXT64 void l109test(u16*c)`：与所有命令函数一致，`TEXT64` 把它放进
   `.text64` 段（64 位高地址可执行区），参数 `c` 是 VGA 光标；
2. 整体赋值 `lesson_102_state=(struct lesson_102_model){102U,103U,104U,105U,1,1,1,1};`：
   `a=102,b=103,c=104,d=105`，四个状态位全 1——这是「快照注入」；
3. 断言表达式：`valid&&active&&ready&&accounted&&b==a+1U`，其中 `b==a+1U` 验证
   「服务拓扑中下一个节点的编号比当前大 1」的链式不变量；
4. 输出：先 `l109test: `，再按 `ok` 三目选择成功串或 `Lesson 102 fallback reported`；
5. 收尾 `putc64(c,'\n')` 换行——命令函数统一由 exec64 末尾 `prompt64(c)` 补提示符。

错误处理与边界：本函数无输入参数、无资源分配、无分支错误路径，因此不存在越界或泄漏；
它验证的是「模型字段自洽」，失败时打印 fallback 串而非崩溃——这就是检查点命令的
「安全失败」设计。

#### 3.2.4 exec64 命令分发（源码逐字，摘新增行）

```c
else if(eq64(word,"l101test")){if(!noargs64(arg))usage64(c,"l101test");else l101test(c);}else if(eq64(word,"l109test")){if(!noargs64(arg))usage64(c,"l109test");else l109test(c);}
```

- 两个分支紧挨着，插在 `l100test` 分支之后、`resourceinfo` 之前；
- 均先 `noargs64` 检查：带参数则打印 `usage64(c,"l109test")`，否则执行；
- `l101test` 分支由 108 课 `l108test` 分支改名而来，`l109test` 分支为新增；
- `help` 命令的长串命令列表没有追加这两个名字（源码事实），读者从 `about`/README 发现。

#### 3.2.5 主题横幅与 about（源码逐字）

```c
else text64(c,"Lesson 109: 服务依赖拓扑\n");
```

```c
text64(&c,"Lesson 109: 服务依赖拓扑\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- `about` 命令输出主题串，末尾 `\n`；
- 开机横幅（`kernel_main64_binary` 中 `clear64(&c)` 之后）第一行是主题
  `Lesson 109: 服务依赖拓扑`，第二行是系统调用 ABI 摘要与 reclaim 元数据说明——两行串都
  是本课增量，验证时逐字比对。

### 3.3 kernel64.c —— 继承机制（本课不新增但支撑主题的资产）

本课主题「服务依赖拓扑」所依赖的机制全部继承自前课，逐段引用并说明其拓扑含义：

```c
struct init_model { u64 pid,started,commands,files,processes,pipes,signals; u8 ready; };
static struct init_model init_model;
```

- init 是服务拓扑的根节点（pid=FIXED_PID=1）；`started` 表示已启动，`ready` 表示可用；
- `commands/files/processes/pipes/signals` 是 init 为下游服务提供的资源计数；
- `init_model_start()` 把 init 初始化为 `{FIXED_PID,1,0,0,0,0,0,1}`（第 410 行）。

```c
struct shell_runtime_model { u64 starts,commands,execs,exits,argv_words,env_words,pipe_links,signal_links,timer_links,deferred_links; u8 ready; };
```

- 这是「服务依赖边」的计数器集合：`pipe_links/signal_links/timer_links/deferred_links`
  分别是 shell 与管道、信号、定时器、延迟工作之间的链接数；
- `shell_exec_path()` 每成功一次 exec 就把这些 links 各 +1，构成 shell 依赖下游子系统的
  拓扑证据（`initinfo`/`shelltest`/`shellrun` 命令输出这些计数）。

```c
struct module_model { u64 name_hash,init_calls,exit_calls; u8 loaded,initialized; };
```

- 模块初始化顺序是另一种依赖拓扑：`module_init_model()` 初始化模块 0（`0x636f7265`=core）
  与模块 1（`0x766673`=vfs），`moduletest` 断言「init 顺序 + 导出符号查找」；
- 依赖方向：模块必须 `initialized` 后其导出符号（`pmm`/`vfs`）才能被 `module_lookup` 命中。

```c
struct session_job { u64 pid,argv,env,fd,pipe,signals,timers,deferred,status; u8 active,execed,zombie,reaped; };
```

- 服务拓扑的叶子节点：每个 session job 持有 argv/env/fd/pipe/signal/timer/deferred 引用；
- `session_job_reap()` 把 fd/pipe/signal/timer/deferred 全部清零再 `reaped=1`，是
  「依赖释放」的顺序模型。

### 3.4 构建管线（Makefile / linker）

- 构建链不变：`kernel64.c` 以 `-m64 -ffreestanding -fpie -fno-stack-protector
  -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fno-asynchronous-unwind-tables -Wall
  -Wextra -Werror` 编译 → `ld -m elf_x86_64 -T kernel64.ld` 链接 → `objcopy -O binary`
  产出 `kernel64.bin`，再由 `boot.S` 的 `.incbin` 嵌进外层 ELF；32 位部分用 `-m32
  -fno-pie`；`grub-mkrescue` 打包 `kernel.iso`；
- `check` 目标（本课增量）：
  - `grub-file --is-x86-multiboot2 build/kernel.elf`；
  - `grep -q '服务依赖拓扑' README.md`（README 必须含主题串）；
  - `grep -q 'l109test' kernel64.c`（内核必须含本课命令）；
  - `grep -q 'Lesson 109' README.md`（README 必须含课号）；
  - 全部通过打印 `Multiboot2 and Lesson 109 checks passed.`；
- `run`：`qemu-system-x86_64 -accel tcg -boot order=d -cdrom build/kernel.iso -serial
  stdio -no-reboot -no-shutdown`，图形窗口 + 串口输出；`kernel64.ld`/`linker.ld` 本课未变。

### 3.5 主控制流

```text
GRUB(grub.cfg) → _start(boot.S)
  → kernel_main32(kernel.c)：解析 MBI、建低/高半区页表
  → 进入长模式 → kernel_main64_binary(kernel64.c)
      ├─ module_init_model(); init_model_start(); wait_model_start();
      │  adoption_start(); resource_start(); pmm_init(h); vma_init();
      │  reclaim_init(); vfs_init(); address_space_init(...)
      ├─ 初始化 user_process/user_thread/threads[0]
      ├─ framebuffer_init(h); stack_guards_init(h); runtime_gdt_tss_init();
      │  idle_init(); install_idt(h); pit_init(); pic_init()
      ├─ 横幅 "Lesson 109: 服务依赖拓扑\nGETTICKS, ... bounded reclaim metadata\n"
      └─ 键盘循环 → exec64
            ├─ l101test → 复验 lesson_101 检查点（更名命令）
            ├─ l109test → 复验 lesson_102 检查点（本课新增）
            └─ 其余百余条继承命令
```

---

## 4. 数据流与运行逻辑

```text
输入 "l109test" → exec64 eq64(word,"l109test") 命中
  → noargs64(arg) 通过 → l109test(c)
  → lesson_102_state = {102,103,104,105, 1,1,1,1}
  → ok = valid(1) && active(1) && ready(1) && accounted(1) && b(103)==a(102)+1
  → text64("l109test: ") + "bounded VFS, devices, epoll, and service management checkpoint passed"
  → '\n' → prompt64("tinyos> ")

输入 "about" → text64("Lesson 109: 服务依赖拓扑\n")

开机 → 横幅第一行 "Lesson 109: 服务依赖拓扑"
```

服务依赖拓扑的运行侧证据：`initinfo` 显示
`init pid/ready/commands/files/processes/pipes/signals: ...`；`shellrun` 走
`/bin/sh` 解析并累加 links；`jobtest` 让两个 session job 退出、wait、reap。这些命令的
输出共同证明「init→shell→job」依赖链完好。

---

## 5. 构建、运行与验证

### 5.1 依赖

`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 109 checks passed.`（README 必须含
`服务依赖拓扑` 与 `Lesson 109`，kernel64.c 必须含 `l109test`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 109: 服务依赖拓扑\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字抄录）：

```bash
l109test
```

预期：

```text
l109test: bounded VFS, devices, epoll, and service management checkpoint passed
```

```bash
about
```

预期：`Lesson 109: 服务依赖拓扑`

```bash
l101test
```

预期：`l101test: bounded VFS, devices, epoll, and service management checkpoint passed`
（更名命令，语义与 108 课 `l108test` 相同）。

继承回归：`moduletest`（模块 init 顺序）、`shellrun`（/bin/sh 依赖链）、`jobtest`
（session job 生命周期）、`reparenttest`/`teardowntest`、`softirqtest`、`lockatomictest`
等命令行为不变。

### 5.4 课程实测记录（稳定快照）

旧 README 声明「stable snapshot：Commands `l102test`」——**命令名以源码为准勘误**：本课
源码只有 `l101test`（更名）与 `l109test`（新增），无 `l102test` 命令；旧 README 记录的
`l102test` 是模型编号（lesson_102），并非可执行命令。`make check` 复验输出
`Multiboot2 and Lesson 109 checks passed.`，`l109test` 显示 passed，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l109test` 输出 `Lesson 102 fallback reported` | `lesson_102_state` 未被赋值或被改写，导致 `valid/active/ready/accounted` 或 `b==a+1` 不成立 | 检查 `l109test` 是否先整体赋值；在 `int ok=` 处逐字段核对 `{102U,103U,104U,105U,1,1,1,1}` |
| `make check` 报错 | README 缺 `服务依赖拓扑`/`Lesson 109` 之一，或 kernel64.c 缺 `l109test` | 对照 Makefile `check` 三条 grep；README 标题与 §5 必须出现这些串 |
| 输入 `l109test` 显示 `unknown command` | exec64 未加 `l109test` 分支，或命令名拼错 | grep `l109test` kernel64.c，确认 `else if(eq64(word,"l109test"))` 存在且位于 exec64 内 |
| `about` 显示旧课号（如 Lesson 108） | 主题串未更新或引用了旧行 | 确认 `about` 分支 `text64(c,"Lesson 109: 服务依赖拓扑\n")` |
| 开机横幅仍是 `Lesson 108` | `kernel_main64_binary` 中横幅串未改 | grep `Lesson 109: 服务依赖拓扑` kernel64.c，确认在主函数横幅处 |
| `make check` 通过但 `l101test` 报错 | `l101test` 更名时误改了断言逻辑 | diff 108→109，确认 `l101test` 与 `l108test` 除函数名外逐字一致 |
| help 命令列表找不到 `l109test` | 已知小瑕疵：help 串未追加检查点命令（源码事实） | 用 `about`/README 发现命令；如需可自行在 help 串追加（勿提交） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `struct init_model`（pid=1, started/ready） | `init/main.c`：`start_kernel()` 启动 PID 1（`kernel_init`/systemd）；`include/linux/sched/task.h` 的 `init_task` | 教学模型只用 7 个计数表示 init 的资源账，Linux 是真实进程；「init 是服务拓扑根」语义一致 |
| `shell_runtime_model` 的 `pipe_links/signal_links/timer_links/deferred_links` | systemd unit 依赖（`After=`/`Requires=`，见 `man systemd.unit`）；POSIX `fork/exec/pipe` 关联 | 教学模型用计数器表示依赖边，Linux 用 unit 描述文件表达 DAG；方向都是「先启动依赖再启动服务」 |
| `struct module_model` init 顺序 + `module_lookup` | `kernel/module.c`：`do_init_module()`、`modprobe` 依赖解析；导出符号查 `kernel/kallsyms.c` | 教学模型预置 core/vfs 两个模块并断言顺序；Linux 运行时解析符号依赖 |
| `struct session_job` 的 fd/pipe/signal/timer 引用 | `include/linux/sched.h` `struct signal_struct`、`fs/file_table.c` 文件引用计数 | 教学模型用 8 个 u64 + 4 个状态位表示 job；Linux 用引用计数与进程组/session 结构 |
| `session_job_reap()` 先清资源再置 reaped | `kernel/exit.c`：`exit_mm()`/`exit_files()` 顺序释放 + `wait` 回收 | 「按依赖逆序释放、防双回收」语义一致；教学模型在元数据层模拟 |

**权威来源**：Linux v6.x 内核源码（`init/main.c`、`kernel/module.c`、`kernel/exit.c`）；
systemd 手册（unit 依赖）；POSIX（进程、session、进程组）。

**教学模型简化了什么**：没有真实进程/线程执行（全是元数据）；没有真实文件系统与模块加载；
服务依赖用固定计数而非图结构；init/shell 不 fork 真实子进程，只模拟状态流转。

---

## 8. 思考题与练习

1. **概念理解**：检查点课的 `lesson_K_model` 为什么必须用 `static` 全局并整体赋值？如果
   `l109test` 不赋值直接读 `lesson_102_state` 会发生什么？
2. **源码定位**：`l101test` 与 `l109test` 用的模型号分别是多少？为什么命令名改成了与
   模型号一致（`l108test`→`l101test`）？这改善了什么问题？
3. **动手实验**：把 `l109test` 里 `ok` 的断言临时改成 `b==a+2U`，`make run` 后输入
   `l109test`，观察输出变为 fallback 串，然后改回（勿提交）。
4. **Linux 对照**：读 `init/main.c` 的 `kernel_init`，对照本课 `init_model_start()` 的
   `pid/ready/commands` 字段，列出 Linux 中与之对应的真实对象。
5. **设计思考**：`b==a+1U` 表达「编号递增」不变量；如果服务拓扑允许任意重排，这个断言
   还成立吗？检查点断言是否应该只验证「自洽」而不断言「具体顺序」？

---

## 9. 本课小结与下一课预告

**小结**：本课是检查点课——内核机制全部继承，源码增量只有「更名一个命令（l108test→
l101test）+ 新增一个模型（lesson_102）+ 新增一条命令（l109test）+ 主题横幅/ about」。
`l109test` 用 `{102U,103U,104U,105U,1,1,1,1}` 注入 `lesson_102_state`，断言
`valid/active/ready/accounted && b==a+1` 后输出
`bounded VFS, devices, epoll, and service management checkpoint passed`。服务依赖拓扑的
机制证据分散在 `init_model`/`shell_runtime_model`/`module_model`/`session_job` 等继承结构
中：init→shell→job 构成依赖链，模块有初始化顺序，job 回收按依赖逆序释放。`make check`
用三条 grep（`服务依赖拓扑`/`l109test`/`Lesson 109`）自动把关。

**下一课预告**：[Lesson 110](../lesson-110-stable/README.md) 主题「服务启动与失败回滚」：
在服务拓扑基础上，检查点模型前进为 `lesson_103`，`l110test` 重申「启动成功才就绪、
失败路径安全回滚」的不变量；同时 `l102test` 由本课 `l109test` 更名收敛。主线仍处于
第 5 阶段 VFS/设备/epoll/服务检查点，直到 113 课转入 mutex/spinlock 并发主题。

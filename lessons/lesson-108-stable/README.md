# Lesson 108: 服务状态机 — 精讲文档

> **课号**：Lesson 108（主线源课编号 Lesson 101 线）
> **本课主题**：服务状态机（service state machine）——服务/子进程从 active → zombie → reaped 的一整套确定性状态迁移；用 `wait_model` 的 `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD` 与 `session_job` 的 `active/zombie/reaped` 标志讲清"谁在跑、谁在等、谁被回收"
> **课程主线位置**：VFS / 设备 / epoll / 服务管理教学模型阶段（Lesson 81 起的「checkpoint 课」系列）的收尾检查点。103–107 把"事件等待/唤醒"讲完，本课把视角转向"服务对象的一生"：状态机是服务管理（systemd、init、作业控制）最核心的建模工具，也是本系列「服务管理」标签的最后一块内容。
> **前置课程**：[`../lesson-107-stable/README.md`](../lesson-107-stable/README.md)（epoll wait/wake 集成：阻塞与唤醒闭环）
> **后续课程**：[`../lesson-109-stable/README.md`](../lesson-109-stable/README.md)（下一主题，衔接本课的服务/作业状态机）
> **本课一句话目标**：理解"服务 = 一组离散状态 + 受约束的迁移 + 每步确定性验证"，能画出 `WAIT_RUNNING → WAIT_ZOMBIE → WAIT_DEAD` 与 `active → zombie → reaped` 两条状态机，并知道每步迁移前的"前置条件检查"为什么是状态机正确性的关键。
> **保留的原始快照信息**：This checkpoint models bounded VFS, devices, epoll, and service management metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l100test` + `l108test`（**勘误**：旧 README 标注的 `l101test` 在源码命令表中并不存在——源码 `exec64` 分派的是 `l100test` 与 `l108test`），plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能用一个统一框架描述"服务的一生"——状态集合（active/zombie/reaped）、迁移条件（必须处于什么状态才能迁移到哪）、迁移副作用（清资源、加计数），并能在源码里指出 `wait_model`、`session_job`、`adoption_model` 三条状态机各自的迁移函数。
- **在课程主线中的位置**：checkpoint 课（Lesson 81 起）系列中的「服务状态机」检查点。本系列每课源码增量只有几行（上一课测试改名 + 新测试 + about/banner 文案），主题由课程标签承载；`kernel64.c` 里没有 `service` 标识符的实现代码（`service` 字样仅出现在 checkpoint 断言串），但**服务管理的核心状态机代码**（`wait_model`、`session_job`、`adoption_model`、`process_group`）都继承自更早课程——本课把它们作为"服务状态机"的教学素材精讲。
- **前置知识清单**（学本课之前必须掌握）：
  1. 状态机的三个要素：状态集合、迁移条件、迁移副作用（Lesson 37 的任务状态机 `TASK_RUNNING`/`EXIT_ZOMBIE` 等）；
  2. 等待/回收模型：`wait_model_wait`/`wait_model_reap` 与 `WAIT_*` 状态（Lesson 90 起）；
  3. 作业与进程组：`session_job`/`process_group` 的 `active/foreground/controlled` 语义（Lesson 99 起）；
  4. 孤儿收养：`adoption_model` 的 `orphaned→adopted`（Lesson 100 起）。
- **本课交付**：新增固定容量记录 `struct lesson_101_model` + `lesson_101_state` + `l108test`；把 `l107test` 改名为 `l100test`；`about` 与 banner 更新为「Lesson 108: 服务状态机」。

---

## 2. 核心概念精讲

### 2.1 状态机：服务的"生命周期地图"

**直觉**：一个服务从被拉起到最后回收，中间经历若干"阶段"。如果允许从任何阶段跳到任何阶段，系统行为就不可预测。状态机就是给服务画一张**只允许沿固定边迁移的地图**：状态 = 服务此刻在哪一站；迁移 = 从这一站到下一站需要满足的条件与执行的动作。

**定义**：一个确定性状态机由三部分组成：
1. **状态集合**：离散的、互斥的阶段（如 `active`/`zombie`/`reaped`）；
2. **迁移条件（guard）**：迁移发生前必须为真的前置条件（如"只有 active 才能 exit"）；
3. **迁移副作用**：迁移时更新哪些字段、累计哪些计数。

### 2.2 第一条状态机：子进程的一生（`wait_model`）

```
WAIT_RUNNING ──(wait_model_exit)──► WAIT_ZOMBIE ──(wait_model_reap)──► WAIT_DEAD
      │                                  │
      └── 运行中；wait 会被拒绝             └── 已退出未回收；wait() 才能成功
```

- `wait_model_exit(code)`：**只有 `WAIT_RUNNING` 能 exit**，记下退出码、状态变 `WAIT_ZOMBIE`、`statuses++`；
- `wait_model_wait()`：每次调用 `wait_calls++`，**只有 `WAIT_ZOMBIE` 才返回成功**并置 `waited`；
- `wait_model_reap()`：**只有 `waited && WAIT_ZOMBIE` 才能 reap**，状态变 `WAIT_DEAD`、`reaps++`。

### 2.3 第二条状态机：服务作业的一生（`session_job`）

```
active(=1) ──(session_job_exit)──► zombie(=1) ──(session_job_reap)──► reaped(=1, active=0)
```

`struct session_job` 用三个标志组合表达状态：`active`（还在跑）、`zombie`（已退出未回收）、`reaped`（已回收）。一条作业被 `session_job_reap` 回收时，**同时清零五个资源计数**（fd/pipe/signals/timers/deferred）——这是"迁移副作用"：回收 = 还资源 + 改状态 + 计数 `session_reaps++`。

### 2.4 第三条状态机：孤儿收养（`adoption_model`）

```
orphaned=0 ──(adoption_exit_parent)──► orphaned=1 ──(adoption_reparent)──► adopted=1
```

父进程先退出（`orphaned=1`），init 再收养（`current_parent=init_pid`、`adopted=1`、`adoptions++`）。迁移同样带 guard：`adoption_reparent` 要求 `orphaned && !adopted`，重复收养会被拒绝。

### 2.5 为什么"前置条件检查"是状态机正确性的关键

三条状态机里每个迁移函数的第一行都是 `if(...状态不满足...)return 0;`。这保证：
1. **确定性**：同一输入永远走同一条边，测试可以精确断言；
2. **防重入**：`session_job_reap` 对已 `reaped` 的作业返回 0——双重回收被拒绝；
3. **可验证**：`waittest`/`jobtest`/`reparenttest` 正是对"合法迁移必须成功、非法迁移必须失败"的成对断言。

### 2.6 checkpoint 固定元数据

`struct lesson_101_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担系列 checkpoint：`l108test` 断言 `b==a+1`（101+1=102）与四标志。`active` 标志字面呼应本课主题——每个固定记录都带着"是否活跃"的状态位。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-107） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：服务/作业/等待状态机、`exec64` 分派 | **微增量（仅 3 行）**：新增 `struct lesson_101_model` + `lesson_101_state` + `l108test()`；把 `l107test` 改名为 `l100test`；`about`/banner/`exec64` 文案更新（`wait_model`/`session_job`/`adoption_model` 状态机继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成「服务状态机 / l108test / Lesson 108」关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的 checkpoint 元数据：`l108test`

```c
static TEXT64 void l108test(u16*c){lesson_101_state=(struct lesson_101_model){101U,102U,103U,104U,1,1,1,1};int ok=lesson_101_state.valid&&lesson_101_state.active&&lesson_101_state.ready&&lesson_101_state.accounted&&lesson_101_state.b==lesson_101_state.a+1U;text64(c,"l108test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 101 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_101_state=(struct lesson_101_model){101U,102U,103U,104U,1,1,1,1};`：`a=101,b=102,c=103,d=104`，四标志全 1。
- `int ok=...`：四标志与 `b==a+1U` 五条件与，`102==101+1` 必须成立。
- 成功串 `bounded VFS, devices, epoll, and service management checkpoint passed`、失败串 `Lesson 101 fallback reported` 逐字来自源码。
- **设计说明**：fallback 里的 101 是被校验状态号，**不是**命令名——旧 README 据此误写 `l101test`；本课真实命令是 `l100test` 与 `l108test`。注意编号巧合：`l108test` 校验 `lesson_101_state`，与 101 号课的话题编号撞车，但语义完全不同。

#### (b) 上一课测试改名为 `l100test`

```c
static TEXT64 void l100test(u16*c){lesson_100_state=(struct lesson_100_model){100U,101U,102U,103U,1,1,1,1};int ok=lesson_100_state.valid&&lesson_100_state.active&&lesson_100_state.ready&&lesson_100_state.accounted&&lesson_100_state.b==lesson_100_state.a+1U;text64(c,"l100test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 100 fallback reported");putc64(c,'\n');}
```

- lesson-107 里它叫 `l107test`；本课改名 `l100test`，测试名与被校验状态号对齐。
- `exec64` 分派把 `l107test` 分支换成 `l100test` 分支，并新增 `l108test` 分支：
```c
else if(eq64(word,"l100test")){if(!noargs64(arg))usage64(c,"l100test");else l100test(c);}
else if(eq64(word,"l108test")){if(!noargs64(arg))usage64(c,"l108test");else l108test(c);}
```

#### (c) 子进程状态机：`wait_model`

```c
struct wait_model { u64 parent_pid,child_pid,exit_code,wait_calls,reaps,statuses; u8 state,waited; };
static struct wait_model wait_model;
#define WAIT_RUNNING 1U
#define WAIT_ZOMBIE 2U
#define WAIT_DEAD 3U
static TEXT64 void wait_model_start(void){wait_model=(struct wait_model){FIXED_PID,SECOND_PID,0,0,0,0,WAIT_RUNNING,0};}
static TEXT64 int wait_model_exit(u64 code){if(wait_model.state!=WAIT_RUNNING)return 0;wait_model.exit_code=code;wait_model.state=WAIT_ZOMBIE;wait_model.statuses++;return 1;}
static TEXT64 int wait_model_reap(void){if(!wait_model.waited||wait_model.state!=WAIT_ZOMBIE)return 0;wait_model.state=WAIT_DEAD;wait_model.reaps++;return 1;}
```

逐行注释（这就是"子服务"的完整状态机）：
- `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD`：三个状态常量——运行、已退出未回收（僵尸）、已回收。
- `wait_model_exit(code)`：guard 是 `state==WAIT_RUNNING`（运行中才能退出）；副作用是记录 `exit_code`、迁移到 `WAIT_ZOMBIE`、`statuses++`。**已 zombie/dead 的再 exit 返回 0**——状态机拒绝非法迁移。
- `wait_model_reap()`：guard 是 `waited && state==WAIT_ZOMBIE`（必须先 wait 成功、必须处于僵尸态）；副作用是迁移到 `WAIT_DEAD`、`reaps++`。**双重 reap 被拒**。
- `wait_model_wait()`（另见）：每次调用 `wait_calls++`，只有 `WAIT_ZOMBIE` 才成功并置 `waited`。
- **为什么这样设计**：对照 Linux，`exit` 后进程进入僵尸态、父进程 `wait4` 回收——这正是 `exit()/wait()/zombie` 的教学缩写；`WAIT_ZOMBIE` 状态保证"先 wait 后 reap"的顺序，防止父进程在未 wait 的情况下误删子进程元数据。

#### (d) 服务作业状态机：`session_job`

```c
struct session_job { u64 pid,argv,env,fd,pipe,signals,timers,deferred,status; u8 active,execed,zombie,reaped; };
static struct session_job session_jobs[2];
static TEXT64 void session_start(void){session_jobs[0]=(struct session_job){SECOND_PID,2,1,2,1,1,1,1,0,1,1,0,0};session_jobs[1]=(struct session_job){SECOND_PID+1,2,1,1,1,1,1,1,0,1,1,0,0};session_commands=0;session_waits=0;session_reaps=0;}
static TEXT64 int session_job_exit(u32 i,u64 code){if(i>=2||!session_jobs[i].active||session_jobs[i].zombie)return 0;session_jobs[i].status=code;session_jobs[i].zombie=1;return 1;}
static TEXT64 int session_job_reap(u32 i){if(i>=2||!session_jobs[i].zombie||session_jobs[i].reaped)return 0;session_jobs[i].fd=session_jobs[i].pipe=session_jobs[i].signals=session_jobs[i].timers=session_jobs[i].deferred=0;session_jobs[i].reaped=1;session_jobs[i].active=0;session_reaps++;return 1;}
```

逐行注释（这是"服务/作业"的完整状态机）：
- 作业记录同时携带**资源计数**（`argv/env/fd/pipe/signals/timers/deferred`）与**状态标志**（`active/execed/zombie/reaped`）——服务的生命周期就是"资源随状态迁移"。
- `session_job_exit(i,code)`：guard 是 `i<2 && active && !zombie`（只有活跃中的作业能退出）；副作用是记 `status`、置 `zombie=1`。
- `session_job_reap(i)`：guard 是 `zombie && !reaped`（只有已退出未回收的作业能回收）；副作用是**清空五个资源计数**、置 `reaped=1`、`active=0`、`session_reaps++`。
- **边界与错误处理**：`i>=2` 越界直接返回 0；`reaped` 后再 reap 返回 0——"双重回收"被拒绝。
- **为什么这样设计**：对照 Linux 的 service 管理（systemd unit 的 `active/failed/dead`）与 shell 作业控制（`jobs` 表、`wait` 回收后台作业），`session_job` 就是"作业表中一条记录的生命周期"；资源清零的副作用对应"回收时归还 fd/信号/timer 等引用"。

#### (e) 孤儿收养状态机：`adoption_model`

```c
struct adoption_model { u64 init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; u8 orphaned,adopted,wait_owner; };
static TEXT64 int adoption_reparent(void){if(!adoption_model.orphaned||adoption_model.adopted)return 0;adoption_model.current_parent=adoption_model.init_pid;adoption_model.adopted=1;adoption_model.adoptions++;return 1;}
static TEXT64 int adoption_exit_parent(void){if(adoption_model.original_parent!=FIXED_PID)return 0;adoption_model.orphaned=1;adoption_model.wait_owner=adoption_model.init_pid;return adoption_reparent();}
```

- `adoption_exit_parent()`：原父退出 → `orphaned=1`，`wait_owner` 指向 init，并立刻调 `adoption_reparent()` 完成收养（一步到位）。
- `adoption_reparent()`：guard 是 `orphaned && !adopted`（必须真是孤儿且没被收养过）；副作用是 `current_parent=init_pid`、`adopted=1`、`adoptions++`。
- **为什么这样设计**：Linux 里父进程先死、子进程被收养给 `child_reaper`（PID 1 / init / 最近祖先 subreaper）；本模型把"检测孤儿 → 交给 init → 记收养次数"压缩成两个函数，保持同一个"guard + 副作用"风格。

#### (f) `exec64` 增量、`about` 与 banner

`about` 分支逐字来自源码：
```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 108: 服务状态机\n");}
```
banner 逐字来自 `kernel_main64_binary`：
```c
text64(&c,"Lesson 108: 服务状态机\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '服务状态机' README.md
	@grep -q 'l108test' kernel64.c
	@grep -q 'Lesson 108' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 108 checks passed.'
```

- 与 lesson-107 的唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链与链接脚本均未变化（同前几课）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ 初始化（同前几课；wait_model_start / adoption_start / session_start 各就各位）
    ├─ banner: "Lesson 108: 服务状态机\nGETTICKS, ...\n"
    └─ for(;;) 键盘循环
        ├─ "l108test" ──► lesson_101_state 校验（a=101,b=102）──► "l108test: bounded VFS, ... checkpoint passed"
        ├─ "l100test" ──► lesson_100_state 校验（上一课回归）
        ├─ "waittest" ──► RUNNING→ZOMBIE→DEAD 全迁移断言
        ├─ "jobtest"  ──► session_job 双作业 exit+reap 断言
        └─ "reparenttest" ──► orphaned→adopted 收养断言
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打出「Lesson 108: 服务状态机」；`wait_model_start`/`adoption_start`/`resource_start` 等把各状态机置于初始状态。
2. **checkpoint**：输入 `l108test` → `lesson_101_state` 新建并校验（`b==a+1`）→ VGA 打印 `l108test: bounded VFS, devices, epoll, and service management checkpoint passed`；`l100test` 回归校验 `lesson_100_state`。
3. **子进程状态机**：输入 `waittest` → `wait_model_start()`（`WAIT_RUNNING`）→ `wait_model_wait()` 被拒（不是 zombie）→ `wait_model_exit(42)` 成功（`WAIT_ZOMBIE`、`exit_code=42`）→ `wait_model_wait()` 成功 → `wait_model_reap()` 成功（`WAIT_DEAD`）→ 打印 `waittest: bounded wait, exit status, zombie selection, and reap passed`。
4. **服务作业状态机**：输入 `jobtest` → `session_start()`（两作业 active）→ `session_job_exit(1,9)`、`session_job_exit(0,7)`（均变 zombie）→ `session_job_reap(1)`、`session_job_reap(0)`（均 reaped、资源清零、`active=0`）→ 打印 `jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`。
5. **收养状态机**：输入 `reparenttest` → `adoption_start()` → `adoption_exit_parent()` 触发 `orphaned=1` + 收养 → `adoption_wait_owner()` 校验 → 打印 `reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`。

输出串与源码逐字一致：`l108test: bounded VFS, devices, epoll, and service management checkpoint passed`；`waittest: bounded wait, exit status, zombie selection, and reap passed`；`jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`；`reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致（`gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`，详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

**构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

**运行**：

```bash
make run
```

> 成功画面在 QEMU 图形窗口，请勿加 `-display none`。

**验证步骤与预期输出**（输出串从源码逐字抄录）：

1. 开机第一屏应显示：
   ```
   Lesson 108: 服务状态机
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l108test`，预期输出：
   ```
   l108test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l108test: Lesson 101 fallback reported`。）
3. 输入 `l100test`（上一课回归），预期输出：
   ```
   l100test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
4. 输入 `waittest`（子进程状态机），预期输出：
   ```
   waittest: bounded wait, exit status, zombie selection, and reap passed
   tinyos>
   ```
5. 输入 `jobtest`（服务作业状态机），预期输出：
   ```
   jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed
   tinyos>
   ```
6. 输入 `reparenttest`（孤儿收养状态机），预期输出：
   ```
   reparenttest: orphan adoption, init wait ownership, and bounded reparent passed
   tinyos>
   ```
7. 输入 `about`，预期输出：
   ```
   Lesson 108: 服务状态机
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 108 checks passed.`；QEMU 中 `l108test`/`l100test` 打印 `...checkpoint passed`，`waittest`/`jobtest`/`reparenttest` 分别打印各自的 `...passed`，即代表本课 checkpoint 与服务状态机主题的验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l108test` 打印 fallback 串 | `lesson_101_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=101,b=102`） |
| 输入 `l101test` 提示 unknown command | 旧 README 把 fallback 编号当成命令名；源码没有该命令 | 用 `l100test`/`l108test`；`help` 里的命令表为准 |
| `waittest` 打印 `BROKEN` | 状态机顺序被打破（如 exit 前先 reap、重复 wait） | 核对 5 步顺序：start → wait 被拒 → exit → wait 成功 → reap；检查 `wait_model.state` 是否按 RUNNING→ZOMBIE→DEAD 走 |
| `jobtest` 打印 `BROKEN` | 作业越界（`i>=2`）或重复 exit/reap | 检查 `session_job_exit`/`session_job_reap` 的 guard：`active`、`zombie`、`reaped` 三标志状态 |
| `reparenttest` 打印 `BROKEN` | 收养 guard 不满足（未 orphaned 或已 adopted） | 检查 `adoption_exit_parent` 里 `original_parent==FIXED_PID`；`adoption_reparent` 里 `orphaned && !adopted` |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep '服务状态机' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l108test` 符号 | `grep -q 'l108test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：exit/zombie/wait/reap 状态机 = `kernel/exit.c`**
- TinyOS：`wait_model` 三态（`WAIT_RUNNING→WAIT_ZOMBIE→WAIT_DEAD`），`wait_model_exit`/`wait_model_wait`/`wait_model_reap` 各带 guard。
- Linux：`kernel/exit.c` 的 `do_exit()` 把任务改为 `EXIT_ZOMBIE`（`TASK_DEAD` 之前的僵尸阶段）；`wait4()`/`wait_task_zombie()` 读退出码后 `release_task()` 回收；`EXIT_DEAD` 表示已回收。任务状态宏在 `include/linux/sched.h`。
- 权威来源：Linux v6.x `kernel/exit.c`（`do_exit`、`wait_task_zombie`、`release_task`）；`include/linux/sched.h`（`EXIT_ZOMBIE`、`EXIT_DEAD`）。
- 教学简化：单子进程、无信号打断、无 `waitid`/`wait4` 的参数矩阵；但"必须先 wait 才能 reap"的顺序约束与 Linux 完全同构。

**对照点 2：作业生命周期与资源回收 = 服务管理/作业控制**
- TinyOS：`session_job` 的 `active→zombie→reaped` 与回收时清零五个资源计数。
- Linux：shell 作业控制把后台作业记入作业表，`wait` 回收；systemd 的 unit 状态机（`inactive → activating → active → deactivating → failed/dead`）是服务生命周期最完整的写照（`src/core/unit.c` 的 `UNIT_*` 状态与 `unit_notify` 迁移）。
- 权威来源：systemd `src/core/unit.h`/`unit.c`（`UnitActiveState`、`UNIT_ACTIVE` 等）；`kernel/exit.c` 的作业相关路径；`include/linux/sched.h`。
- 教学简化：作业数固定 2、无 `SIGCHLD` 通知、无失败（failed）态；`zombie` 位近似"退出但未确认"。

**对照点 3：孤儿收养 = subreaper / init 收养**
- TinyOS：`adoption_model` 的 `orphaned→adopted`，`wait_owner` 指向 init。
- Linux：父进程死亡后，孤儿被收养给最近的有 `PR_SET_CHILD_SUBREAPER` 的祖先，否则给 PID 1（`forget_original_parent()` 在 `kernel/exit.c` 里遍历父链做 `reparent_leader`）；systemd 作为 subreaper 批量收养孤儿并 `wait` 回收。
- 权威来源：Linux v6.x `kernel/exit.c`（`forget_original_parent`、`reparent_leader`、`child_reaper`）。
- 教学简化：收养目标固定为 init（无 subreaper 链、无 PID 命名空间），但"孤儿交给谁、谁负责回收"的语义保留。

**对照点 4：guard = 迁移合法性校验**
- TinyOS：每个迁移函数第一行 `if(状态不满足)return 0;`。
- Linux：`kernel/exit.c` 与 `kernel/fork.c` 有大量前置检查（如 `do_exit` 拒绝在 `TASK_DEAD` 上重复执行、`wait_task_zombie` 校验 `exit_state`）；状态迁移通过 `exit_state`/`state` 位字段受保护。
- 权威来源：Linux v6.x `kernel/exit.c`、`kernel/fork.c`（`exit_state` 检查）。
- 教学简化：单标志位 vs Linux 的 `state`+`exit_state` 复合位字段；但"非法迁移必须被拒"的原则一致。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `wait_model_reap` 要求 `waited && state==WAIT_ZOMBIE` 两个条件？如果去掉 `waited`，会发生什么错误（提示：父进程从未 wait 就"回收"了退出码）？
2. **源码定位**：画出 `session_job` 的三态迁移图，指出 `session_job_exit`/`session_job_reap` 各自的 guard 与副作用（资源清零发生在哪一步？）。
3. **动手实验**：修改 `wait_model_exit`，把 guard `state!=WAIT_RUNNING` 改成 `state!=WAIT_ZOMBIE`，`make run` 后跑 `waittest`，观察状态机允许了非法迁移导致什么断言失败。改完请**恢复原值**。
4. **动手实验**：给 `session_job_reap` 去掉"清空五个资源计数"的副作用，`make run` 后跑 `jobtest`，观察断言是否仍通过（理解：状态迁移断言未必覆盖副作用，这正是思考题的意义）。改完请**恢复原值**。
5. **Linux 对照**：阅读 `kernel/exit.c` 的 `reparent_leader`，说明它把孤儿"挂到哪条链上"，并对比 `adoption_model` 的 `current_parent` 迁移；`PR_SET_CHILD_SUBREAPER` 与教学模型的 `wait_owner=init` 有何异同？

---

## 9. 本课小结与下一课预告

- 本课把"服务状态机"讲透了：状态集合、迁移 guard、迁移副作用三要素，贯穿 `wait_model`、`session_job`、`adoption_model` 三条状态机。
- 你理解了子进程的一生：`WAIT_RUNNING → WAIT_ZOMBIE → WAIT_DEAD`，以及"必须先 wait 才能 reap"的顺序约束。
- 你理解了服务作业的一生：`active → zombie → reaped`，回收的副作用是清零 fd/pipe/signal/timer/work 五个资源计数。
- 你理解了孤儿收养：`orphaned → adopted`，`wait_owner` 决定谁负责回收。
- 你在 Linux 对照里见到了真身：`kernel/exit.c` 的 `do_exit`/`wait_task_zombie`/`release_task`、systemd 的 unit 状态机、`forget_original_parent` 的收养链。
- 你完成了 checkpoint 验证：`l108test` 与回归 `l100test` 打印 `bounded VFS, devices, epoll, and service management checkpoint passed`，并把旧 README 的 `l101test` 命令勘误为源码真实的 `l100test`/`l108test`。

**下一课预告**：Lesson 109 起进入下一主题（服务/设备系列的后续检查点）。本课（108）为「服务管理」标签画上句号：从 103 的 poll 就绪、104 的 epoll 实例、105/106 的 ET/LT、107 的 wait/wake，到本课把"服务对象的一生"用状态机收束。衔接点：`session_job` 的 `reaped` 与进程组的 `controlled` 标志会在后续的会话/作业控制课里继续演化。见 [`../lesson-109-stable/README.md`](../lesson-109-stable/README.md)。

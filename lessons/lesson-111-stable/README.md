# Lesson 111: 守护进程生命周期 — 精讲文档

> **课号**：Lesson 111（检查点课 / Checkpoint Lesson，可执行课）
> **主题**：守护进程生命周期（Daemon Lifecycle）
> **课程主线位置**：第 5 阶段「VFS/设备/epoll/服务管理检查点」主线中段（100–114）。
> 前课 Lesson 110（服务启动与失败回滚）聚焦启动/回滚；本课把「守护进程（daemon）从
> fork/启动、detach、运行、退出到回收的完整生命周期」作为检查点主题；Lesson 112 做
> VFS/设备/epoll/服务综合验证，113 起转入并发。
> **前置课程**：[`lesson-110-stable/README.md`](../lesson-110-stable/README.md)
> **后续课程**：[`lesson-112-stable/README.md`](../lesson-112-stable/README.md)
> **一句话目标**：理解 `l111test` 检查点如何用 `lesson_104_model` 重申守护进程生命周期
> 不变量，并弄清源码里 `adoption_model`（孤儿收养/reparent）、`fork_exec_lifecycle`
> （fork→exec→exit→wait/reap）、`session_job`（job 生命周期）等继承机制如何共同构成
> 「守护进程从生到死」的完整链条。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能画出守护进程的生命周期状态图，并指出源码中每个转移对应的
函数：fork 出生（`lifecycle_fork`）、exec 换体（`lifecycle_exec`）、detach/脱离父进程
（`adoption_reparent`）、退出成 zombie（`lifecycle_exit`/`session_job_exit`）、被 init
wait/reap 回收（`lifecycle_wait_reap`/`adoption_wait_owner`）。`l111test` 输出
`bounded VFS, devices, epoll, and service management checkpoint passed` 即通过。

- **在课程主线中的位置**：检查点主线第 3 站（109→110→111→112）。主题聚焦
  「守护进程的完整生命周期」，机制仍是继承自 66–68 课（孤儿/作业控制）与 88–108 课
  （wait/lifecycle/session/adoption）的元数据模型。
- **责任边界**：本课**不新增** daemon 机制。新增的只有 `struct lesson_104_model`、
  `lesson_104_state`、`l111test()` 与主题串。
- **前置知识清单**：① `fork_exec_lifecycle` 状态机（`lifecycle_fork/exec/exit/
  wait_reap`，对应 `forkexecwaittest` 命令）；② `adoption_model`（`adoption_reparent`/
  `adoption_exit_parent`/`adoption_wait_owner`，对应 `reparenttest`）；③
  `session_job` 的 active/zombie/reaped 状态（对应 `jobtest`）；④
  `orphan_group_model` 与 `process_group_model`（孤儿进程组/session 模型）。
- **本课交付**：命令 `l111test`（新增）与 `l103test`（由 110 课 `l110test` 更名）；
  `about`/横幅显示 `Lesson 111: 守护进程生命周期`；
  `make check` 输出 `Multiboot2 and Lesson 111 checks passed.`。

---

## 2. 核心概念精讲

### 2.1 概念一：守护进程（Daemon）与它的生命周期阶段

定义：守护进程是「脱离控制终端、由 init 收养、后台持续运行」的服务进程。生命周期分为
五个阶段：① 出生（fork）；② 换体（exec，加载新镜像）；③ 脱离（detach，父进程退出或
显式 setsid，孤儿被 init 收养）；④ 运行/退出（exit，留下退出码）；⑤ 回收（zombie 被
init wait/reap）。

为什么需要：真实系统里 init（PID 1）收养所有孤儿（`PR_SET_CHILD_SUBREAPER` 之前由 PID 1
承担），保证没有无人认领的僵尸。TinyOS 用元数据复刻这套「孤儿必被收养、退出必被回收」
的生命周期纪律。

### 2.2 概念二：孤儿收养与 reparent（adoption_model）

定义：父进程退出后，其子进程的 `parent_pid` 改为 init（pid=1），由 init 成为新的
wait 所有者。

为什么需要：如果孤儿无人认领，它变成僵尸后没人 `wait()`，进程表会泄漏。收养（reparent）
让 init 承担收尸责任，这就是守护进程可以脱离原始父进程还不会泄漏的原因。

工作机制（源码逐字引用见 §3.3）：
- `adoption_start()` 初始化 `init_pid=1, original_parent=FIXED_PID, child_pid=SECOND_PID,
  current_parent=FIXED_PID`；
- `adoption_exit_parent()`：父进程退出 → 置 `orphaned=1`、`wait_owner=init_pid`，随即调用
  `adoption_reparent()`；
- `adoption_reparent()`：仅当 `orphaned && !adopted` 时把 `current_parent=init_pid`、
  `adopted=1`、`adoptions++`；
- `adoption_wait_owner()`：返回「已被收养且 wait 所有者是 init 且当前父是 init」；
- `reparenttest` 走完整序列，输出
  `orphan adoption, init wait ownership, and bounded reparent passed`。

### 2.3 概念三：守护进程的 fork→exec→exit→wait/reap 主链

定义：`fork_exec_lifecycle` 是守护进程主体生命周期的状态机：fork 出生 → exec 换镜像 →
exit 退出（带退出码）→ wait 取得状态 → reap 回收（zombie→dead）。

工作机制（源码逐字引用见 §3.3）：
- `lifecycle_fork()`：仅当尚未 fork 且 child 为 WAIT_RUNNING 时置 `forks=1`；
- `lifecycle_exec()`：前置要求 `forks && !execs && exec_validate()`，然后记录
  `old_entry/new_entry/argc/envc`、置 `image_replaced=1`，并 `return exec_stack_validate()`；
- `lifecycle_exit(code)`：要求 `image_replaced && child_state==WAIT_RUNNING`，置
  `exit_code/WAIT_ZOMBIE/exits=1`；
- `lifecycle_wait_reap()`：要求 `WAIT_ZOMBIE`，置 `waits=1,reaps=1`，状态转 `WAIT_DEAD`；
- `forkexecwaittest` 输出
  `fork metadata, exec replacement, exit status, and wait/reap passed`。

### 2.4 概念四：`lesson_104_model` 检查点模型

```c
struct lesson_104_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_104_model lesson_104_state;
```

与 103 模型同构，编号前进一位；`l111test` 注入 `{104U,105U,106U,107U,1,1,1,1}` 并断言
`valid&&active&&ready&&accounted&&b==a+1U`。四个状态位对应守护进程生命周期中的
「合法/激活/就绪/记账」四属性——与 `session_job` 的 active/execed/zombie/reaped 位
一一对应。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-110） |
|---|---|---|
| `boot.S` | Multiboot2 header + 长模式引导 | 未变化 |
| `kernel.c` | 32 位阶段 | 未变化 |
| `kernel64.c` | 64 位内核主体 | `l110test`→`l103test` 更名；新增 `struct lesson_104_model`/`lesson_104_state`/`l111test`；exec64 增 `l111test` 分支；`about` 与横幅改「Lesson 111: 守护进程生命周期」 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 + check | check grep 更新为 `守护进程生命周期`/`l111test`/`Lesson 111` |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 本课增量（源码逐字）

更名命令 `l103test`（即 110 课的 `l110test`）：

```c
static TEXT64 void l103test(u16*c){lesson_103_state=(struct lesson_103_model){103U,104U,105U,106U,1,1,1,1};int ok=lesson_103_state.valid&&lesson_103_state.active&&lesson_103_state.ready&&lesson_103_state.accounted&&lesson_103_state.b==lesson_103_state.a+1U;text64(c,"l103test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 103 fallback reported");putc64(c,'\n');}
```

- 函数名改为 `l103test`（= 模型号 103），逻辑逐字未变；
- 检查点课的更名纪律：diff 除函数名/前缀外应与上一课逐字一致。

新增结构与命令：

```c
struct lesson_104_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_104_model lesson_104_state;
static TEXT64 void l111test(u16*c){lesson_104_state=(struct lesson_104_model){104U,105U,106U,107U,1,1,1,1};int ok=lesson_104_state.valid&&lesson_104_state.active&&lesson_104_state.ready&&lesson_104_state.accounted&&lesson_104_state.b==lesson_104_state.a+1U;text64(c,"l111test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 104 fallback reported");putc64(c,'\n');}
```

逐行拆解：

1. 结构 `lesson_104_model`：4 个 u32 计数 + 4 个 u8 状态位，与 103 同构；
2. 全局 `lesson_104_state`：默认全零，仅 `l111test` 注入；
3. `l111test` 赋值 `{104U,105U,106U,107U,1,1,1,1}`：`a=104,b=105,c=106,d=107`，四状态位
   全 1——表示守护进程「激活且就绪且资源已记账」；
4. 断言 `ok=`：五条件 AND（四状态位 + `b==a+1U`）；
5. 输出：`l111test: ` + 成功串或 `Lesson 104 fallback reported`。

exec64 新增分支（源码逐字）：

```c
else if(eq64(word,"l103test")){if(!noargs64(arg))usage64(c,"l103test");else l103test(c);}else if(eq64(word,"l111test")){if(!noargs64(arg))usage64(c,"l111test");else l111test(c);}
```

- `l103test` 分支由 `l110test` 改名，`l111test` 分支新增；
- `help` 串未追加这两个命令（既有小瑕疵）。

主题横幅与 about（源码逐字）：

```c
text64(&c,"Lesson 111: 守护进程生命周期\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

```c
text64(c,"Lesson 111: 守护进程生命周期\n");
```

### 3.3 继承机制精讲——守护进程生命周期的源码基础

孤儿收养 `adoption_model`（源码逐字）：

```c
struct adoption_model { u64 init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; u8 orphaned,adopted,wait_owner; };
static struct adoption_model adoption_model;
static TEXT64 int adoption_reparent(void){if(!adoption_model.orphaned||adoption_model.adopted)return 0;adoption_model.current_parent=adoption_model.init_pid;adoption_model.adopted=1;adoption_model.adoptions++;return 1;}
static TEXT64 int adoption_exit_parent(void){if(adoption_model.original_parent!=FIXED_PID)return 0;adoption_model.orphaned=1;adoption_model.wait_owner=adoption_model.init_pid;return adoption_reparent();}
static TEXT64 int adoption_wait_owner(void){adoption_model.ownership_checks++;return adoption_model.adopted&&adoption_model.wait_owner==adoption_model.init_pid&&adoption_model.current_parent==adoption_model.init_pid;}
```

- `adoption_reparent` 前置守卫 `!orphaned||adopted`：未成孤儿或已被收养则拒绝——收养是
  一次性动作，防重复；
- `adoption_exit_parent` 要求 `original_parent==FIXED_PID`（只有本模型承认的父退出才算），
  置 `orphaned=1` 后直接调用 `adoption_reparent` 完成收养；
- `adoption_wait_owner` 断言三条同时成立：已收养、wait 所有者是 init、当前父是 init；
- 这就是守护进程 detach 的元数据：守护进程一旦脱离原父，wait 职责自动归 init。

生命周期状态机 `fork_exec_lifecycle`（源码逐字）：

```c
static TEXT64 int lifecycle_fork(void){if(lifecycle_model.forks||lifecycle_model.child_state!=WAIT_RUNNING)return 0;lifecycle_model.forks=1;return 1;}
static TEXT64 int lifecycle_exec(void){if(!lifecycle_model.forks||lifecycle_model.execs||!exec_validate())return 0;lifecycle_model.old_entry=USER_CODE_VA;lifecycle_model.new_entry=exec_model.entry;lifecycle_model.argc=exec_model.argc;lifecycle_model.envc=1;lifecycle_model.execs=1;lifecycle_model.image_replaced=1;return exec_stack_validate();}
static TEXT64 int lifecycle_exit(u64 code){if(!lifecycle_model.image_replaced||lifecycle_model.child_state!=WAIT_RUNNING)return 0;lifecycle_model.exit_code=code;lifecycle_model.child_state=WAIT_ZOMBIE;lifecycle_model.exits=1;return 1;}
static TEXT64 int lifecycle_wait_reap(void){if(lifecycle_model.child_state!=WAIT_ZOMBIE)return 0;lifecycle_model.waits=1;lifecycle_model.reaps=1;lifecycle_model.child_state=WAIT_DEAD;return 1;}
```

- 每个函数都有前置状态守卫（`forks` 未置、`execs` 已置、`child_state` 必须匹配），
  保证生命周期只能按 fork→exec→exit→reap 顺序前进；
- `lifecycle_exec` 把 `old_entry=USER_CODE_VA` 与 `new_entry=exec_model.entry` 都记下，
  是「镜像替换」的证据；`image_replaced=1` 是 exit 的前置条件；
- `lifecycle_wait_reap` 一次性完成 wait+reap，状态 `WAIT_ZOMBIE→WAIT_DEAD`；
- `forkexecwaittest` 用 `g=...argc==2&&envc==1&&exit_code==23` 断言换体后参数与退出码，
  输出 `fork metadata, exec replacement, exit status, and wait/reap passed`。

session job 生命周期（源码逐字）：

```c
static TEXT64 int session_job_exit(u32 i,u64 code){if(i>=2||!session_jobs[i].active||session_jobs[i].zombie)return 0;session_jobs[i].status=code;session_jobs[i].zombie=1;return 1;}
static TEXT64 int session_job_reap(u32 i){if(i>=2||!session_jobs[i].zombie||session_jobs[i].reaped)return 0;session_jobs[i].fd=session_jobs[i].pipe=session_jobs[i].signals=session_jobs[i].timers=session_jobs[i].deferred=0;session_jobs[i].reaped=1;session_jobs[i].active=0;session_reaps++;return 1;}
```

- `session_job_exit`：job 必须 `active && !zombie` 才能退出，置 status/zombie；
- `session_job_reap`：job 必须 `zombie && !reaped` 才能回收；回收时把 fd/pipe/signal/
  timer/deferred 五类引用清零再 `reaped=1,active=0`——这是「守护进程收尸时释放全部
  资源」的顺序模型；
- `jobtest` 走「两个 job 退出 → wait → reap」，输出
  `init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`。

### 3.4 构建管线

与 lesson-110 完全一致：`kernel64.o`（`-m64 -ffreestanding -fpie -mno-red-zone
-mno-sse* -Werror`）→ `ld -m elf_x86_64 -T kernel64.ld` → `objcopy -O binary` →
boot.S `.incbin` → 外层 `kernel.elf` → `grub-mkrescue`。`check` 目标：
`grub-file --is-x86-multiboot2` + grep `守护进程生命周期`/`l111test`/`Lesson 111`，
通过打印 `Multiboot2 and Lesson 111 checks passed.`。`run` 用 `-accel tcg -serial
stdio` 的 QEMU 命令。

### 3.5 主控制流

```text
GRUB → kernel_main32 → 长模式 → kernel_main64_binary
  ├─ module_init_model(); init_model_start(); wait_model_start();
  │  adoption_start(); resource_start(); pmm_init(h); vma_init();
  │  reclaim_init(); vfs_init(); address_space_init(...)
  ├─ ... framebuffer_init(h); install_idt(h); pit_init(); pic_init()
  ├─ 横幅 "Lesson 111: 守护进程生命周期\nGETTICKS, ... bounded reclaim metadata\n"
  └─ 键盘循环 → exec64
        ├─ l103test → 复验 lesson_103 检查点（更名命令）
        ├─ l111test → 复验 lesson_104 检查点（本课新增）
        └─ reparenttest / forkexecwaittest / jobtest / orphan66test 等继承命令
```

---

## 4. 数据流与运行逻辑

```text
输入 "l111test" → exec64 → l111test(c)
  → lesson_104_state = {104,105,106,107, 1,1,1,1}
  → ok = valid(1)&&active(1)&&ready(1)&&accounted(1)&&b(105)==a(104)+1
  → "l111test: bounded VFS, devices, epoll, and service management checkpoint passed"

守护进程生命周期演示链：
  forkexecwaittest: lifecycle_fork → lifecycle_exec → lifecycle_exit(23)
                  → lifecycle_wait_reap → child_state==WAIT_DEAD
  reparenttest:    adoption_exit_parent（父退出）→ adoption_reparent（孤儿给 init）
                  → adoption_wait_owner（init 成为 wait 所有者）
  jobtest:         session_job_exit(1,9) / session_job_exit(0,7)
                  → session_job_reap → fd/pipe/signal/timer/deferred 全部清零
```

三条命令共同刻画守护进程从出生、detach、运行到收尸的完整生命周期。

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

`make check` 输出：`Multiboot2 and Lesson 111 checks passed.`（README 必须含
`守护进程生命周期` 与 `Lesson 111`，kernel64.c 必须含 `l111test`）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 111: 守护进程生命周期\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字抄录）：

```bash
l111test
```

预期：

```text
l111test: bounded VFS, devices, epoll, and service management checkpoint passed
```

```bash
about
```

预期：`Lesson 111: 守护进程生命周期`

```bash
reparenttest
```

预期：`reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`

```bash
forkexecwaittest
```

预期：`forkexecwaittest: fork metadata, exec replacement, exit status, and wait/reap passed`

```bash
jobtest
```

预期：`jobtest: init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed`

继承回归：`l103test`、`orphan66test`、`sessiontest`、`fgtest`、`nohangtest`、
`waittest` 行为不变。

### 5.4 课程实测记录（稳定快照）

旧 README 声明「Commands: `l104test`」——**命令名以源码为准勘误**：本课源码可执行命令是
`l103test`（更名）与 `l111test`（新增）；`l104test` 是模型编号（lesson_104），不是命令。
`make check` 复验输出 `Multiboot2 and Lesson 111 checks passed.`，`l111test` 显示
passed，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l111test` 输出 `Lesson 104 fallback reported` | `lesson_104_state` 未注入或断言不成立 | 核对 `l111test` 赋值 `{104U,105U,106U,107U,1,1,1,1}` 与 `ok` 五条件 |
| `reparenttest` 输出 `BROKEN` | `adoption_exit_parent` 的 `original_parent!=FIXED_PID` 或收养被重复 | 检查 `adoption_start()` 初始化；`adoption_reparent` 的 `!orphaned||adopted` 守卫 |
| `forkexecwaittest` 输出 `BROKEN` | `lifecycle_exec` 的 `exec_validate()` 为假，或调用顺序错 | 先 `execinfo` 确认 exec_model.validated；核对 fork→exec→exit→reap 顺序 |
| `jobtest` 输出 `BROKEN` | `session_job_reap` 的 `zombie && !reaped` 守卫或引用清零出错 | 检查 `session_start()` 初始化；`session_job_exit` 是否先置 zombie |
| `make check` 报错 | README 缺 `守护进程生命周期`/`Lesson 111` 或 kernel64.c 缺 `l111test` | 对照 Makefile 三条 grep |
| `about` 显示旧课号 | 主题串未更新 | grep `Lesson 111: 守护进程生命周期` kernel64.c |
| help 列表找不到 `l111test` | 已知小瑕疵：help 串未追加检查点命令 | 用 `about`/README 发现命令 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `adoption_model`（孤儿被 init 收养、wait 所有者转移） | `kernel/exit.c`：`forget_original_parent()` + `find_new_reaper()`；`PR_SET_CHILD_SUBREAPER`（`include/linux/prctl.h`） | 孤儿 reparent 给 PID 1（或 subreaper）语义一致；教学模型用字段改写模拟 |
| `fork_exec_lifecycle`（fork→exec→exit→wait/reap） | `kernel/fork.c` `copy_process`；`fs/exec.c` `do_execveat_common`；`kernel/exit.c` `do_exit`/`wait4` | 生命周期状态机与 Linux 完全对应，教学模型只记录元数据不执行 |
| `session_job_reap` 把 fd/pipe/signal/timer/deferred 清零 | `kernel/exit.c`：`exit_files()`/`exit_signals()`/`exit_mm()` 顺序清理 | 回收时按依赖释放一致；教学模型用 5 次清零代替真实释放 |
| `lesson_104_model` 的四个状态位 | `kernel/sched.h` `task_struct::exit_state`（EXIT_ZOMBIE/EXIT_DEAD）；`linux/fs.h` 状态位 | 状态位语义一致（active/execed/zombie/reaped vs RUNNING/ZOMBIE/DEAD） |
| `orphan66test`/`sessiontest`/`fgtest` 进程组模型 | `kernel/signal.c` + `include/linux/tty.h`：session/进程组/控制终端 | 与 Linux session 模型对照（lesson-66 起引入） |

**权威来源**：Linux v6.x（`kernel/fork.c`、`kernel/exit.c`、`fs/exec.c`、
`kernel/signal.c`）；POSIX（进程、session、进程组、wait）；systemd（守护进程 daemon 化
规范）。

**教学模型简化了什么**：无真实 fork/exec/exit 系统调用；无真实信号与终端；
adoption 用 u64 字段记录父指针变化，不涉及真实进程树遍历。

---

## 8. 思考题与练习

1. **概念理解**：守护进程「孤儿被 init 收养」在 `adoption_model` 里对应哪两个字段的
   修改？`adoption_wait_owner` 为什么要求三条件同时成立？
2. **源码定位**：`lifecycle_exec` 的前置条件之一是 `!lifecycle_model.execs`。如果同一个
   daemon 想 exec 两次（重启换镜像），这个模型怎么处理？需要改哪几个字段？
3. **动手实验**：把 `l111test` 的注入值改成 `{104U,106U,106U,107U,1,1,1,1}`（b 跳号），
   `make run` 后输入 `l111test` 观察 fallback 串，再改回（勿提交）。
4. **Linux 对照**：读 `find_new_reaper()`（`kernel/exit.c`），对照本课
   `adoption_exit_parent→adoption_reparent` 的决策条件，列出教学模型省略的「候选收尸人
   选择」逻辑。
5. **设计思考**：`session_job_reap` 回收时按 fd→pipe→signal→timer→deferred 顺序清零。
   这个顺序在本课源码里是固定写法；如果要表达「先释放最依赖的」应该怎么改？

---

## 9. 本课小结与下一课预告

**小结**：本课以「守护进程生命周期」为主题，是检查点主线的第 3 站。源码增量依旧是
「更名 + 新模型 + 新命令 + 主题串」：`l110test`→`l103test`、新增
`struct lesson_104_model`/`lesson_104_state`/`l111test`、exec64 增分支、about/横幅更新。
生命周期机制全部继承：`adoption_model` 处理孤儿收养与 wait 所有权转移（detach）、
`fork_exec_lifecycle` 给出 fork→exec→exit→reap 状态机、`session_job` 的
exit/reap 在收尸时依次释放 fd/pipe/signal/timer/deferred。`l111test` 用
`{104U,105U,106U,107U,1,1,1,1}` 重申守护进程「激活、就绪、记账」三属性。

**下一课预告**：[Lesson 112](../lesson-112-stable/README.md) 主题「VFS/设备/epoll/服务
综合验证」：这是第 5 阶段检查点主线的收官站，`l112test` 用 `lesson_105_model` 对
VFS、设备、epoll 与服务管理整套元数据做最终综合验证；`l104test` 由本课 `l111test`
更名收敛。113 课起主题转向 mutex 与 spinlock 竞争。

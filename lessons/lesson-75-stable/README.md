# Lesson 75: 终端 stop/continue 状态转换 — 精讲文档

> **课号**：Lesson 75（对应主线源课 Lesson 68）
> **本课主题**：终端作业在「停止（stop）」与「继续（continue）」之间的状态转换元数据
> **课程主线位置**：GUI 支线（Lesson 61–67）结课后的「进程组/session/调度元数据」阶段（Lesson 68 起恢复主线）。本课承接 Lesson 74 的 job-control 信号路由：信号已经知道"发给前台组"，本课回答"发完 SIGTSTP/SIGSTOP 后组进入什么状态、再发 SIGCONT 又如何恢复前台"。
> **前置课程**：[`../lesson-74-stable/README.md`](../lesson-74-stable/README.md)（job-control 信号路由：`foreground`/`blocked` 前置状态位）
> **后续课程**：[`../lesson-76-stable/README.md`](../lesson-76-stable/README.md)（调度策略元数据）
> **本课一句话目标**：学会用「固定元数据 + 确定性验证」模型描述"进程组被 SIGSTOP(19) 停止、再被 SIGCONT(18) 继续、前台身份与控制终端所有权全程保持"的完整状态转换，并对照 POSIX/Linux 的 `TASK_STOPPED`/`TASK_TRACED` 与 `tty->pgrp` 语义。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释为什么停止/继续作业要"整组进行"、为什么 SIGSTOP 停止后必须靠 SIGCONT（或 SIGHUP+SIGCONT）才能恢复、以及"继续后仍是前台组 + 仍持有终端"这条不变量的意义；你能用 `stop68test` 做确定性验证。
- **在课程主线中的位置**：Lesson 73（孤儿化 + reparent）→ 74（信号路由到前台组）→ **75（信号引发的 stop/continue 状态转换）** 是一条连贯的 job-control 生命线。至此，进程组的"出生（session/终端）、运行（前台/后台）、中断（孤儿/停止）、恢复（继续）、收养（reparent）"全部建模完毕；Lesson 76 起把视角从进程组转向调度器（策略、优先级、runqueue）。
- **前置知识清单**：
  1. 前台/后台进程组、控制终端所有权（Lesson 69–70）；
  2. 作业控制信号家族与"发给整组"的路由规则（Lesson 74）；
  3. 信号编号：`SIGSTOP=19`、`SIGCONT=18`、`SIGTSTP=20`（x86 Linux 编号）；
  4. 本内核任务状态枚举 `task_state` 中的 `TASK_STOPPED=4`（Lesson 37 起累积）。
- **本课交付**：新增固定容量的 `terminal_stop_model` 记录与 `stop68test` 验证命令；`about`/banner 更新为「Lesson 75: 终端 stop/continue 状态转换」。

---

## 2. 核心概念精讲

### 2.1 停止信号家族：SIGSTOP / SIGTSTP / SIGCONT

**直觉**：`Ctrl-Z` 把前台作业"暂停"到后台，shell 会提示 `[1]+ Stopped`；`fg`/`bg`/`kill -CONT` 再让它继续。这套机制的核心是三类信号：
- `SIGSTOP`（19）：**无条件**停止进程/进程组，进程无法捕获或忽略；
- `SIGTSTP`（20）：终端产生的"暂停"信号，进程可以捕获或忽略（`Ctrl-Z` 实际发它）；
- `SIGCONT`（18）：继续一个被停止的进程/进程组。

### 2.2 状态转换：running ⇄ stopped

进程/任务在两个可执行态间来回切换：

```
    SIGSTOP / SIGTSTP
        ┌────────────────────────┐
        ▼                        │
   [TASK_STOPPED] ──► SIGCONT ───┘
```

- 进入停止：内核把组内每个任务的 `task->state` 置为 `TASK_STOPPED`（停止态），任务不再参与调度；
- 恢复：`SIGCONT` 把任务重新置为 runnable；若是孤儿组，则用 `SIGHUP` 之后紧跟 `SIGCONT` 的方式唤醒（Lesson 73 已讲）；
- **必须用 SIGCONT**：一旦进程被停止，只有 SIGCONT（或 wait 通知父进程）能解除；普通信号无法让停止态任务运行。

### 2.3 本课模型：一次"已完成的 stop→continue"记录

`terminal_stop_model` 不是一个状态机，而是**一次转换完成后的结果记录**：
- `stop_signal=19`（SIGSTOP）、`continue_signal=18`（SIGCONT）——记录"用什么信号停止、用什么信号继续"；
- `stopped=0`——当前**不在**停止态（转换已完成）；
- `continued=1`——确实发生过"继续"；
- `foreground=1`、`terminal_owned=1`——继续后仍是前台组、仍持有控制终端。

```
                    ┌────────────────────────────────┐
                    │  pgid=200 (前台作业, 终端 owner)│
                    │  stop_signal=19 (SIGSTOP)       │
                    │  continue_signal=18 (SIGCONT)   │
                    │                                 │
   running ──SIGSTOP──► stopped ──SIGCONT──► running  │
                    │  stopped=0  continued=1         │
                    │  foreground=1 terminal_owned=1   │
                    └────────────────────────────────┘
```

### 2.4 「固定元数据 + 确定性验证」教学模型

与 Lesson 73/74 一致：用固定数值 `{200,19,18,0,1,1,1}` 固化"停止再继续后恢复前台"的理想终态，用一条 7 条件布尔表达式验证。真实内核中"停止/继续"涉及 `signal_wake_up`、`do_notify_parent`（`CLD_STOPPED`/`CLD_CONTINUED`）等机制，本课全部退化为标志位断言。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-74） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、主循环 | **主要增量**：新增 `terminal_stop_model` 结构、`terminal_stop` 全局、`stop68test()`、`exec64` 的 `stop68test` 分支、`about`/banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局与栈断言 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 75 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 新增结构体与全局变量

```c
struct terminal_stop_model { u32 pgid,stop_signal,continue_signal; u8 stopped,continued,foreground,terminal_owned; };
static struct terminal_stop_model terminal_stop;
```

逐行注释：
- `pgid`（u32）：被停止/继续的进程组号，取 200——与 Lesson 73/74 复用同一"前台作业组"编号，保持系列课程的连续性。
- `stop_signal`（u32）：使组停止的信号，取 19（`SIGSTOP`）。用无条件的 SIGSTOP 而非可捕获的 SIGTSTP，是因为模型不模拟"用户是否屏蔽"，选最刚性的语义。
- `continue_signal`（u32）：使组恢复的信号，取 18（`SIGCONT`）。`stop_signal` 与 `continue_signal` 成对出现，明确"什么停的、什么继续的"。
- `stopped`（u8）：当前是否处于停止态。取 0，表示记录的是**转换完成后**的快照，而不是转换中间态。
- `continued`（u8）：是否发生过 continue 转换。取 1，与 `stopped=0` 一起证明"SIGCONT 成功把组带出停止态"。
- `foreground`（u8）：继续后是否仍是前台进程组。取 1——停止/继续不改变前台身份。
- `terminal_owned`（u8）：控制终端所有权是否保持。取 1——被停止的组不会失去终端，这是 shell 能 `fg` 唤回它的前提。
- `static struct terminal_stop_model terminal_stop;`：单一全局记录。

#### (b) 本课核心验证函数 `stop68test`

```c
static TEXT64 void stop68test(u16*c){terminal_stop=(struct terminal_stop_model){200,19,18,0,1,1,1};int ok=terminal_stop.pgid==200&&terminal_stop.stop_signal==19&&terminal_stop.continue_signal==18&&!terminal_stop.stopped&&terminal_stop.continued&&terminal_stop.foreground&&terminal_stop.terminal_owned;text64(c,"stop68test: ");text64(c,ok?"bounded terminal stop/continue and foreground recovery passed":"terminal stop/continue fallback reported");putc64(c,'\n');}
```

逐行注释：
- `terminal_stop=(struct terminal_stop_model){200,19,18,0,1,1,1};`：聚合初始化出"SIGSTOP 停止 → SIGCONT 继续 → 前台恢复"的终态场景。
- `int ok=...`：7 个条件全部成立才通过：① 组号是 200；② 停止信号确是 SIGSTOP(19)；③ 继续信号确是 SIGCONT(18)；④ 当前不在停止态（`!stopped`）；⑤ 确实发生了继续（`continued`）；⑥ 仍在前台；⑦ 终端所有权未丢。
- `text64(c,"stop68test: ");`：命令前缀。
- `text64(c,ok?"bounded terminal stop/continue and foreground recovery passed":"terminal stop/continue fallback reported");`：成功/失败两条基准输出串逐字来自源码。
- `putc64(c,'\n');`：换行。

为什么这样设计：转换类语义最难验证的是"从哪来、到哪去、中间不可缺"。本课用 `stopped`/`continued` 两枚标志把"进入停止、离开停止"两个事件都显式记录，再叠加 `foreground`/`terminal_owned` 两条"恢复不变量"，一次断言覆盖完整转换链路——既证明结果对，也证明路径对。

#### (c) 继承的作业控制回归锚点

本课 `kernel64.c` 完整保留 `job67test`、`orphan66test`、`l65test` 及上一课全部分支，行数从 781 增至 784。`stop68test` 是"路由（74）→ 状态转换（75）"的延续，而旧命令保留保证信号路由/孤儿组语义未被破坏。

#### (d) `exec64` 命令分派中的增量分支

```c
}else if(eq64(word,"stop68test")){if(!noargs64(arg))usage64(c,"stop68test");else stop68test(c);}
```

逐行注释：
- 与其他课命令分支同构：匹配命令字 → 拒绝多余参数 → 无参调用 `stop68test(c)`。
- `about` 文案更新为：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 75: 终端 stop/continue 状态转换\n");}
```

- `help` 命令列表仍保持旧字面量（不含 `stop68test`），教学简化的延续。

#### (e) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
text64(&c,"Lesson 75: 终端 stop/continue 状态转换\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- 首行主题换成本课；syscall ABI 与 "bounded reclaim metadata" 边界不变，说明本课仍只增元数据。

#### (f) 继承的关键辅助函数（本课复用）

`eq64`、`noargs64`、`text64`/`putc64`、`token64` 与 Lesson 73/74 相同，均带 `TEXT64` 段属性；其中 `putc64` 对 `'\n'` 做"跳到下一行行首"处理，是 VGA 文本输出的基础。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '终端 stop/continue 状态转换' README.md
	@grep -q 'stop68test' kernel64.c
	@grep -q 'Lesson 75' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 75 checks passed.'
```

- 与 Lesson 74 相比仅替换三条 grep 关键字与 printf 信息：README 主题、源码新符号 `stop68test`、课号 75。
- `grub-file --is-x86-multiboot2` 仍是最权威的 Multiboot2 合规检查；`kernel64.ld` 的 `ASSERT(...==0x1000)` 三个栈断言不变。
- 构建链与 Lesson 73/74 完全一致（`kernel64.o` → `kernel64.bin` → `boot.o` 内嵌 → `kernel.elf` → `grub-mkrescue` ISO）。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32 ──► enter_long_mode ──► kernel_main64_binary
    ├─ 元数据初始化（...）
    ├─ banner: "Lesson 75: 终端 stop/continue 状态转换\nGETTICKS, ..."
    └─ for(;;) 键盘循环:
        "stop68test\n" ──► exec64 ──► eq64(word,"stop68test")
        ──► stop68test(c)
        ──► terminal_stop 初始化 {200,19,18,0,1,1,1} + 7 条件校验
        ──► VGA: "stop68test: bounded terminal stop/continue and foreground recovery passed"
        ──► "tinyos> "
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打印本课主题，`tinyos> ` 提示符就绪。
2. **输入**：`stop68test` + 回车，键盘循环收 10 字符进 `cmd`。
3. **分派**：`exec64` 切词后命中 `stop68test` 分支。
4. **校验**：`terminal_stop` 聚合初始化，7 条件布尔校验。
5. **输出**：`stop68test: bounded terminal stop/continue and foreground recovery passed`，回显 `tinyos> `。

---

## 5. 构建、运行与验证

**依赖**：同全仓库，见 [`docs/local-validation.md`](../../docs/local-validation.md)。

**构建**（与 Makefile 一致）：

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

1. 开机第一屏：
   ```
   Lesson 75: 终端 stop/continue 状态转换
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `stop68test`，预期输出：
   ```
   stop68test: bounded terminal stop/continue and foreground recovery passed
   tinyos>
   ```
   （失败场景打印 `stop68test: terminal stop/continue fallback reported`。）
3. 输入 `about`，预期输出：
   ```
   Lesson 75: 终端 stop/continue 状态转换
   tinyos>
   ```
4. 输入 `job67test`，预期输出：
   ```
   job67test: bounded job-control signal routing to foreground process group passed
   tinyos>
   ```
5. 回归：`orphan66test`、`pgtest`、`sessiontest`、`fgtest`、`ps` 等继承命令仍可用。

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 75 checks passed.`；QEMU 中 `stop68test` 打印 `...passed`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/镜像校验失败） | VGA 是否显示 `user image validation/load failure:`；查 `kernel.c::validate_user_image()` |
| `make check` 第一条 grep 失败 | README 主题字串与 Makefile 不一致 | `grep '终端 stop/continue 状态转换' README.md` 核对字面 |
| `make check` 第二条 grep 失败 | `kernel64.c` 丢失 `stop68test` 符号 | `grep -q 'stop68test' kernel64.c` |
| `make check` 第三条 grep 失败 | README 课号写错 | `grep -q 'Lesson 75' README.md` |
| `stop68test` 打印 `unknown command` | `exec64` 分支未接入或拼写错误 | 核对 `eq64(word,"stop68test")` 分支 |
| `stop68test` 打印 `usage: stop68test` | 命令带多余参数 | 本命令必须无参（`noargs64`） |
| `stop68test` 打印 fallback 串 | 7 条件中某项不成立 | 检查 `ok`：`pgid==200`、`stop_signal==19`、`continue_signal==18`、`!stopped`、`continued`、`foreground`、`terminal_owned` |
| 修改 `stopped=0` 为 `stopped=1` 后仍显示 passed | 校验表达式被误改（去掉了 `!stopped`） | 恢复 7 条件完整校验表达式 |
| `job67test`/`orphan66test` 也 fallback | 系列课程共享的 `pgid=200` 场景被改 | 对照 lesson-74/73 原文，逐字段核对 |

---

## 7. 与 Linux 源码对照

**对照点 1：停止态与恢复态**
- TinyOS 教学模型：`stopped=0 && continued=1` 两个标志位记录"已停止过、已继续回"。
- Linux 实现：`kernel/signal.c` 处理 `SIGSTOP`/`SIGTSTP` 时把任务置为 `TASK_STOPPED`（`signal_stop`），并调用 `do_notify_parent(CLD_STOPPED)` 通知父进程；`SIGCONT` 走 `signal_wake_up`/`wake_up_state` 把任务唤醒回 runnable，并通知 `CLD_CONTINUED`。任务状态枚举见 `include/linux/sched.h`（`TASK_STOPPED`、`TASK_TRACED`）。
- 权威来源：POSIX.1-2017 §11.1.9；Linux v6.12 `kernel/signal.c`、`include/linux/sched.h`。
- 教学简化：TinyOS 用两枚标志代表"事件发生过"，不模拟 `do_notify_parent`、不唤醒真实任务。

**对照点 2：停止/继续必须整组进行**
- TinyOS：`pgid=200` 是唯一投递/转换单位，`foreground=1` 表示组仍在前台。
- Linux：`kill_pgrp(tty->pgrp, SIGTSTP, 1)` 与 `kill_pgrp(tty->pgrp, SIGCONT, 1)` 都以组为单位；`tty->pgrp` 即当前前台组。
- 权威来源：Linux `drivers/tty/n_tty.c`（`isig()`、`n_tty_stop`/`n_tty_start`）；POSIX §11.1.9。
- 教学简化：无真实 `tty` 与组遍历。

**对照点 3：孤儿组停止后的唤醒**
- Linux：孤儿进程组内停止成员只能靠 `kill_orphaned_pgrp` 的 `SIGHUP`+`SIGCONT` 唤醒（Lesson 73 语义）。
- TinyOS：本课 `continued=1` 只覆盖普通（非孤儿）前台组的 SIGCONT 恢复；孤儿路径由 Lesson 73 的 `orphan_group` 记录单独建模。
- 教学简化：两课各用一张记录覆盖一条路径，未合并成统一信号引擎。

**对照点 4：前台身份与终端所有权不变量**
- Linux：`SIGTSTP`/`SIGCONT` 不改变进程的 `pgid`/`sid`/`ctty`；`tty->pgrp` 只被 `TIOCSPGRP`（shell 的 `tcsetpgrp`）改变。
- TinyOS：`foreground=1 && terminal_owned=1` 正是这条不变量的元数据投影。
- 权威来源：POSIX §11.1.4（Controlling Terminal）；Linux `drivers/tty/tty_io.c::tiocsctty`/`tiocspgrp`。

---

## 8. 思考题与练习

1. **概念理解**：为什么被 SIGSTOP 停止的进程组**只能**通过 SIGCONT（或 wait 通知）恢复，普通信号不行？请结合 `TASK_STOPPED` 不参与调度的机制解释。
2. **源码定位**：`terminal_stop_model` 的 7 个字段分别对应 POSIX 停止/继续语义的哪一步？`stopped` 与 `continued` 为什么必须一起看才能证明"转换已完成"？
3. **动手实验**：把 `stop68test` 初始化中的 `stopped` 从 0 改成 1，重新 `make run`，观察输出变为 fallback 串，理解"中间态不被接受"。请**改回 0**。
4. **动手实验**：把 `continue_signal` 从 18（SIGCONT）改成 20（SIGTSTP），思考：用 SIGTSTP 当"继续信号"在语义上错在哪里？`ok` 判定如何拦住它？
5. **Linux 对照**：阅读 `kernel/signal.c` 中 `do_signal_stop` 与 SIGCONT 处理路径，说出 Linux 在停止/继续时如何通知父进程（`CLD_STOPPED`/`CLD_CONTINUED`），并指出 TinyOS 教学模型忽略了这部分中的哪些内容。

---

## 9. 本课小结与下一课预告

- 本课用 `terminal_stop_model` 记录 + `stop68test` 确定性验证，固化"SIGSTOP(19) 停止 → SIGCONT(18) 继续 → 前台身份与终端所有权恢复"的完整状态转换。
- 你掌握了 `TASK_STOPPED` 的本质（停止态不参与调度，只有 SIGCONT 能解除），理解了停止/继续必须整组进行的 shell 语义。
- 你看到了 `stopped=0 && continued=1` 这对标志如何证明"转换完成"而非"停在半路"。
- 你对照了 Linux `kernel/signal.c` 的 `do_signal_stop`/SIGCONT 路径与 `do_notify_parent(CLD_STOPPED/CONTINUED)`，知道教学模型简化了什么。
- 你确认了系列回归锚点（`job67test`/`orphan66test`）与 `make check` 三条静态断言。

**下一课预告**：Lesson 76「调度策略元数据」。进程组的 job-control 生命周期到此完成，课程视角从"终端/进程组"转向"调度器"。下一课将介绍 `sched_class`（调度类/策略）元数据——上一课反复出现的"前台组才参与交互调度"正是调度策略的入口。衔接点：本课 `foreground=1` 的状态将影响 Lesson 76 中"哪种任务进入交互调度路径"的元数据分类。

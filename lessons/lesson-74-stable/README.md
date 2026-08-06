# Lesson 74: job-control 信号路由 — 精讲文档

> **课号**：Lesson 74（对应主线源课 Lesson 67）
> **本课主题**：job-control（作业控制）信号在前台/后台进程组之间的路由元数据
> **课程主线位置**：GUI 支线（Lesson 61–67）结课后的「进程组/session/调度元数据」阶段（Lesson 68 起恢复主线）。本课紧接 Lesson 73（孤儿进程组检测与安全 reparent），把"孤儿化 + 信号"语义链推进到"作业控制信号该发给哪个进程组"。
> **前置课程**：[`../lesson-73-stable/README.md`](../lesson-73-stable/README.md)（孤儿进程组检测与安全 reparent：session 与控制终端不变量）
> **后续课程**：[`../lesson-75-stable/README.md`](../lesson-75-stable/README.md)（终端 stop/continue 状态转换）
> **本课一句话目标**：学会用「固定元数据 + 确定性验证」模型描述"job-control 信号（如 SIGINT）被路由到前台进程组的全体成员，且目标计数=投递计数、信号未被阻塞"这一规则，并对照 POSIX/Linux 的前台/后台组信号语义。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释作业控制信号为什么必须发给"整个前台进程组"而不是单个进程、`target_count==delivered` 这条计数不变量想防什么，以及 `foreground`/`blocked` 两个位在路由决策中的作用；你能在 `tinyos>` 下用 `job67test` 做确定性验证。
- **在课程主线中的位置**：Lesson 68–80 是「进程组/session/调度元数据」阶段。Lesson 73 刚讲完"组变孤儿 → 安全 reparent → 保持 session/终端不变量"；本课在此基础上回答："终端上敲 Ctrl-C、Ctrl-Z，信号到底投给谁？"——答案是**当前前台进程组**。Lesson 75 将接着讲这些信号让组进入 stop、再被 SIGCONT 继续的状态转换。三课连起来就是完整的 job-control 生命周期。
- **前置知识清单**：
  1. 进程组 pgid、session、控制终端、前台进程组定义（Lesson 68–70）；
  2. 孤儿进程组检测与 reparent 不变量（Lesson 73）；
  3. 信号编号语义：SIGINT=2（终端中断）、SIGTSTP/SIGTTIN/SIGTTOU/SIGCONT 的作业控制家族（Lesson 34–36 的 `signal_record` 模型只到 SIGTRAP/SIGILL/SIGSEGV，本课补作业控制家族）；
  4. 本内核命令行框架 `exec64`/`token64`/`eq64`/`noargs64`（Lesson 34 起累积）。
- **本课交付**：新增固定容量的 `job_signal_model` 记录与 `job67test` 验证命令；`about`/banner 更新为「Lesson 74: job-control 信号路由」。

---

## 2. 核心概念精讲

### 2.1 作业控制信号（Job-Control Signals）

**直觉**：你在 shell 里敲 `Ctrl-C` 时，希望被打断的不是某个"幸运进程"，而是**整个前台作业**（包括管道里的每个进程）。作业控制信号就是一簇"以进程组为投递单位"的信号。

**准确定义（POSIX.1）**：作业控制信号包括 `SIGINT`（2）、`SIGQUIT`（3）、`SIGTSTP`（20/24）、`SIGTTIN`（21）、`SIGTTOU`（22）、`SIGCONT`（18）。规则：
- 终端按键产生的 `SIGINT`/`SIGQUIT`/`SIGTSTP` 投递给**当前前台进程组**（`tty->pgrp`）的全体成员；
- 后台进程组读取终端输入 → 内核向其发送 `SIGTTIN`；后台组写入终端 → 内核发送 `SIGTTOU`；
- 这些信号**默认行为**是终止或停止进程，且大多数情况下不能在后台"悄悄发生"。

### 2.2 为什么投递单位是"进程组"而不是"进程"

因为作业（job）是 shell 把一条命令行（可能含管道）打包成的进程组；只有整组一起终止/停止，shell 才能正确管理它。若只杀组长，管道里的其他成员会变成孤儿继续跑。因此投递算法是：`kill(-pgid, sig)`——负 PID 语义即"发给组"。

### 2.3 路由的三要素：目标组、投递计数、状态位

一次"路由"可以用三个命题完整描述：
1. **目标是谁**：`pgid`（本课取 200）——前台组；
2. **投递完整性**：`target_count == delivered`——需要发 2 个成员就真的发到 2 个，这是"广播到全组"的诚实性断言；
3. **前置条件**：`foreground`（目标确为前台组）为真、`blocked`（组内无人屏蔽该信号）为假。若信号被屏蔽或目标不是前台组，路由决策就会不同（这正是 Lesson 75 状态机的前置）。

```
终端按键 Ctrl-C ──► SIGINT(2)
                      │
                      ▼
          当前前台进程组 pgid=200 (2 个成员)
          foreground=1, blocked=0
                      │
                      ▼
          target_count=2 ──► delivered=2 （全组投递）
```

### 2.4 「固定元数据 + 确定性验证」教学模型

与 Lesson 73 相同的教法：`job_signal_model` 用固定数值（`{200,2,2,2,1,0}`）固化一个"理想路由场景"，再用一条复合布尔表达式把"目标正确 + 投递完整 + 前台未屏蔽"变成可重复验证的断言。没有真实信号队列、没有 `kill()` 系统调用、没有 CPL3 进程；所有结论都在 VGA 文本屏上确定性地打印。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-73） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、主循环 | **主要增量**：新增 `job_signal_model` 结构、`job_signal` 全局、`job67test()`、`exec64` 的 `job67test` 分支、`about`/banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局与栈断言 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 74 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 新增结构体与全局变量

```c
struct job_signal_model { u32 pgid,signal,target_count,delivered; u8 foreground,blocked; };
static struct job_signal_model job_signal;
```

逐行注释：
- `pgid`（u32）：信号路由的目标进程组号，取 200。在真实内核中这是"当前前台进程组"的组号，即 `tty->pgrp` 指向的那一组。
- `signal`（u32）：要路由的信号编号，取 2 = `SIGINT`。本课把"信号是什么"和"发给谁"解耦成两个字段，便于未来扩展成 SIGTSTP/SIGQUIT 等家族成员。
- `target_count`（u32）：目标组需要投递的成员数，取 2。代表"这条命令/管道里有 2 个进程"。
- `delivered`（u32）：实际完成投递的成员数，取 2。`target_count == delivered` 是核心完整性断言：路由不能"丢件"。
- `foreground`（u8）：目标组是前台组。`=1` 表示本课模拟的正是合法前台路由场景。
- `blocked`（u8）：信号在目标组内被屏蔽。`=0` 表示无阻塞，投递会生效。
- `static struct job_signal_model job_signal;`：单一全局记录，与 Lesson 73 的 `orphan_group` 同模式：一记录、一命令、一次确定性验证。

#### (b) 本课核心验证函数 `job67test`

```c
static TEXT64 void job67test(u16*c){job_signal=(struct job_signal_model){200,2,2,2,1,0};int ok=job_signal.pgid==200&&job_signal.signal==2&&job_signal.target_count==job_signal.delivered&&job_signal.foreground&&!job_signal.blocked;text64(c,"job67test: ");text64(c,ok?"bounded job-control signal routing to foreground process group passed":"job-control signal fallback reported");putc64(c,'\n');}
```

逐行注释：
- `job_signal=(struct job_signal_model){200,2,2,2,1,0};`：聚合初始化出理想场景：pgid=200、signal=2(SIGINT)、target_count=2、delivered=2、foreground=1、blocked=0。
- `int ok=job_signal.pgid==200&&job_signal.signal==2&&...`：五个条件同时成立才算通过：① 路由目标确实是 pgid 200；② 路由的确是 SIGINT（2）；③ 投递计数等于目标计数（全组广播无遗漏）；④ 目标是前台组；⑤ 组内没有屏蔽该信号。
- `text64(c,"job67test: ");`：先打印命令前缀。
- `text64(c,ok?"bounded job-control signal routing to foreground process group passed":"job-control signal fallback reported");`：成功/失败两条串逐字来自源码，是验证时的基准输出。
- `putc64(c,'\n');`：换行收尾。

为什么这样设计：真实内核的路由决策依赖 `tty->pgrp`、信号阻塞位图等多个运行态；教学模型把这些运行态压缩成 `foreground`/`blocked` 两个位 + 计数对，把"路由是否正确"翻译成一次布尔校验——**既不执行信号投递，也绝不丢失"投递完整性"这条最重要的语义**。

#### (c) 继承的孤儿组测试（回归锚点）

```c
static TEXT64 void orphan66test(u16*c){...}   /* Lesson 73 引入，本课保留 */
static TEXT64 void l65test(u16*c){...}        /* 回归冒烟，本课保留 */
```

本课 `kernel64.c` 完整保留了 `orphan66test`、`l65test` 与上一课所有命令分支（进程组、GUI、wait、信号、调度等），`kernel64.c` 从 778 行增至 781 行。这样 `job67test` 既验证新语义，又保证孤儿组/session/终端不变量没有被改坏——累积源码 + 回归测试正是"稳定快照"的构成方式。

#### (d) `exec64` 命令分派中的增量分支

```c
}else if(eq64(word,"job67test")){if(!noargs64(arg))usage64(c,"job67test");else job67test(c);}
```

逐行注释：
- 与前两课分支同构：`eq64` 匹配命令字，`noargs64` 拒绝多余参数（否则打印 `usage: job67test`），无参时调用 `job67test(c)`。
- 分支位置紧跟在 `orphan66test` 之后，反映本课在源课序列（66 → 67）中的位置。
- `about` 文案更新为：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 74: job-control 信号路由\n");}
```

- `help` 命令列表依旧保持上一课字面量未变（新命令 `job67test` 未列入），这是课程维护者的有意简化。

#### (e) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
text64(&c,"Lesson 74: job-control 信号路由\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- 首行主题变为本课主题；第二行 syscall ABI 与 "bounded reclaim metadata" 边界声明保持不变——说明本课不触碰 syscall 与回收语义，只新增作业控制信号路由元数据。

#### (f) 继承的关键辅助函数（本课复用）

- `eq64`：字符串相等判断（命令分派）；
- `noargs64`：`return !*s;` 判断无参数；
- `text64`/`putc64`：VGA 文本输出；`putc64` 对 `'\n'` 跳到下一行行首；
- `token64`：命令行切词。

这些函数均带 `TEXT64`（`section(".text64"), noinline`），保证落入 64 位续传镜像段。

### 3.3 构建管线（Makefile / linker）

本课 Makefile 与 Lesson 73 的唯一差异在 `check` 目标：

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'job-control 信号路由' README.md
	@grep -q 'job67test' kernel64.c
	@grep -q 'Lesson 74' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 74 checks passed.'
```

- `grub-file --is-x86-multiboot2`：验证外层 ELF 的 Multiboot2 header 合法性（GNU GRUB 工具）。
- 三条 `grep -q`：README 主题、`kernel64.c` 新符号 `job67test`、课号 74 各设一道静态断言，任何一项对不上 `make check` 即失败。
- `printf`：成功信息 `Multiboot2 and Lesson 74 checks passed.` 逐字来自 Makefile。
- 其余目标（`kernel64.o` → `kernel64.bin` → `boot.o` 内嵌 → `kernel.elf` → ISO）与 Lesson 73 完全一致；`kernel64.ld` 的三个 `ASSERT(...==0x1000)` 栈断言不变。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32 ──► enter_long_mode ──► kernel_main64_binary
    ├─ 元数据初始化（task/init/wait/adoption/resource/PMG/vma/...）
    ├─ banner: "Lesson 74: job-control 信号路由\nGETTICKS, ..."
    └─ for(;;) 键盘循环:
        "job67test\n" ──► exec64 ──► eq64(word,"job67test")
        ──► job67test(c)
        ──► job_signal 初始化 {200,2,2,2,1,0} + 5 条件校验
        ──► VGA: "job67test: bounded job-control signal routing to foreground process group passed"
        ──► "tinyos> "
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打印本课主题 + syscall 边界，随后 `tinyos> ` 提示符。
2. **输入**：`job67test` + 回车。键盘循环收集 8 字符进 `cmd`，`exec64` 用 `token64` 切出 `word="job67test"`、`arg=""`。
3. **分派**：`noargs64(arg)` 为真，进入 `job67test` 分支。
4. **校验**：`job_signal` 被聚合初始化；`ok` 五条件（pgid、signal、计数相等、foreground、非 blocked）全真。
5. **输出**：`job67test: bounded job-control signal routing to foreground process group passed`，随后回显 `tinyos> `。

---

## 5. 构建、运行与验证

**依赖**：同全仓库（`gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`），见 [`docs/local-validation.md`](../../docs/local-validation.md)。

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
   Lesson 74: job-control 信号路由
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `job67test`，预期输出：
   ```
   job67test: bounded job-control signal routing to foreground process group passed
   tinyos>
   ```
   （失败场景打印 `job67test: job-control signal fallback reported`。）
3. 输入 `about`，预期输出：
   ```
   Lesson 74: job-control 信号路由
   tinyos>
   ```
4. 输入 `orphan66test`，预期输出：
   ```
   orphan66test: bounded orphaned process-group detection and safe reparenting passed
   tinyos>
   ```
5. 回归：`pgtest`、`sessiontest`、`fgtest`、`reparenttest`、`jobtest`、`ps` 等继承命令仍可用。

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 74 checks passed.`；QEMU 中 `job67test` 打印 `...passed`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/镜像校验失败） | VGA 是否显示 `user image validation/load failure:`；查 `kernel.c::validate_user_image()` |
| `make check` 第一条 grep 失败 | README 主题字串与 Makefile 不一致 | `grep 'job-control 信号路由' README.md` 核对字面 |
| `make check` 第二条 grep 失败 | `kernel64.c` 丢失 `job67test` 符号 | `grep -q 'job67test' kernel64.c` |
| `make check` 第三条 grep 失败 | README 课号写错 | `grep -q 'Lesson 74' README.md` |
| `job67test` 打印 `unknown command` | `exec64` 分支未接入或拼写错误 | 核对 `eq64(word,"job67test")` 分支；help 列表不含它是已知教学简化 |
| `job67test` 打印 `usage: job67test` | 命令带了多余参数 | 本命令必须无参（`noargs64` 校验） |
| `job67test` 打印 fallback 串 | 5 条件中某项不成立（场景被改动） | 检查 `ok` 表达式各条件：`pgid==200`、`signal==2`、`target_count==delivered`、`foreground`、`!blocked` |
| `orphan66test` 也打印 fallback | 上一课的 `orphan_group` 相关代码被误改 | 对照 lesson-73 的 `orphan66test` 原文，确认本课未动该函数 |
| QEMU 无图形输出 | 显示参数被改为 `-display none` | 用 `make run` 原样启动；GUI 验收参考 `scripts/qemu-vga-check.sh` |

---

## 7. 与 Linux 源码对照

**对照点 1：作业控制信号路由目标**
- TinyOS 教学模型：`job_signal.pgid==200 && job_signal.foreground` 表达"信号应投递给前台进程组"。
- Linux 实现：终端按键产生的信号由 `drivers/tty/n_tty.c`（`n_tty_receive_char`/`isig()`）读取行规程字符，调用 `tty_send_sig_char()` → `kill_pgrp(tty->pgrp, SIGINT, 1)`，其中 `tty->pgrp` 就是当前前台进程组。用户态等价语义是 `kill(-pgid, sig)`。
- 权威来源：POSIX.1-2017 §11.1.9（Job Control Signals）与 §2.10.3；Linux v6.12 `drivers/tty/n_tty.c`、`drivers/tty/tty_io.c`。
- 教学简化：TinyOS 不解析行规程、没有真实 `tty` 设备与 `kill_pgrp`，只用 `pgid`/`foreground` 两个字段代表路由目标。

**对照点 2：投递完整性**
- TinyOS 教学模型：`target_count == delivered` 断言全组广播无遗漏。
- Linux 实现：`kill_pgrp()` 内部遍历组内每个 `struct pid` 的 task 链表逐个 `send_signal`；被屏蔽的信号会留在 pending 位图（`blocked`），实际"生效"数可能小于目标数。
- 教学简化：TinyOS 用计数相等简化"遍历 + 逐进程投递"，并把"是否被屏蔽"单独抽成 `blocked` 位——它不模拟 pending 队列。

**对照点 3：`blocked` 与 SIGINT 默认行为**
- Linux：SIGINT 默认终止进程；若进程用 `sigprocmask(SIG_BLOCK)` 屏蔽，则延迟到解除屏蔽才投递。
- TinyOS：`blocked=0` 只是路由决策的"前置条件真"，并不模拟屏蔽解除后延迟投递的机制。
- 权威来源：POSIX.1 §2.4.1（Signal Generation and Delivery）；Linux `kernel/signal.c::send_signal`。

**对照点 4（衔接上一课）**：孤儿进程组一旦形成，`SIGHUP`/`SIGCONT` 的投递也遵循"发到进程组全体成员"的同一路由原则（`kill_orphaned_pgrp`），这正是 Lesson 73 结尾预告的衔接点——本课把"按组投递"的通用规则补全了。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `Ctrl-C` 产生的 SIGINT 要发给整个前台进程组而不是组长一个进程？请用"管道中的孤儿"场景解释。
2. **源码定位**：在 `kernel64.c` 中找出 `job_signal_model` 的 6 个字段，逐一说明它们对应 Linux 路由决策的哪一步。若把 `foreground` 改成 0，`ok` 为什么变为假？
3. **动手实验**：把 `job67test` 初始化里的 `delivered` 从 2 改成 1，重新 `make run`，观察输出变为 fallback 串。请**改回 2** 以保持 stable 快照的确定性验证。
4. **动手实验**：把 `signal` 字段从 2（SIGINT）改成 18（SIGCONT）并保持其余不变，`ok` 判定会失败——思考：教学模型把"信号种类"也纳入校验，是否合理？为什么？
5. **Linux 对照**：阅读 `drivers/tty/n_tty.c` 中处理 `VINTR` 字符的代码，说明 `kill_pgrp(tty->pgrp, SIGINT, 1)` 的第三个参数 `1` 是什么语义（对照 `SEND_SIG_PRIV`），并指出 TinyOS 的 `delivered` 对应其中哪部分。

---

## 9. 本课小结与下一课预告

- 本课用 `job_signal_model` 记录 + `job67test` 确定性验证，固化"SIGINT(2) 路由到前台进程组 pgid=200 的 2 个成员、投递完整、未阻塞"的理想场景。
- 你理解了作业控制信号以进程组为投递单位的根本原因（防止管道成员变孤儿），并掌握了 `target_count==delivered` 的广播完整性断言。
- 你看到了 `foreground` 与 `blocked` 两个前置状态位如何决定路由合法与否，为下一课的状态转换做了铺垫。
- 你对照了 Linux `drivers/tty/n_tty.c::isig()` → `kill_pgrp(tty->pgrp,...)` 的实现，知道教学模型用固定数值代替了行规程与真实信号队列。
- 你确认了累积源码中的回归锚点（`orphan66test`、`l65test`）与 `make check` 三条静态断言。

**下一课预告**：Lesson 75「终端 stop/continue 状态转换」。信号路由到位后，进程组如何在 `TASK_STOPPED`（被 SIGTSTP 停止）与运行态（被 SIGCONT 继续）之间转换？这将是本课 `foreground`/`blocked` 状态位的直接延伸，把"发信号"变成"改状态"。衔接点：本课的 `job_signal` 记录正是 Lesson 75 状态机的前置条件。

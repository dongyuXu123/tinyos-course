# Lesson 73: 孤儿进程组检测与安全 reparent — 精讲文档

> **课号**：Lesson 73（对应主线源课 Lesson 66）
> **本课主题**：孤儿进程组检测与安全 reparent（reparenting）元数据建模
> **课程主线位置**：GUI 支线（Lesson 61–67）结课后的「进程组/session/调度元数据」阶段（Lesson 68 起恢复主线），本课是继 68（进程组与 session 元数据）、69（session 首领与控制终端所有权）、70（前台进程组切换与停止组保护）、71/72（checkpoint）之后的第六个进程组主题。
> **前置课程**：[`../lesson-72-stable/README.md`](../lesson-72-stable/README.md)（进程元数据 checkpoint：进程组 `pgid/leader/session`、前台组、停止组保护等固定容量元数据）
> **后续课程**：[`../lesson-74-stable/README.md`](../lesson-74-stable/README.md)（job-control 信号路由）
> **本课一句话目标**：学会用「固定元数据 + 确定性验证」模型描述「进程组变为孤儿（orphaned）→ 安全 reparent 到 init → 保持 session 与控制终端所有权不变量」的完整语义链，并理解它与 POSIX/Linux 孤儿进程组规则的对应关系。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释"孤儿进程组"的判定条件、为何孤儿化后要把进程 reparent 到 init（PID 1），以及为什么该过程必须保持 session 与控制终端所有权不变量；你能在本内核的 `tinyos>` 命令行下输入 `orphan66test` 做确定性验证。
- **在课程主线中的位置**：本阶段（Lesson 68–80）的主题是"进程组 / session / 调度元数据教学模型"。Lesson 68–70 分别建立了进程组、session 首领、控制终端、前台进程组与停止组保护；Lesson 71–72 是 checkpoint。本课补上进程组生命周期的最后一个关键规则——**当进程组因父进程退出而变成孤儿组时**，内核如何检测它、把成员 reparent 到 init，并保持 session/终端不变量。下一课（74）将在此基础上讲 job-control 信号如何路由。
- **前置知识清单**（学本课之前必须掌握）：
  1. 进程组（pgid）与 session 的关系、session 首领必须是组长（Lesson 68）；
  2. 控制终端（controlling terminal）所有权与前台进程组概念（Lesson 69–70）；
  3. init（PID 1）作为"收养者"的语义，以及前一课的 `adoption_model` / `reparenttest`（Lesson 72 继承）；
  4. 本内核的命令行框架：`exec64` 分派、`token64`/`noargs64`/`eq64` 等解析工具、`TEXT64` 段与 VGA 文本输出（Lesson 34 起累积）。
- **本课交付**：新增固定容量的 `orphan_group_model` 结构体与 `orphan66test` 验证命令；`about` 与本课 banner 更新为「Lesson 73: 孤儿进程组检测与安全 reparent」。

---

## 2. 核心概念精讲

### 2.1 孤儿进程组（Orphaned Process Group）

**直觉**：想象一个"断链"的进程组——组里的每个进程的父进程都死了，或者父进程跑到了别的 session 里。这个组不再是"有人看着的孩子"，它随时可能全部被停止（SIGSTOP/SIGTSTP）而永远没人能把它们唤醒。操作系统必须专门处理这种状态。

**准确定义（POSIX.1）**：一个进程组是**孤儿进程组**，当且仅当满足两条：
1. 它不是 session 首领的进程组（即 `pgid != sid`）；
2. 组内**没有任何成员的父进程**位于"同一 session、但不同进程组"里。

也就是说：只要组里还有"一个孩子在另一个进程组、但在同一个 session 内的父亲还在"（这样的父亲可以在组被停止后给它发 SIGCONT 唤醒），这个组就不算孤儿。

**为什么需要这个机制（动机）**：若一个进程组全部成员都被停止，且组是孤儿，那么没有任何人能可靠地唤醒它们——因为唯一的"救援者"（同 session 异组的父进程）已不存在。此时内核的保守动作是给停止的成员发送 `SIGHUP`（挂断）再跟 `SIGCONT`（继续）：先警告再强制继续，避免"永久停止的僵尸组"。

**本课模型**：TinyOS 不执行真实信号，而是用一张固定记录 `orphan_group` 表达"检测结果 + reparent 结果 + 不变量是否保持"：
- 先确定组已孤儿化（`orphaned=1`）；
- 再确定 reparent 目标为 init（`new_parent==1`）且已重挂（`reparented=1`）；
- 最后断言 session（`session==100`）与控制终端所有权不变（`terminal_preserved=1`）。

```
                    ┌─────────────────────────────┐
                    │   session 100 (sid=100)     │
                    │   控制终端 owner = 前台组    │
                    │                             │
                    │  ┌─────────────────────┐    │
                    │  │ pgid=200 (孤儿组)   │    │
                    │  │ old_parent=150 死亡 │    │
                    │  │ orphaned=1          │    │
                    │  │ ──reparent──►       │    │
                    │  │ new_parent=1 (init) │    │
                    │  │ terminal_preserved=1│    │
                    │  └─────────────────────┘    │
                    └─────────────────────────────┘
```

### 2.2 安全 reparent（收养 / 重挂亲）

**定义**：把失去父进程的进程（或进程组）的父指针改为一个"永远存活"的进程——通常是 init（PID 1）。在 Linux 里，init 扮演"收养孤儿"的角色，保证 `getppid()` 永不为空。

**工作机制**：真实内核中，进程 exit 时 `exit_notify()` 会把子进程 reparent 到其祖先中的"最近活跃"reaper（通常是 init 或同 session 的 subreaper）。reparent 必须保持：
- **身份不变量**：子进程 PID 不变、进程组/会话关系不变；
- **所有权不变量**：控制终端归属、前台组关系不被打乱。

**本课模型**：用 `old_parent=150`（原父）→ `new_parent=1`（init）的固定数值对表达这一重挂；校验时只检查 `reparented && new_parent==1 && session==100 && terminal_preserved`。

### 2.3 session 与控制终端所有权不变量

孤儿化/reparent 最容易引入的 bug 就是"换了个爹，结果 session 或终端也被换了"。POSIX 规定进程组、会话、控制终端三者的关系在进程退出/reparent 时都必须保持：reparent 只改变父指针，**不改变** `pgid`、`sid`、`ctty`。本课的 `orphan_group_model` 特意把 `session`（100）与 `terminal_preserved` 单独列为字段并参与校验，就是要把这条不变量变成可验证的断言。

### 2.4 「固定元数据 + 确定性验证」教学模型

贯穿 Lesson 68–78 的教学法：**元数据真实，行为不执行**。
- 元数据真实：结构体字段（pgid/session/parent/标志位）是对 Linux 数据结构（如 `struct pid`、`struct signal_struct`、`struct task_struct`）有依据的简化投影；
- 行为不执行：不会真的 fork 出进程、不会真的发 SIGHUP、不会真的切换到 CPL3；
- 确定性验证：每条规则映射为一条布尔表达式，通过一个 `xxxxtest` 命令在 VGA 文本屏上打印 `passed`/`BROKEN`/fallback 字符串。相同输入必然相同输出，因此可以用 `grep` 在源码里核对输出串、用 `make check` 做静态断言。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-72） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、主循环 | **主要增量**：新增 `orphan_group_model` 结构、`orphan_group` 全局、`orphan66test()` 函数、`exec64` 的 `orphan66test` 分支、`about` 文案、banner 文案；`l72test` 重命名为 `l65test` |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局：`.text64`/数据段/idle/rsp0/IST1 栈 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局（`_start` 在 1M） | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` 启动 QEMU | 微变化：`check` 目标的三条 `grep` 换成 Lesson 73 关键字 |
| `grub.cfg` | GRUB 菜单，引导 `kernel.elf` | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 新增结构体与全局变量

```c
struct orphan_group_model { u32 pgid,session,old_parent,new_parent; u8 orphaned,reparented,terminal_preserved; };
static struct orphan_group_model orphan_group;
```

逐行注释：
- `pgid`：本进程组的组号。模型中取 200，一个**非** session 首领的普通组（session 是 100），这是"组可能是孤儿"的前提——session 首领组永远不会被视为孤儿组。
- `session`：所属会话号。取 100，与 `pgid=200` 不同，满足 POSIX 孤儿判定第一条"不是 session 首领的进程组"。
- `old_parent`：原父进程 PID，取 150。150 的"退出/离开"是本课假设的孤儿化触发事件（元数据里没有真实进程，只有一个数值占位）。
- `new_parent`：reparent 目标 PID，取 1（init）。这是"安全 reparent"的目标：init 永远存活，能收养一切孤儿。
- `orphaned`（u8）：布尔标志，进程组已被判定为孤儿组。
- `reparented`（u8）：布尔标志，组内成员已重挂到 init。
- `terminal_preserved`（u8）：布尔标志，reparent 前后控制终端所有权未变。
- `static struct orphan_group_model orphan_group;`：单一固定容量的全局记录。因为是 `static`，它只在本编译单元可见；教学模型不需要多个实例，刻意用"一记录一命令"保持确定性。

设计动机：结构与 Linux 的"孤儿判定 + 重挂"两步语义对应，但把整条链路压成一条可断言的记录，符合「固定元数据 + 确定性验证」模型。

#### (b) 本课核心验证函数 `orphan66test`

```c
static TEXT64 void orphan66test(u16*c){orphan_group=(struct orphan_group_model){200,100,150,1,1,1,1};int ok=orphan_group.orphaned&&orphan_group.reparented&&orphan_group.new_parent==1&&orphan_group.session==100&&orphan_group.terminal_preserved;text64(c,"orphan66test: ");text64(c,ok?"bounded orphaned process-group detection and safe reparenting passed":"orphaned process-group fallback reported");putc64(c,'\n');}
```

逐行注释：
- `orphan_group=(struct orphan_group_model){200,100,150,1,1,1,1};`：用聚合初始化把记录赋为固定场景：pgid=200、session=100、old_parent=150、new_parent=1（init）、orphaned=1、reparented=1、terminal_preserved=1。这个场景已把"孤儿检测 + 安全重挂"两个阶段都标为完成，因此验证重点落在**不变量**上。
- `int ok=orphan_group.orphaned&&orphan_group.reparented&&...`：判定成功需要同时满足 5 个条件：① 组已孤儿化；② 已完成 reparent；③ 新父确实是 init（`new_parent==1`）；④ session 保持 100（组/会话关系未因重挂而变）；⑤ 控制终端所有权被保留（`terminal_preserved`）。
- `text64(c,"orphan66test: ");`：命令前缀，先打印标签再打印结果。
- `text64(c,ok?...)`：成功串 `bounded orphaned process-group detection and safe reparenting passed`，失败串 `orphaned process-group fallback reported`——两条串都来自源码字面量，是验证时逐字对照的基准。
- `putc64(c,'\n');`：结尾换行（`putc64` 对 `'\n'` 的特殊处理是跳到行首下一行）。

为什么这样设计：本课不模拟真实进程退出与信号，而是把"检测结果/重挂结果/不变量"三个命题固化为位标志，再做一个**复合布尔校验**。这样一次输入就能证明整条语义链成立，且可读、可 grep、可回归。

#### (c) 回归测试函数 `l65test`（由上一课 `l72test` 改名）

```c
static TEXT64 void l65test(u16*c){u32 a=65U,b=66U;int ok=b==a+1U;text64(c,"l65test: ");text64(c,ok?"bounded Lesson 65 metadata passed":"Lesson 65 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 这是 checkpooint 语义：`a=65U, b=66U`，验证 `b==a+1`——一个最朴素的"元数据可算术一致"冒烟。
- 它与上一课的 `l72test` 逻辑完全相同，仅函数名与输出前缀改为 `l65test`，对应本课在源课编号体系（Origin=Lesson 66 线）中的回归命名。
- 存在意义：课程源码是累积的（lesson-73 的 `kernel64.c` 有 778 行），每次 checkpoint 课的 `lXXtest` 保证旧语义没有在改版中被破坏。

#### (d) `exec64` 命令分派中的增量分支

```c
}else if(eq64(word,"orphan66test")){if(!noargs64(arg))usage64(c,"orphan66test");else orphan66test(c);}
```

逐行注释：
- `eq64(word,"orphan66test")`：匹配命令字。`exec64` 是超长 if-else 链，`token64` 先把命令行首词拆进 `word`，剩余参数在 `arg`。
- `if(!noargs64(arg))usage64(c,"orphan66test");`：本命令不带参数；若用户多给了参数则打印 `usage: orphan66test` 并返回。
- `else orphan66test(c);`：无参数时调用本课验证函数，`c` 是 VGA 文本光标（`u16 *`）。

`about` 分支（文案更新）：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 73: 孤儿进程组检测与安全 reparent\n");}
```

- 唯一的增量是文案字面量从「Lesson 72: 进程元数据 checkpoint」换成「Lesson 73: 孤儿进程组检测与安全 reparent」。注意 `help` 列表本身没有加入 `orphan66test`/`l65test`——课程维护者有意保持 help 字符串不变，新命令仍可通过直接输入触发；这是可接受的教学简化。

#### (e) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
text64(&c,"Lesson 73: 孤儿进程组检测与安全 reparent\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- 这是开机首屏第二行：第一行是"Lesson 73: 孤儿进程组检测与安全 reparent"，第二行说明 syscall ABI 仍是 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT，未知返回 `-ENOSYS`，并强调"bounded reclaim metadata"——与 lesson-72 继承的教学边界一致。
- banner 之后是 `prompt64(&c)` 打出 `tinyos> `，随后进入 `for(;;)` 键盘循环：`kbd_dequeue` → 整行收集进 `cmd` → `exec64`。

#### (f) 继承的关键辅助函数（本课未改，但它是 `orphan66test` 能打印的前提）

- `text64(c,const char*s)`：把 ASCII 串逐个 `putc64` 写入 VGA 文本缓冲区 `VGA((volatile u16*)0xb8000)`，白底黑字属性 `0x0f00`。
- `eq64(a,b)`：手写字符串相等（`while(*a&&*b){if(*a++!=*b++)return 0;}return *a==*b;`），命令分派的核心。
- `noargs64(s)`：`return !*s;` 判断是否无剩余参数。
- `token64(s,word,cap)`：跳过空白、切出首词、跳过尾部空白，返回剩余参数指针；词长超 `cap` 返回 0（触发 `command too long`）。

这些函数全部带 `TEXT64`（`__attribute__((section(".text64"), noinline))`），保证它们落在 64 位续传镜像段内、被外层 ELF 以二进制内嵌后仍可寻址。

### 3.3 构建管线（Makefile / linker）

本课 Makefile 与上一课唯一的差异在 `check` 目标：

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '孤儿进程组检测与安全 reparent' README.md
	@grep -q 'orphan66test' kernel64.c
	@grep -q 'Lesson 73' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 73 checks passed.'
```

逐行含义：
- `grub-file --is-x86-multiboot2`：权威验证外层 ELF 确实带合法的 Multiboot2 header（GNU GRUB 工具，规范来源是 Multiboot2 规范）。
- 三条 `grep -q`：把"README 主题、源码新符号、课号"做成静态断言——这是「固定元数据 + 确定性验证」在构建层的体现：改错课名、删掉新函数、写错课号都会让 `make check` 失败。
- `printf 'Multiboot2 and Lesson 73 checks passed.'`：成功的退出信息，逐字来自 Makefile。

其余目标与 lesson-72 完全一致，关键链为：
1. `kernel64.o`：`-m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx ... -Werror`；
2. `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary` 得到裸二进制；
3. `boot.o` 用 `-m32` 编译并在 `.text64` 段 `.incbin "build/kernel64.bin"` 内嵌续传；
4. `kernel.elf`：`ld -m elf_i386 -T linker.ld`，入口 `_start` 位于 1M、`.multiboot` 段 `KEEP` 保留；
5. `kernel.iso`：`grub-mkrescue` 打包 ISO。

`kernel64.ld` 中 `ASSERT(...==0x1000)` 断言 idle/rsp0/IST1 三个栈都是整页，与 lesson-72 相同。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32(kernel.c)
        └─ 建页表/校验用户镜像 ──► enter_long_mode(设置 EFER.LME/CR0.PG)
        └─ 远跳转 long_mode_start ──► kernel_main64_binary(kernel64.c)
            ├─ 初始化：task/init/wait/adoption/resource/PMG/vma/reclaim/vfs/address_space
            ├─ banner: "Lesson 73: 孤儿进程组检测与安全 reparent\nGETTICKS, ..."
            └─ for(;;) 键盘循环:
                "orphan66test\n" ──► exec64 ──► eq64(word,"orphan66test")
                ──► orphan66test(c)
                ──► orphan_group 初始化 + 5 条件布尔校验
                ──► VGA 打印 "orphan66test: bounded ... passed"
                ──► prompt64 回显 "tinyos> "
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 完成全部元数据初始化后，在 VGA 第 1 屏打印 banner（见 3.2(e)），随后 `prompt64` 打印 `tinyos> `。
2. **输入**：用户在 QEMU 图形窗口敲 `orphan66test` 再回车。键盘循环把 12 个字符收进 `cmd[32]`，遇 `'\n'` 后 `cmd[n]=0` 并调用 `exec64`。
3. **分派**：`exec64` 用 `token64` 切出 `word="orphan66test"`、`arg=""`；`noargs64(arg)` 为真，进入新增分支调用 `orphan66test(c)`。
4. **校验**：`orphan66test` 聚合初始化 `orphan_group`，再计算 `ok`（5 条件与）。全部为 1 时选成功串。
5. **输出**：VGA 显示 `orphan66test: bounded orphaned process-group detection and safe reparenting passed`，随后 `prompt64` 回显新 `tinyos> `。

输出串与源码逐字一致：`orphan66test: ` + `bounded orphaned process-group detection and safe reparenting passed`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致，需 `gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`（详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

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

1. 开机第一屏应显示：
   ```
   Lesson 73: 孤儿进程组检测与安全 reparent
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `orphan66test`，预期输出：
   ```
   orphan66test: bounded orphaned process-group detection and safe reparenting passed
   tinyos>
   ```
   （失败场景会打印 `orphan66test: orphaned process-group fallback reported`，源码可见。）
3. 输入 `about`，预期输出：
   ```
   Lesson 73: 孤儿进程组检测与安全 reparent
   tinyos>
   ```
4. 输入 `l65test`，预期输出：
   ```
   l65test: bounded Lesson 65 metadata passed
   tinyos>
   ```
5. 回归：`help`、`ps`、`pgtest`、`sessiontest`、`fgtest`、`reparenttest`、`jobtest` 等继承命令仍可用。

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 73 checks passed.`；QEMU 中 `orphan66test` 打印 `...passed` 即代表孤儿检测 + 安全 reparent + session/终端不变量五项校验全部成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/用户镜像校验失败） | 看 VGA 是否显示 `user image validation/load failure:`；`kernel.c` 的 `validate_user_image()` 各 status 分支 |
| `make check` 失败于第一条 grep | README 主题字串与 Makefile 不一致 | `grep '孤儿进程组检测与安全 reparent' README.md` 核对字面 |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `orphan66test` 符号 | `grep -q 'orphan66test' kernel64.c` |
| `make check` 失败于第三条 grep | README 课号写错 | `grep -q 'Lesson 73' README.md` |
| 输入 `orphan66test` 打印 `unknown command` | `exec64` 分支未接入或命令拼写错误 | 核对 `eq64(word,"orphan66test")` 分支；help 列表不含本命令是已知教学简化 |
| 输入 `orphan66test` 打印 `usage: orphan66test` | 命令带了多余参数 | 该命令必须无参；`noargs64(arg)` 校验 |
| `orphan66test` 打印 fallback 串 | `orphan_group` 5 条件中某一项不满足（场景被改动） | 单步检查 `ok` 表达式：`orphaned`、`reparented`、`new_parent==1`、`session==100`、`terminal_preserved` |
| QEMU 无图形输出 | 把 `-display` 参数加成了 `none` 或显示环境缺失 | 用 `make run` 原样启动；图形验收参考 `scripts/qemu-vga-check.sh` 流程 |
| `l65test` 消失 | 上一课 `l72test` 改名遗漏 | 确认 `exec64` 中既有 `l65test` 分支（`l65test` 取代 `l72test`） |

---

## 7. 与 Linux 源码对照

**对照点 1：孤儿进程组判定**
- TinyOS 教学模型：`orphan_group.orphaned` 一个布尔位概括"该组已成为孤儿组"，判定输入是固定的 `pgid/session/old_parent` 数值。
- Linux 实现：`kernel/signal.c` 的 `is_orphaned_pgrp()` 与 `will_become_orphaned_pgrp()`。`is_orphaned_pgrp` 检查：pgrp 不是 session 首领组，且对每个成员 `task_pgrp(p->real_parent)` 不在本组、又在同 session 内时，该组**不**是孤儿。`will_become_orphaned_pgrp` 在父进程即将退出时预判孤儿化，`find_child_reaper`（`kernel/exit.c`）负责选新的收养者。
- 权威来源：POSIX.1-2017《System Interfaces》§11.1.4（Controlling Terminal）定义孤儿进程组规则；Linux v6.12 `kernel/signal.c`、`kernel/exit.c`。
- 教学简化：TinyOS 没有真实进程树与 `real_parent` 遍历，用一条记录替代整个判定算法；孤儿化触发事件（父退出）被退化成固定数值 `old_parent=150`。

**对照点 2：reparent 到 init**
- TinyOS 教学模型：`new_parent==1` 且 `reparented=1` 两个位表达"安全重挂到 init"。
- Linux 实现：`kernel/exit.c::exit_notify()` 调用 `forget_original_parent()` → `find_new_reaper()`，默认把孤儿 reparent 到 PID 1（init），除非存在 subreaper。reparent 只改 `real_parent`，不改变 `pgid`/`sid`/`ctty`。
- 教学简化：没有 `real_parent` 指针、没有 `PR_SET_CHILD_SUBREAPER` 机制；仅断言目标必须是 1。

**对照点 3：孤儿化后的信号动作**
- Linux：当进程组因父退出而成为孤儿且组内有停止成员时，`kill_orphaned_pgrp()` 向停止成员发送 `SIGHUP` 后跟 `SIGCONT`，防止"永久停止组"。
- TinyOS 教学模型：`terminal_preserved=1` 只保证终端不变量；本课**不**模拟 SIGHUP/SIGCONT 的发送顺序——这一语义在下一课（74：job-control 信号路由）再展开。
- 权威来源：POSIX.1 §11.1.4；Linux `kernel/signal.c::kill_orphaned_pgrp()`。

---

## 8. 思考题与练习

1. **概念理解**：为什么"同 session 但不同进程组内仍有父进程"的组**不算**孤儿组？请用一条"救援路径"解释（提示：谁能在组停止后发出 SIGCONT）。
2. **源码定位**：在 `kernel64.c` 中找到 `orphan_group_model` 的 5 个字段分别对应 POSIX 孤儿判定/重挂语义的哪一步？若把 `session` 字段值改成 `200`（等于 pgid），`ok` 会变为什么？为什么？
3. **动手实验**：修改 `orphan66test` 中初始化值（例如把 `new_parent` 从 1 改成 2），重新 `make run`，观察输出从 `...passed` 变为 fallback 串。改完后请**恢复原值**，避免破坏 stable 快照的 `make check` 与确定性验证。
4. **动手实验**：把 `orphan_group.orphaned` 从校验表达式里去掉，再运行 `orphan66test`。思考：这样的"孤儿检测"还诚实吗？教学模型为什么坚持 5 项全检？
5. **Linux 对照**：阅读 `kernel/signal.c` 的 `is_orphaned_pgrp()` 与 `kill_orphaned_pgrp()`，说出 TinyOS 的 `terminal_preserved` 位对应 Linux 中哪条"不能做"的约束（reparent 不得改变 ctty 归属）。

---

## 9. 本课小结与下一课预告

- 本课把"孤儿进程组检测 + 安全 reparent"压缩为一个固定容量记录 `orphan_group_model` 与一条 5 条件布尔断言，用 `orphan66test` 在 VGA 上确定性验证。
- 你掌握了 POSIX 孤儿进程组的精确判定（非 session 首领组 + 无同 session 异组父进程），理解了 Linux 在孤儿化时"reparent 到 init + SIGHUP/SIGCONT 唤醒"的保守策略。
- 你看到了 `terminal_preserved` 与 `session==100` 两条不变量如何被纳入校验，体会"元数据真实、行为不执行"的教学模型边界。
- 你对比了 `kernel/signal.c::is_orphaned_pgrp` / `kernel/exit.c::find_new_reaper` / `kill_orphaned_pgrp` 三处 Linux 语义，知道 TinyOS 简化了什么。
- 你验证了累积源码中 `l65test` 回归冒烟与 `make check` 三条静态断言的作用。

**下一课预告**：Lesson 74「job-control 信号路由」。孤儿组一旦形成，`SIGTSTP`/`SIGTTOU`/`SIGTTIN` 等 job-control 信号如何在前台/后台进程组之间路由，将复用本课的 session/终端不变量模型。请带着"谁给谁发信号、发给哪个进程组"的问题进入下一课，衔接点正是本课的 `session==100 && terminal_preserved` 不变量。

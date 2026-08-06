# Lesson 110: 服务启动与失败回滚 — 精讲文档

> **课号**：Lesson 110（检查点课 / Checkpoint Lesson，可执行课）
> **主题**：服务启动与失败回滚（Service Startup and Failure Rollback）
> **课程主线位置**：第 5 阶段「VFS/设备/epoll/服务管理检查点」主线中段（100–114）。
> 前课 Lesson 109（服务依赖拓扑）确立了 init→shell→job 的依赖链；本课把「启动必须成功
> 才就绪、启动失败必须安全回滚」作为检查点主题；后续 Lesson 111（守护进程生命周期）、
> 112（综合验证）继续此主线，113 起转入并发。
> **前置课程**：[`lesson-109-stable/README.md`](../lesson-109-stable/README.md)
> **后续课程**：[`lesson-111-stable/README.md`](../lesson-111-stable/README.md)
> **一句话目标**：理解 `l110test` 检查点如何用 `lesson_103_model` 重申服务启动/回滚
> 不变量，并弄清源码里 `resource_teardown`、`wait_block`、`lifecycle`、`shell_exec_path`
> 等继承机制如何表达「启动失败时按序回滚、不产生泄漏或双重释放」。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能说清「服务启动与失败回滚」在 TinyOS 元数据层的样子——启动
命令先做前置校验（路径/能力/状态），失败路径不留下半初始化资源；回滚按依赖逆序释放资源，
并有 double-release 防护。`l110test` 输出
`bounded VFS, devices, epoll, and service management checkpoint passed` 即通过。

- **在课程主线中的位置**：检查点主线第 2 站（109→110→111→112）。每站只换主题串与检查点
  模型号，机制全部继承；本课主题聚焦「启动成功性 + 失败回滚」两条不变量。
- **责任边界**：本课**不新增**启动/回滚机制（它们来自 108 课的 resource teardown、
  lifecycle、wait_block 等）。新增的只是 `struct lesson_103_model` 与 `l110test`。
- **前置知识清单**：① `struct resource_ledger` 与 `resource_teardown`（108 课，回滚的
  核心）；② `wait_model`/`wait_block_model` 的启动失败重试与 wake 语义；③
  `fork_exec_lifecycle` 的 fork→exec→exit→wait/reap 状态机；④
  `shell_exec_path` 的前置校验（路径 inode、argc/envc 上限）。
- **本课交付**：命令 `l110test`（新增）与 `l102test`（由 109 课 `l109test` 更名）；
  `about`/横幅显示 `Lesson 110: 服务启动与失败回滚`；
  `make check` 输出 `Multiboot2 and Lesson 110 checks passed.`。

---

## 2. 核心概念精讲

### 2.1 概念一：启动成功性（Startup Success）

定义：一个服务只有在「前置依赖就绪 + 自身初始化完整」后才置 `ready`；任何一环失败，服务
保持「未就绪」状态，绝不能出现「进程活着但服务没就绪」的中间态。

为什么需要：服务依赖拓扑（109 课）告诉我们谁先谁后，但没回答「后一个是否真的能用」。
如果 init 声称 ready 而 shell 起不来，job 就会挂在已死的依赖上。TinyOS 用元数据保证
「ready 是推导出来的，不是口号」。

工作机制（源码证据）：
- `init_model_start()` 里 init 的 `ready=1` 与 `started=1` 同时成立；
- `shell_exec_path("/bin/sh",2,1)` 先 `ramfs_lookup` 找 inode，再 `fd_open_model`
  开文件描述符；任一失败立即 `return 0`，不推进 `shell_runtime.commands/execs`；
- `shelltest` 断言 `init_model.ready` 为真且 `a&&b&&d`（路径、fd、pipe 全通）才输出
  `init/shell/file/process/pipe coordination passed`。

### 2.2 概念二：失败回滚（Failure Rollback）

定义：启动过程中若后续步骤失败，前面已获得的资源必须按依赖逆序释放，回到「未启动」的
一致状态；同一资源不得被释放两次。

为什么需要：真实内核里 `module_init` 失败要卸载已注册子模块、`devm_` 框架自动释放已申请
资源；教学模型在元数据层复刻同一纪律。回滚的「逆序」对应依赖拓扑：先 fd/pipe 再 signal/
timer，最后 address_space。

工作机制（源码证据）：
- `resource_ledger` 记录 `address_space/fd_refs/pipe_refs/signal_refs/timer_refs/
  deferred_refs` 六类引用；
- `resource_teardown()`：先检查 `zombie && !teardown_done`（未成僵尸或已回滚过则拒绝），
  然后把六类引用全部清零、`releases=6`、`teardown_done=1`；
- `teardowntest` 调用两次 `resource_teardown()`，断言第二次返回 0（`b=!resource_teardown()`）
  且 `releases==6`——这就是 double-release 防护的证明。

### 2.3 概念三：启动失败的重试与唤醒（wait_block 模型）

定义：阻塞式等待（blocked wait）在子服务未就绪时挂起调用者；一旦退出/失败事件发生，
`wait_block_exit()` 唤醒一个等待者；`nohang`（WNOHANG）模式则不阻塞直接返回当前状态。

为什么需要：启动另一个服务时父服务可能要等它结束或失败；失败后要能继续执行（重试或
清理）。`wait_block_wait(0)` 阻塞、`wait_block_wait(1)` 不阻塞，是
`waitid(WNOHANG)`/`waitpid` 的教学模型。

工作机制（源码逐字引用见 §3.3）：`wait_block_exit()` 在子进程 RUNNING 时先
`wait_model_exit(17)` 再唤醒 blocked 等待者并 `wakes++`；随后 `wait_block_wait(0)`
能返回真，`wait_model_reap()` 完成回收。

### 2.4 概念四：`lesson_103_model` 检查点模型

```c
struct lesson_103_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_103_model lesson_103_state;
```

与 102 模型同构，编号前进一位；`l110test` 注入 `{103U,104U,105U,106U,1,1,1,1}` 并断言
`valid&&active&&ready&&accounted&&b==a+1U`。四个状态位中 `ready` 直接对应本课主题
「启动成功性」，`accounted` 对应「失败回滚的记账完整性」。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-109） |
|---|---|---|
| `boot.S` | Multiboot2 header + 长模式引导 | 未变化 |
| `kernel.c` | 32 位阶段 | 未变化 |
| `kernel64.c` | 64 位内核主体 | `l109test`→`l102test` 更名；新增 `struct lesson_103_model`/`lesson_103_state`/`l110test`；exec64 增 `l110test` 分支；`about` 与横幅改「Lesson 110: 服务启动与失败回滚」 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 + check | check grep 更新为 `服务启动与失败回滚`/`l110test`/`Lesson 110` |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 本课增量（源码逐字）

更名命令 `l102test`（即 109 课的 `l109test`）：

```c
static TEXT64 void l102test(u16*c){lesson_102_state=(struct lesson_102_model){102U,103U,104U,105U,1,1,1,1};int ok=lesson_102_state.valid&&lesson_102_state.active&&lesson_102_state.ready&&lesson_102_state.accounted&&lesson_102_state.b==lesson_102_state.a+1U;text64(c,"l102test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 102 fallback reported");putc64(c,'\n');}
```

- 函数名改为 `l102test`（= 模型号 102），逻辑逐字未变：仍写 `lesson_102_state`、仍断言
  五个条件、输出前缀 `l102test: `；
- 语义不变性是检查点课最核心的验收点——更名不得破坏任何断言。

新增结构与命令：

```c
struct lesson_103_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_103_model lesson_103_state;
static TEXT64 void l110test(u16*c){lesson_103_state=(struct lesson_103_model){103U,104U,105U,106U,1,1,1,1};int ok=lesson_103_state.valid&&lesson_103_state.active&&lesson_103_state.ready&&lesson_103_state.accounted&&lesson_103_state.b==lesson_103_state.a+1U;text64(c,"l110test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 103 fallback reported");putc64(c,'\n');}
```

逐行拆解：

1. 结构 `lesson_103_model`：与 101/102 完全同构——4 个 u32 计数 + 4 个 u8 状态位；
2. 全局 `lesson_103_state`：默认全零，仅 `l110test` 注入；
3. `l110test` 赋值 `{103U,104U,105U,106U,1,1,1,1}`：`a=103,b=104,c=105,d=106`，四状态位
   全 1，表示「服务已启动且资源已记账」；
4. 断言 `ok=`：`valid/active/ready/accounted` 全真且 `b==a+1U`；
5. 输出：`l110test: ` + 成功串
   `bounded VFS, devices, epoll, and service management checkpoint passed`，失败串
   `Lesson 103 fallback reported`——失败时安全回退，不崩溃。

exec64 新增分支（源码逐字）：

```c
else if(eq64(word,"l102test")){if(!noargs64(arg))usage64(c,"l102test");else l102test(c);}else if(eq64(word,"l110test")){if(!noargs64(arg))usage64(c,"l110test");else l110test(c);}
```

- `l102test` 分支由 `l109test` 改名，`l110test` 分支新增，都执行 `noargs64` 校验后调用；
- `help` 串未追加这两个命令（既有小瑕疵，见 §6）。

主题横幅与 about（源码逐字）：

```c
text64(&c,"Lesson 110: 服务启动与失败回滚\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

```c
text64(c,"Lesson 110: 服务启动与失败回滚\n");
```

### 3.3 继承机制精讲——启动/回滚的源码基础

回滚核心 `resource_ledger` 与 `resource_teardown`（源码逐字）：

```c
struct resource_ledger { u64 address_space,fd_refs,pipe_refs,signal_refs,timer_refs,deferred_refs,releases,double_releases; u8 zombie,teardown_done; };
static struct resource_ledger resource_ledger;
static TEXT64 int resource_teardown(void){if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;resource_ledger.address_space=0;resource_ledger.fd_refs=0;resource_ledger.pipe_refs=0;resource_ledger.signal_refs=0;resource_ledger.timer_refs=0;resource_ledger.deferred_refs=0;resource_ledger.releases=6;resource_ledger.teardown_done=1;return 1;}
```

- 六类引用字段对应服务持有的一组资源；`releases` 记录已释放次数，`double_releases`
  预留记录双重释放（本课代码中 teardown 只置 6 次 releases，不递增 double_releases，
  用「第二次调用返回 0」来防双重释放）；
- 前置守卫 `if(!zombie||teardown_done)return 0`：服务未死或已回滚过，拒绝再次回滚；
- 回滚动作按 address_space→fd→pipe→signal→timer→deferred 的顺序清零，最后
  `teardown_done=1` 锁住状态；
- `resourceinfo` 命令输出七项：`resources as/fd/pipe/signal/timer/work/releases:`；
  `teardowntest` 用 `a=resource_teardown(),b=!resource_teardown()` 验证「一次性回滚」，
  输出 `zombie retention, ordered resource release, and double-reap guard passed`。

启动失败重试 `wait_block_model`（源码逐字）：

```c
static TEXT64 int wait_block_wait(u8 nohang){wait_block_model.ready_checks++;if(nohang){wait_block_model.nohang_calls++;return wait_model.state==WAIT_ZOMBIE;}if(wait_model.state!=WAIT_ZOMBIE){wait_block_model.blocked=1;wait_block_model.blocks++;return 0;}wait_block_model.woken=1;return 1;}
static TEXT64 void wait_block_exit(void){if(wait_model.state==WAIT_RUNNING){wait_model_exit(17);if(wait_block_model.blocked){wait_block_model.blocked=0;wait_block_model.woken=1;wait_block_model.wakes++;}}}
```

- `wait_block_wait(0)`：服务未就绪（非 ZOMBIE）则置 `blocked=1` 并返回 0（阻塞）；
- `wait_block_wait(1)`：WNOHANG 模式不阻塞，`nohang_calls++`，直接返回当前就绪状态；
- `wait_block_exit()`：子服务退出时先 `wait_model_exit(17)` 记退出码，若确有阻塞者则
  唤醒一个并 `wakes++`——这是「启动失败后唤醒等待者继续」的元数据表达；
- `waitblocktest` 完整走「阻塞→退出唤醒→重试→reap」，输出
  `blocked wait, exit wake-one, status retry, and reap passed`。

启动前置校验 `shell_exec_path`（源码逐字）：

```c
static TEXT64 int shell_exec_path(const char *path,u32 argc,u32 envc){int inode=ramfs_lookup(path);int fd;u64 image_hash=0x5348454c4c494d47ULL;if(inode<0||argc>4U||envc>4U)return 0;fd=fd_open_model((u32)inode,0);if(fd<0)return 0;shell_runtime.commands++;shell_runtime.execs++;shell_runtime.argv_words+=argc;shell_runtime.env_words+=envc;shell_runtime.pipe_links++;shell_runtime.signal_links++;shell_runtime.timer_links++;shell_runtime.deferred_links++;if(image_hash!=0x5348454c4c494d47ULL)return 0;fd_close_model((u32)fd);shell_runtime.exits++;return 1;}
```

- 前置校验：`ramfs_lookup(path)<0`（路径不存在）或 `argc>4`/`envc>4`（超上限）直接
  `return 0`——启动失败路径在分配资源**之前**就截断；
- 第二道校验：`fd_open_model<0`（fd 表满或 inode 非法）直接 `return 0`；
- 只有全部通过才累加 `commands/execs/links` 计数并 `fd_close_model` 释放 fd、`exits++`；
- `if(image_hash!=...)return 0` 是恒假守卫（常量与自己比较），用来显式标注
  「镜像校验点」，教学痕迹，不影响控制流。

### 3.4 构建管线

与 lesson-109 完全一致：`kernel64.o`（`-m64 -ffreestanding -fpie -mno-red-zone
-mno-sse* -Werror`）→ `kernel64.elf`（`ld -m elf_x86_64 -T kernel64.ld`）→
`objcopy -O binary` → boot.S `.incbin` → 外层 `kernel.elf` → `grub-mkrescue`。
`check` 目标：`grub-file --is-x86-multiboot2` + grep `服务启动与失败回滚`/`l110test`/
`Lesson 110`，通过打印 `Multiboot2 and Lesson 110 checks passed.`。
`run`：`qemu-system-x86_64 -accel tcg -boot order=d -cdrom build/kernel.iso -serial
stdio -no-reboot -no-shutdown`。

### 3.5 主控制流

```text
GRUB → kernel_main32 → 长模式 → kernel_main64_binary
  ├─ module_init_model(); init_model_start(); wait_model_start();
  │  adoption_start(); resource_start(); pmm_init(h); vma_init();
  │  reclaim_init(); vfs_init(); address_space_init(...)
  ├─ ... framebuffer_init(h); install_idt(h); pit_init(); pic_init()
  ├─ 横幅 "Lesson 110: 服务启动与失败回滚\nGETTICKS, ... bounded reclaim metadata\n"
  └─ 键盘循环 → exec64
        ├─ l102test → 复验 lesson_102 检查点（更名命令）
        ├─ l110test → 复验 lesson_103 检查点（本课新增）
        └─ teardowntest / waitblocktest / shellrun / jobtest 等继承命令
```

---

## 4. 数据流与运行逻辑

```text
输入 "l110test" → exec64 → l110test(c)
  → lesson_103_state = {103,104,105,106, 1,1,1,1}
  → ok = valid(1)&&active(1)&&ready(1)&&accounted(1)&&b(104)==a(103)+1
  → "l110test: bounded VFS, devices, epoll, and service management checkpoint passed"

服务启动回滚演示链：
  resource_start() → resource_ledger{zombie=1}
  teardowntest → resource_teardown() 一次成功（releases=6, teardown_done=1）
              → 二次调用返回 0（double-release 防护）
  waitblocktest → wait_block_wait(0) 阻塞 → wait_block_exit() 唤醒
                → wait_block_wait(0) 成功 → wait_model_reap() 回收
```

`initinfo`/`shellrun`/`jobtest` 的输出串构成服务启动证据链，见 §5.3 与旧 README 保留的
验证记录。

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

`make check` 输出：`Multiboot2 and Lesson 110 checks passed.`（README 必须含
`服务启动与失败回滚` 与 `Lesson 110`，kernel64.c 必须含 `l110test`）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 110: 服务启动与失败回滚\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字抄录）：

```bash
l110test
```

预期：

```text
l110test: bounded VFS, devices, epoll, and service management checkpoint passed
```

```bash
about
```

预期：`Lesson 110: 服务启动与失败回滚`

```bash
teardowntest
```

预期：`teardowntest: zombie retention, ordered resource release, and double-reap guard passed`

```bash
waitblocktest
```

预期：`waitblocktest: blocked wait, exit wake-one, status retry, and reap passed`

继承回归：`l102test`、`shellrun`、`jobtest`、`forkexecwaittest`、`reparenttest`、
`softirqtest`、`lockatomictest` 行为不变。

### 5.4 课程实测记录（稳定快照）

旧 README 声明「Commands: `l103test`」——**命令名以源码为准勘误**：本课源码可执行命令是
`l102test`（更名）与 `l110test`（新增）；`l103test` 是模型编号（lesson_103），不是命令。
`make check` 复验输出 `Multiboot2 and Lesson 110 checks passed.`，`l110test` 显示
passed，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l110test` 输出 `Lesson 103 fallback reported` | `lesson_103_state` 未注入或断言不成立 | 核对 `l110test` 赋值 `{103U,104U,105U,106U,1,1,1,1}` 与 `ok` 五条件 |
| `teardowntest` 输出 `BROKEN` | `resource_teardown` 前置守卫被破坏（zombie 未置位或 teardown_done 提前为真） | 检查 `resource_start()` 是否置 `zombie=1`；`teardowntest` 是否先 `resource_start()` |
| `waitblocktest` 输出 `BROKEN` | `wait_block_exit` 在 blocked 前被调用，或 `wait_model_exit(17)` 未先执行 | 检查调用顺序：先 `wait_block_wait(0)` 再 `wait_block_exit()` 再 `wait_block_wait(0)` |
| `make check` 报错 | README 缺 `服务启动与失败回滚`/`Lesson 110` 或 kernel64.c 缺 `l110test` | 对照 Makefile 三条 grep |
| `about` 显示旧课号 | 主题串未更新 | grep `Lesson 110: 服务启动与失败回滚` kernel64.c |
| help 列表找不到 `l110test` | 已知小瑕疵：help 串未追加检查点命令 | 用 `about`/README 发现命令 |
| 输入 `l102test` 显示 unknown | exec64 分支未改名 | 确认 `else if(eq64(word,"l102test"))` 存在 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `resource_teardown` 的六类引用清零 + `teardown_done` 防双释放 | `kernel/module.c`：`do_init_module()` 失败时 `module_unload_free()`；`devm_` 框架（`drivers/base/devres.c`）自动释放 | 「失败后按逆序释放、防双重释放」语义一致；教学模型在 u64 计数层做 |
| `wait_block_wait(nohang)` | `kernel/exit.c` `wait_consider_task`；`waitpid(WNOHANG)`（`kernel/wait.c`） | WNOHANG 不阻塞、直接返回就绪状态的语义一致 |
| `wait_block_exit` 唤醒一个阻塞者 | `wake_up_process`（`kernel/sched/core.c`） | 教学模型用 `wakes++` 计数代替真实唤醒 |
| `shell_exec_path` 先校验路径/上限再分配 | `fs/open.c` `do_filp_open` + `fs/exec.c` `do_execveat_common` | 「先验参数再做事」的一致设计 |
| `lesson_103_model` 的 `ready`/`accounted` 位 | systemd `active`/`sub` 状态 + cgroup 资源记账 | 教学模型用 2 个 u8 位表达「已就绪+已记账」 |

**权威来源**：Linux v6.x（`kernel/module.c`、`kernel/exit.c`、`kernel/sched/core.c`、
`fs/exec.c`）；POSIX `waitpid`/`WNOHANG`；systemd unit 状态机。

**教学模型简化了什么**：无真实回滚动作（只清零计数）；无真实唤醒（只有计数）；
启动失败不产生内核日志，只影响命令输出串。

---

## 8. 思考题与练习

1. **概念理解**：`resource_teardown` 的「先检查 zombie 再检查 teardown_done」两个守卫
   各防住什么？如果颠倒顺序会怎样？
2. **源码定位**：`wait_block_exit` 中为何要先调用 `wait_model_exit(17)` 再唤醒？
   把退出码改成别的值会影响 `waitblocktest` 的哪一行断言？
3. **动手实验**：在 `l110test` 里把 `ready` 注入值改为 `0`，`make run` 后输入
   `l110test` 观察 fallback 串，再改回（勿提交）。
4. **Linux 对照**：读 `devm_` 框架（`drivers/base/devres.c`）的释放顺序，对照本课
   `resource_teardown` 的清零顺序，列出教学模型遗漏的释放类别。
5. **设计思考**：本课 `l102test` 是「更名不动逻辑」。你认为更名应该遵循什么纪律才能让
   检查点课可审计？（提示：diff 除函数名外逐字一致）

---

## 9. 本课小结与下一课预告

**小结**：本课以「服务启动与失败回滚」为主题，是检查点主线的第 2 站。源码增量仍是
「更名 + 新模型 + 新命令 + 主题串」四件套：`l109test`→`l102test`、新增
`struct lesson_103_model`/`lesson_103_state`/`l110test`、exec64 增分支、about/横幅更新。
主题机制全部继承：`resource_teardown` 做六类资源逆序回滚并防双重释放、
`wait_block` 处理启动失败后的阻塞/唤醒/重试、`shell_exec_path` 在分配前先做路径与参数
上限校验。`l110test` 的 `ready` 与 `accounted` 位分别对应「启动成功性」与「回滚记账」。

**下一课预告**：[Lesson 111](../lesson-111-stable/README.md) 主题「守护进程生命周期」：
检查点模型前进为 `lesson_104`，`l111test` 重申守护进程从 fork/daemon 化到
detach/退出/回收的完整生命周期；`l103test` 由本课 `l110test` 更名收敛。主线仍在
VFS/设备/epoll/服务检查点段，直到 113 课转入 mutex/spinlock。

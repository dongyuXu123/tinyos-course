# Lesson 59: fork → exec → exit 完整元数据生命周期 — 精讲文档

> **课号**：59　**主题**：fork 元数据 → 受控 ELF-like exec 替换 → exit 状态发布 → 父进程 wait/reap 的完整闭环（综合课）
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）
> **前置课程**：[lesson-58-stable/README.md](../lesson-58-stable/README.md)（有界多子进程 waitpid 选择）
> **后续课程**：[lesson-60-stable/README.md](../lesson-60-stable/README.md)（受控用户空间 job/session 模型）
> **一句话目标**：把第 39、40、54–58 课积累的 fork/exec/exit/wait/reap 零件，串成「一个子进程从出生到被收割」的完整元数据生命周期，并验证其状态约束的严密性。

> **Course status: stable snapshot (validated; verified build artifacts included).**

A fixed child lifecycle links fork metadata, controlled ELF-like exec replacement, exit status publication, and parent wait/reap without executing arbitrary user instructions.

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能不看笔记画出「fork 出子进程 → exec 替换镜像 → 子进程 exit 发布状态 → 父进程 wait/reap 收割」的完整状态链，并解释为什么 exec 必须发生在 fork 之后、exit 必须发生在 exec 之后、reap 必须发生在 exit 之后。
- **在课程主线中的位置**：本课属于第 3 阶段（32–60）的**综合课（checkpoint）**。此前各课把生命周期拆成独立小模型：第 39 课 `fork_model`（fork/clone 元数据）、第 40 课 `exec_model`（ELF 段校验与替换）、第 54 课 `wait_model`（running→zombie→dead）、第 55 课阻塞/非阻塞 wait、第 56 课收养、第 57 课资源账本、第 58 课多子进程选择。本课的 `fork_exec_lifecycle` 模型把这些**阶段约束**收进一个结构：`lifecycle_fork → lifecycle_exec → lifecycle_exit → lifecycle_wait_reap` 必须严格按序，任何跳步都被拒绝。它是阶段末验收（§9.3）「运行一条命令 → 输出 → 退出 → 父进程收割」的元数据版本，也是第 60 课 job/session 模型中「每个 job 都要经历完整生命周期」的直接素材。
- **前置知识清单**：
  1. 第 39 课 `fork_model`：`fork_attempts/fork_successes`、`parent_pid/child_pid`、`RESOURCE_COPIED/RESOURCE_SHARED` 复制/共享边界；
  2. 第 40 课 `exec_validate/exec_stack_validate` 与 `exec_model` 字段（`entry/argc/envp/stack_pointer`），以及 `embedded_exec_image` 的 ELF-like 头部结构；
  3. 第 54 课 `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD` 状态机与「wait 后必须 reap」的顺序约束；
  4. `FIXED_PID=1`、`SECOND_PID=2`、`USER_CODE_VA=0x00400000` 等常量语义。
- **本课交付（可见结果）**：新命令 `lifecycleinfo`（打印生命周期账本）与 `forkexecwaittest`（确定性验证），输出串见 §5。

## 2. 核心概念精讲

### 2.1 进程生命周期的四大阶段

- **直觉**：进程的一生像一列单向火车：`fork`（上车）→ `exec`（换乘客/换程序）→ `exit`（到站下车宣布状态）→ `wait/reap`（站台工作人员回收车厢）。本课把每个「换乘点」都变成一道闸门：没买上一站的车票就不能进入下一站。
- **准确定义**（Linux 视角）：
  - `fork`：复制调用者，产生新进程（`kernel/fork.c` `copy_process`），子进程从 fork 返回点继续；
  - `exec`：用新程序镜像替换当前进程的地址空间与入口（`fs/exec.c` `do_execveat_common`），**不产生新 PID**；
  - `exit`：进程终止，发布退出状态，进入 `EXIT_ZOMBIE`（`kernel/exit.c` `do_exit`）；
  - `wait/reap`：父进程取走状态、`release_task` 释放（`kernel/exit.c` `wait_task_zombie`）。
- 教学模型把四大阶段压缩为四个函数 + 一组**前置条件**（`forks/execs/exits/waits` 位），每一跳都验证「上一阶段已完成、本阶段未重复」。

### 2.2 状态约束：为什么不能跳步

本课 `lifecycle_*` 系列函数的每个前置检查都对应一条真实 Linux 约束：

| 跳步 | 模型拒绝方式 | Linux 对应约束 |
|---|---|---|
| 未 fork 就 exec | `lifecycle_exec` 要求 `lifecycle_model.forks==1` | exec 只影响当前已存在进程 |
| 重复 fork | `lifecycle_fork` 要求 `forks==0` | 一次 fork 产生一个子进程 |
| 未 exec 就 exit | `lifecycle_exit` 要求 `image_replaced==1` | 正常流程子进程 exec 后才退出（这里固化教学顺序） |
| 未 exit 就 wait/reap | `lifecycle_wait_reap` 要求 `child_state==WAIT_ZOMBIE` | wait 只能收割已退出的子进程 |
| 重复 wait/reap | `child_state` 被置 `WAIT_DEAD` 后不再等于 `WAIT_ZOMBIE` | release_task 只执行一次 |

### 2.3 exec 替换：old_entry → new_entry

- `lifecycle_exec` 记录两个入口：`old_entry=USER_CODE_VA`（fork 时继承的原镜像入口，即第 40 课 `exec_model` 校验过的 0x00400000）与 `new_entry=exec_model.entry`（`exec_validate()` 从 `embedded_exec_image` 头部解析出的新入口）。
- `forkexecwaittest` 断言 `old_entry==USER_CODE_VA && new_entry==exec_model.entry`，证明「替换确实发生、且新入口来自已校验镜像」——这是「受控 ELF-like exec」的元数据投影：**不执行任何指令**，只记录入口与栈布局（`argc==2`、`envc==1`）。
- 为什么 `envc=1`？`exec_model.argc` 来自第 40 课 `EXEC_STACK_ARGC=2`（固定 argv 两个词），而 `envc` 由本课写死 1，表示「受控环境变量元数据」。`lifecycleinfo` 会把 `argc=2/envc=1` 展示出来供验证。

### 2.4 教学模型边界

- 只跟踪**一个**子进程的完整生命周期（`parent/child` 固定为 1/2），不叠加第 58 课三子进程表；
- exec 是「记录替换」而非真实装载：`exec_model.entry` 由第 40 课校验函数产出，从不执行用户指令；
- 四大阶段都发生在元数据层，无真实调度、无动态内存、无用户指针解引用；
- 本课不调用 `wait_model`/`fork_model` 本身——它是一个**独立的总成模型**，用来验证「顺序约束」这一主题。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | Multiboot2 header + long mode 切换 | 未变化 |
| `kernel.c` | 32 位阶段初始化 | 未变化 |
| `kernel64.c` | 64 位内核：命令分发 + **本课生命周期总成模型** | **新增 `fork_exec_lifecycle` 及 7 个函数 + 2 条命令分支 + 2 个前置声明** |
| `kernel64.ld` | 64 位裸二进制链接 | 未变化 |
| `linker.ld` | 32 位外 ELF 链接 | 未变化 |
| `Makefile` | 构建 + `check` 校验 | **`check` 关键词改为 `fork`/`exec`/`forkexecwaittest`/`lifecycleinfo`/`multichildtest`/`waitpidtest`/`shellrun`** |
| `grub.cfg` | GRUB 菜单 | 未变化 |

注意：本课**未**在 `kernel_main64_binary` 启动路径新增调用（`lifecycle_model` 零初始化 + `lifecycle_start()` 测试内复位）。同时它在文件靠前位置（445–446 行）为**后面才定义**的第 40 课函数做了前置声明 `static TEXT64 int exec_validate(void);` / `exec_stack_validate(void);`——这是本课源码结构上的一个重要特点：生命周期模型位于 wait 模型附近（443 行），而 `exec_validate` 实体在第 649 行，C 语言要求先声明后使用，因此这两行前置声明是编译必需的。

### 3.2 结构与常量精讲（本课新增）

`kernel64.c` 第 443–446 行：

```c
struct fork_exec_lifecycle { u64 parent,child,old_entry,new_entry,argc,envc,exit_code,forks,execs,exits,waits,reaps; u8 child_state,image_replaced; };
static struct fork_exec_lifecycle lifecycle_model;
static TEXT64 int exec_validate(void);
static TEXT64 int exec_stack_validate(void);
```

- `parent`/`child`：固定父 PID 1 与子 PID 2；
- `old_entry`/`new_entry`：exec 替换前后的程序入口（虚址）；
- `argc`/`envc`：exec 后栈布局的 argv 词数与 env 词数（`argc=2` 来自 `EXEC_STACK_ARGC`，`envc=1` 为本课固定）；
- `exit_code`：子进程发布的退出码（测试用 23）；
- `forks/execs/exits/waits/reaps`：五个阶段完成位/计数（`u64`，值为 0 或 1），是「顺序约束」的观测账本；
- `child_state`：复用第 54 课 `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD` 值；
- `image_replaced`：`u8`，exec 替换完成标志——`lifecycle_exit` 用它要求「先 exec 再 exit」。

### 3.3 函数精讲

#### `lifecycle_start` — 复位（用指定初始化器）

```c
static TEXT64 void lifecycle_start(void){lifecycle_model=(struct fork_exec_lifecycle){.parent=FIXED_PID,.child=SECOND_PID,.old_entry=USER_CODE_VA,.child_state=WAIT_RUNNING};}
```

- 使用 C99 **指定初始化器**（designated initializers）：只显式设置 `parent=1, child=2, old_entry=0x00400000, child_state=WAIT_RUNNING`，其余字段（`new_entry/argc/envc/exit_code/forks/execs/exits/waits/reaps/image_replaced`）由标准规则自动置 0。
- 为什么用指定初始化器而非全量赋值？相比第 55/56/57 课的全字段列表，这里「只挑关键身份字段、其余清零」让意图更清晰：本模型的初始状态是「子进程已被 fork 出来、继承旧入口、但尚未 exec」。`old_entry=USER_CODE_VA` 即「fork 时的继承入口」，与第 40 课用户代码基址一致。

#### `lifecycle_fork` — 阶段①：fork 元数据

```c
static TEXT64 int lifecycle_fork(void){
    if(lifecycle_model.forks||lifecycle_model.child_state!=WAIT_RUNNING)return 0;  /* 未 fork 过且子进程尚在 running */
    lifecycle_model.forks=1;                                                         /* 完成 fork */
    return 1;
}
```

- 算法步骤：① 检查 `forks==0`（未 fork 过）；② 检查 `child_state==WAIT_RUNNING`（子进程还活着，能接受后续 exec/exit）；③ 置 `forks=1`；④ 返回 1。
- 边界与错误处理：重复 fork 返回 0；子进程已是 zombie/dead 时 fork 返回 0（不该给死进程「再 fork」）。
- 为什么这样设计：对照 `kernel/fork.c`，fork 是「从无到有造一个子进程」，只发生一次；`forks` 位把「一次」固化下来。本课没有像第 39 课那样记录 `parent_address_space/child_address_space` 等复制细节，因为本课主题是**阶段顺序**而非复制边界。

#### `lifecycle_exec` — 阶段②：受控 exec 替换（本课核心）

```c
static TEXT64 int lifecycle_exec(void){
    if(!lifecycle_model.forks||lifecycle_model.execs||!exec_validate())return 0;      /* 必须已 fork、未 exec、镜像有效 */
    lifecycle_model.old_entry=USER_CODE_VA;                                            /* 记录替换前入口 */
    lifecycle_model.new_entry=exec_model.entry;                                        /* 记录替换后入口（来自校验） */
    lifecycle_model.argc=exec_model.argc;                                              /* 栈布局 argc 来自 exec_model */
    lifecycle_model.envc=1;                                                            /* 受控 env 词数 */
    lifecycle_model.execs=1;                                                           /* 完成 exec */
    lifecycle_model.image_replaced=1;                                                  /* 镜像已替换：解锁 exit */
    return exec_stack_validate();                                                      /* 栈布局必须同时有效 */
}
```

- 算法步骤：① 三重前置：`forks==1`（先 fork）∧ `execs==0`（未 exec 过）∧ `exec_validate()` 通过（镜像合法）；② 记录 `old_entry`；③ 从 `exec_model` 读入 `new_entry/argc`；④ 设 `envc=1`；⑤ 置 `execs=1`、`image_replaced=1`；⑥ 返回 `exec_stack_validate()` 的结果。
- 输入输出：输入是全局 `exec_model`（第 40 课 `exec_validate()` 填充）；输出是 `lifecycle_model` 的 `old_entry/new_entry/argc/envc/execs/image_replaced`。
- 为什么这样设计：它把第 40 课独立的 `exec_validate`（ELF 头/段/入口校验）**拉进**生命周期链——exec 不是凭空发生，而是「通过校验的受控替换」。`image_replaced` 是 exec 与 exit 之间的桥梁位：只有它置 1，`lifecycle_exit` 才放行。对照 Linux `fs/exec.c`：`do_execveat_common` 校验镜像后 `exec_mmap` 替换地址空间，随后才允许进程继续（并最终可能 exit）——「替换完成」是先于「退出」的硬约束。
- 边界：`exec_validate()` 失败则整个 exec 返回 0 且不写任何字段（`execs` 保持 0，后续 `lifecycle_exit` 也会被 `image_replaced==0` 拒绝）——镜像不合法则整条链停在 exec 前。

#### `lifecycle_exit` — 阶段③：exit 状态发布

```c
static TEXT64 int lifecycle_exit(u64 code){
    if(!lifecycle_model.image_replaced||lifecycle_model.child_state!=WAIT_RUNNING)return 0;  /* 必须已 exec、子进程尚活 */
    lifecycle_model.exit_code=code;                                                           /* 发布退出码 */
    lifecycle_model.child_state=WAIT_ZOMBIE;                                                  /* 进入可收割状态 */
    lifecycle_model.exits=1;
    return 1;
}
```

- 算法步骤：① 检查 `image_replaced==1`（先 exec 后 exit）∧ `child_state==WAIT_RUNNING`（只允许从 running 进入 zombie）；② 记 `exit_code`；③ `child_state=WAIT_ZOMBIE`；④ `exits=1`；⑤ 返回 1。
- 边界：`image_replaced==0`（未 exec）或 `child_state!=WAIT_RUNNING`（重复 exit / 已 dead）都返回 0。这个「只允许 running→zombie 一次」的约束与第 54/55/58 课完全一致——整个课程群的退出发布规则是统一的。
- 为什么 `exit` 必须等 `exec`：教学模型把「子进程 fork 后直接 exit（不 exec）」视为非法，从而把「正常流程 = fork→exec→exit」固化；这与 Linux 中大部分子进程先 exec 再 exit 的惯例一致（教学刻意不模拟「fork 后不 exec 直接退出」的旁路）。

#### `lifecycle_wait_reap` — 阶段④：wait + reap 合并收割

```c
static TEXT64 int lifecycle_wait_reap(void){
    if(lifecycle_model.child_state!=WAIT_ZOMBIE)return 0;   /* 必须已退出 */
    lifecycle_model.waits=1;
    lifecycle_model.reaps=1;
    lifecycle_model.child_state=WAIT_DEAD;                  /* 收割后不再可 wait */
    return 1;
}
```

- 算法步骤：① 要求 `child_state==WAIT_ZOMBIE`；② `waits=1`；③ `reaps=1`；④ `child_state=WAIT_DEAD`；⑤ 返回 1。
- 为什么把 wait 与 reap 合并：第 54 课把「wait（查状态）」与「reap（释放）」分成 `wait_model_wait`/`wait_model_reap` 两步（中间隔着 `waited` 位）；本课为了强调**生命周期闭环**，把两步合一——`waits/reaps` 两个账本位仍分开记录，但调用点只有一处，减少测试噪音。`WAIT_DEAD` 状态保证二次收割被拒（`child_state` 不再是 `WAIT_ZOMBIE`），one-shot 语义与前几课一致。

#### `lifecycleinfo` — 生命周期账本展示

```c
static TEXT64 void lifecycleinfo(u16*c){text64(c,"lifecycle parent/child/old/new/state/fork/exec/exit/wait/reap: ");hex64(c,lifecycle_model.parent);hex64(c,"/");hex64(c,lifecycle_model.child);hex64(c,"/");hex64(c,lifecycle_model.old_entry);hex64(c,"/");hex64(c,lifecycle_model.new_entry);hex64(c,"/");hex64(c,lifecycle_model.child_state);hex64(c,"/");hex64(c,lifecycle_model.forks);hex64(c,"/");hex64(c,lifecycle_model.execs);hex64(c,"/");hex64(c,lifecycle_model.exits);hex64(c,"/");hex64(c,lifecycle_model.waits);hex64(c,"/");hex64(c,lifecycle_model.reaps);putc64(c,'\n');}
```

- 输出格式串（逐字）：`"lifecycle parent/child/old/new/state/fork/exec/exit/wait/reap: "`，随后以 `/` 分隔打印 `parent/child/old_entry/new_entry/child_state/forks/execs/exits/waits/reaps` 十个十六进制值，换行收尾。验证时可逐字对照源码（452 行）。

#### `forkexecwaittest` — 生命周期闭环验证

```c
static TEXT64 void forkexecwaittest(u16*c){
    lifecycle_start();                                                     /* 复位：fork 就绪 */
    int a=lifecycle_fork(),                                                /* a: fork 成功 */
        b=lifecycle_exec(),                                                /* b: exec 成功（镜像校验+栈布局） */
        d=lifecycle_exit(23),                                              /* d: exit(23) 发布状态 */
        e=lifecycle_wait_reap(),                                           /* e: wait/reap 收割 */
        f=lifecycle_model.old_entry==USER_CODE_VA&&lifecycle_model.new_entry==exec_model.entry, /* f: 入口替换正确 */
        g=lifecycle_model.argc==2&&lifecycle_model.envc==1&&lifecycle_model.exit_code==23;       /* g: 栈布局与退出码正确 */
    text64(c,"forkexecwaittest: ");text64(c,a&&b&&d&&e&&f&&g&&lifecycle_model.child_state==WAIT_DEAD?"fork metadata, exec replacement, exit status, and wait/reap passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`：`lifecycle_fork()` 成功（`forks=1`）；
  - `b`：`lifecycle_exec()` 成功（`exec_validate` 通过 + `exec_stack_validate` 通过）；
  - `d`：`lifecycle_exit(23)` 成功（`exit_code=23`，`child_state=WAIT_ZOMBIE`）；
  - `e`：`lifecycle_wait_reap()` 成功（`child_state=WAIT_DEAD`）；
  - `f`：替换前后入口正确（`old_entry==0x00400000`，`new_entry==exec_model.entry`）；
  - `g`：栈布局与退出码正确（`argc==2`、`envc==1`、`exit_code==23`）；
  - 汇总：`child_state==WAIT_DEAD`（闭环走完）。
- 验证输出串（逐字）：成功时 `"fork metadata, exec replacement, exit status, and wait/reap passed"`，失败 `"BROKEN"`，前缀 `"forkexecwaittest: "`（源码 453 行）。

#### 命令分支（`exec64` 内新增，kernel64.c 第 687 行区域）

```c
}else if(eq64(word,"forkexecwaittest")){if(!noargs64(arg))usage64(c,"forkexecwaittest");else forkexecwaittest(c);}
}else if(eq64(word,"lifecycleinfo")){if(!noargs64(arg))usage64(c,"lifecycleinfo");else lifecycleinfo(c);}
```

- 两条分支插在第 58 课 `waitpidtest` 分支之后，沿用「`noargs64` → `usage64`/直接调用」模式。

### 3.4 构建管线（Makefile / linker）

- 构建目标、编译标志、链接脚本与上一课完全一致，无新增步骤。
- 唯一变化：`check` 目标关键词（见 §5）——新增 `fork`/`exec`/`forkexecwaittest`/`lifecycleinfo` 四项，并保留 `multichildtest`/`waitpidtest`/`shellrun`。README 需同时包含这些关键词。
- 特别注意：本课**编译层面**因为新增了前置声明 `exec_validate/exec_stack_validate`，`gcc -Werror` 下如果漏掉声明会报 `implicit declaration` 错误；这两个声明保证模型位于 exec 实体之前也能正常引用（这也是构建管线中「声明先行」约束的体现）。

### 3.5 主控制流

```
kernel_main64_binary:   ← 本课无启动路径增量
  ... resource_start(); pmm_init; ...; install_idt; pit_init; pic_init; prompt64; sti
  for(;;){ kbd_dequeue → exec64(c,h,cmd) }
    forkexecwaittest / lifecycleinfo                             ← 本课新命令
```

- 生命周期模型由测试命令驱动；`lifecycle_start()` 在测试内复位。启动路径保持第 58 课形态（零新增调用）。

## 4. 数据流与运行逻辑

- **输入命令** → `forkexecwaittest` → `exec64` 命中分支 → `forkexecwaittest(c)`。
- 数据流：`lifecycle_start()`（parent=1, child=2, old_entry=0x400000, child_state=RUNNING, 其余 0）→ `lifecycle_fork()`（`forks=1`）→ `lifecycle_exec()`（调 `exec_validate()` 校验 `embedded_exec_image` 头部 → `exec_model.entry/argc` 被填充 → 记录 `old_entry/new_entry`、`argc=2/envc=1`、`execs=1`、`image_replaced=1` → `exec_stack_validate()` 通过）→ `lifecycle_exit(23)`（`exit_code=23`、`child_state=ZOMBIE`、`exits=1`）→ `lifecycle_wait_reap()`（`waits=1`、`reaps=1`、`child_state=DEAD`）→ `text64` 打印。
- **屏幕上显示什么**：`forkexecwaittest: fork metadata, exec replacement, exit status, and wait/reap passed`（源码 453 行）。`lifecycleinfo` 显示形如 `lifecycle parent/child/old/new/state/fork/exec/exit/wait/reap: 1/2/400000/400000/3/1/1/1/1/1`（十六进制）——注意 `new_entry` 显示为 `exec_model.entry` 的值（本课镜像入口仍落在 `USER_CODE_VA` 附近），`child_state` 为 `WAIT_DEAD`（3）。

## 5. 构建、运行与验证

**依赖**：与第 55–58 课相同，无新增工具。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
```

**关键词自检**：

```bash
make check
```

`check` 依次验证：`grub-file --is-x86-multiboot2 build/kernel.elf`、README 含 `fork`/`exec`、kernel64.c 含 `forkexecwaittest`/`lifecycleinfo`/`multichildtest`/`waitpidtest`/`shellrun`，最后打印 `Multiboot2 and lesson 59 checks passed.`。

**运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口（勿加 `-display none`）。`-serial stdio` 输出仅供诊断。

**VGA 验证**（旧 README 记录，保留）：

```bash
../../scripts/qemu-vga-check.sh . forkexecwaittest lifecycleinfo waitpidtest multichildtest reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

**验证步骤与预期输出**（输出串均从源码逐字抄录）：

1. 输入 `forkexecwaittest` → 预期屏幕出现
   `forkexecwaittest: fork metadata, exec replacement, exit status, and wait/reap passed`
   （源码 453 行成功串）。
2. 输入 `lifecycleinfo` → 预期形如 `lifecycle parent/child/old/new/state/fork/exec/exit/wait/reap: ` 后跟十个十六进制数（源码 452 行）。
3. 回归：`waitpidtest`、`multichildtest`、`reparenttest`、`waitblocktest`、`nohangtest`、`waittest`、`teardowntest`、`resourceinfo` 及更早回归命令保持通过。

**如何判断成功**：VGA 文本区出现上述成功串；任一断言失败输出 `BROKEN`；`qemu-vga-check.sh` 逐命令注入并检查物理 VGA 文本内存后返回成功。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `forkexecwaittest` 输出 `BROKEN` 且 `a` 为假 | `forks` 已为 1（重复执行残留）或 `child_state` 非 RUNNING | 确认测试开头 `lifecycle_start()`；`lifecycleinfo` 看 `forks`/`child_state` |
| `forkexecwaittest` 输出 `BROKEN` 且 `b` 为假 | `exec_validate()` 或 `exec_stack_validate()` 失败（镜像/栈布局异常） | 单跑第 40 课 `exectest` 命令验证 exec 模型本身；检查 `exec_model` 字段 |
| `forkexecwaittest` 输出 `BROKEN` 且 `d` 为假 | `image_replaced` 为 0（exec 未成功）或 `child_state!=WAIT_RUNNING` | 检查 `lifecycle_exec` 是否在 `d` 之前成功；`lifecycleinfo` 看 `execs`/`image_replaced`（经 info 的 `execs` 位间接观察） |
| `forkexecwaittest` 输出 `BROKEN` 且 `e` 为假 | `child_state` 不是 `WAIT_ZOMBIE`（exit 未发布） | 确认 `lifecycle_exit` 先于 `lifecycle_wait_reap`；`lifecycleinfo` 看 `child_state`/`exits` |
| `forkexecwaittest` 输出 `BROKEN` 且 `f` 为假 | `new_entry` 与 `exec_model.entry` 不一致（exec 未写入） | 检查 `lifecycle_exec` 中 `new_entry=exec_model.entry` 一行；确认 `exec_validate()` 填充了 `exec_model` |
| `forkexecwaittest` 输出 `BROKEN` 且 `g` 为假 | `argc`/`envc`/`exit_code` 与期望不符 | 检查 `EXEC_STACK_ARGC` 与 `lifecycle_exec` 的 `envc=1`；确认 `lifecycle_exit(23)` 传参 23 |
| 编译报 implicit declaration | 漏掉 `exec_validate/exec_stack_validate` 前置声明 | 确认 445–446 行两个 `static TEXT64 int` 声明存在；`make` 用 `-Werror` 强制 |
| 构建通过但 `make check` 失败 | README 关键词缺失 | 检查 README 是否含 `fork`、`exec`；kernel64.c 含 `forkexecwaittest`/`lifecycleinfo`/`multichildtest`/`waitpidtest`/`shellrun` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现 |
|---|---|---|
| fork 阶段 | `lifecycle_fork`：`forks=1`，一次性 | `kernel/fork.c` `copy_process`/`_do_fork`：复制 task_struct、mm、files 等；教学模型只记「已 fork」位 |
| exec 替换 | `lifecycle_exec`：记录 `old_entry/new_entry`，`image_replaced=1`，镜像须过 `exec_validate` | `fs/exec.c` `do_execveat_common`：`exec_mmap` 替换地址空间、`exec_binprm` 设置新入口；教学模型不执行指令 |
| exit 发布 | `lifecycle_exit`：`exit_code` + `child_state=WAIT_ZOMBIE` | `kernel/exit.c` `do_exit`：设置 `exit_code`，`exit_notify` 通知父进程，进入 `EXIT_ZOMBIE` |
| wait/reap | `lifecycle_wait_reap`：`waits=1`、`reaps=1`、`WAIT_DEAD` | `kernel/exit.c` `wait_task_zombie` → `release_task`：取状态、置 `EXIT_DEAD`、释放 task_struct |
| 阶段顺序约束 | `lifecycle_*` 前置条件严格按序 | Linux 语义约束（exec 只作用于存在进程、wait 只作用于 zombie）；教学模型把约束显式编码为条件 |
| 一次性 | `forks/execs/exits` 位防重复 | Linux 生命周期天然单次（同一进程不能 fork 两次、不能 exec 两次语义不同）；`release_task` 单次 |
| 栈布局 | `argc==2/envc==1`（`EXEC_STACK_ARGC`） | `fs/exec.c` `create_elf_tables` 布置 `argc/argv/envp` 到用户栈；教学模型只记账 |

**权威来源**：Intel SDM、Multiboot2 规范、Linux v6.x `kernel/fork.c`、`kernel/exit.c`、`fs/exec.c`。

**教学模型简化了什么**：不复制真实 mm/files（第 39 课另有 `fork_model` 管复制/共享边界）；exec 不真正装载可执行文件、不执行任何用户指令；wait 与 reap 合并为一个函数；只有一个固定子进程；无调度、无动态内存、无用户指针。

## 8. 思考题与练习

1. **概念理解**：为什么 `lifecycle_exit` 要求 `image_replaced==1`？如果把这条约束去掉，`forkexecwaittest` 是否还能通过？请从「教学模型要固化的流程」角度解释。
2. **源码定位**：在 `kernel64.c` 中找出 `exec_validate` 的实体（第 649 行）与前置声明（第 445 行），说明如果不写前置声明，`gcc -Werror` 会报什么错，以及为什么模型要放在 exec 实体之前。
3. **动手实验**：把 `forkexecwaittest` 中的调用顺序改成 `lifecycle_exit(23)` 先于 `lifecycle_exec()`，重新构建运行——观察哪个断言失败，并用 §6 调试地图解释「未 exec 不能 exit」约束。
4. **动手实验**：修改 `lifecycle_exec` 的 `envc=1` 为 `envc=2`，重新构建运行，观察 `g` 断言的变化，并思考 `lifecycleinfo` 中哪个字段会随之改变。
5. **Linux 对照**：阅读 Linux `fs/exec.c` 的 `exec_mmap` 与 `kernel/exit.c` 的 `exit_mm`，说明 exec 替换地址空间与 exit 释放地址空间在「引用计数/时序」上的差异，并对照本课 `old_entry → new_entry` 与 `resource_ledger.address_space=0` 两个教学动作。

## 9. 本课小结与下一课预告

- 本课把第 39/40/54–58 课的零件串成**完整生命周期闭环**：`fork_exec_lifecycle` 用五个阶段位（`forks/execs/exits/waits/reaps`）+ 两个状态字段（`child_state`/`image_replaced`）表达「fork → exec → exit → wait/reap」的严格顺序。
- 你学会了用前置条件把「跳步」拒绝掉：未 fork 不能 exec、未 exec 不能 exit、未 exit 不能 wait/reap、已收割不能再 wait——这些正是 Linux 生命周期语义的元数据投影。
- 你验证了 `forkexecwaittest: fork metadata, exec replacement, exit status, and wait/reap passed`，并理解了 `old_entry → new_entry` 记录的「受控 ELF-like 替换」与 `argc=2/envc=1` 的栈布局记账。
- 你注意到了本课源码的两处工程细节：C 语言前置声明（`exec_validate` 声明先于实体）与测试内复位（启动路径零增量）。
- 至此，单个进程的完整生命周期已经闭环。但真实用户空间里，进程以 **job/session** 的形式被组织管理，一个会话可能同时跑多个 job、每个 job 都要走这条生命周期。
- **下一课**（[lesson-60-stable/README.md](../lesson-60-stable/README.md)）引入「受控用户空间 job/session 模型」：一个固定 init/shell 会话拥有至多两个 job，把 argv/env、描述符、管道、信号、定时器、延迟工作、wait/reap 与资源 teardown 的元数据全部组合进 `session_job` 结构——这是阶段 3 的收官课，也是第 5 阶段（68–87）进程组/session 的前置。

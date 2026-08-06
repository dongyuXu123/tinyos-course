# Lesson 56: init adoption 与有界父进程重挂接 — 精讲文档

> **课号**：56　**主题**：孤儿进程被固定 init（PID 1）收养，wait 所有权一次性转移给 init
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）
> **前置课程**：[lesson-55-stable/README.md](../lesson-55-stable/README.md)（阻塞 wait/wake 与 WNOHANG）
> **后续课程**：[lesson-57-stable/README.md](../lesson-57-stable/README.md)（进程退出资源清理账本）
> **一句话目标**：当子进程的**原始父进程先退出**时，孤儿进程的 wait 所有权自动、且只能一次地转移给固定的 init 进程，保证「总有父进程来收割它」。

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 56 models the fixed init process (PID 1) adopting one orphan child when its original parent exits. Wait ownership moves to init exactly once, while the child identity and bounded lifecycle remain explicit metadata.

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说清「为什么孤儿进程必须有人收养、为什么一定是 init、收养发生后 wait 所有权在哪里」，并能用 `reparenttest` 在 VGA 上验证「孤儿检测 → init 收养 → wait 所有权转移 → 一次性重挂接」的完整链路。
- **在课程主线中的位置**：本课属于第 3 阶段（32–60），紧接第 55 课的 wait 模型。第 55 课建立了「父进程等子进程、exit 唤醒、WNOHANG 探询」，但那个模型隐含假设**父进程永远活着**。本课把父进程纳入生命周期：父进程先死，子进程变成孤儿，wait 关系由 init 接替——这是 Linux `reparent_leader`/`forget_original_parent` 语义的教学投影，也是第 59 课「fork→exec→exit 完整元数据生命周期」的必要一环。
- **前置知识清单**：
  1. 第 54/55 课的 `wait_model` 状态机（`WAIT_RUNNING/ZOMBIE/DEAD`）与「谁 wait、谁 reap」的所有权概念；
  2. `FIXED_PID`（1，init）与 `SECOND_PID`（2，固定子进程）两个常量在 `kernel64.c` 中的身份语义；
  3. `kernel_main64_binary` 的初始化顺序（`init_model_start()`、`wait_model_start()` 的调用位置）；
  4. `exec64` 的命令分发与 `adoption_start()` 在启动路径中的位置。
- **本课交付（可见结果）**：新命令 `adoptioninfo`（打印收养账本）与 `reparenttest`（确定性验证），输出串见 §5。

## 2. 核心概念精讲

### 2.1 孤儿进程（orphan）与收养（adoption）

- **直觉**：现实生活中孩子没了父母，需要有人接管抚养；操作系统中进程没了父进程，需要有人负责 `wait` 它、回收它。Linux 把这个角色固定给 **init（PID 1）**——它是内核启动后第一个、永不退出的进程。
- **准确定义**：当某进程的父进程先于它 `exit`，该进程成为孤儿，其 `parent` 被重挂接到 init；此后 init 成为它唯一的 wait 所有权人。本课模型固定为「**一个孤儿、一个 init、一次收养**」：`adoption_model.child_pid` 固定为 `SECOND_PID`（2），原始父固定为 `FIXED_PID`（1 即 init 自身？注意这里模型里 `original_parent=FIXED_PID`），收养目标是 `init_pid=FIXED_PID`。
  - 这里有一个值得注意的教学设定：`original_parent` 与 `init_pid` 都是 `FIXED_PID=1`——模型只关心「wait 所有权从‘某个父’转给‘init’」这条链的**元数据形态**，并不区分 init 是否真和父进程是同一个 PID（真实 Linux 中 init 与任何其他父进程 PID 必然不同）。这意味着本模型把「重挂接」抽象为「`current_parent` 字段被改写 + `wait_owner` 置为 init」。
- **为什么需要**：若孤儿无人收割，它会永远滞留 `EXIT_ZOMBIE`，成为泄漏的进程表项。Linux 中 `forget_original_parent`（`kernel/exit.c`）会遍历子进程并调用 `reparent_leader` 把它们过继给 init（或最近的线程组 leader）。
- **示意图**：

```
  父进程(PID 1? 模型)            子进程(PID 2)
      │                              │
      │ exit()                       │ 仍 RUNNING
      ▼                              ▼
  adoption_exit_parent():
    orphaned=1
    wait_owner=init_pid (1)          │
    current_parent → init_pid (1)    │
      ▼                              ▼
  adoption_wait_owner() == 1        wait 所有权已属于 init
```

### 2.2 wait 所有权（wait ownership）

- 一旦收养完成，**只有 init 能 `wait` 这个孤儿**。本课用 `wait_owner` 字段表达：`adoption_exit_parent()` 把它置为 `init_pid`；`adoption_wait_owner()` 只有在 `adopted && wait_owner==init_pid && current_parent==init_pid` 三者同时成立时才返回真——这三点分别验证「收养已发生」「所有权登记正确」「父指针确实改写到位」。
- 为什么用三个条件的合取？因为「所有权」是一个**派生状态**，不是单一标志：`adopted` 只能说明收养动作发生过，不能保证 `current_parent` 真的被改写过；`wait_owner` 是权属声明，`current_parent` 是结构事实。Linux 中 `struct task_struct->real_parent` 与 `parent` 也是分离维护的，`__set_task_state` 之外专门有 `reparent` 流程同步两者。

### 2.3 一次性（one-shot）重挂接

- `adoption_reparent()` 的前置条件：`orphaned` 必须为真（确实成了孤儿）**且** `adopted` 必须为假（还没收养过）。第二次调用会直接返回 0。这把「收养」变成**幂等拒绝**的一次性动作，防止重复重挂接把 `current_parent` 反复改写。
- 对照 Linux：`reparent_leader` 把子进程加入 init 的子进程链表后，`forget_original_parent` 遍历完即结束；孤儿不会二次被收养。教学模型用两个布尔位实现了同样的「只一次」保证。

### 2.4 教学模型边界

- 只有一个孤儿、一个原始父、一个 init；`child_pid` 不扩展为任意进程树；
- 没有 namespace、没有进程组/session 重挂接、没有跨会话收养；
- 收养不改变子进程状态机（仍由 `wait_model` 管理 running→zombie→dead）；本课模型**不执行** `wait_model_*` 本身，而是建立「谁有权等」的所有权层，供第 57–60 课在其上叠加 teardown、waitpid、生命周期。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | Multiboot2 header + 32→64 位 long mode 切换 | 未变化 |
| `kernel.c` | 32 位阶段：用户镜像校验、`long_mode_handoff` 构建 | 未变化 |
| `kernel64.c` | 64 位内核：命令分发 + **本课 adoption 收养模型** | **新增 `adoption_model` 及 6 个函数 + 2 条命令分支 + 启动路径 1 行** |
| `kernel64.ld` | 64 位裸二进制链接 | 未变化 |
| `linker.ld` | 32 位外 ELF 链接 | 未变化 |
| `Makefile` | 构建 + `check` 校验 | **`check` 关键词改为 `adoption`/`reparenttest`/`init`** |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 结构与常量精讲（本课新增）

`kernel64.c` 第 421–428 行（紧接第 55 课的 `wait_block_model` 块）：

```c
struct adoption_model { u64 init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; u8 orphaned,adopted,wait_owner; };
static struct adoption_model adoption_model;
```

- `init_pid`：收养目标，固定为 1；
- `original_parent`：收养发生前的父 PID，固定为 `FIXED_PID`；
- `child_pid`：孤儿身份，固定为 `SECOND_PID`（2）；
- `current_parent`：**结构事实**——当前父指针，收养前为 `FIXED_PID`，收养后被改写为 `init_pid`；
- `adoptions`：收养发生次数（只能为 0 或 1）；
- `ownership_checks`：`adoption_wait_owner()` 被调用的次数（可观测账本）；
- `orphaned/adopted/wait_owner`：三个 `u8` 位，分别表示「已成孤儿」「已收养」「wait 权属登记为 init」。

设计要点：`current_parent` 与 `wait_owner` 分离，正是 Linux `parent` 与 `real_parent` 分离的教学缩略版——一个记录「现在谁管我」，一个记录「权属声明」。

### 3.3 函数精讲

#### `adoption_start` — 复位收养账本

```c
static TEXT64 void adoption_start(void){adoption_model=(struct adoption_model){1,FIXED_PID,SECOND_PID,FIXED_PID,0,0,0,0,0};}
```

- 按字段顺序整体赋值：`init_pid=1, original_parent=FIXED_PID, child_pid=SECOND_PID, current_parent=FIXED_PID, adoptions=0, ownership_checks=0, orphaned=0, adopted=0, wait_owner=0`。
- 边界：只复位收养模型自身，不动 `wait_model`；测试前需自行复位 wait 模型。设计上让每条测试（`reparenttest`）以 `adoption_start()` 开头保证可重复执行。

#### `adoption_reparent` — 执行重挂接（一次性）

```c
static TEXT64 int adoption_reparent(void){
    if(!adoption_model.orphaned||adoption_model.adopted)return 0;   /* 必须已孤儿且未收养 */
    adoption_model.current_parent=adoption_model.init_pid;          /* 改写结构事实 */
    adoption_model.adopted=1;                                       /* 标记收养完成 */
    adoption_model.adoptions++;                                     /* 账本 +1 */
    return 1;
}
```

- 算法步骤：① 前置检查 `orphaned && !adopted`；② `current_parent` 改写为 `init_pid`；③ 置 `adopted`；④ `adoptions++`；⑤ 返回 1。任一前置不满足立即返回 0。
- 边界与错误处理：`adoptions` 一旦为 1，`adopted` 即真，任何后续调用都被拒绝——这就是 one-shot 保证的落地。没有无效输入路径，因为模型只接受预置状态。
- 为什么这样设计：把「校验」与「执行」分开——`adoption_exit_parent()` 负责置 `orphaned`/`wait_owner`，`adoption_reparent()` 只负责「改写父指针 + 记账」。这与 Linux `reparent_leader` 的单一职责一致：它只调整父子链表，不负责判定孤儿资格。

#### `adoption_exit_parent` — 父进程退出 → 孤儿化 + 触发收养

```c
static TEXT64 int adoption_exit_parent(void){
    if(adoption_model.original_parent!=FIXED_PID)return 0;          /* 父身份必须符合模型 */
    adoption_model.orphaned=1;                                      /* 孤儿检测 */
    adoption_model.wait_owner=adoption_model.init_pid;              /* 所有权登记给 init */
    return adoption_reparent();                                     /* 立即重挂接 */
}
```

- 算法步骤：① 校验 `original_parent==FIXED_PID`；② 置 `orphaned=1`；③ 置 `wait_owner=init_pid`；④ 调用 `adoption_reparent()` 完成结构改写并返回其结果。
- 设计原因：把「孤儿化」与「重挂接」合并在一个调用里，模拟 Linux `do_exit → forget_original_parent` 的同步重挂接路径——父进程退出时立即处理孤儿，而不是等到孤儿自己退出时才补救。`wait_owner` 先于 `reparent` 设置，保证重挂接成功的瞬间权属登记已经完成（避免出现「已重挂接但无人有权 wait」的中间态）。
- 边界：若 `original_parent` 不是 `FIXED_PID`（模型外状态），整个函数返回 0，不产生任何副作用。

#### `adoption_wait_owner` — 查询 wait 所有权

```c
static TEXT64 int adoption_wait_owner(void){
    adoption_model.ownership_checks++;
    return adoption_model.adopted&&adoption_model.wait_owner==adoption_model.init_pid&&adoption_model.current_parent==adoption_model.init_pid;
}
```

- 返回值是三个条件的合取：`adopted`（动作已发生）∧ `wait_owner==init_pid`（权属声明正确）∧ `current_parent==init_pid`（结构事实到位）。
- 每次调用 `ownership_checks++`，让 `reparenttest` 能断言「查询真的发生过」，也让 `adoptioninfo` 展示查询计数。
- 为什么这样设计：所有权是派生状态，单一布尔位不足以证明「声明」与「事实」一致；三条件合取把两种信息源绑定，这是本课最值得品味的正确性约束。

#### `adoptioninfo` — 账本展示

```c
static TEXT64 void adoptioninfo(u16*c){text64(c,"adoption init/original/child/current/adoptions/owner: ");hex64(c,adoption_model.init_pid);hex64(c,"/");hex64(c,adoption_model.original_parent);hex64(c,"/");hex64(c,adoption_model.child_pid);hex64(c,"/");hex64(c,adoption_model.current_parent);hex64(c,"/");hex64(c,adoption_model.adoptions);hex64(c,"/");hex64(c,adoption_model.wait_owner);putc64(c,'\n');}
```

- 输出格式串（逐字）：`"adoption init/original/child/current/adoptions/owner: "`，随后打印 `init_pid/original_parent/child_pid/current_parent/adoptions/wait_owner` 六个十六进制值，换行收尾。验证时可逐字对照源码（427 行）。

#### `reparenttest` — 收养链路的确定性验证

```c
static TEXT64 void reparenttest(u16*c){
    adoption_start();                                            /* 复位 */
    int a=adoption_model.current_parent==FIXED_PID,              /* a: 初始父指针正确 */
        b=adoption_exit_parent(),                                /* b: 父退出 → 孤儿化+收养成功 */
        d=adoption_wait_owner(),                                 /* d: wait 所有权已属 init */
        e=adoption_model.orphaned&&adoption_model.adopted&&adoption_model.adoptions==1; /* e: 三账本一致 */
    text64(c,"reparenttest: ");text64(c,a&&b&&d&&e?"orphan adoption, init wait ownership, and bounded reparent passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`：收养前 `current_parent==FIXED_PID`（1），确认初始结构事实；
  - `b`：`adoption_exit_parent()` 返回 1（孤儿化 + 重挂接成功）；
  - `d`：`adoption_wait_owner()` 返回 1（三条件合取成立，权属归 init）；
  - `e`：`orphaned`、`adopted`、`adoptions==1` 三个账本一致——收养确实只发生一次。
- 验证输出串（逐字）：成功时 `"orphan adoption, init wait ownership, and bounded reparent passed"`，失败 `"BROKEN"`，前缀 `"reparenttest: "`（源码 428 行）。
- 注意：测试**没有断言** `adoption_reparent()` 的二次拒绝——这是一道思考题（§8 第 4 题），读者可自行验证 one-shot 边界。

#### 命令分支（`exec64` 内新增，kernel64.c 第 662 行区域）

```c
}else if(eq64(word,"adoptioninfo")){if(!noargs64(arg))usage64(c,"adoptioninfo");else adoptioninfo(c);}
}else if(eq64(word,"reparenttest")){if(!noargs64(arg))usage64(c,"reparenttest");else reparenttest(c);}
```

- 两条分支插在第 55 课 `nohangtest` 分支之后，遵循 exec64 统一的「`noargs64` → `usage64`/直接调用」模式。

#### 启动路径（`kernel_main64_binary`，kernel64.c 第 663 行）

```c
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;task_names_keep();active_sched_class=&fair_sched_class;sched_enqueues=sched_dequeues=sched_picks=0;module_init_model();init_model_start();wait_model_start();adoption_start();pmm_init(h);...
```

- 本课唯一的启动路径增量是插入 `adoption_start();`（位于 `wait_model_start();` 之后、`pmm_init(h);` 之前）。因为它只做结构体清零、不依赖 PMM，放在分页初始化前是安全的；先于任何用户输入执行，保证 shell 一启动收养账本就是干净可查询状态。

### 3.4 构建管线（Makefile / linker）

- 构建目标、编译标志、链接脚本**与上一课完全一致**：`-m32`/`-m64` 双编译器、`-ffreestanding -mno-red-zone -mno-sse` 系列、`-T kernel64.ld`/`-T linker.ld`、`grub-mkrescue` 出 ISO。
- 唯一变化：`check` 目标关键词（见 §5）——上一课的 `blocking wait`/`waitblocktest`/`WNOHANG`/`wait/wake` 替换为 `adoption`/`reparenttest`/`init`。构建产物与上一课结构相同，本课是纯内核源码增量。

### 3.5 主控制流

```
kernel_main64_binary:
  ... init_model_start(); wait_model_start(); adoption_start();   ← 本课新增 1 行
  pmm_init; vma_init; reclaim_init; vfs_init; address_space_init; ...
  install_idt; pit_init; pic_init; prompt64; sti
  for(;;){ kbd_dequeue → exec64(c,h,cmd) }
    reparenttest / adoptioninfo                                   ← 本课新命令
```

- 收养模型不依赖任何硬件资源，纯元数据；其生命周期由测试命令驱动，`kernel_main64_binary` 只负责复位。

## 4. 数据流与运行逻辑

- **输入命令** → `reparenttest` → `exec64` 命中分支 → `reparenttest(c)`。
- 数据流：`adoption_start()`（init_pid=1, original_parent=1, child_pid=2, current_parent=1, 全 0 计数）→ 断言 `a`（current_parent==1）→ `adoption_exit_parent()`：置 `orphaned=1`、`wait_owner=1`，调 `adoption_reparent()` 把 `current_parent` 改写为 1、`adopted=1`、`adoptions=1` → `adoption_wait_owner()`：`ownership_checks=1`，三条件合取为真返回 1 → `text64` 打印。
- **屏幕上显示什么**：`reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`（源码 428 行）。`adoptioninfo` 显示形如 `adoption init/original/child/current/adoptions/owner: 1/1/2/1/1/1`（十六进制）——注意这里 `original_parent` 与 `current_parent` 都是 1（模型设定），`adoptions` 为 1、`wait_owner` 为 1。

## 5. 构建、运行与验证

**依赖**：与第 55 课相同（`gcc`/`ld`/`objcopy`/`grub-mkrescue`/`grub-file`/`qemu-system-x86_64`/`../../scripts/qemu-vga-check.sh`）。无新增工具。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
```

**关键词自检**：

```bash
make check
```

`check` 依次验证：`grub-file --is-x86-multiboot2 build/kernel.elf`、README 含 `shell`/`adoption`/`init`/`status`/`wait/wake`、kernel64.c 含 `reparenttest`/`shelltest`/`shellrun`/`waittest`/`waitblocktest`，最后打印 `Multiboot2 and lesson 56 checks passed.`。

**运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口（勿加 `-display none`）。`-serial stdio` 输出仅供诊断。

**VGA 验证**（旧 README 记录，保留并原样引用）：

```bash
../../scripts/qemu-vga-check.sh . adoptioninfo reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

**验证步骤与预期输出**（输出串均从源码逐字抄录）：

1. 输入 `reparenttest` → 预期屏幕出现
   `reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`
   （源码 428 行成功串）。
2. 输入 `adoptioninfo` → 预期形如 `adoption init/original/child/current/adoptions/owner: ` 后跟六个十六进制数（源码 427 行）。
3. 回归：`waitblocktest`、`nohangtest`、`waittest` 仍输出各自成功串（第 54/55 课），`shellrun`、`fdtest` 等其余回归命令保持通过。

**如何判断成功**：VGA 文本区出现上述成功串；任一断言失败输出 `BROKEN`；`qemu-vga-check.sh` 逐命令注入并检查物理 VGA 文本内存后返回成功。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `reparenttest` 输出 `BROKEN` 且 `a` 为假 | `adoption_start()` 未执行或 `current_parent` 初始值错误 | 确认测试命令先调 `adoption_start()`；对照源码 423 行复位串 |
| `reparenttest` 输出 `BROKEN` 且 `b` 为假 | `original_parent!=FIXED_PID`，或收养前 `adopted` 已为真（重复执行残留） | 检查是否在测试开头复位；`adoptioninfo` 看 `adoptions` 是否已为 1 |
| `reparenttest` 输出 `BROKEN` 且 `d` 为假 | `wait_owner` 或 `current_parent` 未变成 `init_pid` | 逐项查三条件：`adopted`、`wait_owner`、`current_parent`（`adoptioninfo` 全字段） |
| `reparenttest` 输出 `BROKEN` 且 `e` 为假 | `adoptions` 不为 1（0 或 2） | 检查 `adoption_reparent` 是否被多次调用；确认 `adopted` 守卫生效 |
| 想验证 one-shot 却无法复现 | `reparenttest` 只断言了成功路径，未覆盖二次拒绝 | 手动追加第二个 `adoption_exit_parent()` 调用并观察 `adoptions` 是否保持 1 |
| `adoptioninfo` 数值与预期不符 | 前一命令残留（`reparenttest` 后再查会看到 adoptions=1） | 在 `reparenttest` 前后各跑一次 `adoptioninfo`，对比 `orphaned/adopted/adoptions` 变化 |
| 构建通过但 `make check` 失败 | README 关键词缺失 | 检查 README 是否含 `adoption`、`init`、`shell`、`status`、`wait/wake` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现 |
|---|---|---|
| 孤儿判定 | `adoption_exit_parent()` 置 `orphaned=1` | `kernel/exit.c`：`do_exit` 中父进程死亡后，子进程成为孤儿；`forget_original_parent` 遍历子进程链表 |
| 重挂接到 init | `adoption_reparent()` 把 `current_parent` 改为 `init_pid`，`adopted=1` | `kernel/exit.c`：`reparent_leader` 把孤儿挂到 init 的子进程链表（`tsk->real_parent`/`parent` 更新），init 保证永不退出 |
| wait 所有权转移 | `wait_owner` 置为 `init_pid`；`adoption_wait_owner()` 三条件合取 | Linux 中 init 的 `do_wait` 遍历所有已收养子进程；`kernel/pid.c` 维护 PID 身份边界 |
| 一次性保证 | `adopted` 位守卫，二次 `adoption_reparent` 返回 0 | Linux 每个孤儿只被 `reparent_leader` 处理一次；收养后从原父的子进程链表移除 |
| 父指针双字段 | `current_parent`（事实）+ `wait_owner`（权属声明） | `struct task_struct` 的 `parent`（当前父）与 `real_parent`（线程组领导）分离维护 |
| 孤儿何时被收割 | 本课只建所有权层，wait 模型（54/55 课）负责收割 | Linux init 周期性调用 `wait` 回收被收养的僵尸（`kernel/exit.c` `release_task`） |

**权威来源**：Intel SDM（进程与系统调用交接）、Multiboot2 规范、Linux v6.x `kernel/exit.c`（`do_exit`/`forget_original_parent`/`reparent_leader`）、`kernel/pid.c`（PID 生命周期）。

**教学模型简化了什么**：只有一个孤儿、一个 init、无进程树/线程组、无 namespace/session 语义；`original_parent` 与 `init_pid` 在教学设定上同为 `FIXED_PID`，模型只表达「父指针改写 + 权属登记」的元数据形态，不表达真实 PID 差异；收养后的实际 wait 收割由第 54/55 课的 `wait_model` 承担，本课不重复实现。

## 8. 思考题与练习

1. **概念理解**：`adoption_wait_owner()` 为什么要同时检查 `adopted`、`wait_owner`、`current_parent` 三个条件？如果只检查 `adopted`，会漏掉什么错误？
2. **源码定位**：在 `kernel64.c` 中找出 `adoption_start()` 被调用的两个位置（测试内与启动路径），说明它们各自保证什么，以及为什么启动路径的调用位于 `wait_model_start()` 之后、`pmm_init(h)` 之前。
3. **动手实验**：在 `reparenttest` 中 `e` 断言之后追加一次 `adoption_exit_parent()` 调用，观察返回值与 `adoptions` 字段，验证 one-shot 守卫是否生效，并解释 `adoptioninfo` 的输出变化。
4. **动手实验**：修改 `adoption_start()` 使 `child_pid=3`，重新构建运行 `adoptioninfo` 与 `reparenttest`，判断哪些断言会失败、为什么——这考察你对「固定子进程身份」的理解。
5. **Linux 对照**：阅读 Linux `kernel/exit.c` 的 `reparent_leader`，说明它除了更新 `parent` 之外还做了什么（如 `ptrace` 链、`sibling` 链表），并指出 TinyOS 模型省略了其中哪些部分。

## 9. 本课小结与下一课预告

- 本课在第 55 课 wait 模型之上建立了**所有权层**：孤儿进程由固定 init 收养，`current_parent` 与 `wait_owner` 双字段分别承载「结构事实」与「权属声明」。
- 你学会了用 `orphaned/adopted` 两个布尔位 + `adoptions` 计数表达「孤儿检测 → 收养 → 一次性重挂接」的状态机，并理解了为什么 `adoption_wait_owner` 要用三条件合取来证明所有权。
- 你验证了 `reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`，并理解了模型边界：一个孤儿、一个 init、无进程树。
- 启动路径只新增一行 `adoption_start();`——这体现了本阶段「元数据模型自包含、不侵入启动流程」的设计习惯。
- 这一层解决的是「**谁有资格 wait**」，但还没解决「wait 之后要释放哪些资源」——这正是下一课的主题。
- **下一课**（[lesson-57-stable/README.md](../lesson-57-stable/README.md)）引入「进程退出资源清理账本」：zombie 保留期间各资源引用仍被持有，reap 时按序释放且拒绝二次 teardown——把「收割」从状态跳转升级为「资源引用有序释放」，并为第 59 课的完整生命周期闭环铺路。

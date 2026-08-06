# Lesson 58: 有界多子进程 waitpid 选择 — 精讲文档

> **课号**：58　**主题**：固定三子进程表中，按精确 PID 或聚合 `-1` 选择已退出子进程并 one-shot 收割
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）
> **前置课程**：[lesson-57-stable/README.md](../lesson-57-stable/README.md)（进程退出资源清理账本）
> **后续课程**：[lesson-59-stable/README.md](../lesson-59-stable/README.md)（fork → exec → exit 完整元数据生命周期）
> **一句话目标**：让 `waitpid` 从「唯一一个子进程」升级为「多个子进程中精确挑选」，掌握 `pid==-1` 聚合选择、exited-only 过滤与 one-shot 收割三件事。

> **Course status: stable snapshot (validated; verified build artifacts included).**

A fixed three-child table supports exact PID and aggregate `-1` wait selection, exited-only filtering, and one-shot reaping.

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说清「`waitpid(pid)` 与 `waitpid(-1)` 的选择规则、为什么只考虑已 zombie 的子进程、为什么收割一次后该子进程不再可被选择」，并能用 `multichildtest`（聚合选择）与 `waitpidtest`（精确 PID 选择）在 VGA 上验证。
- **在课程主线中的位置**：本课属于第 3 阶段（32–60）。第 54–57 课的所有 wait 模型都是「一对一」：`wait_model`、`wait_block_model`、`adoption_model`、`resource_ledger` 都只跟踪**一个**子进程。但 Linux 的 `wait`/`waitpid` 面对的是**子进程集合**。本课引入固定三子进程表 `multiwait`，把选择算法独立成 `multiwait_select`/`multiwait_reap`，是第 59 课综合生命周期与第 60 课 job/session 模型（两个 job 需要区分）的直接前置。
- **前置知识清单**：
  1. 第 54/55 课 `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD` 状态值与状态机；
  2. 第 56 课所有权概念（本课假设父进程是唯一合法 wait 者）；
  3. `FIXED_PID`/`SECOND_PID` 常量与 `SECOND_PID+1`/`SECOND_PID+2` 的派生 PID 规则；
  4. Linux `waitpid(2)` 手册语义：`pid>0` 精确匹配、`pid==-1` 匹配任意子进程。
- **本课交付（可见结果）**：新命令 `multichildtest`、`waitpidtest`，输出串见 §5。

## 2. 核心概念精讲

### 2.1 精确 PID 选择 vs 聚合 `-1` 选择

- **直觉**：`waitpid(pid)` 就像点菜——你说要「3 号桌的菜」（精确 PID）；`waitpid(-1)` 就像「哪个桌先做好就上哪个」（任意子进程）。
- **准确定义**：
  - 精确选择：`pid` 是一个具体子进程 PID，只有当该子进程状态为 `WAIT_ZOMBIE` 时才匹配；
  - 聚合选择：`pid==(u64)-1`（即无符号 -1），扫描整张表，**按索引升序**选中第一个 zombie。
- Linux `waitpid(2)` 手册原文（权威）：`pid == -1` — wait for any child process; `pid > 0` — wait for the child whose process ID equals pid. 本课 `multiwait_select` 把这两种语义压进一个 `if`：

```c
if(multiwait.states[i]==WAIT_ZOMBIE&&(pid==(u64)-1||pid==multiwait.children[i]))
```

- 条件拆解：前半 `states[i]==WAIT_ZOMBIE` 是 **exited-only 过滤**（没退出的子进程绝不能被选中）；后半 `pid==-1 || pid==children[i]` 是**目标过滤**（聚合或精确）。两个条件必须同时成立，缺一不可。

### 2.2 exited-only 过滤

- 只允许 `WAIT_ZOMBIE` 的子进程被选中，`WAIT_RUNNING`（还活着）与 `WAIT_DEAD`（已被收割）都不可选。这是 Linux `wait_consider_task` 的核心约束：一个还活着的子进程没有状态可报，一个已被收割的子进程不存在了。
- `waitpidtest` 的 `a=!multiwait_select(multiwait.children[0])` 专门验证这一点：索引 0 还是 `WAIT_RUNNING`，即使 PID 精确匹配也必须返回 0。

### 2.3 选择与收割的解耦

- `multiwait_select(pid)` 只做「选」，把选中的 PID 存入 `multiwait.selected` 并返回 1；`multiwait_reap()` 再根据 `selected` 找到对应槽位把状态翻为 `WAIT_DEAD`、`reaps++`。
- 为什么解耦？Linux 中 `wait` 分为「找候选（`wait_consider_task`）」与「收割（`wait_task_zombie` → `release_task`）」两步，中间隔着 `pid` 指针回传与锁边界。解耦让 `multichildtest` 能插桩验证「选了 2 号 → 收割 2 号 → 再选 3 号 → 收割 3 号」的完整序列。
- one-shot 收割：`multiwait_reap()` 只收割 `states[i]==WAIT_ZOMBIE` 的那个被选子进程，收割后 `states[i]=WAIT_DEAD`，该槽位从此不可再选——重复收割被状态位天然拒绝。

### 2.4 教学模型边界

- 固定三子进程表（`children[3]`），PID 固定为 2、3、4（`SECOND_PID`、`SECOND_PID+1`、`SECOND_PID+2`）；
- 无真实调度、无等待队列、无动态分配；选择是纯顺序扫描，不做优先级/顺序策略；
- 不区分 `wait`/`waitpid`/`waitid` 的其他选项（如 `WUNTRACED`）；`-1` 之外的负 PID 语义（进程组）不在本课范围——那是第 60 课之后第 5 阶段（68–87）进程组/session 的内容。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | Multiboot2 header + long mode 切换 | 未变化 |
| `kernel.c` | 32 位阶段初始化 | 未变化 |
| `kernel64.c` | 64 位内核：命令分发 + **本课 multiwait 三子进程表** | **新增 `multiwait_model` 及 5 个函数 + 2 条命令分支** |
| `kernel64.ld` | 64 位裸二进制链接 | 未变化 |
| `linker.ld` | 32 位外 ELF 链接 | 未变化 |
| `Makefile` | 构建 + `check` 校验 | **`check` 关键词改为 `child`/`multi-child`/`waitpid`/`selection`** |
| `grub.cfg` | GRUB 菜单 | 未变化 |

注意：本课**没有**在 `kernel_main64_binary` 启动路径新增调用——`multiwait` 是零初始化的静态量，每条测试命令以 `multiwait_start()` 复位，因此启动序列与第 57 课完全一致。这是与第 55/56/57 课启动路径各加一行不同的显著区别。

### 3.2 结构与常量精讲（本课新增）

`kernel64.c` 第 435–442 行（紧接第 57 课 `resource_ledger` 块之后）：

```c
struct multiwait_model { u64 parent,children[3],codes[3],selected,waits,reaps; u8 states[3]; };
static struct multiwait_model multiwait;
```

- `parent`：父进程 PID（固定 `FIXED_PID`=1）；
- `children[3]`：三个子进程 PID，初始化为 `{SECOND_PID, SECOND_PID+1, SECOND_PID+2}` 即 `{2,3,4}`；
- `codes[3]`：每个子进程的退出码（对应 Linux 的 `exit_code` 槽位）；
- `selected`：最近一次 `multiwait_select` 选中的子进程 PID（0 = 未选中）；
- `waits`：`multiwait_select` 调用次数（可观测账本）；
- `reaps`：`multiwait_reap` 成功次数（可观测账本）；
- `states[3]`：三个子进程的状态，初始全 `WAIT_RUNNING`，复用第 54 课常量 `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD`。

设计要点：`codes[]` 与 `states[]` 按索引并行，索引即「槽位」；`children[]` 是身份的数组，`selected` 存的是 PID 而不是索引——这样 `multiwait_reap` 必须「按 PID 反查槽位」，逼着实现真正走一遍匹配逻辑而不是偷懒用索引。

### 3.3 函数精讲

#### `multiwait_start` — 复位三子进程表

```c
static TEXT64 void multiwait_start(void){multiwait=(struct multiwait_model){FIXED_PID,{SECOND_PID,SECOND_PID+1,SECOND_PID+2},{0,0,0},0,0,0,{WAIT_RUNNING,WAIT_RUNNING,WAIT_RUNNING}};}
```

- 按字段顺序整体赋值：`parent=1, children={2,3,4}, codes={0,0,0}, selected=0, waits=0, reaps=0, states={WAIT_RUNNING,WAIT_RUNNING,WAIT_RUNNING}`。
- 三条测试命令（`multichildtest`/`waitpidtest`）都以它开头，保证可重复执行；它不触碰其他模型。

#### `multiwait_exit` — 单个子进程退出发布

```c
static TEXT64 int multiwait_exit(u32 i,u64 code){
    if(i>=3||multiwait.states[i]!=WAIT_RUNNING)return 0;   /* 索引越界或非 running 拒绝 */
    multiwait.codes[i]=code;                                /* 记录退出码 */
    multiwait.states[i]=WAIT_ZOMBIE;                        /* 进入可收割状态 */
    return 1;
}
```

- 算法步骤：① 边界检查 `i<3`；② 检查该槽位必须处于 `WAIT_RUNNING`（防止对 zombie/dead 重复 exit）；③ 写 `codes[i]=code`；④ `states[i]=WAIT_ZOMBIE`；⑤ 返回 1。
- 边界与错误处理：`i>=3` 或非 running 均返回 0，零副作用——这与第 55 课 `wait_model_exit` 的「只允许 running→zombie」约束一致。
- 为什么这样设计：`multiwait_exit` 模拟每个子进程独立 `do_exit`。测试里可以乱序触发（`multichildtest` 先退 1 号再退 0 号），正好验证选择算法按索引升序而不是按退出顺序取第一个——这是与真实 Linux（按进程树遍历顺序）的一处教学差异。

#### `multiwait_select` — waitpid 选择核心

```c
static TEXT64 int multiwait_select(u64 pid){
    u32 i;
    multiwait.waits++;                                       /* 记账一次 wait 调用 */
    for(i=0;i<3;i++)
        if(multiwait.states[i]==WAIT_ZOMBIE&&(pid==(u64)-1||pid==multiwait.children[i])){  /* exited-only + 目标过滤 */
            multiwait.selected=multiwait.children[i];        /* 记录被选子进程 PID */
            return 1;
        }
    return 0;
}
```

- 算法步骤：① `waits++`；② 顺序扫描 0..2；③ 对每个槽位检查「`WAIT_ZOMBIE` 且（`pid==-1` 或精确匹配）」；④ 命中则 `selected=children[i]` 返回 1；⑤ 全表无命中返回 0。
- 边界与错误处理：`selected` 只在成功时被改写，失败返回 0 且保留上次值——测试里可以用「先聚合选到 2 号 → 收割 → 再精确选 3 号」观察 `selected` 的逐步变化。
- 为什么这样设计：对应 Linux `wait_consider_task` 的「逐个子进程考虑、命中即返回」扫描。`pid==(u64)-1` 用无符号全 1 表示，避免 `-1` 与合法 PID 混淆，是 64 位无符号下表达「any」的标准技巧。

#### `multiwait_reap` — 按 selected 反查并收割

```c
static TEXT64 int multiwait_reap(void){
    u32 i;
    for(i=0;i<3;i++)
        if(multiwait.children[i]==multiwait.selected&&multiwait.states[i]==WAIT_ZOMBIE){  /* PID 反查 + 必须 zombie */
            multiwait.states[i]=WAIT_DEAD;                   /* 收割：进入 dead */
            multiwait.reaps++;
            return 1;
        }
    return 0;
}
```

- 算法步骤：① 扫描三槽；② 匹配 `children[i]==selected` **且** `states[i]==WAIT_ZOMBIE`；③ 置 `WAIT_DEAD`、`reaps++`；④ 返回 1；⑤ 无匹配返回 0。
- 边界：如果 `selected` 为 0（没选过）或 `selected` 对应槽位已被收割（`WAIT_DEAD`），则返回 0——这就是「one-shot」的硬保证：收割一次后状态位拒绝二次收割。
- 为什么这样设计：先按 PID 反查槽位再检查状态，保证「只有被选中的 zombie 才能被收割」，杜绝收割了「另一槽位」或「已被收割槽位」的错误。对照 Linux `wait_task_zombie`：找到 `p` 后置 `EXIT_DEAD` 并调用 `release_task`，同样以「找到目标 + 状态正确」为前提。

#### `multichildtest` — 聚合 `-1` 选择验证

```c
static TEXT64 void multichildtest(u16*c){
    multiwait_start();                                       /* 复位三表 */
    int a=multiwait_exit(1,7),                               /* a: 1 号先退，code=7 */
        b=multiwait_exit(0,3),                               /* b: 0 号后退，code=3 */
        d=multiwait_select((u64)-1),                         /* d: 聚合 -1 选中 */
        e=multiwait.selected==multiwait.children[0],         /* e: 选中的是索引 0（2 号），因为扫描升序 */
        f=multiwait_reap(),                                  /* f: 收割 2 号 */
        g=multiwait_select(multiwait.children[1]),           /* g: 精确选 3 号 */
        h=multiwait_reap();                                  /* h: 收割 3 号 */
    text64(c,"multichildtest: ");text64(c,a&&b&&d&&e&&f&&g&&h&&multiwait.reaps==2?"bounded three-child exit filtering and aggregate selection passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`/`b`：1 号与 0 号先后退出（乱序），`codes` 分别为 7、3；
  - `d`：`multiwait_select((u64)-1)` 聚合选择命中（表里有 zombie）；
  - `e`：`selected==children[0]`——尽管 **1 号先退出**，聚合选择仍按**索引升序**挑中 0 号（2 号），验证了「扫描顺序 ≠ 退出顺序」；
  - `f`：收割选中者（2 号）成功，`reaps=1`；
  - `g`：再精确 `multiwait_select(children[1])` 选 3 号；
  - `h`：收割 3 号，`reaps=2`；
  - 汇总断言 `reaps==2`。
- 验证输出串（逐字）：成功时 `"bounded three-child exit filtering and aggregate selection passed"`，失败 `"BROKEN"`，前缀 `"multichildtest: "`（源码 441 行）。

#### `waitpidtest` — 精确 PID 选择验证

```c
static TEXT64 void waitpidtest(u16*c){
    multiwait_start();                                       /* 复位三表 */
    multiwait_exit(2,9);                                     /* 只有 2 号（PID 4）退出，code=9 */
    int a=!multiwait_select(multiwait.children[0]),          /* a: 精确选 PID 2（running）→ 拒绝 */
        b=multiwait_select(multiwait.children[2]),           /* b: 精确选 PID 4（zombie）→ 命中 */
        d=multiwait_reap();                                  /* d: one-shot 收割 */
    text64(c,"waitpidtest: ");text64(c,a&&b&&d&&multiwait.codes[2]==9?"exact PID selection and one-shot waitpid reap passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`：精确 `waitpid(children[0])`（PID 2，running）被 exited-only 过滤拒绝——返回 0 取反为真；
  - `b`：精确 `waitpid(children[2])`（PID 4，zombie）命中；
  - `d`：one-shot 收割成功；
  - `codes[2]==9`：确认读到的退出码是 9（与第 55 课读 `exit_code==17` 同一思路）。
- 验证输出串（逐字）：成功时 `"exact PID selection and one-shot waitpid reap passed"`，失败 `"BROKEN"`，前缀 `"waitpidtest: "`（源码 442 行）。

#### 命令分支（`exec64` 内新增，kernel64.c 第 676 行区域）

```c
}else if(eq64(word,"multichildtest")){if(!noargs64(arg))usage64(c,"multichildtest");else multichildtest(c);}
}else if(eq64(word,"waitpidtest")){if(!noargs64(arg))usage64(c,"waitpidtest");else waitpidtest(c);}
```

- 两条分支插在第 57 课 `teardowntest` 分支之后，沿用统一模式。

### 3.4 构建管线（Makefile / linker）

- 构建目标、编译标志、链接脚本与上一课完全一致，无新增步骤。
- 唯一变化：`check` 目标关键词——上一课的 `resource`/`ledger`/`teardown` 替换为 `child`/`multi-child`/`waitpid`/`selection`（见 §5）。注意第 57 课 `check` 中的 `shell`/`status` 两项在 58 课被移除、新增了 `child`/`multi-child`，README 需同时保留这些关键词。

### 3.5 主控制流

```
kernel_main64_binary:   ← 本课无启动路径增量
  ... resource_start(); pmm_init; ...; install_idt; pit_init; pic_init; prompt64; sti
  for(;;){ kbd_dequeue → exec64(c,h,cmd) }
    multichildtest / waitpidtest                             ← 本课新命令
```

- `multiwait` 零初始化 + 测试内 `multiwait_start()` 自复位，因此启动序列不需要新增调用。这是本课与 55/56/57 的构建/启动差异点：纯命令层增量。

## 4. 数据流与运行逻辑

- **输入命令** → `multichildtest` → `exec64` 命中分支 → `multichildtest(c)`。
- 数据流：`multiwait_start()`（children={2,3,4}, states 全 RUNNING, codes 全 0）→ `multiwait_exit(1,7)`（1 号 zombie, code=7）→ `multiwait_exit(0,3)`（0 号 zombie, code=3）→ `multiwait_select(-1)`（`waits=1`，扫描命中索引 0，`selected=2`）→ `multiwait_reap()`（0 号 → DEAD, `reaps=1`）→ `multiwait_select(3)`（`waits=2`，命中索引 1，`selected=3`）→ `multiwait_reap()`（1 号 → DEAD, `reaps=2`）→ `text64` 打印。
- **屏幕上显示什么**：`multichildtest: bounded three-child exit filtering and aggregate selection passed`（源码 441 行）；`waitpidtest: exact PID selection and one-shot waitpid reap passed`（源码 442 行）。

## 5. 构建、运行与验证

**依赖**：与第 55–57 课相同，无新增工具。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
```

**关键词自检**：

```bash
make check
```

`check` 依次验证：`grub-file --is-x86-multiboot2 build/kernel.elf`、README 含 `child`/`multi-child`/`waitpid`/`selection`、kernel64.c 含 `multichildtest`，最后打印 `Multiboot2 and lesson 58 checks passed.`。

**运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口（勿加 `-display none`）。`-serial stdio` 输出仅供诊断。

**VGA 验证**（旧 README 记录，保留）：

```bash
../../scripts/qemu-vga-check.sh . waitpidtest multichildtest reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

**验证步骤与预期输出**（输出串均从源码逐字抄录）：

1. 输入 `multichildtest` → 预期屏幕出现
   `multichildtest: bounded three-child exit filtering and aggregate selection passed`
   （源码 441 行成功串）。
2. 输入 `waitpidtest` → 预期屏幕出现
   `waitpidtest: exact PID selection and one-shot waitpid reap passed`
   （源码 442 行成功串）。
3. 回归：`reparenttest`、`waitblocktest`、`nohangtest`、`waittest`、`teardowntest`、`resourceinfo` 及更早回归命令保持通过。

**如何判断成功**：VGA 文本区出现上述成功串；任一断言失败输出 `BROKEN`；`qemu-vga-check.sh` 逐命令注入并检查物理 VGA 文本内存后返回成功。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `multichildtest` 输出 `BROKEN` 且 `e` 为假 | 聚合选择没按索引升序（选中了 1 号而非 0 号） | 单步阅读 `multiwait_select` 的 `for(i=0;i<3;i++)` 扫描顺序；确认 0 号在表头 |
| `multichildtest` 输出 `BROKEN` 且 `f` 或 `h` 为假 | `selected` 与目标槽位不一致，或状态不是 zombie | 检查 `multiwait_reap` 的 PID 反查与 `WAIT_ZOMBIE` 条件；确认 `selected` 在 select 中先被赋值 |
| `multichildtest` 输出 `BROKEN` 且 `reaps` 不为 2 | 某次 reap 被拒绝（二次收割或 selected=0） | 在两次 select/reap 之间观察 `selected` 与 `states`；确认每个 zombie 只 reap 一次 |
| `waitpidtest` 输出 `BROKEN` 且 `a` 为假 | exited-only 过滤失效（running 也被选中） | 检查 `multiwait_select` 的 `states[i]==WAIT_ZOMBIE` 条件是否在 `pid` 匹配之前求值（两者是 && 关系） |
| `waitpidtest` 输出 `BROKEN` 且 `d` 为假 | one-shot 失败（reap 返回 0） | 确认 `multiwait_exit(2,9)` 确实把索引 2 置为 zombie；用 `waitpidtest` 前的 `multichildtest` 残留状态排查 |
| 想观察中间状态 | 测试只打印最终断言 | 手动在 shell 中先 `multichildtest` 再想象 `selected` 值；或临时在测试内加 `text64` 打印 `selected`/`states` |
| 构建通过但 `make check` 失败 | README 关键词缺失 | 检查 README 是否含 `child`、`multi-child`、`waitpid`、`selection` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现 |
|---|---|---|
| 精确 PID 选择 | `multiwait_select(pid>0)`：`pid==multiwait.children[i]` | `kernel/exit.c` `do_wait`/`wait_consider_task`：`pid==tsk->pid` 精确匹配子进程 |
| 聚合 `-1` 选择 | `multiwait_select((u64)-1)`：匹配任意 zombie | `waitpid(2)` 的 `pid==-1`：等任意子进程（`kernel/wait.c` 语义），`do_wait` 遍历子进程链表 |
| exited-only 过滤 | `states[i]==WAIT_ZOMBIE` | `kernel/exit.c`：只有 `EXIT_ZOMBIE`/`EXIT_DEAD` 前状态可被 wait；running 子进程调用 `schedule` 挂起而非返回 |
| 选择与收割分离 | `multiwait_select` 选 PID → `multiwait_reap` 反查收割 | `wait_consider_task`（找候选）→ `wait_task_zombie`（收割）两步 |
| one-shot 收割 | `states[i]=WAIT_DEAD` 后不可再选 | `kernel/exit.c`：`release_task` 置 `EXIT_DEAD` 并释放 task_struct，同一子进程不可能被 wait 两次 |
| 等待队列 | 本课不涉及（选择即返回） | `include/linux/wait.h`：真正的阻塞 wait 需要把调用者挂进 `wait_queue_head_t`；本课把「等待」留给 55 课模型 |
| 选择顺序 | 按槽位索引升序 | Linux 按子进程链表/`children` 遍历顺序，无严格语义保证 |

**权威来源**：Intel SDM、Multiboot2 规范、Linux v6.x `kernel/wait.c`（waitqueue 与非阻塞 wait 语义）、`include/linux/wait.h`（`wait_event`/`wake_up` 宏与条件判断）、`kernel/exit.c`（`do_wait`/`wait_consider_task`/`wait_task_zombie`）、`waitpid(2)`/`wait4(2)` 手册。

**教学模型简化了什么**：三槽位固定数组代替子进程链表；无 waitqueue（阻塞语义由第 55 课布尔模型承担）；不处理 `WUNTRACED`/`WCONTINUED`/进程组 `pid<0` 语义；收割无真实内存释放（第 57 课账本负责概念性的资源清零）。聚合选择的顺序是「索引升序」这一教学约定，与 Linux 的链表遍历顺序无强对应。

## 8. 思考题与练习

1. **概念理解**：`multichildtest` 中 1 号先退出、0 号后退出，为什么聚合 `-1` 选择最终选中了 0 号？如果扫描顺序改成「按退出先后」，`e` 断言会发生什么变化？
2. **源码定位**：在 `kernel64.c` 中确认 `WAIT_RUNNING/WAIT_ZOMBIE/WAIT_DEAD` 的宏值（第 414–416 行区域），说明 `states[i]==WAIT_DEAD` 的子进程为什么永远不会被 `multiwait_select` 选中。
3. **动手实验**：修改 `multichildtest`，把 `multiwait_exit(1,7)` 与 `multiwait_exit(0,3)` 顺序对调，重新构建运行——预期输出是否变化？请解释聚合选择对退出顺序的敏感性（或非敏感性）。
4. **动手实验**：在 `multiwait_select` 的 `if` 条件中删掉 `multiwait.states[i]==WAIT_ZOMBIE&&`，重新构建运行 `waitpidtest`，观察 `a` 断言的变化，并用 §6 调试地图解释「exited-only 过滤」的意义。
5. **Linux 对照**：阅读 `waitpid(2)` 手册关于 `pid==-1` 与 `pid>0` 的语义，并阅读 Linux `kernel/exit.c` 的 `do_wait` 中对 `pid_type`/`pid` 的处理，说明本课三槽位表的扫描对应了 Linux 中哪一段逻辑。

## 9. 本课小结与下一课预告

- 本课把 wait 从「一对一」升级为「一对多」：固定三子进程表 `multiwait` 用 `children[3]/codes[3]/states[3]` 三个并行数组表达三个子进程的身份、退出码与状态。
- 你学会了 `multiwait_select` 的双重过滤：`states[i]==WAIT_ZOMBIE`（exited-only）∧ `pid==-1 || pid==children[i]`（聚合/精确），以及选择（记 `selected`）与收割（按 PID 反查、置 `WAIT_DEAD`）的解耦设计。
- 你验证了 `multichildtest: bounded three-child exit filtering and aggregate selection passed` 与 `waitpidtest: exact PID selection and one-shot waitpid reap passed` 两条确定性串，理解了索引升序聚合选择与精确 PID 选择。
- 模型边界清晰：三槽位、无 waitqueue、无进程组负 PID 语义；阻塞仍由第 55 课模型承担。
- 现在我们有完整的零件：fork 元数据（39）、exec 校验（40）、exit 状态发布（54）、阻塞/非阻塞 wait（55）、孤儿收养（56）、资源 teardown（57）、多子进程选择（58）。
- **下一课**（[lesson-59-stable/README.md](../lesson-59-stable/README.md)）把这些零件串成一条链：**fork → exec → exit 完整元数据生命周期**——一个综合课，用一个 `fork_exec_lifecycle` 模型验证「fork 出子进程 → exec 替换镜像 → exit 发布状态 → wait/reap 收割」的完整闭环。

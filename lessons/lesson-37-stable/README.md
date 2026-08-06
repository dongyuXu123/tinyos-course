# Lesson 37: Linux 风格 task_struct 与任务状态机教学模型 — 精讲文档

> **课号**：Lesson 37（可执行课）
> **主题**：Linux 风格 `task_struct` 与任务状态机教学模型
> **课程主线位置**：第 5 阶段「Linux 风格内核抽象」的开端。从本课起课程改用
> Linux 教学模型：不再把「线程 TCB」当作唯一执行流描述，而是引入与 Linux
> `include/linux/sched.h` 对齐的 `task_struct` 类比，为后续 wait-queue / fork /
> exec / VMA / uaccess 六连课铺路。
> **前置课程**：[`lesson-36-stable/README.md`](../lesson-36-stable/README.md)
> **后续课程**：[`lesson-38-stable/README.md`](../lesson-38-stable/README.md)
> **一句话目标**：在不动现有调度/进程代码的前提下，新增一张 4 槽固定
> `task_table`，用 Linux 的任务状态位枚举、PID/TID/parent 身份字段和迁移计数器，
> 精确描述「任务是谁、从哪来、当前在哪个状态、迁移过几次」。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——为什么 Linux 用一个巨大的 `struct task_struct`
描述「一切执行流」，并能在 TinyOS 里用一张 4 槽固定表复刻它的**最小教学子集**：
身份（pid/tid/parent）、种类（kernel/user）、状态（7 个 Linux 状态位）与
不可逆的迁移规则；能用 `tasklist` 查看、`taskvalidate` 验证整张表的完整性。

- **在课程主线中的位置**：这是第 5 阶段（Linux 风格教学模型，lesson-37 ~ lesson-42）
  的第一课。此前（lesson-33~36）内核已拥有 PIT 抢占调度、普通 TCB（`struct thread`）、
  有界进程/地址空间/用户线程对象、CPL3 syscall ABI 与 IRQ0 帧保存恢复。本课在这些
  **已冻结且可运行**的机制之上**加一层元数据**，不替换任何旧机制。
- **前置知识清单**：
  1. Linux 进程/线程基本概念：进程有 PID、线程有 TID、子进程有 parent（父 PID）；
  2. 本课程线程状态机：`THREAD_RUNNING/RUNNABLE/SLEEPING/BLOCKED_*/FINISHED`
     （lesson-33/34 的 `struct thread` 与 `irq0_schedule`）；
  3. 有界进程对象 `struct process` 与 `struct user_thread`、CPL3 syscall ABI
     （lesson-34/35/36）；
  4. 能在 64 位 freestanding C（`-m64 -ffreestanding -mno-red-zone`）里读懂
     位掩码枚举、固定数组、`_Static_assert`。
- **本课交付**：新增 `enum task_state`、`enum task_kind`、`struct task_struct`、
  `task_table[4]` 与状态机函数 `task_transition`/`task_table_validate`，QEMU 中
  新增 `tasklist`、`taskvalidate` 命令；`user_process_enter/exit` 会把迁移同步
  到 `task_table[1]`。

---

## 2. 核心概念精讲

### 2.1 概念一：Linux 的 `task_struct` ——「每个执行流一张档案」

Linux 里进程与线程不是两个独立概念：`fork()` 得到进程，`clone()` 得到线程，
但两者都由**同一结构** `struct task_struct` 描述（`include/linux/sched.h`）。
直觉类比：`task_struct` 是一张「执行流档案」，上面既有身份信息（PID/TID/父子关系），
也有调度信息（状态、优先级）、地址空间指针、打开的文件表、信号位图等。

TinyOS 教学模型只保留档案里**与身份和状态有关的四个字段**：

```c
struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; };
```

`pid`/`tid`/`parent_pid` 是身份；`kind` 区分内核/用户任务；`state` 存状态位；
`transitions` 是状态迁移计数器；`valid` 标志该槽是否在用。真实 Linux
`task_struct` 有数百个字段（约 2~3 KB），本模型刻意压缩到 4 槽 × 4 个 u64。

### 2.2 概念二：PID、TID 与 parent —— 身份三件套

Linux 语义：`task_struct.pid` 实际是 **TGID/线程组 ID**（通常叫 PID），
`task_struct.tgid` 与 `task_struct.pid` 区分线程组；`real_parent`/`parent`
指向父任务。教学模型用**一个字段对**表示身份：

| 字段 | 含义（教学模型） | Linux 对应 |
|---|---|---|
| `pid` | 进程 ID | `task_struct->tgid`（对外 PID） |
| `tid` | 线程 ID（同一进程内唯一） | `task_struct->pid`（内核 pid） |
| `parent_pid` | 父任务 PID | `task_struct->real_parent->tgid` |

因为本课没有创建线程（无 clone），表里 `pid==tid`。验证规则要求**槽内 PID 与 TID
全表唯一**、`parent_pid < pid`（内核任务 `task_table[0]` 的 parent 为 0，即「无父」）。

### 2.3 概念三：Linux 任务状态位 —— 用位值编码状态

Linux 用位掩码而非顺序枚举表示任务状态（`include/linux/sched.h` 中的
`TASK_RUNNING` 等）。本课 `enum task_state` 完整抄录了这一编号习惯：

```c
enum task_state { TASK_RUNNING=0, TASK_INTERRUPTIBLE=1, TASK_UNINTERRUPTIBLE=2,
                  TASK_STOPPED=4, TASK_TRACED=8, EXIT_DEAD=16, EXIT_ZOMBIE=32 };
```

**为什么 TASK_RUNNING 是 0？** 因为 Linux 里状态位是「在可运行集之外附加的修饰位」，
`TASK_INTERRUPTIBLE=1`、`TASK_UNINTERRUPTIBLE=2` 是两位，`TASK_STOPPED=4`、
`TASK_TRACED=8` 继续按 2 的幂排——这样多个状态可以 OR 组合（如
`TASK_INTERRUPTIBLE | TASK_STOPPED`）。教学模型不做位组合，但**保留位值**，
让读者以后读 Linux 源码时编号能对上。

状态语义（教学模型）：

| 状态 | 含义 | 谁在用 |
|---|---|---|
| `TASK_RUNNING` | 可运行/正在运行 | 内核任务 0、3；用户任务运行后 |
| `TASK_INTERRUPTIBLE` | 可中断睡眠（等待事件） | 两个用户任务初始态 |
| `TASK_UNINTERRUPTIBLE` | 不可中断睡眠 | 保留，未使用 |
| `TASK_STOPPED` / `TASK_TRACED` | 停止/被跟踪 | 保留，未使用 |
| `EXIT_ZOMBIE` | 已退出，等待父回收 | `user_process_exit` 后 |
| `EXIT_DEAD` | 已被回收 | 保留，任何函数都不允许迁移到它 |

### 2.4 概念四：状态机 —— 迁移是「有方向的」

`task_transition(i, next)` 不是简单赋值，而是先校验再迁移：

```text
活状态（TASK_RUNNING / TASK_INTERRUPTIBLE / ...）
   │  可迁移到任意合法活状态（transitions++）
   ▼
EXIT_ZOMBIE（退出）── 之后任何迁移都拒绝
   ▼
EXIT_DEAD（回收）── 绝不可作为 next（回收语义由进程回收逻辑独占）
```

核心不变式：**终结态不可逆**。一旦任务进入 `EXIT_ZOMBIE`/`EXIT_DEAD`，
任何 `task_transition` 返回 0；`EXIT_DEAD` 也不允许作为目标态（由内核回收流程
独占语义，本课用代码拒绝）。这与 Linux `do_exit()` 后任务进入僵尸、由
`release_task()` 回收的流程方向一致。

### 2.5 概念五：有界表 + 验证函数 —— 教学模型的「边界」

整个第 5 阶段的共同纪律：**凡是 Linux 用动态链表/哈希表的地方，教学模型一律用
固定大小数组**，再写一个返回 bool 的 `*_validate` 函数在运行前自检。
本课 `TASK_TABLE_CAP 4U`：槽 0 是内核任务（pid=0），槽 1/2 是继承来的两个用户程序
（pid=FIXED_PID=1、SECOND_PID=2），槽 3 是另一个内核任务（pid=3）。
`task_table_validate()` 在 `kernel_main64_binary` 初始化完成后立即执行，失败则
`cli; hlt` 死循环——「宁可死机，不让坏表继续跑」。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-36） |
|---|---|---|
| `boot.S` | Multiboot2 i386 交接 → 建立页表/GDT → long mode | 未变化 |
| `kernel.c` | 32 位引导：校验用户镜像、建表、填 handoff | 未变化 |
| `kernel64.c` | 64 位内核主体：所有教学模型与命令 | **本课核心**：新增 task_struct 家族 |
| `kernel64.ld` | 64 位 continuation 布局（栈守卫段） | 未变化 |
| `linker.ld` | 外层 ELF32 布局 | 未变化 |
| `Makefile` | 构建两阶段内核 + ISO + 校验 | 未变化 |
| `grub.cfg` | `menuentry` 装载 `kernel.elf` | 未变化 |

### 3.2 kernel64.c：结构 / 枚举 / 全局变量精讲

```c
/* Lesson 37: deliberately small task_struct analogue. Linux task_struct is
 * much larger; this bounded record retains identity, ancestry, kind, and
 * scheduler-visible state for this teaching kernel. */
enum task_state { TASK_RUNNING=0, TASK_INTERRUPTIBLE=1, TASK_UNINTERRUPTIBLE=2,
                  TASK_STOPPED=4, TASK_TRACED=8, EXIT_DEAD=16, EXIT_ZOMBIE=32 };
enum task_kind { TASK_KIND_KERNEL=1, TASK_KIND_USER=2 };
struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; };
#define TASK_TABLE_CAP 4U
static struct task_struct task_table[TASK_TABLE_CAP];
```

逐行注释：
- 头注释直接声明这是 `task_struct` 的**教学类比**，且刻意很小；
- `enum task_state`：7 个状态值，其中 0/1/2/4/8/16/32 是 2 的幂，为将来
  位组合留接口；`EXIT_ZOMBIE=32` 与 Linux 的 `EXIT_ZOMBIE` 数值语义一致
  （Linux 在 `include/linux/sched.h` 中 `EXIT_DEAD=0x10, EXIT_ZOMBIE=0x20`）；
- `enum task_kind`：1=内核任务，2=用户任务（TinyOS 特有，Linux 用进程上下文
  区分，不放在状态位里）；
- `struct task_struct`：3 个 u64（身份）+ 4 个 u8（种类/状态/迁移计数/有效位），
  紧凑到 32 字节（3×8+4×1=28，对齐到 32）；
- `TASK_TABLE_CAP 4U`：固定容量，没有动态分配、没有链表；
- `task_table`：唯一的任务表实例，`static` 全局。

### 3.3 kernel64.c：状态机函数精讲

**`task_state_name` / `task_kind_name` / `task_state_valid`**（打印与合法性）

```c
static TEXT64 const char *task_state_name(u8 s){return s==TASK_RUNNING?"running":
    s==TASK_INTERRUPTIBLE?"interruptible":s==TASK_UNINTERRUPTIBLE?"uninterruptible":
    s==TASK_STOPPED?"stopped":s==TASK_TRACED?"traced":s==EXIT_ZOMBIE?"zombie":
    s==EXIT_DEAD?"dead":"invalid";}
static TEXT64 int task_state_valid(u8 s){return s==TASK_RUNNING||s==TASK_INTERRUPTIBLE||
    s==TASK_UNINTERRUPTIBLE||s==TASK_STOPPED||s==TASK_TRACED||s==EXIT_ZOMBIE||s==EXIT_DEAD;}
```

分析：
- `task_state_name` 是「打印映射」：把状态位翻译成 `tasklist` 显示用的英文名，
  未识别值显示 `invalid`，保证任何位值都有可读输出；
- `task_state_valid` 是「白名单」：只接受 7 个枚举值。`task_transition` 与
  `task_table_validate` 都依赖它做前置校验——这是「先验证再迁移」纪律的第一层；
- 两个函数都是纯函数（无副作用），`TEXT64` 确保进入 `.text64` 段。

**`task_transition`**（状态迁移核心）

```c
static TEXT64 int task_transition(u32 i,u8 next){u8 old;if(i>=TASK_TABLE_CAP||
    !task_table[i].valid||!task_state_valid(next))return 0;old=task_table[i].state;
    if(old==next)return 1;if(old==EXIT_DEAD||old==EXIT_ZOMBIE)return 0;
    if(next==EXIT_DEAD)return 0;task_table[i].state=next;task_table[i].transitions++;return 1;}
```

算法步骤：
1. **越界/未用槽/非法目标态检查**：`i>=TASK_TABLE_CAP`、`!valid`、
   `!task_state_valid(next)` 任一成立直接返回 0（不改变任何状态）；
2. **幂等**：`old==next` 返回 1 但不计数——重复设置同状态不算迁移；
3. **终结态不可逆**：`old` 已是 `EXIT_ZOMBIE`/`EXIT_DEAD` 则拒绝任何迁移；
   `next==EXIT_DEAD` 也一律拒绝（回收语义不属于迁移 API）；
4. 全部通过后写入新状态并 `transitions++`，返回 1。

边界与动机：状态迁移不是「任意赋值」，必须经过白名单与方向性检查。这与 Linux
`__set_current_state` / `__set_task_state` 的「设置前先定义合法状态」思想一致，
但教学模型把合法性检查显式写成了 `task_state_valid`，可读性优先于性能。

**`task_table_validate`**（整表完整性自检）

```c
static TEXT64 int task_table_validate(void){u32 i,j;if(!task_table[0].valid||
    task_table[0].pid!=0||task_table[0].tid!=0||task_table[0].parent_pid!=0)return 0;
    for(i=0;i<TASK_TABLE_CAP;i++){struct task_struct*t=&task_table[i];
    if(!t->valid||((i!=0)&&(!t->pid||!t->tid))||!task_state_valid(t->state)||!t->kind)
    return 0;for(j=i+1;j<TASK_TABLE_CAP;j++)if(task_table[j].valid&&i&&j&&
    (t->pid==task_table[j].pid||t->tid==task_table[j].tid))return 0;
    if(i&&t->parent_pid>=t->pid)return 0;}return 1;}
```

分析（至少 3 行实质分析）：
- **槽 0 特判**：内核任务必须 `pid==tid==parent_pid==0`，它是「无父的任务」，
  因此后续的 parent 约束（`parent_pid < pid`）对槽 0 豁免；
- **逐槽检查**：每个槽必须 `valid`；非槽 0 的 pid/tid 不能为 0（避免与内核任务
  混淆）；状态必须通过 `task_state_valid` 白名单；`kind` 不能为 0；
- **唯一性双重检查**：`for(j=i+1;...)` 对每个槽检查它后面的所有槽，PID 或 TID
  重复即失败（`i&&j` 排除与槽 0 的比较——槽 0 的 pid=0 只允许存在一次）；
- **父子有序**：`parent_pid < pid` 保证「父先于子」，为 lesson-39 的 fork 模型
  里 `child_pid > parent_pid` 规则埋下伏笔；
- 该函数无副作用，可在启动时（失败即 `cli; hlt`）与 `taskvalidate` 命令时各跑一次。

**`tasklist` / `taskvalidate`**（观察命令）

```c
static TEXT64 void tasklist(u16*c){u32 i;text64(c,"tasks: slot pid tid parent kind state transitions\n");
    for(i=0;i<TASK_TABLE_CAP;i++)if(task_table[i].valid){struct task_struct*t=&task_table[i];
    text64(c,"task ");hex64(c,i);text64(c," ");hex64(c,t->pid);text64(c," ");
    hex64(c,t->tid);text64(c," ");hex64(c,t->parent_pid);text64(c," ");
    text64(c,task_kind_name(t->kind));text64(c," ");text64(c,task_state_name(t->state));
    text64(c," ");hex64(c,t->transitions);putc64(c,'\n');}}
static TEXT64 void taskvalidate(u16*c){text64(c,"task validation: ");
    text64(c,task_table_validate()?"passed":"BROKEN");
    text64(c," (bounded table, unique PID/TID, valid parent/state)\n");}
```

分析：`tasklist` 只打印 `valid` 槽，列名与字段一一对应；`taskvalidate` 是
`task_table_validate()` 的一行式报告——成功串与失败串都从源码逐字固定。

### 3.4 kernel64.c：进程生命周期与 task 表的联动

`user_process_enter` / `user_process_exit` 在原有逻辑上新增两行：

```c
static TEXT64 int user_process_enter(struct long_mode_handoff *h)
{
    if(!h || user_process.state!=PROCESS_READY || user_thread.state!=USER_THREAD_READY) return 0;
    user_process.state=PROCESS_RUNNING; user_thread.state=USER_THREAD_RUNNING;
    (void)task_transition(1,TASK_RUNNING);
    user_thread.transitions++; return 1;
}
static TEXT64 int user_process_exit(void)
{
    if(user_process.state!=PROCESS_RUNNING || user_thread.state!=USER_THREAD_RUNNING ||
       !user_context_valid(&user_thread.context)) return 0;
    user_process.state=PROCESS_EXITED; user_thread.state=USER_THREAD_EXITED;
    (void)task_transition(1,EXIT_ZOMBIE);
    user_thread.transitions++; user_exit_count++; return 1;
}
```

- `(void)task_transition(1,TASK_RUNNING)`：槽 1 是用户进程 1 的档案；进入 CPL3
  用户态时同步把它的 Linux 状态置为 RUNNING；
- `(void)task_transition(1,EXIT_ZOMBIE)`：用户程序请求退出时，进程对象进入
  `PROCESS_EXITED`、线程对象进入 `USER_THREAD_EXITED`，档案同步置为僵尸——
  这是 Linux「进程退出先变僵尸、等父进程回收」的教学映射；
- 迁移失败不影响原有生命周期（原代码已校验前提条件，返回值被 `(void)` 丢弃）。

### 3.5 kernel_main64_binary：启动期初始化与自检

```c
    task_table[0].pid=0; task_table[0].tid=0; task_table[0].parent_pid=0;
    task_table[0].kind=TASK_KIND_KERNEL; task_table[0].state=TASK_RUNNING; task_table[0].valid=1;
    task_table[1].pid=FIXED_PID; task_table[1].tid=FIXED_PID; task_table[1].parent_pid=0;
    task_table[1].kind=TASK_KIND_USER; task_table[1].state=TASK_INTERRUPTIBLE; task_table[1].valid=1;
    task_table[2].pid=SECOND_PID; task_table[2].tid=SECOND_PID; task_table[2].parent_pid=0;
    task_table[2].kind=TASK_KIND_USER; task_table[2].state=TASK_INTERRUPTIBLE; task_table[2].valid=1;
    task_table[3].pid=3; task_table[3].tid=3; task_table[3].parent_pid=0;
    task_table[3].kind=TASK_KIND_KERNEL; task_table[3].state=TASK_RUNNING; task_table[3].valid=1;
    if(!task_table_validate()) for(;;)__asm__ volatile("cli; hlt");
```

- 四个槽硬编码初始化：0=内核（pid 0），1/2=继承的两个用户程序（`FIXED_PID=1`、
  `SECOND_PID=2`，初始为 `TASK_INTERRUPTIBLE` 表示「等待被用户态进入唤醒」），
  3=内核任务（pid 3，`TASK_RUNNING`）；
- 初始化完立刻 `task_table_validate()`；失败进入 `cli; hlt` 永久停机——
  这是「启动期自检」而非「运行时恢复」，与教学模型不引入容错机制的原则一致；
- `exec64` 新增 `tasklist` / `taskvalidate` 两个分支（help 列表未同步更新，
  属源码既有事实）。

### 3.6 构建管线（Makefile / linker）

Makefile 与本课无新增目标，管线与 lesson-36 完全一致：

| 目标 | 含义 |
|---|---|
| `build/kernel64.o` | `-m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone` 编译 64 位内核 |
| `build/kernel64.bin` | `ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary` 得裸 continuation |
| `build/kernel.elf` | `-m32` 引导部分 + `ld -m elf_i386 -T linker.ld` 链接外层 ELF32 |
| `build/kernel.iso` | `grub-mkrescue` 打包可启动 CD |
| `check` | `grub-file --is-x86-multiboot2` 校验 Multiboot2 header |
| `run` | `qemu-system-x86_64 -accel tcg -cdrom build/kernel.iso` |

`-mno-red-zone`：64 位内核不依赖红区（中断会覆盖），沿用旧课；
`kernel64.ld` 的 `KEEP`/`ALIGN` 与栈守卫段（idle/rsp0/ist1）全部不变。

### 3.7 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode
  → kernel_main64 (boot.S) → kernel_main64_binary (kernel64.c)
      pmm_init / address_space_init
      task_table[0..3] 初始化 → task_table_validate（失败 cli;hlt）
      runtime_gdt_tss_init / install_idt / pit_init / pic_init
      打印继承自 lesson-36 的横幅
      键盘循环：enter → exec64 → tasklist / taskvalidate / processinfo / ...
```

---

## 4. 数据流与运行逻辑

以 `taskvalidate` 为例串起完整路径：

```text
键盘输入 "taskvalidate\n"
  → kbd_dequeue 取到命令 → exec64(word="taskvalidate")
  → taskvalidate(c) → task_table_validate()
      ├─ 槽 0：pid/tid/parent 必须全 0；每槽：valid / pid-tid 非 0 / 状态合法 / kind
      ├─ 两两：PID/TID 唯一；非 0 槽：parent_pid < pid
  → 输出串（逐字）："task validation: passed (bounded table, unique PID/TID, valid parent/state)"
  → VGA 该行下方回到 "tinyos> " 提示符
```

`tasklist` 的数据来源是同一张 `task_table`：只打印 `valid` 槽，每行格式
`task <slot> <pid> <tid> <parent> <kind> <state> <transitions>`。
`processinfo` 仍读 `user_process`/`user_thread`；`userpitest`/`cpl3test` 触发
`user_process_enter`，会连带把 `task_table[1]` 迁移到 `TASK_RUNNING`——
所以「先跑 `userpitest` 再 `tasklist`」能看到槽 1 状态变为 running。

---

## 5. 构建、运行与验证

### 5.1 依赖

与旧课相同：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 header check passed.`。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：

```text
TinyOS lesson 36: validated user image with bounded PIT preemption
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

注意：横幅与 `about` 文本仍沿用 lesson-36 的字符串（源码事实：本课只新增
`tasklist`/`taskvalidate` 分支，未改写横幅）。

在 `tinyos> ` 后依次输入：

```bash
tasklist
```

预期（源码 `tasklist` 输出格式，具体地址/计数按运行变化）：

```text
tasks: slot pid tid parent kind state transitions
task 0 0000000000000000 0000000000000000 0000000000000000 kernel running 0000000000000000
task 1 0000000000000001 0000000000000001 0000000000000000 user interruptible 0000000000000000
task 2 0000000000000002 0000000000000002 0000000000000000 user interruptible 0000000000000000
task 3 0000000000000003 0000000000000003 0000000000000000 kernel running 0000000000000000
```

```bash
taskvalidate
```

预期输出串（源码逐字）：`task validation: passed (bounded table, unique PID/TID, valid parent/state)`

继承自 lesson-36 的回归验证：

```bash
processtest     # process lifecycle: bounded two bounded program objects ready
processinfo     # process pid/state / address-space / image code/stack ...
cpl3test        # 进入 CPL3 跑 0,1,2,99,3；EXIT 后用户进程变僵尸（槽 1 状态 zombie）
```

再用 `tasklist` 复查：槽 1 的状态应已从 `interruptible` 迁移为 `zombie`、
`transitions` 变为 1（首次迁移），证明状态机与进程生命周期已联动。
`syscallinfo` 与 `threadinfo` 确认继承的 syscall ABI、PIT 频率与调度策略不变。

### 5.4 课程实测记录（2026-08，稳定快照）

`make clean && make -j` 与 `make check` 通过；QEMU 中 `tasklist` 显示 4 槽，
`taskvalidate` 输出 `passed`；`cpl3test`（内含 EXIT）后 `tasklist` 槽 1 显示
`zombie`，`transitions` 计数为 1；`processtest`/`processinfo` 输出与 lesson-36
一致。所有观察均在 QEMU 图形窗口完成，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `taskvalidate` 显示 `BROKEN` | `task_table` 初始化不一致（pid/tid 重复、parent 无序、状态非法） | 核对 `kernel_main64_binary` 中 4 槽赋值；对照 `task_table_validate` 的每条规则 |
| 开机黑屏/直接停机 | `task_table_validate()` 失败进入 `cli; hlt` | 用 QEMU 调试器断在 `kernel_main64_binary` 检查 `task_table` 内容 |
| 跑 `cpl3test` 后槽 1 状态仍是 interruptible | 用户态未真正进入（`user_process_enter` 前提不满足） | 先确认 `processinfo` 显示进程 `ready`；检查 IRQ0 帧路径 |
| `tasklist` 无输出 | `task_table[i].valid` 全为 0 | 检查初始化是否执行到 `valid=1` 的赋值行 |
| `transitions` 一直为 0 | 状态迁移条件不满足或幂等（`old==next` 不计数） | 复习 `task_transition`：只有合法且非幂等迁移才计数 |
| 命令无响应 | 键盘扫描码未映射 / IRQ1 未开 | `kbdinfo` 查 raw bytes；确认 `pic_masks` 主片掩码 |
| help 列表里找不到 `tasklist` | 本课未同步更新 help 字符串（源码事实） | 直接输入 `tasklist` 仍可用；分支在 `exec64` 中已存在 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `include/linux/sched.h`**（`struct task_struct` 与任务状态位
的规范来源）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `enum task_state` 的位值 0/1/2/4/8/16/32 | `include/linux/sched.h` 的 `TASK_RUNNING`/`TASK_INTERRUPTIBLE`/`TASK_UNINTERRUPTIBLE`/`TASK_STOPPED`/`TASK_TRACED`/`EXIT_DEAD`/`EXIT_ZOMBIE` | 位值与 Linux 一致：`EXIT_DEAD=0x10`、`EXIT_ZOMBIE=0x20` |
| `struct task_struct { pid,tid,parent_pid; ... }` | `task_struct` 的 `pid`、`tgid`、`real_parent->tgid` | 教学模型把 TGID 简化成 `pid`，且未区分进程/线程组 |
| `task_transition(i,next)` | `kernel/sched/core.c` 中 `__set_current_state` 等状态设置路径 | Linux 状态位可 OR 组合、可被抢占回滚；教学模型只做单向白名单迁移 |
| 退出 → `EXIT_ZOMBIE` | `kernel/exit.c` 的 `do_exit()` 置 `EXIT_ZOMBIE`，父进程 `wait()` 后回收 | 教学模型由 `user_process_exit` + `user_program_reclaim` 模拟 |
| `task_table[4]` 固定数组 | `task_struct` 由 `PIDTYPE_PID` 哈希表、`init_task` 链表组织 | **教学模型用固定槽替换动态 pid 分配与链表**；不实现 clone、不动态创建任务 |
| `task_table_validate` 启动自检 | 无直接对应（Linux 靠 `BUG_ON`/`WARN_ON` 断言） | 教学模型把不变量集中成一个显式函数，失败即停机 |

**权威来源**：Intel SDM（long mode/中断）、Multiboot2 规范（交接）、
GNU GRUB（装载）——本课不引入新硬件机制，三者只作为既有机制背景。

**教学模型简化了什么**：
1. `task_struct` 从数百字段缩到 7 个字段；2. 无 `fork`/`clone`，pid 分配是
  硬编码（0/1/2/3）；3. 状态位不 OR 组合，只存单值；4. 无调度器联动——真正的
   调度仍由 `irq0_schedule` 的 `struct thread` 完成，`task_table` 是**元数据层**；
5. Linux 中 `TASK_RUNNING` 是 0 因而能「在位图中留 0 表示就绪」，教学模型继承
   这个值但不再做位图。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `TASK_RUNNING` 取值 0？如果把它改成 4，`task_state_valid`
   与 `task_state_name` 需要改哪几处？
2. **源码定位**：在 `kernel64.c` 中找出所有调用 `task_transition` 的位置，说明
   每个位置代表 Linux 的哪一次状态迁移（进入用户态 / 退出成僵尸）。
3. **动手实验**：把 `task_table_validate` 中的 `parent_pid>=pid` 规则临时改成
   `parent_pid>pid`（或删掉），重新构建运行 `taskvalidate`，观察输出从
   `passed` 变为 `BROKEN`（可改后改回，勿提交）。
4. **Linux 对照**：打开本机 `include/linux/sched.h`（或在线源码），找出
   `EXIT_DEAD` 与 `EXIT_ZOMBIE` 的宏定义值，与 `enum task_state` 核对。
5. **设计思考**：如果要给 `task_table` 增加第 5 个槽并让 `tasklist` 显示它，
   需要修改哪几个文件/哪些函数？（提示：容量宏、初始化、验证规则、命令分支）

---

## 9. 本课小结与下一课预告

**小结**：本课完成了从「课程自有 TCB」到「Linux 风格任务描述」的过渡——引入了
`enum task_state`（位值抄录 Linux）、`struct task_struct`（pid/tid/parent/kind/
state/transitions/valid）、固定 4 槽 `task_table`、白名单校验 `task_state_valid`、
单向迁移 `task_transition`、整表自检 `task_table_validate`，并用
`tasklist`/`taskvalidate` 命令观察；`user_process_enter/exit` 会把进程生命周期
同步到档案上（running / zombie）。本课**只加元数据，不改调度**。

**下一课预告**：进入 [`lesson-38-stable/README.md`](../lesson-38-stable/README.md)，
在既有 `wait_queue`（事件/信号量/键盘等待）之上引入 Linux 风格的 `sched_class`
抽象（`pick_next`/`enqueue`/`dequeue` 函数指针表）与 `waitq_enqueue`/
`waitq_dequeue` 命名对齐，并用 `schedinfo` 观察三个操作计数。

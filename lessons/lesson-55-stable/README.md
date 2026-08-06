# Lesson 55: 阻塞 wait/wake 与 WNOHANG — 精讲文档

> **课号**：55　**主题**：阻塞等待（wait-before-exit）与唤醒发布、WNOHANG 非阻塞观察
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）
> **前置课程**：[lesson-54-stable/README.md](../lesson-54-stable/README.md)（shell wait、exit status 与 zombie 收割）
> **后续课程**：[lesson-56-stable/README.md](../lesson-56-stable/README.md)（init adoption 与有界父进程重挂接）
> **一句话目标**：在既有「running → zombie → dead」收割模型之上，为 wait 补充「子进程未退出时父进程阻塞、子进程退出时 wake-one 唤醒、以及 WNOHANG 非阻塞探询」两种语义。

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 55 extends the bounded parent/child wait model with a wait-before-exit blocked state, exit wake-one publication, retry, and nonblocking `WNOHANG` observation. The model remains fixed-capacity metadata: no scheduler-wide wait set, dynamic allocation, arbitrary user pointers, or real child execution.

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说清「子进程还没退出时，`wait` 为什么会让父进程‘阻塞’；子进程 `exit` 之后，这个阻塞怎么被一次性唤醒；而带上 `WNOHANG` 时又该如何立刻返回‘还没就绪’」——并能用 `waitblocktest`、`nohangtest` 两条确定性命令在 VGA 上验证这三件事。
- **在课程主线中的位置**：本课属于第 3 阶段（32–60），位于里程碑课程 52（综合用户空间）与 54（shell wait/zombie）之后、56（init adoption）之前。第 54 课建立了「一个固定父 + 一个固定子」的 wait 关系与 `EXIT_ZOMBIE → EXIT_DEAD` 状态机，但那个模型里 **wait 只能发生在子进程已退出之后**（`wait_model_wait` 只在 `state==WAIT_ZOMBIE` 时成功）。本课补上「子进程还在运行、父进程提前来等」的中间状态，为第 56 课的孤儿收养和第 58 课的多子进程 waitpid 打基础。
- **前置知识清单**（学本课之前必须掌握）：
  1. 第 54 课的 `wait_model`：`WAIT_RUNNING / WAIT_ZOMBIE / WAIT_DEAD` 三态与 `wait_model_exit / wait_model_wait / wait_model_reap` 的调用顺序；
  2. 第 38 课的 wait queue 原语：`waitq_enqueue / waitq_dequeue / waitq_wake_one / waitq_wake_all` 的 FIFO 语义与 `WAIT_QUEUE_CAP` 上界；
  3. `kernel_main64_binary` 的初始化顺序（`wait_model_start()` 在进入 shell 循环前被调用）；
  4. `exec64` 命令分发的 `token64 / eq64 / noargs64 / hex64` 等基础设施。
- **本课交付（可见结果）**：三条新命令 `waitblockinfo`、`waitblocktest`、`nohangtest`，屏幕上分别打印计数值与「blocked wait, exit wake-one, status retry, and reap passed」/「WNOHANG empty/ready results and one-shot reap passed」两类确定性验证串（完整串见 §5）。

## 2. 核心概念精讲

### 2.1 阻塞等待（blocking wait）与「wait-before-exit」

- **直觉**：`wait` 的语义是「父进程要等子进程死掉，好回收它的资源」。如果子进程还活着，`wait` 调用不能立即返回，否则父进程拿不到任何状态；它应当挂起自己，直到子进程退出那一刻被唤醒。这就像车站接人：人没到就一直在站台等（阻塞），人到了才接上并离开（返回）。
- **准确定义**：阻塞 wait =「在子进程状态 ≠ `WAIT_ZOMBIE` 时，调用方不返回、进入 blocked 状态；当子进程 exit 发布 `WAIT_ZOMBIE` 时，恰好唤醒一个等待者」。教学模型里没有真正的调度器休眠，它用 `wait_block_model` 的 `blocked/woken` 两位标志 + 计数来「模拟」这个挂起与唤醒，因为本阶段是**固定容量的元数据模型**：不建 scheduler-wide 等待集、不动态分配、不执行真实子进程。
- **为什么需要**：真实 Linux 中 `wait` 系列系统调用（`kernel/exit.c` 的 `wait_consider_task`、`do_wait`）必须在子进程是 zombie 前就把调用方放进等待队列；否则父进程 `wait` 比子进程 `exit` 早一点就会永远错过状态。第 54 课模型「wait 只在 zombie 时成功」省略了这半段，本课把它补齐。
- **示意图**：

```
父进程 wait() 时刻:
   子进程状态 = WAIT_RUNNING ──► 父进程 blocked=1, blocks++
                              │
   （时间流逝，无内核动作）    │
                              ▼
   子进程 exit(17)            │  wait_block_exit():
      state → WAIT_ZOMBIE ────┼──► blocked=0, woken=1, wakes++
                              ▼
   父进程 retry wait()        │  state==WAIT_ZOMBIE → 成功返回 1
```

### 2.2 exit wake-one：谁唤醒谁

- 本课用一个**布尔标志 `blocked`** 模拟等待队列里「有没有人在等」。`wait_block_exit()` 在子进程状态仍是 `WAIT_RUNNING` 时执行 `wait_model_exit(17)` 发布退出，随后检查 `wait_block_model.blocked`：若确实有人阻塞，就把 `blocked` 清掉、置 `woken=1`、`wakes++`。这正好对应第 38 课 `waitq_wake_one` 的「一次只唤醒队首一个等待者」语义——本模型只有一个父进程，因此「wake-one」退化为「置一个标志」。
- **为什么是 17**：`wait_block_exit()` 里写死 `wait_model_exit(17)`，与第 54 课测试用 42、第 59 课用 23 一样，都是「固定退出码教学常量」。验证时从 `wait_model.exit_code` 读回 17，确认状态发布确实生效。

### 2.3 WNOHANG 非阻塞等待

- **直觉**：`WNOHANG` 的意思是「我不愿意等：子进程没退出就立刻返回‘没就绪’，绝不挂起」。Linux `waitpid(2)` 手册：`WNOHANG` — return immediately if no child has exited.
- **本课实现**：`wait_block_wait(u8 nohang)` 是一个函数两种模式。`nohang!=0` 时它**不做任何阻塞标志操作**，只统计 `nohang_calls++`，然后直接读 `wait_model.state==WAIT_ZOMBIE` 作为返回值：子进程已退出返回 1，还没退出返回 0。`nohangtest` 依次验证「空结果（子进程 running）→ WNOHANG 返回 0」与「就绪结果（子进程 zombie）→ WNOHANG 返回 1 且能读到 exit_code=17」。
- **与阻塞模式的对比**：

| 模式 | 子进程 running | 子进程 zombie | 副作用 |
|---|---|---|---|
| 阻塞（`nohang=0`） | `blocked=1; blocks++` 返回 0（等 exit 唤醒） | `woken=1` 返回 1 | 修改 `blocked/woken` |
| 非阻塞（`nohang=1`） | 返回 0 | 返回 1 | 只 `nohang_calls++` |

### 2.4 教学模型边界（必须说清）

- **没有真正的休眠**：真实内核里阻塞 wait 会把调用任务放进 `wait queue` 并调度走；本模型只是翻转 `wait_block_model.blocked` 位并数数。子进程也不会真执行——它只是一个 0..65535 的固定元数据记录。
- **没有等待队列与多等待者**：`WAIT_QUEUE_CAP`、`waitq_*` 在第 38 课线程模型里仍存在，但本课的 wait 模型是独立的一对一布尔量，不接入任何调度器。
- **没有动态分配与用户指针**：全部静态全局变量，`kernel64.c` 顶部没有新增 `#include` 或堆分配。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | Multiboot2 header + 32→64 位 long mode 切换、内嵌 `kernel64.bin` | 未变化 |
| `kernel.c` | 32 位阶段：校验用户镜像、构建 `long_mode_handoff` 分页结构 | 未变化 |
| `kernel64.c` | 64 位内核主体：命令分发 + 本课 wait 阻塞模型 | **新增 `wait_block_model` 及 5 个函数 + 3 条命令分支** |
| `kernel64.ld` | 64 位裸二进制链接：`.text64/.rodata/.data` 与三块守卫栈布局 | 未变化 |
| `linker.ld` | 32 位外 ELF 链接：Multiboot2 header 对齐、可写段分页 | 未变化 |
| `Makefile` | 双编译器构建 + `check` 关键词校验 | **`check` 校验串改为本课关键词**（`blocking wait`/`WNOHANG`/`wait/wake`/`waitblocktest`） |
| `grub.cfg` | GRUB 菜单项（menuentry 文案仍写 Lesson 52） | 未变化 |

### 3.2 结构与常量精讲（本课新增）

以下代码位于 `kernel64.c` 第 421–429 行，紧随第 54 课的 `wait_model` 块之后：

```c
struct wait_block_model { u64 blocks,wakes,nohang_calls,ready_checks; u8 blocked,woken; };
static struct wait_block_model wait_block_model;
```

- `blocks`：阻塞等待发生过几次（每次「子进程 running 时来 wait」+1）；
- `wakes`：exit 唤醒发生过几次（`wait_block_exit` 成功把 blocked 置回非阻塞 +1）；
- `nohang_calls`：WNOHANG 模式被调用几次；
- `ready_checks`：每次进入 `wait_block_wait`（两种模式都算）+1，用于区分「有没有人来查」；
- `blocked`/`woken`：两位布尔状态，模拟「当前是否有一个父进程阻塞」「是否被唤醒过」。

为什么是 `u8` 位标志 + `u64` 计数器？因为本阶段是确定性元数据模型：布尔位承载「状态机跳转」，计数器承载「可验证的观测账本」，两者分离让 `waitblockinfo` 可以逐项打印、`waitblocktest` 可以逐断言核对。

### 3.3 函数精讲

#### `wait_block_start` — 复位记账

```c
static TEXT64 void wait_block_start(void){wait_block_model=(struct wait_block_model){0,0,0,0,0,0};}
```

- 签名与职责：无参，把 `wait_block_model` 整体清零（6 个字段按声明顺序：`blocks, wakes, nohang_calls, ready_checks, blocked, woken`）。
- 设计原因：与 `wait_model_start()` 成对使用。每条测试命令（`waitblocktest`/`nohangtest`）开头都会先复位 `wait_model` 再复位 `wait_block_model`，保证测试彼此独立、可重复执行，这是「确定性验证」的硬前提。
- 边界：因为是结构体整体赋值，不存在漏清字段的问题；但它**不会**复位 `wait_model`，调用方必须自行先调 `wait_model_start()`。

#### `wait_block_wait` — 两种 wait 模式的核心

```c
static TEXT64 int wait_block_wait(u8 nohang){
    wait_block_model.ready_checks++;                     /* 每次查询都记账：调用者确实来了 */
    if(nohang){                                          /* WNOHANG：非阻塞探询 */
        wait_block_model.nohang_calls++;
        return wait_model.state==WAIT_ZOMBIE;            /* 直接读状态，绝不阻塞 */
    }
    if(wait_model.state!=WAIT_ZOMBIE){                   /* 子进程还没退出：进入阻塞 */
        wait_block_model.blocked=1;
        wait_block_model.blocks++;
        return 0;
    }
    wait_block_model.woken=1;                            /* 子进程已 zombie：视为被唤醒 */
    return 1;
}
```

- 算法步骤（编号）：
  1. 无条件 `ready_checks++`，记录一次 wait 调用；
  2. 若 `nohang` 非零：`nohang_calls++`，返回「当前状态是否为 `WAIT_ZOMBIE`」，不做任何阻塞标记；
  3. 否则若状态不是 `WAIT_ZOMBIE`：置 `blocked=1`、`blocks++`，返回 0（模拟「挂起」）；
  4. 否则（状态已是 `WAIT_ZOMBIE`）：置 `woken=1`，返回 1。
- 边界与错误处理：不检查参数有效性以外的任何东西——`wait_block_wait(0)` 在子进程 running 时返回 0，但**不会**死循环，因为模型不做真实调度；`wait_block_exit()` 的唤醒是测试脚本显式驱动的。
- 为什么这样设计：把「阻塞」与「唤醒」解耦成两个独立函数（wait 只管置标志，exit 只管翻标志），让 `waitblocktest` 可以用「阻塞→exit→重试→收割」的调用序列精确复现 Linux 中 `wait 先到、exit 后到、父进程被唤醒后重试收割` 的时序。对照 Linux：`do_wait` 在 `wait_consider_task` 未找到可收割子进程时返回 `0`，调用方 `wait4` 决定是否 `schedule_timeout` 挂起——本模型的 `return 0` + `blocked` 标志就是这一步的元数据投影。

#### `wait_block_exit` — exit 时的 wake-one 发布

```c
static TEXT64 void wait_block_exit(void){
    if(wait_model.state==WAIT_RUNNING){                  /* 只允许从 running 进入 zombie */
        wait_model_exit(17);                             /* 发布退出状态 17 */
        if(wait_block_model.blocked){                    /* 若有人阻塞在 wait 上 */
            wait_block_model.blocked=0;                  /* 清阻塞 */
            wait_block_model.woken=1;                    /* 标记已唤醒 */
            wait_block_model.wakes++;                    /* 唤醒计数 +1 */
        }
    }
}
```

- 算法步骤：① 检查子进程还在 `WAIT_RUNNING`（否则不动作）；② 调 `wait_model_exit(17)` 把状态推进到 `WAIT_ZOMBIE` 并记录 exit_code；③ 若 `blocked` 为真，执行「清除阻塞 + 置 woken + wakes++」，完成一次 wake-one。
- 为什么这样设计：它把「子进程退出」和「唤醒等待者」放在同一次调用里，模拟 Linux `do_exit → exit_notify → wake_up_parent` 的发布顺序。`woken` 与 `wakes` 的区分很重要：`woken` 是「状态位」（供 `waitblocktest` 断言），`wakes` 是「累计账本」（供 `waitblockinfo` 展示）。因为只允许从 `WAIT_RUNNING` 进入，`wait_block_exit` 天然防止了对同一个 zombie 重复发布——这对应 Linux 里 `exit_notify` 只执行一次。

#### `waitblockinfo` — 记账展示

```c
static TEXT64 void waitblockinfo(u16*c){text64(c,"wait block/blocked/wakes/nohang/checks: ");hex64(c,wait_block_model.blocks);text64(c,"/");hex64(c,wait_block_model.blocked);text64(c,"/");hex64(c,wait_block_model.wakes);text64(c,"/");hex64(c,wait_block_model.nohang_calls);text64(c,"/");hex64(c,wait_block_model.ready_checks);putc64(c,'\n');}
```

- 输出格式串（逐字）：`"wait block/blocked/wakes/nohang/checks: "`，随后以 `/` 分隔打印 `blocks`、`blocked`、`wakes`、`nohang_calls`、`ready_checks` 五个十六进制值，最后换行。这是 §5 验证时可以对照源码逐字抄录的串之一。
- 设计原因：与第 54 课 `waitinfo`（打印 parent/child/state/code/calls/reaps）互补——`waitinfo` 看「子进程侧」，`waitblockinfo` 看「父进程阻塞侧」。

#### `waitblocktest` — 阻塞语义的确定性验证

```c
static TEXT64 void waitblocktest(u16*c){
    wait_model_start();wait_block_start();               /* 双双复位 */
    int a=!wait_block_wait(0),                           /* a: 子进程 running 时阻塞 wait 返回 0 */
        b=wait_block_model.blocked,                      /* b: blocked 位确实置 1 */
        k=!wait_block_wait(1),                           /* k: WNOHANG 下 running 子进程返回 0 */
        d=wait_block_model.nohang_calls==1;              /* d: nohang_calls 恰好 1 */
    wait_block_exit();                                   /* 子进程 exit(17)，触发 wake-one */
    int e=wait_block_wait(0),                            /* e: 状态已 zombie，重试 wait 返回 1 */
        f=wait_block_model.wakes==1&&wait_block_model.woken; /* f: 唤醒计数 1 且 woken 位为真 */
    wait_model.waited=1;                                 /* 标记 wait 已完成（收割前置） */
    int g=wait_model_reap();                             /* g: 收割成功，state → WAIT_DEAD */
    text64(c,"waitblocktest: ");text64(c,a&&b&&k&&d&&e&&f&&g?"blocked wait, exit wake-one, status retry, and reap passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`：在 `WAIT_RUNNING` 下阻塞 wait 必须返回 0（还没就绪）；
  - `b`：同时 `blocked` 必须为真（确实进入了阻塞态）；
  - `k`：`wait_block_wait(1)`（WNOHANG）在 running 下也返回 0，但**不**置 blocked——这一步和 `a` 的区别正是本课的核心：返回 0 的原因不同（一个阻塞、一个非阻塞探询）；
  - `d`：`nohang_calls==1` 确认 WNOHANG 只被调用了一次；
  - `e`：`wait_block_exit()` 之后子进程变 zombie，此时阻塞 wait 重试直接返回 1（被唤醒后看到就绪）；
  - `f`：唤醒账本 `wakes==1` 且 `woken` 为真（exit 时确实发布了 wake-one）；
  - `g`：最终 `wait_model_reap()` 成功，走完 `WAIT_ZOMBIE → WAIT_DEAD`。
- 验证输出串（逐字）：全部 7 个断言为真时输出 `"blocked wait, exit wake-one, status retry, and reap passed"`，否则输出 `"BROKEN"`，前缀均为 `"waitblocktest: "`。这个成功串在第 54 课基础上新增了 "blocked wait" 与 "wake-one" 两个语义词，是对本课增量的直接描述。

#### `nohangtest` — WNOHANG 两种结果的验证

```c
static TEXT64 void nohangtest(u16*c){
    wait_model_start();wait_block_start();               /* 双双复位 */
    int a=!wait_block_wait(1),                           /* a: WNOHANG 空结果（running）返回 0 */
        b=wait_block_model.nohang_calls==1;              /* b: nohang_calls 恰好 1 */
    wait_block_exit();                                   /* 子进程 exit(17) */
    int d=wait_block_wait(1),                            /* d: WNOHANG 就绪结果返回 1 */
        e=d&&wait_model.exit_code==17;                   /* e: 能读到发布的退出码 17 */
    wait_model.waited=1;
    int f=wait_model_reap();                             /* f: one-shot 收割 */
    text64(c,"nohangtest: ");text64(c,a&&b&&e&&f?"WNOHANG empty/ready results and one-shot reap passed":"BROKEN");putc64(c,'\n');
}
```

- 逐断言分析：
  - `a`：WNOHANG 在子进程 running 时返回 0——注意此时 `blocked` **保持 0**（非阻塞绝不挂起），`waitblocktest` 中的 `k` 与此对称；
  - `b`：`nohang_calls==1` 记录一次非阻塞调用；
  - `d`：`wait_block_exit()` 把子进程变 zombie 后，同一 `nohang=1` 调用返回 1（就绪）；
  - `e`：通过 `wait_model.exit_code==17` 确认「非阻塞观察到的就是 exit 发布的退出码」，验证了状态发布对两种模式可见；
  - `f`：收割仍然 one-shot，`wait_model_reap()` 成功。
- 验证输出串（逐字）：成功时 `"WNOHANG empty/ready results and one-shot reap passed"`，失败 `"BROKEN"`，前缀 `"nohangtest: "`。

#### 命令分支（`exec64` 内新增，kernel64.c 第 654 行区域）

```c
}else if(eq64(word,"waitblockinfo")){if(!noargs64(arg))usage64(c,"waitblockinfo");else waitblockinfo(c);}
}else if(eq64(word,"waitblocktest")){if(!noargs64(arg))usage64(c,"waitblocktest");else waitblocktest(c);}
}else if(eq64(word,"nohangtest")){if(!noargs64(arg))usage64(c,"nohangtest");else nohangtest(c);}
```

- 三条分支插在既有 `waittest`/`reaptest` 分支之后、`anoninfo` 分支之前，遵循 exec64 一贯的「`noargs64` 校验 → `usage64` 提示 / 直接调用」模式。注意 `help` 命令的固定文案**未更新**（仍是旧命令列表），这是历史遗留，不影响新命令可用性。

### 3.4 构建管线（Makefile / linker）

- 本课构建管线相对上一课**没有任何新目标或新编译标志**：仍是 `CFLAGS := -m32 -ffreestanding -fno-pie ...`（32 位外核）+ `CFLAGS64 := -m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone -mno-sse ...`（64 位内核）+ `LDFLAGS := -m elf_i386 -T linker.ld -nostdlib`。
- `-mno-red-zone` 保护：64 位内核在中断/系统调用路径上不依赖红区，保证 `irq0_schedule` 等基于栈帧的切换安全；`-ffreestanding` 禁止引入 libc，全内核无任何库调用。
- `kernel64.ld` 的 `ASSERT(...==0x1000, ...)` 确保 idle/rsp0/IST1 三块栈守卫布局不变（本课未触碰）。
- **相对上一课的新增构建步骤**：无。唯一变化在 `check` 目标的校验串（见 §5）：把上一课的 `parent`/`waitinfo`/`zombie`/`bounded shell wait` 关键词替换为 `blocking wait`/`waitblocktest`/`WNOHANG`/`wait/wake`。这说明本课属于「行为增量、构建零增量」的课程——每次稳定快照只改 README 校验关键词，构建产物不变。

### 3.5 主控制流

```
GRUB → boot.S _start → kernel_main32 (kernel.c)
     → enter_long_mode → kernel_main64 (boot.S .text64 段)
     → kernel_main64_binary:
         task_names_keep(); active_sched_class=...; module_init_model();
         init_model_start(); wait_model_start();        ← 既有 wait 模型就位
         pmm_init(h); vma_init(); reclaim_init(); vfs_init();
         address_space_init(...); user_process/thread 初始化;
         install_idt; pit_init; pic_init; clear64; prompt64; sti
     → for(;;){ kbd_dequeue → exec64(c,h,cmd) }
         waitblockinfo / waitblocktest / nohangtest     ← 本课新分支
```

- 关键点：本课**没有修改** `kernel_main64_binary` 的初始化序列——`wait_block_model` 是零初始化的静态量，且每条测试命令自复位，因此主循环无需任何新增初始化调用。这体现了「记账模型尽量自包含」的设计：新模型不侵入启动路径。

## 4. 数据流与运行逻辑

- **输入命令** → `waitblocktest` → `exec64` 命中 `eq64(word,"waitblocktest")` 分支 → `waitblocktest(c)`。
- 数据流：`wait_model_start()` 复位（parent=FIXED_PID=1, child=SECOND_PID=2, state=WAIT_RUNNING）→ `wait_block_start()` 复位 → 阻塞 wait（`blocks=1, blocked=1`）→ WNOHANG 探询（`nohang_calls=1`）→ `wait_block_exit()` 发布 `exit_code=17` 并置 `state=WAIT_ZOMBIE`、`wakes=1, woken=1` → 重试 wait（返回 1）→ `wait_model.waited=1` → `wait_model_reap()` 置 `state=WAIT_DEAD, reaps=1` → `text64` 打印。
- **屏幕上显示什么**：`waitblocktest: blocked wait, exit wake-one, status retry, and reap passed`（7 断言全真）；`nohangtest: WNOHANG empty/ready results and one-shot reap passed`。`waitblockinfo` 显示形如 `wait block/blocked/wakes/nohang/checks: 1/1/1/1/3`（十六进制）的账本行——具体数值取决于最后一次执行哪条测试，因为 `wait_block_start()` 会清零。

## 5. 构建、运行与验证

**依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`，以及脚本 `../../scripts/qemu-vga-check.sh`（旧 README 验证记录，保留）。本课无新增工具。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
```

`all` 目标链为：`kernel64.o → kernel64.bin → boot.o/kernel.o → kernel.elf → ISO_ROOT 布局 → grub-mkrescue → build/kernel.iso`。

**关键词自检**：

```bash
make check
```

`check` 依次验证：`grub-file --is-x86-multiboot2 build/kernel.elf`、README 含 `shell`/`blocking wait`/`status`/`WNOHANG`/`wait/wake`、kernel64.c 含 `waitblocktest`/`shelltest`/`shellrun`/`waittest`，最后打印 `Multiboot2 and lesson 55 checks passed.`。

**运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口（勿加 `-display none`）。`-serial stdio` 输出仅供诊断。

**VGA 验证**（旧 README 记录，保留并原样引用）：

```bash
../../scripts/qemu-vga-check.sh . waitblockinfo waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

**验证步骤与预期输出**（输出串均从源码逐字抄录）：

1. 输入 `waitblocktest` → 预期屏幕出现
   `waitblocktest: blocked wait, exit wake-one, status retry, and reap passed`
   （源码 428 行：`a&&b&&k&&d&&e&&f&&g?"blocked wait, exit wake-one, status retry, and reap passed":"BROKEN"`）。
2. 输入 `nohangtest` → 预期屏幕出现
   `nohangtest: WNOHANG empty/ready results and one-shot reap passed`
   （源码 429 行：`a&&b&&e&&f?"WNOHANG empty/ready results and one-shot reap passed":"BROKEN"`）。
3. 输入 `waitblockinfo` → 预期形如 `wait block/blocked/wakes/nohang/checks: ` 后跟五个十六进制数（源码 427 行）。
4. 回归命令 `waittest` → `waittest: bounded wait, exit status, zombie selection, and reap passed`（第 54 课，源码 430 行）；`waitinfo` 打印 parent/child/state/code/calls/reaps。

**如何判断成功**：VGA 文本区出现上述成功串；任一断言失败输出 `BROKEN`；`qemu-vga-check.sh` 逐命令注入并检查物理 VGA 文本内存后返回成功。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `waitblocktest` 输出 `BROKEN` 且 `a` 为假 | 子进程状态不是 `WAIT_RUNNING`（上一命令残留 zombie） | 先确认 `wait_model_start()` 已复位；输入 `waitinfo` 看 state 字段 |
| `waitblocktest` 输出 `BROKEN` 且 `f` 为假 | `wait_block_exit()` 未触发 wake（`blocked` 在 exit 前被清掉） | 单步阅读 `wait_block_wait`/`wait_block_exit` 调用顺序：阻塞 wait 必须在 `wait_block_exit()` 之前执行 |
| `waitblocktest` 输出 `BROKEN` 且 `g` 为假 | 收割前置 `wait_model.waited` 未置位 | 检查测试里 `wait_model.waited=1;` 是否在 `wait_model_reap()` 前（源码 428 行顺序） |
| `nohangtest` 输出 `BROKEN` 且 `e` 为假 | `wait_model.exit_code` 不是 17（`wait_block_exit` 未执行或重复执行被拒） | 输入 `waitinfo` 查 code 字段；确认 `state` 从 running 进入 zombie 只发生一次 |
| 命令敲入无响应 | `exec64` 分支顺序错误或 README 与源码不同步 | `make check` 校验 README 关键词；确认 `waitblocktest` 分支插在 `waittest` 之后 |
| `waitblockinfo` 数值与预期不符 | `wait_block_start()` 复位时机（每条测试命令开头） | 在 `waitblocktest` 与 `nohangtest` 之后各跑一次 `waitblockinfo`，观察计数是否只反映最后一次测试 |
| 构建通过但 `make check` 失败 | README 关键词被改写或缺失 | 检查 README 是否含 `blocking wait`、`WNOHANG`、`wait/wake`、`shell`、`status` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现 |
|---|---|---|
| wait 的阻塞语义 | `wait_block_wait(0)` 在子进程非 zombie 时置 `blocked=1` 返回 0 | `kernel/wait.c` 的 waitqueue 机制：`wait_event`/`add_wait_queue` 把当前任务挂起；`do_wait`（`kernel/exit.c`）在 `wait_consider_task` 无果时挂起调用方 |
| exit 时的唤醒 | `wait_block_exit()`：发布 exit_code 17 + 若 `blocked` 则 `woken=1; wakes++` | `kernel/exit.c` 的 `do_exit → exit_notify`，最终通过 `wake_up_parent`/`__wake_up_parent` 唤醒等待的父进程 |
| WNOHANG | `wait_block_wait(1)`：不做阻塞，直接返回 `state==WAIT_ZOMBIE` | `kernel/wait.c` 与 `wait4` 的 `WNOHANG` 标志：`wait_consider_task` 找不到已退出子进程时返回 0 而非挂起（`include/linux/wait.h` 的 `wait_event` 用条件值区分） |
| wait 后必须收割 | 模型强制 `wait_model.waited=1` 后才能 `wait_model_reap()` | Linux `wait4` 成功后 `do_wait` 返回 status，`release_task` 才真正释放 task_struct（`kernel/exit.c`） |
| 等待者个数 | 一对一布尔位（本课），`WAIT_QUEUE_CAP` FIFO 在 38 课 | Linux waitqueue 可容纳多个等待者，`wake_up` 语义（`include/linux/wait.h`） |
| 状态边界 | `wait_block_exit` 只允许 `WAIT_RUNNING → WAIT_ZOMBIE`，防重复发布 | Linux `exit_notify` 对 `EXIT_ZOMBIE` 状态只执行一次 release 发布 |

**权威来源**：Intel SDM（long mode/中断交接）、Multiboot2 规范（`grub-file --is-x86-multiboot2` 校验）、Linux v6.x 源码树 `kernel/exit.c`、`kernel/wait.c`、`kernel/sched/core.c`（sleep/wake 转换边界）。

**教学模型简化了什么**：无真实调度切换（不 `schedule()`）、无 waitqueue 链表（一个布尔标志）、无真实子进程执行、无动态内存、无用户指针解引用。wait 的「阻塞→唤醒→重试→收割」时序被压缩进一个测试函数内、由调用顺序显式驱动。

## 8. 思考题与练习

1. **概念理解**：`waitblocktest` 里的 `k`（`!wait_block_wait(1)`）和 `a`（`!wait_block_wait(0)`）都得到 0，但为什么一个证明「非阻塞」、一个证明「阻塞」？它们分别靠哪个字段区分？
2. **源码定位**：在 `kernel64.c` 中找出 `wait_block_wait` 中 `ready_checks++` 的位置，说明为什么它在 `if(nohang)` 判断**之前**执行，以及这对 `nohangtest` 的 `b` 断言意味着什么。
3. **动手实验**：把 `wait_block_exit()` 里的 `wait_model_exit(17)` 改成 `wait_model_exit(42)`，重新构建运行 `nohangtest`——预期输出会变成什么？`waitblocktest` 会受影响吗？为什么？
4. **动手实验**：删除 `waitblocktest` 中 `wait_model.waited=1;` 这一行，观察 `g`（`wait_model_reap()`）的结果，并用 §6 调试地图解释现象。
5. **Linux 对照**：阅读 Linux `kernel/exit.c` 中 `wait_consider_task`，找出它返回「非 0（可收割）」与「0（无结果）」的两条路径，并说明它们分别对应本课 `wait_block_wait` 的哪个分支。

## 9. 本课小结与下一课预告

- 本课在第 54 课「wait 只能等 zombie」的模型上，补上了「子进程还活着时父进程 wait 会怎样」的完整语义：`wait_block_wait(0)` 进入阻塞态（`blocked=1, blocks++`），`wait_block_exit()` 在子进程 exit 时发布状态并 wake-one（`woken=1, wakes++`），重试后 wait 返回成功，最终 one-shot 收割。
- 你学会了用**两位布尔标志 + 五路计数器**表达一个完整的「阻塞/唤醒/就绪」状态机，并且知道了为什么要把「阻塞行为」和「唤醒发布」拆成两个函数——对应 Linux `do_wait` 挂起与 `exit_notify` 唤醒的职责分离。
- 你验证了 WNOHANG 的两种结果：空（running → 返回 0）与就绪（zombie → 返回 1），并确认了非阻塞模式**绝不**触碰 `blocked` 位——这正是它区别于阻塞 wait 的本质。
- 你也理解了本阶段的模型边界：没有真实调度、没有等待队列链表、没有动态内存，一切都是固定容量的确定性元数据。
- **下一课**（[lesson-56-stable/README.md](../lesson-56-stable/README.md)）将回答一个新问题：如果**父进程自己先退出了**，孤儿子进程由谁来 `wait`？答案是固定的 init 进程（PID 1）收养它——这就是「init adoption 与有界父进程重挂接」，本课的 `wait_model` 将成为那个收养关系的等待端。

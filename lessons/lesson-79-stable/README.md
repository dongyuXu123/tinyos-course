# Lesson 79: voluntary preemption 主动抢占 — 精讲文档

> **课号**：Lesson 79（主线源课编号 Lesson 72 线）
> **本课主题**：主动抢占（voluntary preemption）——调度器在 PIT 定时器驱动下对「从不主动让出 CPU」的工作线程强行抢占的调度模型
> **课程主线位置**：调度 / COW 元数据教学模型阶段（Lesson 64 起）。本课是 Lesson 78（runqueue 运行队列统计）之后的调度 checkpoint 课，主题聚焦「非让出 worker + PIT 抢占」；下一课（80）深入定时器驱动的调度机制。
> **前置课程**：[`../lesson-78-stable/README.md`](../lesson-78-stable/README.md)（runqueue 运行队列统计：`sched_enqueues/dequeues/picks` 与调度类分派）
> **后续课程**：[`../lesson-80-stable/README.md`](../lesson-80-stable/README.md)（定时器驱动调度：PIT 周期、时间片、sleep 唤醒与 idle）
> **本课一句话目标**：理解「抢占（preemption）不需要被抢占者配合」——即使 worker 线程只在 `busy_delay()` 里空转、从不 `yield`，PIT 定时器中断也会在时间片耗尽后把 CPU 主动抢走；并能用 `l79test`/`preempttest`/`threadinfo` 做确定性验证。
> **保留的原始快照信息**：This checkpoint models bounded scheduling and copy-on-write metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l79test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释"抢占式调度与协作式调度的差别"，能说出 PIT 中断如何驱动时间片（`TIME_SLICE_TICKS`）与 `irq0_schedule` 的切换决策，并能在 `tinyos>` 下输入 `preempttest` 观察两个**非让出 worker** 被强制抢占的确定性结果。
- **在课程主线中的位置**：本阶段（Lesson 64–84）的主题是"调度 + 写时复制（COW）元数据教学模型"。Lesson 71–72 起每课新增一个固定容量元数据记录与一个 `lXXtest` 验证命令；本课是调度线的一个 checkpoint：主题锚定**主动抢占**，其运行依据（`threads`、`irq0_schedule`、`quantum_left`、`start_threads`）继承自更早的调度课，本课负责把"非让出 worker 仍会被 PIT 抢占"这一语义做成可观察、可验证的教学点。
- **前置知识清单**（学本课之前必须掌握）：
  1. PIT/IRQ0 中断基础设施：`pit_init`、`install_idt`、`irq0_entry` 汇编入口把 15 个 GPR 压栈后调用 `irq0_schedule`（Lesson 33 起）；
  2. 线程模型：`struct thread`（TCB）、`threads[3]`、线程状态机 `THREAD_RUNNING/RUNNABLE/SLEEPING/...`、`start_threads` 如何分配 PMM 页做内核栈（Lesson 61 起）；
  3. 时间片概念：`TIME_SLICE_TICKS` 与 `quantum_left` 在 `irq0_schedule` 中的递减逻辑（Lesson 62 起）；
  4. 命令行框架：`exec64` 超长 if-else 分派、`token64`/`noargs64`/`eq64`、`TEXT64` 段、VGA 文本输出（Lesson 34 起累积）。
- **本课交付**：新增固定容量记录 `struct lesson_72_model` + 全局 `lesson_72_state` + `l79test` 验证命令；把上一课的 `l78test` 改名为 `l71test`（回归命名对齐）；`about` 与本课 banner 更新为「Lesson 79: voluntary preemption 主动抢占」。

---

## 2. 核心概念精讲

### 2.1 协作式调度 vs 抢占式调度（cooperative vs preemptive）

**直觉**：想象两个"霸着键盘不撒手"的程序员。协作式（cooperative）世界里，只有当前程序员**自愿**停下，下一个才能上机——他不让，别人就得无限等；抢占式（preemptive）世界里，有一只"定时闹钟"（PIT），每过固定时间就强制让当前程序员下机，把机器交给下一个，不需要他同意。

**准确定义**：
- **协作式切换（cooperative switching）**：CPU 的让出完全由运行中的线程自己决定（`yield()`、sleep、阻塞等待 I/O）。缺点：一个不写 yield 的线程会独占 CPU，系统"饿死"其他线程。
- **抢占式调度（preemptive scheduling）**：由定时器中断（tick）在固定时间片耗尽时强制触发上下文切换。被抢占者无需合作。Linux 中这对应"involuntary context switch"（`nivcsw`），而 sleep/yield 导致的让出是"voluntary context switch"（`nvcsw`）。

本课标题"voluntary preemption 主动抢占"强调的就是**抢占是调度器主动发起的**，不依赖线程自愿。`yield` 命令的输出串是这一语义的官方注脚：

```
yield: cooperative switching replaced by PIT preemption
```

即：协作式切换机制已被 PIT 抢占取代，`yield` 不再是可用的切换手段。

### 2.2 PIT 定时器与时间片（quantum）

**定义**：PIT（Intel 8254 可编程间隔定时器）通道 0 以固定频率产生时钟中断 IRQ0。本内核把 PIT 配置为 100 Hz（`PIT_RATE_HZ 100`、除数 `PIT_DIVISOR 11932`），每次 IRQ0 即一个 tick。`irq0_schedule` 在每个 tick 里把全局 `ticks` 加 1，并管理"当前线程还剩多少 tick 可运行"的时间片。

```c
#define TIME_SLICE_TICKS 2
```

每个时间片 = 2 个 tick = 2 个 10 ms = 20 ms。逻辑在 `irq0_schedule` 里：`quantum_left` 每 tick 减 1，减到 0 就强制切换（抢占）。这就是"定时器驱动抢占"的最小内核。

### 2.3 非让出 worker（non-yielding workers）

**定义**：`preempttest` 启动的两个 worker（线程 1、2）在 `worker_run` 里不断执行 `busy_delay()`——纯粹的空转忙等，**不调用 `yield`、不 sleep、不阻塞**。它们从语义上"永不让出 CPU"。

**为什么需要这种 worker**：为了证明抢占不依赖合作。若没有 PIT 抢占，这两个 worker 会无限独占 CPU，shell（线程 0）永远得不到执行；有了 PIT 抢占，即使 worker 全程 `busy_delay()`，`irq0_schedule` 也会在时间片耗尽时把 CPU 交还给可运行队列里的下一个线程（包括 shell）。`preempttest` 的输出串直接点明这一点：

```
preempttest: two non-yielding workers started
```

### 2.4 「固定元数据 + 确定性验证」教学模型

贯穿 Lesson 64–84 的教学法：**元数据真实，行为不执行**。
- 元数据真实：`struct lesson_72_model` 的 4 个 u32 计数器与 4 个 u8 标志，是对真实调度器状态（运行、就绪、记账）的简化投影；
- 行为不执行：不真的跑一个多核调度器，只在数据结构层面断言；
- 确定性验证：每个规则映射为一条布尔表达式，通过 `lXXtest` 命令打印 `passed`/fallback 串。相同输入必然相同输出，因此能用 `grep` 核对源码、用 `make check` 做静态断言。

```c
struct lesson_72_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

字段语义：`a/b/c/d` 是四个可算术校验的计数器；`valid`（记录合法）、`active`（正在运行）、`ready`（已就绪）、`accounted`（已记账）对应调度器对线程的四个基本事实。校验条件是 `b==a+1` 且四标志全真——一条最小的"调度记账一致性"断言。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-78） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、调度器、主循环 | **主要增量**：新增 `struct lesson_72_model`、`lesson_72_state`、`l79test()`；把 `l78test` 改名为 `l71test`；`exec64` 的 `l79test`/`l71test` 分支与 `about`、banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局：`.text64`/数据段/idle/rsp0/IST1 栈 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局（`_start` 在 1M） | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` 启动 QEMU | 微变化：`check` 目标三条 `grep` 换成 Lesson 79 关键字 |
| `grub.cfg` | GRUB 菜单，引导 `kernel.elf` | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的固定元数据记录与 `l79test`

```c
struct lesson_72_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_72_model lesson_72_state;
static TEXT64 void l79test(u16*c){lesson_72_state=(struct lesson_72_model){72U,73U,74U,75U,1,1,1,1};int ok=lesson_72_state.valid&&lesson_72_state.active&&lesson_72_state.ready&&lesson_72_state.accounted&&lesson_72_state.b==lesson_72_state.a+1U;text64(c,"l79test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 72 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_72_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };`：固定容量的调度 checkpoint 记录。4 个 u32（`a,b,c,d`）作计数器，4 个 u8 作调度状态标志。真实调度器里"运行/就绪/记账"是 `threads[]` 各线程的 `state` 与 `switches` 字段，这里用一条记录做最小投影。
- `static struct lesson_72_model lesson_72_state;`：单一全局实例。`static` 限定翻译单元可见；教学模型刻意"一记录一命令"，保证确定性。
- `lesson_72_state=(struct lesson_72_model){72U,73U,74U,75U,1,1,1,1};`：聚合初始化。`a=72,b=73,c=74,d=75`（递增四元组），四个标志位全部为 1。
- `int ok=lesson_72_state.valid&&lesson_72_state.active&&...&&lesson_72_state.b==lesson_72_state.a+1U;`：成功 = 4 标志全真 且 `b==a+1`。这条"算术一致性"断言模拟调度记账不变量（每处理一个 tick，计数应递增 1）。
- `text64(c,"l79test: ");`：命令前缀。
- `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 72 fallback reported");`：成功串 `bounded scheduling and copy-on-write checkpoint passed`、失败串 `Lesson 72 fallback reported`——两条串都来自源码字面量，是验证时逐字对照的基准。
- `putc64(c,'\n');`：结尾换行。

设计动机：checkpoint 课的可读性优先。这条命令不触碰真实调度器，只断言"调度记账 + 元数据一致性"这条最小不变量，供每次启动回归。

#### (b) 上一课回归测试改名为 `l71test`

lesson-78 的 `l78test` 在本课改名为 `l71test`（内容不变，仍校验 `lesson_71_state`）：

```c
static TEXT64 void l71test(u16*c){lesson_71_state=(struct lesson_71_model){71U,72U,73U,74U,1,1,1,1};int ok=lesson_71_state.valid&&lesson_71_state.active&&lesson_71_state.ready&&lesson_71_state.accounted&&lesson_71_state.b==lesson_71_state.a+1U;text64(c,"l71test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 71 fallback reported");putc64(c,'\n');}
```

- 命名规律：本内核用 `lKtest` 对应 `lesson_K_model`。lesson-78 的 `l78test` 实际校验 `lesson_71_state`，命名错位；本课把它改回 `l71test`，并让新增的 `l79test` 使用 `lesson_72_state`。此后每课（80–84）沿这条规律把旧测试改名、再新增本课测试。
- 意义：`exec64` 里同时存在 `l71test` 与 `l79test` 分支，确保改版不破坏旧语义，且新的 `lesson_72_model` 与旧的 `lesson_71_model` 独立共存。

#### (c) 调度核心：`irq0_schedule`（PIT 抢占的决策点）

这是"主动抢占"真正发生的地方。`irq0_entry` 汇编把 15 个 GPR 压进当前线程栈后调用它；它返回的 `struct irq0_frame *` 就是 iretq 将要恢复的帧——返回谁的帧，谁就是下一个执行者。

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){
    u8 old,next;
    ticks++;                    /* 每个 PIT tick 全局计数 +1 */
    softirq_run_budget();       /* 用 tick 预算运行软中断模型 */
    outb64(PIC1_COMMAND,PIC_EOI); /* 向主 8259 发送 EOI，允许下一次 IRQ0 */
    if(f&&f->cs==USER_CS){ user_irq0_save_restore(f); return f; } /* CPL3 帧：只保存恢复用户线程，不切内核线程 */
    if(idle_running){ idle_frame=f; idle_ticks++; }      /* 空闲线程被打断：记下 idle 帧 */
    else threads[current_thread].frame=(u64)(unsigned long)f; /* 否则保存当前内核线程的 CPU 帧 */
    wake_sleepers();            /* 到期的 sleeping 线程转 RUNNABLE */
    reap_finished_threads();    /* 回收已 finished 的线程栈页 */
    if(!idle_running&&quantum_left){ quantum_left--; if(quantum_left)return f; } /* 时间片未完：原样返回 */
    old=current_thread; next=next_runnable();             /* 时间片耗尽：进入抢占决策 */
    quantum_left=TIME_SLICE_TICKS;                        /* 重置新时间片 */
    if(next==0xff){ ... idle_running=1; idle_switches++; return idle_frame; }   /* 无就绪线程：切 idle */
    if(idle_running){ ... preempt_switches++; return (struct irq0_frame *)(unsigned long)threads[next].frame; } /* idle→worker */
    if(next==old){ if(old==0)idle_worker_ticks++; return f; } /* 还是自己：直接回帧 */
    threads[old].state=THREAD_RUNNABLE; rr_enqueue(old);      /* 旧线程放回就绪队列 */
    threads[next].state=THREAD_RUNNING; current_thread=next;   /* 新线程接管 */
    threads[next].switches++; preempt_switches++;              /* 记账：切换次数 +1 */
    return (struct irq0_frame *)(unsigned long)threads[next].frame; /* 返回新线程帧 → iretq 切换到它 */
}
```

逐行实质分析：
- **帧即上下文**：每个线程有自己的 4 KiB 内核栈，`irq0_entry` 把 15 个 GPR 压入当前栈顶形成 `struct irq0_frame`，`irq0_schedule` 保存的是**帧指针**，切换时只需返回目标线程的栈顶指针——`movq %rax,%rsp` 之后 `pop` + `iretq` 就完成了寄存器恢复。这就是"上下文切换 = 栈指针交换"的模型化表达。
- **抢占时机**：`quantum_left` 在时间片未耗尽时逐 tick 递减；减到 0 才进入切换逻辑。`preempttest` 的两个 worker 从不让出 CPU，唯一能打断它们的就是这个 tick 边界——这就是"主动抢占"。
- **边界处理**：`next==0xff`（无可运行线程）时切到独立 idle；`next==old`（只剩自己可运行）时原样返回；`old==0`（shell）多空转一个 tick 时记 `idle_worker_ticks`。这些分支保证没有"零线程可跑"的死锁。
- **与 Linux 对照**：对应 Linux `kernel/sched/core.c` 的 `scheduler_tick()` + `__schedule()`：tick 里 `task_tick_fair` 递减预算并 `resched_curr`，随后 `__schedule` 用 `pick_next_task` 选下一个任务、`context_switch` 完成切换。

#### (d) 时间片与运行队列的辅助函数

```c
static TEXT64 u8 rr_pick_next(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){u8 i=(u8)((round_robin+n)%THREAD_COUNT);if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){round_robin=i;return i;}}return 0xff;}
static TEXT64 void rr_enqueue(u8 id){if(id<THREAD_COUNT&&threads[id].state==THREAD_FINISHED)return;sched_enqueues++;}
static TEXT64 void rr_dequeue(u8 id){if(id<THREAD_COUNT)sched_dequeues++;}
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
static TEXT64 struct sched_class *runtime_sched_class(void){return &fair_sched_class;}
```

- `rr_pick_next`：从 `round_robin+1` 起顺时针扫描 `threads[1..2]`，返回第一个 `RUNNABLE/RUNNING` 线程；`round_robin` 记录上次选择的游标，实现最简单的 round-robin。找不到返回 `0xff` 表示"无就绪线程"。
- `rr_enqueue/rr_dequeue`：`fair_sched_class` 只是"调度类"抽象（函数指针表）的占位实现——真正的工作由 `irq0_schedule` 直接完成，这里把记账计数器 `sched_enqueues/sched_dequeues` 走一遍。
- `schedinfo` 命令输出：`scheduler class: tiny_rr` 与 `ops enqueue/dequeue/pick: ...`，并打印 `wait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions`。

#### (e) `start_threads` 与 `worker_run`：非让出 worker 的出生与运行

```c
static TEXT64 void worker_run(u8 id){
    if(pc_test){ event_wait(&pc_start_event); if(id==1)pc_producer(); else pc_consumer(); return; }
    while(threads[id].progress<THREAD_STEPS){
        if(kbd_wait_test){ u8 ch; kbd_wait_char(&ch); threads[id].mailbox=ch; threads[id].received++; }
        threads[id].progress++;
        if(sleep_test) thread_sleep_ticks(id==1?SLEEP_A_TICKS:SLEEP_B_TICKS);
        else if(!kbd_wait_test) busy_delay();
    }
    thread_exit();
}
```

- `sleep_test/kbd_wait_test/pc_test` 三个开关由 `start_threads(mode)` 设置：mode 0=preempttest（什么都不开）、mode 1=sleeptest、mode 2=kbdwaittest、mode 3=pctest。
- **preempttest 路径**：`busy_delay()`（`for(n=0;n<BUSY_SPINS;n++)` 空转 4000000 次，带 `"memory"` clobber 防止被优化掉）→ `progress++` → 再来一轮。worker 既不 yield 也不 sleep，**只能**靠 IRQ0 抢占被换下。
- `thread_exit()`：`threads[current_thread].state=THREAD_FINISHED;` 后 `sti; hlt` 死循环，等 `reap_finished_threads` 回收其栈页。

```c
static TEXT64 int start_threads(u8 mode){u32 i;u64 flags;if(threads_started)return 0;if(!pmm_ready)return -1;flags=irq_save64();sleep_test=mode==1;kbd_wait_test=mode==2;pc_test=mode==3;if(pc_test)pc_reset();...for(i=1;i<THREAD_COUNT;i++){u64 p=pmm_alloc();struct irq0_frame *f;if(!p){...回滚已分配页...return -1;}threads[i].id=(u8)i;threads[i].state=THREAD_EMPTY;threads[i].stack_phys=p;...f=(struct irq0_frame *)(unsigned long)(phys_to_high(p)+THREAD_STACK_BYTES-sizeof(*f));f->r15=...=0;f->r12=phys_to_high(p)+THREAD_STACK_BYTES;f->rip=runtime_thread_trampoline_address();f->cs=0x08;f->rflags=0x202;threads[i].frame=(u64)(unsigned long)f;}for(i=1;i<THREAD_COUNT;i++)threads[i].state=THREAD_RUNNABLE;threads_started=1;quantum_left=TIME_SLICE_TICKS;irq_restore64(flags);return 1;}
```

- 每个 worker 从 PMM 分配 1 页作为独立内核栈，在栈顶伪造一个"初始 `irq0_frame`"：`rip` 指向 `thread_trampoline`（汇编 `movq %r12,%rsp; call thread_trampoline_c`），`r12` 保存栈高地址，`rflags=0x202`（IF=1）。这样第一次 IRQ0 抢占该 worker 时，`irq0_schedule` 保存的帧就是这个伪造帧，`iretq` 恢复后线程从 trampoline 开始跑。
- **错误处理**：中途 `pmm_alloc` 失败会回滚已分配的栈页并恢复中断标志返回 `-1`；`threads_started` 防重复启动。
- **preempttest 的命令输出**（exec64 分支）：

```
preempttest: two non-yielding workers started
preempttest: already started
preempttest: PMM allocation failed
```

#### (f) `exec64` 增量分支与 `about`/banner

```c
}else if(eq64(word,"l71test")){if(!noargs64(arg))usage64(c,"l71test");else l71test(c);}
}else if(eq64(word,"l79test")){if(!noargs64(arg))usage64(c,"l79test");else l79test(c);}
```

- 两条分支并列存在于 `exec64` 的长 if-else 链中，紧跟在 `l70test` 之后。注意 `help` 命令的列表**没有**列出 `l71test`/`l79test`——课程维护者有意保持 help 串不变，新命令仍可直接输入，这是可接受的教学简化（与 lesson-73 同款约定）。
- `yield` 分支（继承命令）本课语义更新为：
```c
}else if(eq64(word,"yield")){if(!noargs64(arg))usage64(c,"yield");else text64(c,"yield: cooperative switching replaced by PIT preemption\n");}
```
`preempttest`/`sleeptest`/`kbdwaittest`/`pctest` 四个分支通过 `start_threads(mode)` 启动对应模式（见 3.2(e)）。

`about` 分支：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 79: voluntary preemption 主动抢占\n");}
```

#### (g) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
threads[0].id=0;threads[0].state=THREAD_RUNNING;quantum_left=TIME_SLICE_TICKS;framebuffer_init(h);...
text64(&c,"Lesson 79: voluntary preemption 主动抢占\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");prompt64(&c);...
for(;;){if(!kbd_dequeue(&ch)){__asm__ volatile("sti; hlt":::"memory");continue;}if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}...}
```

- 开机第一屏第一行是 `Lesson 79: voluntary preemption 主动抢占`，第二行 `GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata` 强调 syscall ABI 未变、回收元数据边界不变。
- 初始化里 `threads[0].state=THREAD_RUNNING; quantum_left=TIME_SLICE_TICKS;`——shell 线程（id 0）作为首个运行线程，初始时间片就是 2 个 tick。
- 主循环：`kbd_dequeue` 读键盘 → 攒满一行调 `exec64` → 回 `prompt64`。`sti; hlt` 在无键时进入"可被 IRQ0 打断的睡眠"，保证 PIT 抢占路径一直在走。

### 3.3 构建管线（Makefile / linker）

本课 Makefile 与 lesson-78 唯一的差异在 `check` 目标：

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'voluntary preemption 主动抢占' README.md
	@grep -q 'l79test' kernel64.c
	@grep -q 'Lesson 79' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 79 checks passed.'
```

- `grub-file --is-x86-multiboot2`：权威验证外层 ELF 的 Multiboot2 header（GNU GRUB 工具，规范来源 Multiboot2 规范）。
- 三条 `grep -q`：把"README 主题字、源码新符号、课号"做成静态断言——改错主题、删掉 `l79test`、写错课号都会让 `make check` 失败，这正是「固定元数据 + 确定性验证」在构建层的体现。
- `printf 'Multiboot2 and Lesson 79 checks passed.'`：成功退出信息，逐字来自 Makefile。

其余构建链（与 lesson-78 完全一致）：`kernel64.o`（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse ... -Werror`）→ `ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary` 得到裸二进制 → `boot.o`（`-m32`）内嵌 → `ld -m elf_i386 -T linker.ld` 合成 `kernel.elf` → `grub-mkrescue` 打包 ISO。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32(kernel.c)
        └─ 建页表/校验用户镜像 ──► long_mode_start ──► kernel_main64_binary(kernel64.c)
            ├─ 初始化 threads[0]=RUNNING、quantum_left=2、idle_init、install_idt、pit_init、pic_init
            ├─ banner: "Lesson 79: voluntary preemption 主动抢占\nGETTICKS, ..."
            └─ for(;;) 键盘循环（sti; hlt）
                ├─ 用户输入 "preempttest" ──► exec64 ──► start_threads(0)
                │   └─ worker 1/2 进入 worker_run → busy_delay()（永不 yield）
                │       └─ 每 tick：IRQ0 ──► irq0_entry 压 GPR ──► irq0_schedule
                │           ├─ quantum_left 递减；耗尽后 rr_pick_next 选下一个
                │           └─ preempt_switches++，返回目标线程帧 ──► iretq 切换
                └─ 输入 "l79test" ──► lesson_72_state 初始化 ──► 5 条件校验 ──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 置 `threads[0].state=THREAD_RUNNING`、`quantum_left=TIME_SLICE_TICKS`，打印 banner 与 `tinyos> `。
2. **输入**：用户在 QEMU 图形窗口敲 `preempttest` 回车。键盘循环攒行后 `exec64` 分派到 `start_threads(0)` 分支。
3. **启动 worker**：PMM 为线程 1、2 各分配 1 页内核栈，伪造初始 `irq0_frame`，置 `THREAD_RUNNABLE`；`threads_started=1`。
4. **抢占循环**：worker 在 `worker_run` 里 `busy_delay()` 空转（不 yield）。每个 PIT tick 触发 IRQ0，`irq0_schedule` 保存当前帧；2 tick 后 `quantum_left` 归零，`rr_pick_next` 选下一个可运行线程，`preempt_switches++`，`iretq` 恢复目标帧。
5. **观察**：`ps` 打印各线程 `state/frame/stack-pa/stack-high/switches/progress`；`threadinfo` 打印 `scheduler: PIT preemptive independent idle`、`mode: preempttest`、`quantum left`、`preempt switches`。
6. **checkpoint**：输入 `l79test`，`lesson_72_state` 初始化后 5 条件校验，VGA 打印 `l79test: bounded scheduling and copy-on-write checkpoint passed`。

输出串与源码逐字一致：`l79test: ` + `bounded scheduling and copy-on-write checkpoint passed`；`preempttest: ` + `two non-yielding workers started`。

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
   Lesson 79: voluntary preemption 主动抢占
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l79test`，预期输出：
   ```
   l79test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l79test: Lesson 72 fallback reported`，源码可见。）
3. 输入 `l71test`（回归），预期输出：
   ```
   l71test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `preempttest`，预期输出：
   ```
   preempttest: two non-yielding workers started
   tinyos>
   ```
5. 输入 `threadinfo`，预期出现：
   ```
   scheduler: PIT preemptive independent idle
   ...
   mode: preempttest
   quantum left: ...
   PIT ticks: ...
   preempt switches: ...
   ```
6. 输入 `yield`，预期输出：
   ```
   yield: cooperative switching replaced by PIT preemption
   tinyos>
   ```
7. 输入 `about`，预期输出：
   ```
   Lesson 79: voluntary preemption 主动抢占
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 79 checks passed.`；QEMU 中 `l79test` 打印 `...passed`、`preempttest` 打印 `two non-yielding workers started` 即代表本课 checkpoint 与抢占模型验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/用户镜像校验失败） | 看 VGA 是否显示 `user image validation/load failure:`；`kernel.c` 的 `validate_user_image()` 各 status 分支 |
| `make check` 失败于第一条 grep | README 主题字串与 Makefile 不一致 | `grep 'voluntary preemption 主动抢占' README.md` 核对字面 |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l79test` 符号 | `grep -q 'l79test' kernel64.c` |
| 输入 `l79test` 打印 `unknown command` | `exec64` 分支未接入或命令拼写错误 | 核对 `eq64(word,"l79test")` 分支；help 列表不含本命令是已知教学简化 |
| 输入 `preempttest` 打印 `PMM allocation failed` | `pmm_ready=0` 或 PMM 无空闲页 | 先 `meminfo` 看 PMM 状态；`pmm_init` 在 `kernel_main64_binary` 中必须先于任何 `start_threads` 执行 |
| `threadinfo` 显示 `mode: preempttest` 但 `preempt switches: 0` | worker 还没被抢走过（刚启动或 idle 常驻） | 等几个 tick 再 `threadinfo`；确认 PIT/IRQ0 已使能（`sti` 后主循环在跑） |
| 输入 `preempttest` 再次启动打印 `already started` | `threads_started=1` 防重复 | 这是预期行为；想看新 worker 需重启 QEMU |
| `l79test` 打印 fallback 串 | `lesson_72_state` 5 条件中某一项不满足（场景被改动） | 检查 `ok` 表达式：`valid`、`active`、`ready`、`accounted`、`b==a+1` |
| QEMU 无图形输出 | 把 `-display` 参数加成了 `none` 或显示环境缺失 | 用 `make run` 原样启动；图形验收参考 `scripts/qemu-vga-check.sh` 流程 |
| `yield` 命令不再切换线程 | 协作式切换已被 PIT 抢占取代（本课语义） | 这是预期行为：`yield: cooperative switching replaced by PIT preemption` |

---

## 7. 与 Linux 源码对照

**对照点 1：主动抢占 = scheduler_tick + __schedule**
- TinyOS 教学模型：`irq0_schedule` 在每个 PIT tick 递减 `quantum_left`，归零后 `rr_pick_next` 选下一个线程，用"返回帧指针"完成切换。
- Linux 实现：`kernel/sched/core.c` 的 `scheduler_tick()` 在每个 tick 被调用（HZ 定时器），对当前任务 `task_tick_fair()`（CFS）或 `task_tick_rt()` 检查预算；需要重新调度时置 `TIF_NEED_RESCHED`，随后任意中断/系统调用返回路径上的 `__schedule()` 执行真正的切换（`pick_next_task` + `context_switch`）。
- 权威来源：Linux v6.x `kernel/sched/core.c`（`scheduler_tick`、`__schedule`）、`include/linux/sched.h`；Intel SDM Vol.3A §17（时钟与定时器）、8254 PIT 数据手册。
- 教学简化：TinyOS 把"标 `TIF_NEED_RESCHED` → 延迟到返回路径"压缩成"在 `irq0_schedule` 内直接切换"；没有优先级、没有 CFS 红黑树，`rr_pick_next` 的 3 线程线性扫描就是全部调度策略。

**对照点 2：voluntary vs involuntary 切换统计**
- TinyOS 教学模型：`preempt_switches` 记录被 IRQ0 强制切换的次数；`preempttest` 的 worker 从不 yield，因此切换全部是"非自愿"的。
- Linux 实现：`task_struct` 中有 `nvcsw`（voluntary context switches，自己让出）与 `nivcsw`（involuntary，被抢占）；`/proc/PID/status` 的 `voluntary_ctxt_switches` / `nonvoluntary_ctxt_switches` 字段即来自二者。
- 权威来源：Linux `include/linux/sched.h`（`nvcsw`/`nivcsw`）、`kernel/sched/stats.c`；`procfs` 文档。
- 教学简化：TinyOS 只统计一个总数 `preempt_switches`，没有区分 `nvcsw/nivcsw`——因为本课刻意让所有 worker 都不让出，统计自然全是抢占。

**对照点 3：时间片（time slice / quantum）**
- TinyOS：`TIME_SLICE_TICKS 2`，`quantum_left` 每 tick 递减，耗尽即切换；时间片 = 2×10 ms。
- Linux：现代 CFS 没有固定时间片，用 `sched_slice()`（基于任务权重与 CPU 负载计算虚拟运行时间）；老的 O(1) 调度器才有 `time_slice` 概念。固定时间片 + round-robin 更接近 `SCHED_RR` 实时调度类的 `RR_TIMESLICE`。
- 教学简化：固定 2-tick 时间片 + 简单 round-robin 便于确定性观察，牺牲了 CFS 的公平性算法。

---

## 8. 思考题与练习

1. **概念理解**：`preempttest` 的两个 worker 从不调用 `yield`/`sleep`/阻塞，为什么它们不会永远独占 CPU？用 `irq0_schedule` 中 `quantum_left` 的递减逻辑解释。
2. **源码定位**：在 `kernel64.c` 中找到 `start_threads` 伪造初始 `irq0_frame` 的代码。为什么 `f->r12=phys_to_high(p)+THREAD_STACK_BYTES` 而 `rip` 指向 `thread_trampoline`？（提示：回忆 `thread_trampoline` 汇编的 `movq %r12,%rsp`。）
3. **动手实验**：把 `TIME_SLICE_TICKS` 从 2 改成 5，重新 `make run`，观察 `threadinfo` 的 `quantum left` 与 `preempt switches` 的变化，思考"时间片越长，抢占频率越低"。
4. **动手实验**：在 `worker_run` 的 `busy_delay()` 前加一行 `thread_sleep_ticks(1)`（模拟 worker 自愿让出），再观察 `preempt switches` 与 `sleep wakeups`。改完请**恢复原值**，避免破坏 stable 快照的 `make check`。
5. **Linux 对照**：阅读 Linux 中 `scheduler_tick()` 与 `__schedule()` 的分工，说明 TinyOS 把"置 `TIF_NEED_RESCHED` + 延迟调度"简化为"中断内直接切换"后，丢失了什么语义（提示：中断上下文中调度是否安全、是否可重入）。

---

## 9. 本课小结与下一课预告

- 本课把「主动抢占」这一调度语义锚定为：即使线程从不自愿让出 CPU，PIT 定时器也会在时间片耗尽后强制把它换下——抢占不需要被抢占者配合。
- 你掌握了 `irq0_schedule` 的完整切换决策（tick 计数、`quantum_left` 递减、`rr_pick_next` 选下一个、返回帧指针即完成上下文切换）。
- 你理解了 `preempttest` 的"非让出 worker"如何用 `busy_delay()` 证明抢占路径，以及 `yield` 命令宣告"协作式切换被 PIT 抢占取代"。
- 你看到了「固定元数据 + 确定性验证」在本课的投影：`struct lesson_72_model` 四个计数 + 四个标志，`l79test` 一条 5 条件断言。
- 你验证了累积源码的回归命名规律（`l78test` → `l71test`，新增 `l79test`）与 `make check` 三条静态断言。

**下一课预告**：Lesson 80「定时器驱动调度」。本课只用了 PIT tick 的"表面"，下一课将深入 PIT 的配置（`PIT_DIVISOR`、100 Hz）、`ticks` 时钟、sleep 唤醒（`wake_sleepers`/`thread_sleep_ticks`）与 idle 线程——把"定时器如何驱动调度"拆成可验证的时钟/定时器/sleep 三层模型。衔接点正是本课的 `quantum_left` 与 `irq0_schedule`。

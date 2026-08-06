# Lesson 121: per-CPU runqueue — 精讲文档

> **Course status: stable snapshot.**
>
> - 课号：Lesson 121
> - 本课主题：per-CPU runqueue（每 CPU 就绪队列）
> - 课程主线位置：第四阶段（SMP / RCU / 诊断元数据检查点系列），紧接「跨 CPU 唤醒」之后
> - 前置课程：[Lesson 120（跨 CPU 唤醒）](../lesson-120-stable/README.md)
> - 后续课程：[Lesson 122（SMP 负载均衡）](../lesson-122-stable/README.md)
> - 一句话目标：掌握「每 CPU 一个就绪队列」这一 SMP 调度核心数据结构在 TinyOS 教学模型中的元数据表示，并用 `l121test` 验证检查点不变量。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说清 per-CPU runqueue 为什么是 SMP 调度器的地基，能指认 `kernel64.c` 中 `cpu_local`、`this_cpu()`、调度类派发表与检查点模型 `lesson_114_model` 各自扮演的角色，并正确运行 `l121test` 验证。
- **在课程主线中的位置**：从 Lesson 69 开始，课程进入「检查点课」节奏：每课只新增一个 `struct lesson_N_model` 元数据模型、一个 `lNtest()` 校验函数、一条 `exec64` 命令分支与 about/banner 文本，内核主体（调度、PMM、VFS、GUI、软中断等）保持不变。Lesson 120 引入「跨 CPU 唤醒」，本课把镜头对准唤醒/就绪管理所依托的数据结构——per-CPU runqueue；下一课在此基础上讲「SMP 负载均衡」。
- **前置知识清单**：
  1. 上一课的检查点模型结构（`struct lesson_N_model { a,b,c,d,valid,active,ready,accounted }`）与 `lNtest()` 断言方式；
  2. 单 CPU 时间片轮转（`rr_pick_next`、`irq0_schedule`、`quantum_left`）如何工作；
  3. `cpu_local` / `this_cpu()` / `NR_CPUS` 三件套的含义（从 Lesson 105 附近「per-CPU 数据」课继承）；
  4. `exec64` 命令分发与 `text64`/`hex64`/`putc64` VGA 输出接口。
- **本课交付**：
  - 新命令 `l121test`（输出逐字取自源码，见第 5 节）；
  - 新数据结构 `struct lesson_114_model lesson_114_state`；
  - `about` 命令显示 `Lesson 121: per-CPU runqueue`；
  - 启动横幅显示 `Lesson 121: per-CPU runqueue`。

## 2. 核心概念精讲

### 2.1 为什么 SMP 需要「每 CPU 一个 runqueue」

- **定义**：per-CPU runqueue 指系统里有多少个 CPU，就维护多少份独立的就绪任务队列；每个 CPU 只从自己那份队列里取任务执行。
- **动机（对比单队列）**：如果所有 CPU 共享一个全局就绪队列，那么任意一次入队/出队都必须拿同一个全局锁，多 CPU 并发调度时自旋等待成为瓶颈，而且 CPU 缓存（cache）会在不同核之间反复颠簸。Linux 在 `kernel/sched/sched.h` 中把就绪状态（`cfs_rq`、`rt_rq`、`dl_rq`）放进每个 CPU 私有的 `struct rq`，配合 `struct rq *rq = this_rq()` 就近访问。
- **TinyOS 的教学表示**：本内核单核运行（`NR_CPUS == 1U`），但已经为「每 CPU 一份状态」保留了骨架：`struct cpu_local` 数组 + `this_cpu()`，调度器只从自己的 `cpu_local` 读取 `softirq_pending`、`work_used` 等。检查点 `l121test` 用 `valid/active/ready/accounted` 四个布尔位把「runqueue 数据被正确初始化、可以被激活、就绪、且计数闭合」这一不变量编码进元数据模型。

```
传统做法：所有 CPU 抢一把锁访问一个全局队列
  CPU0 ----┐                     ┌---- CPU1
           ├──(全局锁)──► [ 全局 runqueue ] ◄──(全局锁)──┤
           └----                   ----┘

per-CPU runqueue：每人一份，锁粒度细到本地
  CPU0 ──► [ rq0: 队头→t2→t1→t3 ]
  CPU1 ──► [ rq1: 队头→t4→t5 ]
  （负载均衡课才会引入跨队列搬移）
```

### 2.2 检查点模型（checkpoint model）哲学

- **定义**：每课新增一个 `struct lesson_N_model`，字段固定为 `u32 a,b,c,d` + `u8 valid,active,ready,accounted`，以及一个 `lNtest()` 函数。
- **为什么这样设计**：课程大纲比代码实现走得更快。为了在「不改动已验证安全内核」的前提下给每个新主题留下确定性、可自动校验的记录，模型只描述主题的**不变量结论**，不模拟真实行为（本课并不真的实现多核队列入队/出队）。
- **工作机制**：`lNtest()` 先把模型填充为形如 `{114U,115U,116U,117U,1,1,1,1}` 的固定值，然后计算 `ok = valid && active && ready && accounted && b == a+1`。只有全部布尔位为真且 `b` 恰好比 `a` 大 1（代表「队列计数序号连续」）才打印 passed。
- **断言含义**：`valid`=模型已定义；`active`=主题机制处于激活态；`ready`=组件可被调度/回收；`accounted`=计数账目闭合；`b==a+1`=序号递进无缺口。四个连续整数字段 `a,b,c,d` 记录「本课课号起点的四连号」，作为唯一标识。

### 2.3 调度类派发表（sched_class dispatch table）

- **定义**：`struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); }` 是把「选择下一个线程 / 入队 / 出队」这三个操作抽象成函数指针的小型 OOP 表。
- **动机**：Linux 的 `kernel/sched/` 下 CFS、RT、DL、stop 等调度类正是用类似的 `struct sched_class` 派发表实现多态调度。TinyOS 借同样的形状，让 `rr_pick_next`/`rr_enqueue`/`rr_dequeue` 成为可插拔对象，但行为仍是继承自前几课的有界轮转扫描。
- **本课关联**：per-CPU runqueue 在 Linux 中正是挂在 `struct rq` 里的每个调度类队列的**宿主**；TinyOS 的 `fair_sched_class`（名字 `tiny_rr`）代表该宿主里的默认类。检查点模型记录的是「队列宿主数据是 ready 的」。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 120） |
|------|------|------------------------------|
| `boot.S` | Multiboot2 头、32→64 位切换、`kernel64.bin` 内嵌 | 未变化（与 Lesson 120 逐字节一致） |
| `kernel.c` | 32 位主流程 `kernel_main32`，建立临时页表、PMM 预扫描 | 未变化 |
| `kernel64.c` | 64 位内核主体：shell、调度、RCU/软中断元数据、检查点模型 | **有增量**：新增 `lesson_114_model`/`lesson_114_state`/`l121test()`；`l120test` 更名为 `l113test`；`about` 与启动横幅改为 Lesson 121 |
| `kernel64.ld` | 64 位裸二进制布局，含 idle/rsp0/ist1 三个守护页 | 未变化 |
| `linker.ld` | 32 位内核镜像布局（Multiboot2 头须在首 32768 字节内） | 未变化 |
| `Makefile` | 双阶段编译（32 位壳 + 64 位裸二进制）与 `check`/`run` | 微小变化：`check` 的 grep 串改为 `per-CPU runqueue`、`l121test`、`Lesson 121` |
| `grub.cfg` | GRUB 启动项（`multiboot2 /boot/kernel.elf`） | 未变化（menuentry 文案仍是 Lesson 52 时期遗留） |

### 3.2 kernel64.c 精讲

#### 结构 / 宏 / 全局变量

1. **检查点模型链**：`kernel64.c` 第 746 行起按课号递增堆叠 `struct lesson_69_model … lesson_114_model`，每个结构体一字不差地重复同一定义：

```c
struct lesson_114_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_114_model lesson_114_state;
```

逐行注释：
- `struct lesson_114_model { u32 a,b,c,d; ... }`：本课新检查点模型；`a,b,c,d` 为四连课号标识（本课填 `114,115,116,117`），`valid/active/ready/accounted` 四个位描述「模型有效 / 机制激活 / 组件就绪 / 计数闭合」。
- `static struct lesson_114_model lesson_114_state;`：唯一的模型实例。`static` 文件内可见，`l121test()` 与打印路径共享。
- 之所以要一整条模型链，是为了让历史课的 `l106test … l113test` 命令仍可回放：每课新增一个模型，并把上一课命名的 `lNtest` 保留为历史回归。

2. **per-CPU 骨架**（第 239–241、254 行）：

```c
#define NR_CPUS 1U
struct cpu_local { u8 id,softirq_pending,work_head,work_tail,work_used; };
static struct cpu_local cpu_locals[NR_CPUS];
...
static TEXT64 struct cpu_local *this_cpu(void){return &cpu_locals[0];}
```

逐行注释：
- `#define NR_CPUS 1U`：教学内核单核，但接口已经按「多核数组」写——将来改大数字即可扩展。
- `struct cpu_local { u8 id, softirq_pending, work_head, work_tail, work_used; }`：每 CPU 私有状态：CPU 编号、待处理软中断位图、工作队列环形缓冲三指针。这就是 TinyOS 版的「per-CPU 数据」。
- `cpu_locals[NR_CPUS]`：按 CPU 数实例化数组，等价于 Linux `DEFINE_PER_CPU(...)` 的静态版。
- `this_cpu()`：返回 `&cpu_locals[0]`。对照 Linux `this_cpu_ptr()`/`smp_processor_id()`，它在单核下恒为 0 号槽，但调用点（`lockatomicinfo`、`lockatomictest`、`softirq_raise` 的消费者）都不写死下标，保证语义上「操作的是当前 CPU 的队列」。

3. **调度类派发表**（第 316–325、487 行）：

```c
struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static struct sched_class *active_sched_class;
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
```

逐行注释：
- `name` 为 `"tiny_rr"`，`schedinfo` 命令直接打印它。
- `pick_next/enqueue/dequeue` 三个函数指针对应「从 runqueue 挑下一个 / 入队 / 出队」——在 Linux 里这些操作挂在 `struct sched_class` 上且操作的是 per-CPU 的 `struct rq`，在 TinyOS 里它们操作全局 `threads[]` 数组（单核时 per-CPU 队列退化为这个全局数组）。

#### 函数精讲

**`l121test(u16 *c)`**（第 883 行，本课新增）

```c
static TEXT64 void l121test(u16*c){lesson_114_state=(struct lesson_114_model){114U,115U,116U,117U,1,1,1,1};int ok=lesson_114_state.valid&&lesson_114_state.active&&lesson_114_state.ready&&lesson_114_state.accounted&&lesson_114_state.b==lesson_114_state.a+1U;text64(c,"l121test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 114 fallback reported");putc64(c,'\n');}
```

- **签名与职责**：`static TEXT64 void l121test(u16 *c)`，`c` 是 VGA 游标（cell 序号）；职责是把本课检查点模型装好并做确定性断言，结果打到屏幕。
- **输入输出**：无真实输入；输出固定两段文本：前缀 `"l121test: "`，随后三选一为 `"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed"` 或 `"Lesson 114 fallback reported"`，最后换行。
- **算法步骤**：
  1. `lesson_114_state = (struct lesson_114_model){114U,115U,116U,117U,1,1,1,1}`：一次性初始化四个布尔位为 1、四连号从 114 起；
  2. `ok = valid && active && ready && accounted && b == a+1U`：五个条件全真才通过；`b==a+1U` 表示「队列序号无空洞」；
  3. `text64` 打印前缀与三选一结果，`putc64(c,'\n')` 换行。
- **边界检查**：没有入参可越界；所有量都是编译期常量。失败路径（`ok==0`）打印 `"Lesson 114 fallback reported"`，词面上的「114」是模型号，不是课号 121——这是检查点体系的统一约定。
- **为什么这样设计**：与 Lesson 69 以来的全部 `lNtest` 模板一致，保证课程回放与自动化 grep 校验（Makefile 的 `check` 目标 grep `l121test`）不受新主题影响。

**`exec64` 中本课新增分支**（第 884 行）

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 121: per-CPU runqueue\n");}
...
}else if(eq64(word,"l121test")){if(!noargs64(arg))usage64(c,"l121test");else l121test(c);}
```

- **签名与职责**：`exec64(u16 *c, struct long_mode_handoff *h, const char *s)` 是 shell 命令解释器；本课只在 `about` 分支与末尾 `l121test` 分支上做了小改。
- **算法步骤**：`token64` 切出命令词 `word` 后按 `eq64` 逐条匹配；`about` 无参数时打印主题行，`l121test` 无参数时调用 `l121test(c)`。带多余参数都走 `usage64` 提示。
- **边界处理**：`if(!noargs64(arg))usage64(...)` 拒绝带参调用；未知命令落到 `text64(c,"unknown command\n")`。这些行为与全课命令体系共用。

**`lockatomicinfo` / `lockatomictest`（继承，但与本课直接相关）**

```c
static TEXT64 void lockatomicinfo(u16*c){text64(c,"locks/atomics/percpu: NR_CPUS ");hex64(c,NR_CPUS);text64(c," lock ");hex64(c,deferred_lock.locked);text64(c," cpu id/pending/work: ");hex64(c,this_cpu()->id);text64(c,"/");hex64(c,this_cpu()->softirq_pending);text64(c,"/");hex64(c,this_cpu()->work_used);text64(c," memory order: acquire/release/relaxed\n");}
```

- 该命令是观察 per-CPU 数据的窗口：`NR_CPUS`、`this_cpu()->id`、`this_cpu()->softirq_pending`、`this_cpu()->work_used` 全部来自 `cpu_local`。本课标题虽为「per-CPU runqueue」，代码层面能真实看到的就是这份 per-CPU 结构；`lockatomictest` 则演示在 `raw_spin_lock_irqsave` 保护下用 acquire/release 顺序读写 `this_cpu()->softirq_pending`。
- **为什么相关**：Linux 中 per-CPU runqueue 的并发安全恰恰依赖每个 CPU 只在本地锁下访问自己的 `rq`；TinyOS 用 `deferred_lock` + `irq_save64` 复刻了「关中断 + 原子交换」的本地锁协议（对照 `kernel/sched/core.c` 的 `raw_spin_lock(&rq->__lock)` 与 `smp_processor_id()` 归属检查）。

**调度器主体（继承，作为 runqueue 的消费者）**

```c
static TEXT64 u8 rr_pick_next(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){u8 i=(u8)((round_robin+n)%THREAD_COUNT);if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){round_robin=i;return i;}}return 0xff;}
static TEXT64 struct sched_class *runtime_sched_class(void){return &fair_sched_class;}
static TEXT64 u8 next_runnable(void){sched_picks++;return rr_pick_next();}
```

逐行注释：
- `rr_pick_next`：从 `round_robin+1` 起环形扫描 `threads[1..THREAD_COUNT-1]`，命中 `RUNNABLE`/`RUNNING` 即返回并把游标推进；全部不可运行返回 `0xff`（交给 idle）。
- `runtime_sched_class()`：返回 `&fair_sched_class`，等价于 Linux `sched_class_highest`/`__sched_class` 的查找，但只有一个类，无需遍历。
- `next_runnable()`：包一层计数 `sched_picks++`，供 `schedinfo` 显示 pick 次数。

**`irq0_schedule(struct irq0_frame *f)`（继承）**：PIT 中断（IRQ0）每 `TIME_SLICE_TICKS` 个 tick 调用一次；它先 `ticks++`、跑 `softirq_run_budget()`、送 EOI，然后按 `quantum_left` 决定是否换人，必要时用 `next_runnable()` 从「本 CPU 就绪集」中挑下一个并把返回的 frame 交给 IRQ0 的 `movq %rax,%rsp` 恢复。本课不新增调度逻辑，但它是「runqueue 被消费」的现场——可以理解为 `cpu_locals[0]` 上的调度循环。

### 3.3 构建管线（Makefile / linker）

- `CFLAGS`（32 位壳）：`-m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`。裸机 32 位代码不需要 PIE 重定位、栈金丝雀或 unwind 表。
- `CFLAGS64`（64 位裸二进制）：`-m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx`。`-fpie` 让 `kernel64.bin` 使用 RIP 相对寻址以便在低地址 VMA=0 下可被 `boot.S` 的高别名搬运执行；`-mno-red-zone` 防止中断在 `rsp` 下方 128 字节处乱写；禁用 SSE 系指令，因为 64 位内核没有开 CR4.OSFXSR。
- 编译链：`kernel64.c → kernel64.o → kernel64.elf → objcopy -O binary → kernel64.bin`（VMA=0 的裸镜像，见 `kernel64.ld`）→ 内嵌进 `boot.S` 的 `.incbin` → 与 32 位 `kernel.c` 一起链接成 GRUB 可加载的 `kernel.elf` → `grub-mkrescue` 生成 ISO。
- `kernel64.ld` 的关键：`. = 0;` 起点、`.text64 : { *(.text64.entry) *(.text64 .text64.*) }` 保证 `kernel_main64_binary`（`ENTRY64` 段）排在最前；`.data` 段里用 `__idle_guard_start`/`__idle_stack_start`/`__ist1_stack_end` 等符号划定三块 4KB 守护/栈区域，并有三个 `ASSERT(...== 0x1000)` 编译期校验栈大小。
- `linker.ld`：`ENTRY(_start)`、`. = 1M` 起点，Multiboot2 头放在 `.multiboot` 段并 `KEEP()`；`. = ALIGN(CONSTANT(MAXPAGESIZE))` 把可写段推到新页，避免 RWX 合一。
- `Makefile check` 目标（本课）：

```
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'per-CPU runqueue' README.md
	@grep -q 'l121test' kernel64.c
	@grep -q 'Lesson 121' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 121 checks passed.'
```

`grub-file --is-x86-multiboot2` 用 GNU GRUB 的规范校验头；三个 `grep -q` 把「文档主题、命令存在、课号」钉死，防止课程内容与代码脱节。

### 3.4 主控制流

```
_start (boot.S) ──► kernel_main32 (kernel.c, 32 位页表/PMM) ──► enter_long_mode (boot.S)
      └─► kernel_main64 (boot.S .incbin 入口) ──► kernel_main64_binary (kernel64.c ENTRY64)
            ├─ task_names_keep / module_init_model / init_model_start / wait_model_start …
            ├─ pmm_init(h) / vma_init / reclaim_init / vfs_init / address_space_init
            ├─ threads[0].state=THREAD_RUNNING; idle_init(); install_idt; pit_init; pic_init
            ├─ text64(&c,"Lesson 121: per-CPU runqueue\nGETTICKS, …\n")  ← 本课横幅
            └─ 主循环：kbd_dequeue → 回车 → exec64(&c,h,cmd)
                   ├─ "about"      → text64 "Lesson 121: per-CPU runqueue\n"
                   └─ "l121test"   → l121test(c) → 打印检查点结果
```

`kernel_main64_binary`（第 885–889 行）以 `task_names_keep(); active_sched_class=&fair_sched_class;` 开头（即把调度类激活），随后铺陈全部子系统初始化，最后进入 `for(;;)` 键盘读环；`l121test` 经由该环触达。

## 4. 数据流与运行逻辑

1. **输入**：用户在 `tinyos> ` 提示符后敲 `l121test` 并回车。
2. **进入分支**：键盘中断（IRQ1）把扫描码经 `kbd_queue` 投递；主循环取到 `'\n'` 后把 `cmd` 以 NUL 结尾交给 `exec64(&c,h,cmd)`；`token64` 得到 `word=="l121test"`，命中新增分支。
3. **执行**：`l121test(c)` 把 `lesson_114_state` 赋为 `{114U,115U,116U,117U,1,1,1,1}`，计算 `ok`。
4. **输出**：`text64(c,"l121test: ")` 之后，`ok` 为真时打印 `bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`，为假时打印 `Lesson 114 fallback reported`，最后 `putc64(c,'\n')`。
5. **屏幕显示**：一行

```
l121test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed
```

（若模型任何一位被清零，则是 `l121test: Lesson 114 fallback reported`。）同时 `about` 显示 `Lesson 121: per-CPU runqueue`。

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`（与课程一贯要求相同，无新增工具）。
- **构建**：

```bash
make clean && make -j"$(nproc)"
```

生成 `build/kernel.iso`；随后

```bash
make check
```

预期输出（Makefile 末行逐字）：

```
Multiboot2 and Lesson 121 checks passed.
```

- **运行**：

```bash
make run
```

成功画面在 QEMU 图形窗口，勿加 `-display none`；串口/图形均可见启动横幅 `Lesson 121: per-CPU runqueue`。
- **验证步骤**：
  1. 在提示符输入 `about` → 预期输出 `Lesson 121: per-CPU runqueue`（源码 `exec64` 中逐字抄录）；
  2. 输入 `l121test` → 预期输出 `l121test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（源码 `l121test()` 中逐字抄录；任一布尔位破坏时输出 `Lesson 114 fallback reported`）；
  3. 输入 `schedinfo` → 预期首行 `scheduler class: tiny_rr`（源码 `schedinfo` 打印 `active_sched_class->name`）；
  4. 输入 `lockatomicinfo` → 预期首行 `locks/atomics/percpu: NR_CPUS 1 …`，其中 `cpu id/pending/work` 来自 `this_cpu()`。
- **如何判断成功**：横幅、`about`、`l121test` 三条输出与上文逐字一致，且 `make check` 无报错即通过。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| 屏幕滚动异常或字符乱码 | VGA 游标 `c` 超过 `COLS*ROWS` 未按 25 行封顶处理 | 查 `putc64` 的 `*c>=COLS*ROWS` 分支；对照 `clear64` 后游标归零 |
| `l121test` 打印 fallback | `lesson_114_state` 某位为 0 或 `b!=a+1` | 检查模型初始化字面量 `{114U,115U,116U,117U,1,1,1,1}` 是否被改动；`ok` 的五个条件逐一用 `hex64` 打印验证 |
| `about` 仍显示旧课号 | 未更新 `exec64` 中 `about` 分支的 `text64` 字符串 | grep `'Lesson 121'` kernel64.c，确认仅出现于 about 与横幅 |
| `make check` 失败于 grep | 命令名与源码/文档不一致 | `grep -q 'l121test' kernel64.c`、`grep -q 'per-CPU runqueue' README.md`；确认命令是 `l121test` 而非旧 README 误写的 `l114test` |
| `schedinfo` 显示 `none` | `active_sched_class` 未在 `kernel_main64_binary` 开头赋值为 `&fair_sched_class` | 单步确认第 885 行 `active_sched_class=&fair_sched_class;` 在 `sched_enqueues=…` 之前执行 |
| `lockatomicinfo` 的 cpu id 非 0 | `this_cpu()` 返回下标被改 | 检查 `cpu_locals[0]` 初始化与 `this_cpu()` 实现是否仍返回 `&cpu_locals[0]` |
| 敲键无回显 | IRQ1 键盘队列满或中断未安装 | 查 `kbd_queue`/`kbd_head`/`kbd_tail` 与 `install_idt(h)`；用 `kbdinfo` 观察溢出计数 |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现（路径） |
|--------|-----------------|--------------------|
| per-CPU runqueue | `struct cpu_local cpu_locals[NR_CPUS]` + `this_cpu()`；单核退化为 `threads[]` 全局数组 | `struct rq` 挂在每 CPU 上（`kernel/sched/sched.h`），`this_rq()` / `cpu_rq(cpu)` 按 `smp_processor_id()` 索引 |
| 每 CPU 本地锁 | `raw_spin_lock_irqsave(&deferred_lock,...)` + `atomic_exchange_acquire_u32` 自旋 | `struct rq` 自带 `raw_spinlock_t __lock`；调度器在 `double_rq_lock` 等场景关中断取锁（`kernel/sched/core.c`） |
| 调度类派发 | `struct sched_class` 三函数指针，`fair_sched_class={"tiny_rr",…}` | `struct sched_class`（`kernel/sched/sched.h`），`pick_next_task`/`enqueue_task`/`dequeue_task`，类链表按优先级遍历 |
| 就绪选择 | `rr_pick_next()` 环形扫描 `threads[]`，返回 `0xff` 表示无任务（去 idle） | CFS `pick_next_task_fair` 从红黑树取最左叶子；`idle` 类是最终兜底 |
| 检查点语义 | `valid/active/ready/accounted` + `b==a+1` 的元数据断言 | Linux 没有等价物；对应的是调度器自检（如 `sched_debug`、`debug_check_no_locks_freed`）这类「不变量校验」精神 |
| 权威来源 | —— | Intel SDM Vol.3（IRQ/中断/页表）；Multiboot2 规范（GRUB 交接）；GNU GRUB `grub-file` 校验 |

**教学模型简化了什么**：真实 per-CPU runqueue 有入队/出队/迁移/亲和力/优先级继承等全套逻辑，且每个 CPU 的队列加锁互不影响；TinyOS 只保留「每 CPU 一份状态 + 本地访问接口 + 一把全局自旋锁」，行为上用有界数组模拟，并发正确性由检查点断言而非真机多核时序保证。

## 8. 思考题与练习

1. **概念理解**：为什么 per-CPU runqueue 能减少锁竞争？如果 `NR_CPUS` 改成 4，`cpu_locals` 需要改什么，`this_cpu()` 又应该如何区分当前 CPU？
2. **源码定位**：在 `kernel64.c` 中找到 `struct cpu_local`、`this_cpu()` 与 `lockatomicinfo` 三处，说明「runqueue 宿主数据」与「锁/原子操作」是如何耦合的。
3. **动手实验**：把 `l121test` 中模型字面量 `{114U,115U,116U,117U,1,1,1,1}` 的 `ready` 位改成 0，重新构建运行，观察输出变为 `Lesson 114 fallback reported`；随后改回并验证 passed。
4. **Linux 对照**：阅读 `kernel/sched/core.c` 中 `__schedule()` 与 `pick_next_task()`，找出它与 `irq0_schedule` + `next_runnable()` 调用链的对应关系。
5. **综合**：解释为什么本课的检查点模型把 `b==a+1U` 作为断言之一，这一「序号无空洞」不变量在真实 runqueue 的哪些计数字段上有对应物（如 `nr_running`、`nr_uninterruptible`）。

## 9. 本课小结与下一课预告

- 本课确认了检查点课的三件套节奏：新增 `struct lesson_114_model` + `l121test()` + `exec64` 分支，主体代码零改动。
- 学懂了 per-CPU runqueue 的概念：每 CPU 一份就绪队列、本地锁、就近访问，单核下退化为 `threads[]` 全局数组。
- 厘清了 `cpu_local`/`this_cpu()`/`NR_CPUS` 与调度类派发表（`fair_sched_class`）在本内核中的位置。
- 用 `about`、`l121test`、`schedinfo`、`lockatomicinfo` 四条命令把「主题、检查点、调度类、per-CPU 数据」串成可验证链条。
- 理解了检查点模型用 `valid/active/ready/accounted` 四布尔位编码主题不变量、以 `b==a+1` 保证序号连续的确定性校验哲学。
- 与 Linux 对照找到了 `struct rq`、`this_rq()`、`struct sched_class` 的逐点映射，明确了教学模型省略了队列迁移与真实多核互斥。

**下一课**：[Lesson 122（SMP 负载均衡）](../lesson-122-stable/README.md) 在 per-CPU runqueue 的基础上讨论「队列之间如何搬移任务」，将引入负载均衡相关检查点模型 `lesson_115_model` 与命令 `l122test`，并对照 Linux `kernel/sched/topology.c` 的调度域与 `load_balance` 流程。

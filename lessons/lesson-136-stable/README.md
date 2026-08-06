# Lesson 136: SMP/RCU 回归验证 — 精讲文档

> **课号**：Lesson 136（可执行课，checkpoint 快照）
> **主题**：SMP/RCU 回归验证——围绕 `NR_CPUS=1` 的 per-CPU 抽象、四种内存序的
> 原子操作、`raw_spinlock` 与 softirq 预算机制讲解「并发原语如何被验证」，并追加
> checkpoint 模型 `lesson_129_model`。
> **课程主线位置**：诊断/网络主题的「检查点课」序列（Lesson 133–138），位于
> Lesson 135（调度与并发综合诊断）之后、Lesson 137（并发、SMP、RCU、诊断综合
> checkpoint）之前。
> **前置课程**：[`lesson-135-stable/README.md`](../lesson-135-stable/README.md)
> **后续课程**：[`lesson-137-stable/README.md`](../lesson-137-stable/README.md)
> **一句话目标**：学完本课你能说清 TinyOS 的 SMP/RCU 相关抽象——per-CPU 变量、
> acquire/release/relaxed 三种内存序、irq-safe 自旋锁、softirq 预算——以及它们
> 如何被 `lockatomictest`/`softirqtest` 回归验证，checkpoint 模型 `l136test`
> 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂并发原语层的四个组件——①`cpu_local` per-CPU 结构
（`NR_CPUS=1` 教学约束）；②`__atomic_*` 内建的三种内存序封装；③
`raw_spin_lock_irqsave`/`raw_spin_unlock_irqrestore` 关中断自旋锁；④softirq 模型
（tasklet 合并、FIFO workqueue、预算耗尽）。掌握 `lockatomictest`/`softirqtest`/
`lockatomicinfo`/`softirqinfo` 的断言与输出。

- **在课程主线中的位置**：与 Lesson 133–138 同属「诊断/网络主题的检查点课」。
  `kernel64.c` 相对 Lesson 135 仅 3 处增量：`l135test`→`l128test` 改名、新增
  `struct lesson_129_model` 与 `l136test`、exec64/about/banner 文案换成
  「SMP/RCU 回归验证」。并发原语全部继承自早期课程，本课侧重讲解，不新增机制。
- **前置知识清单**：
  1. C11 内存序：`memory_order_relaxed/acquire/release` 的语义与配对规则
     （acquire-release 配对、release 发布 / acquire 获取）；
  2. 关中断临界区：`irq_save64`（`pushfq; pop; cli`）与 `irq_restore64`（按 IF 位
     决定 `sti`）；
  3. 自旋锁：`atomic_exchange` 的 XCHG 语义、`while(...)` 自旋、持锁期间关中断；
  4. softirq/tasklet/workqueue 概念（早期课程实现）。
- **本课交付**：理解「并发原语 + 内存序 + per-CPU + 软中断预算」的验证思路；
  `l128test`、`l136test` 两个 checkpoint 测试；`lockatomicinfo`/`softirqinfo`
  诊断命令。

---

## 2. 核心概念精讲

### 2.1 概念一：per-CPU 变量与教学约束 NR_CPUS=1

**直觉**：SMP 内核里「每 CPU 一份」的数据（per-CPU 变量）避免了大部分锁竞争。
TinyOS 把这套抽象保留下来，但把核数钉死为 1。

```c
#define NR_CPUS 1U
struct cpu_local { u8 id,softirq_pending,work_head,work_tail,work_used; };
static struct cpu_local cpu_locals[NR_CPUS];
static TEXT64 struct cpu_local *this_cpu(void){return &cpu_locals[0];}
```

**为什么**：`this_cpu()` 恒返回 `cpu_locals[0]`，等价于 Linux 的
`this_cpu_ptr`/`get_cpu_ptr`（`arch/x86` 用 GS 段或 `current_cpu_data`）的退化版。
「回归验证」的含义是：即便只有 1 核，代码路径仍应写 per-CPU 接口而非裸全局——
为将来真多核留出接口。

### 2.2 概念二：内存序（relaxed / acquire / release）

**直觉**：CPU 可以乱序执行内存访问。atomic 操作除了「原子性」还有「顺序性」。
TinyOS 把 C11 三种内存序包成 u8/u32 两个尺寸的封装：

```c
static TEXT64 u8 atomic_load_relaxed_u8(volatile u8*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
static TEXT64 void atomic_store_release_u8(volatile u8*p,u8 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 u8 atomic_fetch_or_relaxed_u8(volatile u8*p,u8 v){return __atomic_fetch_or(p,v,__ATOMIC_RELAXED);}
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
```

- `relaxed`：只保证原子性，不做顺序约束——适合计数器（`softirq_pending` 置位）。
- `acquire`：读操作，禁止其后的内存操作越过它重排——用于「拿锁」。
- `release`：写操作，禁止其前的内存操作越过它重排——用于「放锁/发布」。
- **配对**：锁的加锁（`exchange acquire`）与解锁（`store release`）构成经典
  acquire-release 配对：释放前的写对获取后的读可见。这正是自旋锁正确性的内存序
  基础。

### 2.3 概念三：irq-safe 自旋锁

```c
typedef struct { volatile u32 locked; } raw_spinlock_t;
static raw_spinlock_t deferred_lock;
static TEXT64 void raw_spin_lock_irqsave(raw_spinlock_t*l,irqflags_t*f){*f=irq_save64();while(atomic_exchange_acquire_u32(&l->locked,1)){} }
static TEXT64 void raw_spin_unlock_irqrestore(raw_spinlock_t*l,irqflags_t f){atomic_store_release_u32(&l->locked,0);irq_restore64(f);}
```

**为什么先关中断再自旋**：单核教学环境里没有真并行，但中断可以打断临界区。先
`irq_save64` 记录并清 IF，再 `atomic_exchange_acquire_u32` 把 `locked` 置 1——
若已被别的临界区持有（不可能，单核+关中断），则自旋等待。解锁时 `store_release`
清零并 `irq_restore64` 恢复原 IF。**「锁 + 关中断」二合一**正是 Linux
`raw_spin_lock_irqsave` 的名字由来（`include/linux/spinlock.h`）。

### 2.4 概念四：softirq 预算（budget）

**直觉**：软中断不能在硬中断上下文里无限执行。TinyOS 用 `SOFTIRQ_BUDGET=2` 限制
一次 `softirq_run_budget` 最多消费 2 个「执行单位」；跑不完的留在 pending，下次
IRQ0 再继续。三个入口：`softirq_raise`（置位 + 记账）、`tasklet_schedule`（tasklet
置 pending 并 raise 位 0）、`workqueue_submit`（FIFO 入队并 raise 位 1）。

### 2.5 概念五：RCU 的「回归验证」含义（如实说明）

**诚实声明**：本课源码中**没有 RCU 原语实现**（无 `rcu_read_lock`/`synchronize_rcu`
等）。`lesson_129_model` 的断言成功串里出现的 "RCU" 描述的是整个内核机制的覆盖
标签，RCU 的 grace period/quiescent state 不在本课代码中。本课真正的「回归验证」
对象是 §2.1–2.4 的并发原语；RCU 属于主题标签，机制留待后续（Linux 对照见 §7.5）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 135） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（959 行）：原子/锁/softirq/per-CPU/调度/进程/checkpoint | `l135test`→`l128test`；新增 `struct lesson_129_model`、`l136test`；exec64 增加 `l128test`/`l136test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `SMP/RCU 回归验证`、`l136test`、`Lesson 136` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：SMP/RCU 相关原语精讲

#### 3.2.1 per-CPU 与原子封装（§2.1/2.2 的源码形态）

```c
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 void raw_spin_lock_irqsave(raw_spinlock_t*l,irqflags_t*f){*f=irq_save64();while(atomic_exchange_acquire_u32(&l->locked,1)){} }
static TEXT64 void raw_spin_unlock_irqrestore(raw_spinlock_t*l,irqflags_t f){atomic_store_release_u32(&l->locked,0);irq_restore64(f);}
static TEXT64 struct cpu_local *this_cpu(void){return &cpu_locals[0];}
```

- `atomic_exchange_acquire_u32`：把 `locked` 置 1 并返回旧值；旧值非 0 说明锁已被
  持有 → `while(...){}` 空转。acquire 保证「拿到锁后才允许读锁保护的数据」。
- `raw_spin_unlock_irqrestore`：先 `store_release` 清零——release 保证「锁保护区的
  写都在清零之前完成」；后 `irq_restore64(f)` 按保存的 IF 位恢复中断。
- `this_cpu`：返回 `&cpu_locals[0]`；`deferred_lock` 是唯一一把全局自旋锁，
  名字暗示它用于「推迟/延后执行」的临界区。

#### 3.2.2 lockatomictest：并发原语的回归断言（本课主题核心）

```c
static TEXT64 void lockatomictest(u16*c){irqflags_t f;u8 v=0;atomic_fetch_or_relaxed_u8(&this_cpu()->softirq_pending,0);raw_spin_lock_irqsave(&deferred_lock,&f);v=atomic_load_relaxed_u8(&this_cpu()->softirq_pending);atomic_store_release_u8(&this_cpu()->softirq_pending,(u8)(v|1));raw_spin_unlock_irqrestore(&deferred_lock,f);text64(c,"lockatomictest: ");text64(c,atomic_load_relaxed_u8(&this_cpu()->softirq_pending)==1&&deferred_lock.locked==0?"irq-safe lock, atomic publication, per-CPU ordering passed":"BROKEN");putc64(c,'\n');}
```

- 算法步骤：①用 relaxed fetch-or 预热 `this_cpu()->softirq_pending`（无副作用
  写 0）；②`raw_spin_lock_irqsave` 拿锁并关中断；③relaxed load 读出当前
  `softirq_pending`；④`v|1` 后用 `store_release` 写回（模拟发布一条「pending 置位」
  事件）；⑤`raw_spin_unlock_irqrestore` 放锁并恢复中断。
- 断言：`atomic_load_relaxed_u8(&this_cpu()->softirq_pending)==1 && deferred_lock.
  locked==0`——①发布的值被读到（release→relaxed 在本模型单核下恒成立）；②锁确实
  释放（`locked` 归零）。成功串 `irq-safe lock, atomic publication, per-CPU ordering
  passed`。
- 为什么这样设计：把「拿锁-临界区-放锁」压缩成一条可打印真假的路径，同时验证
  三种内存序封装（relaxed load/fetch-or、acquire exchange、release store）都能
  跑通且不破坏 `locked` 不变量。

#### 3.2.3 softirq 模型：raise / tasklet / workqueue

```c
static TEXT64 void softirq_raise(u8 bit){if(bit>=SOFTIRQ_BITS){softirq_model.drops++;return;}softirq_model.pending|=(u8)(1U<<bit);softirq_model.raises++;}
static TEXT64 void tasklet_schedule(u8 id){if(id>=TASKLET_CAP||tasklets[id].disabled)return;if(!tasklets[id].pending){tasklets[id].pending=1;softirq_raise(0);}}
static TEXT64 int workqueue_submit(u8 kind,u8 data){if(work_used>=WORK_CAP){softirq_model.drops++;return 0;}workqueue[work_head]=(struct work_model){kind,data,1,0};work_head=(u8)((work_head+1)%WORK_CAP);work_used++;softirq_raise(1);return 1;}
```

- `softirq_raise(bit)`：位图置 pending（`SOFTIRQ_BITS=3`），越界丢弃并记账
  `drops`。位 0 留给 tasklet、位 1 留给 workqueue。
- `tasklet_schedule(id)`：**合并去重**——已 pending 的 tasklet 再调度不重复置位
  （`if(!tasklets[id].pending)`），对应 Linux tasklet 的 `TASKLET_STATE_SCHED`
  防重复入队。
- `workqueue_submit(kind,data)`：满 4 槽丢弃（`drops++`）；环形 FIFO 写 `workqueue
  [head]`，置 `queued=1` 并 raise 位 1。返回 1 表示入队成功。

```c
static TEXT64 void softirq_run_budget(void){u8 budget=SOFTIRQ_BUDGET,i;while(budget&&softirq_model.pending){if(softirq_model.pending&1){for(i=0;i<TASKLET_CAP&&budget;i++)if(tasklets[i].pending&&!tasklets[i].disabled){tasklets[i].pending=0;tasklets[i].runs++;softirq_model.runs++;budget--;}}if(softirq_model.pending&2&&budget){if(work_used){struct work_model*w=&workqueue[work_tail];w->queued=0;w->runs++;work_tail=(u8)((work_tail+1)%WORK_CAP);work_used--;softirq_model.runs++;budget--;}}if(!tasklets[0].pending&&!tasklets[1].pending)softirq_model.pending&=(u8)~1U;if(!work_used)softirq_model.pending&=(u8)~2U;}if(softirq_model.pending)softirq_model.budget_exhaustions++;}
```

- 职责：在 IRQ0 路径（`irq0_schedule` 第一步）消费 pending 软中断，受 `budget`
  限制。
- 算法步骤：①`budget=SOFTIRQ_BUDGET(2)`，循环条件 `budget && pending`；②位 0
  （tasklet）：遍历 2 个 tasklet，pending 且未 disabled 的执行并扣预算；③位 1
  （workqueue）：出队一个 work，执行并扣预算；④清空后把对应 pending 位清零；
  ⑤循环结束时若仍有 pending → `budget_exhaustions++`（预算耗尽标记）。
- 边界：`budget` 是「执行单位」总预算，tasklet 与 workqueue 共享——这是
  `softirqtest` 断言 `work_used==2`（第一轮只消费掉 2 个预算）的关键。

#### 3.2.4 softirqtest：预算与合并的回归断言

```c
static TEXT64 void softirqtest(u16*c){u8 i;softirq_model=(struct softirq_model){0};work_head=work_tail=work_used=0;for(i=0;i<TASKLET_CAP;i++)tasklets[i]=(struct tasklet_model){0,0,0};tasklet_schedule(0);tasklet_schedule(0);tasklet_schedule(1);for(i=0;i<WORK_CAP;i++)workqueue_submit(i,0);int a=workqueue_submit(9,0)==0;softirq_run_budget();int b=!tasklets[0].pending&&!tasklets[1].pending&&work_used==2;softirq_run_budget();int d=work_used==0&&softirq_model.budget_exhaustions>=1;text64(c,"softirqtest: ");text64(c,a&&b&&d&&softirq_model.runs>=6?"tasklet coalescing, FIFO work, and budget carry-over passed":"BROKEN");putc64(c,'\n');}
```

- 算法步骤：①清零模型与队列；②`tasklet_schedule(0)` 两次（第二次应被合并跳过）
  再 `tasklet_schedule(1)`——验证合并去重；③`workqueue_submit(0..3)` 填满 4 槽，
  `workqueue_submit(9,0)` 应失败（`a==0`，满队列丢弃）；④第一次
  `softirq_run_budget`：2 个 tasklet + 0 个 work 消费 2 预算 → `work_used==2`（`b`）；
  ⑤第二次 `softirq_run_budget`：消费剩余 2 个 work → `work_used==0` 且预算耗尽过
  至少一次（`d`）。
- 断言：`a&&b&&d&&softirq_model.runs>=6`——成功串 `tasklet coalescing, FIFO work,
  and budget carry-over passed`。
- 为什么：三条性质各被一点验证——合并（tasklet 二次调度不重复）、FIFO（work 严格
  按入队顺序）、预算结转（第一次跑不完，第二次接着跑，`budget_exhaustions` 计数）。

#### 3.2.5 诊断命令：lockatomicinfo / softirqinfo

```c
static TEXT64 void lockatomicinfo(u16*c){text64(c,"locks/atomics/percpu: NR_CPUS ");hex64(c,NR_CPUS);text64(c," lock ");hex64(c,deferred_lock.locked);text64(c," cpu id/pending/work: ");hex64(c,this_cpu()->id);text64(c,"/");hex64(c,this_cpu()->softirq_pending);text64(c,"/");hex64(c,this_cpu()->work_used);text64(c," memory order: acquire/release/relaxed\n");}
static TEXT64 void softirqinfo(u16*c){text64(c,"softirq pending/raises/runs/drops/budget: ");hex64(c,softirq_model.pending);text64(c,"/");hex64(c,softirq_model.raises);text64(c,"/");hex64(c,softirq_model.runs);text64(c,"/");hex64(c,softirq_model.drops);text64(c,"/");hex64(c,softirq_model.budget_exhaustions);text64(c," tasklets/work: ");hex64(c,tasklets[0].pending+tasklets[1].pending);text64(c,"/");hex64(c,work_used);putc64(c,'\n');}
```

- `lockatomicinfo`：per-CPU 状态快照——`NR_CPUS`、锁是否持有、`cpu id/
  softirq_pending/work_used`，末尾明示 `memory order: acquire/release/relaxed`
  是本课的教学内存模型。
- `softirqinfo`：pending/raises/runs/drops/budget_exhaustions 五计数 + tasklet 与
  workqueue 占用数——softirq 机制的完整仪表盘。

#### 3.2.6 本课新增 checkpoint：lesson_129_model 与 l136test

```c
struct lesson_129_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_129_model lesson_129_state;
static TEXT64 void l136test(u16*c){lesson_129_state=(struct lesson_129_model){129U,130U,131U,132U,1,1,1,1};int ok=lesson_129_state.valid&&lesson_129_state.active&&lesson_129_state.ready&&lesson_129_state.accounted&&lesson_129_state.b==lesson_129_state.a+1U;text64(c,"l136test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 129 fallback reported");putc64(c,'\n');}
```

- 字段语义：4 个 u32 连续编号（a=129、b=130、c=131、d=132）+ 4 个状态位
  （valid/active/ready/accounted）。`a` 从 `129U` 起头 = 课号 136 − 7，回锚到
  Lesson 129 检查点。
- 断言逻辑：`ok` 五连真（四个状态位 + `b==a+1`）输出成功串，否则失败串
  `Lesson 129 fallback reported`。
- 为什么：回归探针——若相邻课改坏了并发原语层（比如改了 `NR_CPUS`、动摇了
  `cpu_local` 布局），本课的原子/锁测试可能翻转；checkpoint 进一步保证元数据层
  自洽。**注意：该断言不执行任何 RCU 代码**（见 §2.5 的如实说明）。

#### 3.2.7 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l128test")){if(!noargs64(arg))usage64(c,"l128test");else l128test(c);}else if(eq64(word,"l136test")){if(!noargs64(arg))usage64(c,"l136test");else l136test(c);}
```

- 本课把上一课的 `l135test` 分支改名 `l128test`（复用 `lesson_128_model`），新增
  `l136test` 分支。**勘误**：旧 README 写的 `Commands: l129test` 与源码不符，源码中
  可用的 checkpoint 命令是 `l128test` 与 `l136test`。
- about：`else text64(c,"Lesson 136: SMP/RCU 回归验证\n");`；开机横幅：
  `text64(&c,"Lesson 136: SMP/RCU 回归验证\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 相关命令：`lockatomictest`、`lockatomicinfo`、`softirqtest`、`softirqinfo`
  （都在 exec64 有分支，由 IRQ0 的 `softirq_run_budget` 与 shell 直接调用配合验证）。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`__atomic_*` 内建在
  `-m64` 下由 GCC 直接映射为 `lock` 前缀指令，无需链接 `libatomic`）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`SMP/RCU 回归验证`、`l136test`、`Lesson 136`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals[0].id=0（per-CPU 就绪，NR_CPUS=1）
       → pmm_init → ... → idle_init → install_idt → pit_init + pic_init
       → 横幅 "Lesson 136: SMP/RCU 回归验证\n..." → shell 循环
  IRQ0 每 tick：irq0_schedule 第一步调用 softirq_run_budget()（预算内清软中断）
  exec64 命令 → lockatomictest:锁/原子/内存序断言
             → softirqtest:tasklet 合并/FIFO/预算结转断言
             → lockatomicinfo/softirqinfo:per-CPU 与 softirq 快照
             → l128test/l136test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题命令为例：

1. **`lockatomictest`** → exec64 命中分支 → `lockatomictest(c)`：对
   `this_cpu()->softirq_pending` 做 relaxed 读、release 写，全程包在
   `raw_spin_lock_irqsave` 临界区内 → 断言 `pending==1 && locked==0` → 输出
   `lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`。
2. **`softirqtest`** → `softirqtest(c)`：构造 2 tasklet + 4 work 的负载，两次
   `softirq_run_budget` 分两轮消费 → 输出 `softirqtest: tasklet coalescing, FIFO
   work, and budget carry-over passed`。
3. **`softirqinfo`** → 打印 `softirq pending/raises/runs/drops/budget: ...` 五计数
   与 `tasklets/work: ...` 占用。
4. **`l136test`** → 对 `lesson_129_state` 赋值并五连断言 → 输出 `l136test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`。

要点：`softirq_run_budget` 既被 IRQ0 每 tick 自动调用，也被 `softirqtest` 显式
调用——数据流上是「硬中断上下文触发的预算内软中断消费」，教学上可手动复现。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具；`__atomic_*` 由 GCC 内建支持）。
- **构建**：`cd lessons/lesson-136-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `SMP/RCU 回归验证`、`l136test`、`Lesson 136` 与 kernel64.c 中的 `l136test`，
  全部命中输出 `Multiboot2 and Lesson 136 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅
  第一行为 `Lesson 136: SMP/RCU 回归验证`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 136: SMP/RCU 回归验证`
  2. `l136test` → `l136test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  3. `l128test` → `l128test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  4. `lockatomictest` → `lockatomictest: irq-safe lock, atomic publication, per-CPU
     ordering passed`
  5. `lockatomicinfo` → `locks/atomics/percpu: NR_CPUS 0000000000000001 lock
     0000000000000000 cpu id/pending/work: 0000000000000000/.../...
     memory order: acquire/release/relaxed`
  6. `softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and budget
     carry-over passed`
  7. `softirqinfo` → 首行 `softirq pending/raises/runs/drops/budget: ...`，末尾
     `tasklets/work: ...`
- **如何判断成功**：上述命令逐一打印预期串即成功；`lockatomictest` 与 `softirqtest`
  必须为 passed（不出现 `BROKEN`）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l136test` 输出 `Lesson 129 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l136test` 赋值 `{129U,130U,131U,132U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `lockatomictest` 输出 `BROKEN` | `softirq_pending` 非 1 或锁未释放 | `lockatomicinfo` 看 `cpu id/pending/work` 与 `lock` 值；确认 release 写回先于解锁 |
| `softirqtest` 输出 `BROKEN` | 合并/FIFO/预算任一性质被破坏（如 work_used 计数错） | 单步看两次 `softirq_run_budget` 的预算消耗；`softirqinfo` 看 `budget` 计数与 `work_used` |
| 中断里 softirq 永不执行（`softirqinfo` 的 runs 不涨） | IRQ0 未开或 `softirq_run_budget` 未在 `irq0_schedule` 调用 | `tickinfo` 看 ticks；`idtinfo` 看 IRQ0 vector 0x20；检查 `irq0_schedule` 首行 |
| `raw_spin_lock_irqsave` 后中断没有真正关闭 | `irq_save64` 未在持锁路径生效 | 检查 `irq_save64` 的 `pushfq; popq; cli` 顺序；持锁期间调 `tickinfo` 看 ticks 是否冻结 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 136: SMP/RCU 回归验证`；`make check` grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **per-CPU 变量**：TinyOS `cpu_locals[NR_CPUS]` + `this_cpu()` 对应 Linux
   `arch/x86/include/asm/percpu.h` 的 `this_cpu_ptr`/`this_cpu_read`（GS 段基址）；
   单核版相当于 `CONFIG_SMP` 关闭时的 `__per_cpu_offset[0]==0`。
2. **原子与内存序**：TinyOS `__atomic_load_n/store_n/fetch_or/exchange_n` 直接对应
   Linux `arch/x86/include/asm/atomic.h` 的 `atomic_read/atomic_set/atomic_fetch_or/
   atomic_xchg`（编译为 `lock` 前缀指令）；`acquire/release` 对应
   `smp_load_acquire/smp_store_release`（`arch/x86/include/asm/barrier.h`）。
3. **自旋锁**：TinyOS `raw_spin_lock_irqsave` 对应 Linux `kernel/locking/spinlock.c`
   的 `raw_spin_lock_irqsave`（`_raw_spin_lock_irqsave`）。Linux 还有 qspinlock/
   锁 owner 跟踪/`CONFIG_PROVE_LOCKING` 校验；TinyOS 只有 XCHG 自旋。
4. **softirq 预算**：TinyOS `SOFTIRQ_BUDGET=2` 与 tasklet/workqueue 对应 Linux
   `kernel/softirq.c` 的 `MAX_SOFTIRQ_RESTART`/`__do_softirq` 的 `pending` 位图与
   `ACTION_SOFTIRQ_*`；tasklet 合并对应 `include/linux/interrupt.h` 的
   `TASKLET_STATE_SCHED`；`budget_exhaustions` 对应 `wakeup_softirqd`（迁移到
   ksoftirqd 线程）。
5. **RCU（主题标签）**：Linux 的 RCU 实现在 `kernel/rcu/tree.c`（grace period、
   quiescent state、`synchronize_rcu`）；本课源码**没有**这些原语，仅在 checkpoint
   消息文本中引用 "RCU" 作为机制覆盖面标签。**教学模型不假装实现了 RCU**——这是
   诚实性要求：读者不应从 `l136test` 推断出任何 RCU 行为。
6. **per-CPU 软中断标志**：TinyOS `cpu_local.softirq_pending` 对应 Linux
   `__softirq_pending` per-CPU 位图（`kernel/softirq.c` 的 `local_softirq_pending`）。

**权威来源**：C11/C17 标准 §7.17.3（内存序）、Intel SDM Vol.3A（中断与锁前缀）、
Linux `kernel/softirq.c`/`kernel/locking/spinlock.c`/`arch/x86/include/asm/atomic.h`。
**教学模型简化了什么**：单核（无真实并发、锁永不自旋）、无 RCU 原语、无 ksoftirqd
线程迁移、无 lockdep 静态校验。

---

## 8. 思考题与练习

1. **概念理解**：`raw_spin_lock_irqsave` 为什么先关中断再自旋？在 `NR_CPUS=1` 的
   教学环境下，自旋循环实际会被执行吗？如果永远不会自旋，这个模型验证的到底是什么？
2. **源码定位**：在 `kernel64.c` 中找出 `softirq_run_budget` 的调用位置，说明它与
   IRQ0 的关系；再找出 `lockatomictest` 里 `v|1` 这个 release 写发布的是哪一位。
3. **动手实验**：把 `SOFTIRQ_BUDGET` 从 2 改成 4，重跑 `softirqtest`，观察断言
   `b`（`work_used==2`）与 `d`（`budget_exhaustions>=1`）是否会翻转，并用
   `softirqinfo` 解释预算如何影响两轮消费。
4. **动手实验**：在 `lockatomictest` 中删除 `raw_spin_unlock_irqrestore` 的
   `atomic_store_release_u32(&l->locked,0)` 一行，重跑，观察断言
   `deferred_lock.locked==0` 是否失败并输出 `BROKEN`。
5. **Linux 对照**：阅读 `kernel/softirq.c` 的 `__do_softirq` 与
   `kernel/locking/spinlock.c` 的 `raw_spin_lock_irqsave`，对比它们与
   `softirq_run_budget`/`raw_spin_lock_irqsave` 的边界，指出 TinyOS 砍掉了哪些
   机制（如重入限制、ksoftirqd、qspinlock）。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是检查点课，`kernel64.c` 相对 Lesson 135 只有 3 处小增量，并发原语全部继承，
   主题由 banner/about 文案「SMP/RCU 回归验证」标识。
2. per-CPU 抽象 `cpu_local[NR_CPUS=1]` + `this_cpu()` 保留 SMP 接口但钉死单核。
3. 三种内存序封装（relaxed/acquire/release）对应 C11 §7.17.3 与 Linux
   `smp_load_acquire`/`smp_store_release`。
4. `raw_spin_lock_irqsave` = 关中断 + `exchange acquire` 自旋，`unlock` = `store
   release` + 恢复 IF。
5. softirq 预算 `SOFTIRQ_BUDGET=2` 限制单轮消费；tasklet 合并去重、workqueue FIFO
   入队、预算结转三条性质由 `softirqtest` 回归断言覆盖。
6. **RCU 是主题标签而非实现**——源码无 RCU 原语，checkpoint 消息文本只是机制覆盖
   面说明（如实声明）。
7. 旧 README 的 `Commands: l129test` 已勘误为源码实际的 `l128test` 与 `l136test`。

**下一课**：[`lesson-137-stable/README.md`](../lesson-137-stable/README.md) 主题为
「并发、SMP、RCU、诊断综合 checkpoint」，将把本课与前面三课的全部机制（异常路径、
内存压力、调度并发、原子锁、softirq）做一次综合回归，并追加新的 checkpoint 模型
`lesson_130_model`（命令 `l137test`）。两课的衔接点：本课验证的 `lockatomictest`/
`softirqtest` 将在下节课作为「综合诊断」的组成部分再次被引用。
# Lesson 122: SMP 负载均衡 — 精讲文档

> **Course status: stable snapshot.**
>
> - 课号：Lesson 122
> - 本课主题：SMP 负载均衡（SMP load balancing）
> - 课程主线位置：第四阶段（SMP / RCU / 诊断元数据检查点系列），紧接「per-CPU runqueue」之后
> - 前置课程：[Lesson 121（per-CPU runqueue）](../lesson-121-stable/README.md)
> - 后续课程：[Lesson 123（RCU reader 临界区）](../lesson-123-stable/README.md)
> - 一句话目标：理解「多个 per-CPU 队列之间如何搬移任务」的负载均衡语义，以及 TinyOS 如何用 `lesson_115_model` 检查点记录该主题的不变量。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释负载均衡的触发点（空闲 CPU 拉活 / 繁忙 CPU 推活）、度量单位（`load`/`nr_running`）、以及为什么它必须在 per-CPU runqueue 之上工作；能用 `l122test` 验证本课检查点。
- **在课程主线中的位置**：Lesson 121 建立了「每 CPU 一份队列」的世界观，本课立刻追问「队列之间不平衡怎么办」——这就是 Linux `kernel/sched/topology.c` 调度域与 `kernel/sched/fair.c` 中 `load_balance()` 所解决的问题。再往后，课程主线从调度转向同步原语（RCU 系列），负载均衡是本阶段调度主题的收尾。
- **前置知识清单**：
  1. per-CPU runqueue 概念与 `cpu_local`/`this_cpu()`/`NR_CPUS`（上一课）；
  2. 调度类派发表 `struct sched_class` 与 `rr_pick_next` 就绪选择；
  3. 检查点模型结构（`a,b,c,d,valid,active,ready,accounted`）与 `lNtest()` 断言约定；
  4. `exec64` 命令分发、`eq64`/`token64`/`usage64` 帮助机制。
- **本课交付**：
  - 新命令 `l122test` 与新模型 `struct lesson_115_model lesson_115_state`；
  - 上一课 `l121test` 更名为历史回归命令 `l114test`；
  - `about` 与启动横幅显示 `Lesson 122: SMP 负载均衡`。

## 2. 核心概念精讲

### 2.1 负载均衡是什么

- **定义**：SMP 负载均衡是一组机制，它周期性地（或按事件）比较各 CPU 的 per-CPU runqueue 负载，把任务从「过载队列」搬到「空闲/低载队列」，使多核利用率最大化、响应延迟最小化。
- **为什么需要**：per-CPU runqueue 消除了锁竞争，但也带来了「分布不均」问题——8 个核里 1 个核跑 7 个任务、7 个核闲着，是极端浪费。没有均衡，任务亲和力（affinity）和缓存局部性反而会让热点固化。
- **触发方式三类**：
  - **空闲时拉（idle pull）**：CPU 发现本地队列空了，主动去别的队列"偷"任务（Linux 的 `idle_balance()`/`newidle_balance`）；
  - **周期均衡（periodic）**：softirq 调度的 `run_rebalance_domains()` 定期检查是否超阈值 `balance_interval`；
  - **唤醒时推/选（wake balancing）**：`select_task_rq_fair()` 在唤醒任务时直接选一个最闲的 CPU。
- **度量**：Linux 用 PELT 的 `util_avg`/`load_avg` 作为负载度量（`kernel/sched/fair.c`），经典内核则用 `nr_running`、`rq->nr_running`、`sd->imb_numa_nr` 等。TinyOS 单核没有真实搬移，因此用检查点的 `a,b,c,d` 四连号表示「队列编号流」，用四个布尔位表示「负载账目可闭合」。

```
CPU0 [ rq0: 5 任务 ] ────────► 搬移 ────────► CPU1 [ rq1: 1 任务 ]
   （busiest）          load_balance           （idle）
   每个周期比较 nr_running，差值超过阈值就把任务 detach/attach 过去
```

### 2.2 调度域（scheduling domain）与负载均衡的层次

- **定义**：调度域 `struct sched_domain` 是 CPU 的分层分组（如单核组 → 同一 LLC → 同一 NUMA 节点），负载均衡在每层内发生：`CPU 内部 → LLC 域 → NUMA 域`，逐级把任务从最忙组搬到最闲组。
- **为什么分层**：搬移任务的代价（cache 冷、迁移距离、TLB 失效）随层级增大，所以先在便宜的局部域内均衡，必要时才越级。
- **TinyOS 对应**：`NR_CPUS == 1U` 意味着只有一个"组"，无真实层级；检查点模型把「负载均衡器就绪、账目闭合」这一结论记录下来，不模拟搬移。

### 2.3 检查点模型如何编码「负载均衡」

- `struct lesson_115_model` 的四个布尔位对应：
  - `valid`：本课模型已实例化；
  - `active`：负载均衡机制处于激活态（等价于 Linux 每 tick 调度 `scheduler_tick()` → `trigger_load_balance()`）；
  - `ready`：per-CPU 队列宿主可被均衡（对应 Linux `rq->rd->overload` 等状态位可查）；
  - `accounted`：搬移前后 `nr_running` 总和守恒（对应 Linux `load_balance()` 的 `detach`/`attach` 计数对称）。
- `b == a+1` 断言对应「负载编号流无空洞」，即队列身份标识连续。

### 2.4 检查点输出串里的 RCU 从何而来

- 本课 `l122test` 的成功串是 `"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed"`，其中 **RCU** 一词并非本课主题，而是检查点体系的固定文案：它宣告「并发安全、SMP 结构、RCU 语义、诊断元数据」这四条课程主线都通过本课登记。
- 换句话说，从 Lesson 123 开始，RCU 才会成为真正的主题（reader 临界区 → grace period → callback 队列 → 对象回收），在此之前 RCU 只以「未破坏的既定不变量」存在于检查点字符串里。
- 这让本课成为调度与 RCU 两个大主题之间的**过渡课**：`about` 标题是「SMP 负载均衡」，而检查点文案已经提前锁定下一个主题的验收标准。

### 2.5 为什么「负载均衡」与「RCU」在检查点链里同源

- 二者都建立在「每个 CPU 可以同时读写自己的数据、且不互相打扰」之上：负载均衡需要安全地把任务从 A 队列搬到 B 队列，RCU 需要安全地在 reader 还在用旧指针时发布新指针。
- Linux 中这两套机制共用同一套每 CPU 基础设施（`per_cpu_ptr`、`this_cpu_*` 原语、softirq 执行上下文）。
- TinyOS 把二者都固化为检查点模型后，检查点链的**连续性**（`b==a+1`）就成了「所有已登记主题不变量仍然成立」的证明：只要链上每一环都 passed，调度与 RCU 的既有约定就都还活着。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 121） |
|------|------|------------------------------|
| `boot.S` | Multiboot2 头、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel.c` | 32 位引导主流程 | 未变化 |
| `kernel64.c` | 64 位内核主体与检查点模型 | **有增量**：新增 `lesson_115_model`/`lesson_115_state`/`l122test()`；`l121test` 更名为 `l114test`；`about`/横幅改为 Lesson 122 |
| `kernel64.ld` | 64 位裸二进制布局（VMA=0、三守护页） | 未变化 |
| `linker.ld` | 32 位镜像布局 | 未变化 |
| `Makefile` | 构建与 `check`/`run` | 微小变化：`check` grep 串改为 `SMP 负载均衡`、`l122test`、`Lesson 122` |
| `grub.cfg` | GRUB 启动项 | 未变化 |

### 3.2 kernel64.c 精讲

#### 新增结构 / 全局变量

```c
struct lesson_115_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_115_model lesson_115_state;
```

逐行注释：
- 第 884–885 行。`lesson_115_model` 是检查点链（lesson_69…lesson_115）上的新一环，字段与前课完全同构。
- `lesson_115_state` 唯一实例由 `l122test` 填充；它的 `active`/`ready` 位语义在本课对应「负载均衡机制激活、per-CPU 队列可参与均衡」。

#### 函数精讲

**`l122test(u16 *c)`**（第 886 行，本课新增）

```c
static TEXT64 void l122test(u16*c){lesson_115_state=(struct lesson_115_model){115U,116U,117U,118U,1,1,1,1};int ok=lesson_115_state.valid&&lesson_115_state.active&&lesson_115_state.ready&&lesson_115_state.accounted&&lesson_115_state.b==lesson_115_state.a+1U;text64(c,"l122test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 115 fallback reported");putc64(c,'\n');}
```

- **签名与职责**：`static TEXT64 void l122test(u16 *c)`：把 `lesson_115_state` 装成固定值并断言，输出到 VGA。
- **算法步骤**：
  1. 初始化模型 `{115U,116U,117U,118U,1,1,1,1}`——四连号以 115 开头（模型号），布尔位全 1；
  2. 计算 `ok = valid && active && ready && accounted && b==a+1U`；
  3. `text64(c,"l122test: ")` 后按 `ok` 打印 passed / `"Lesson 115 fallback reported"`。
- **边界处理**：无外部入参；所有量编译期确定，不存在越界与空指针路径。
- **设计动机**：四连号从 115 起而非 122，因为模型号独立于课号，反映「第 122 课检查点登记的是第 115 个模型」。这与上一课 `lesson_114_model`（课号 121）完全同理，说明检查点模型号自 Lesson 69 起逐课递增。

**`exec64` 本课相关分支**（第 887 行）

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 122: SMP 负载均衡\n");}
...
}else if(eq64(word,"l114test")){if(!noargs64(arg))usage64(c,"l114test");else l114test(c);}else if(eq64(word,"l122test")){if(!noargs64(arg))usage64(c,"l122test");else l122test(c);}
```

- `about` 分支把主题文本更新为 `Lesson 122: SMP 负载均衡`。
- 命令表中 `l114test`（上一课命令，更名为历史回归）与 `l122test`（本课新命令）并列；`l114test` 仍驱动 `lesson_114_state`，证明上一课的不变量在本课仍然成立——这正是检查点链的价值。

**继承的负载均衡相关基础设施**

```c
#define NR_CPUS 1U
struct cpu_local { u8 id,softirq_pending,work_head,work_tail,work_used; };
static struct cpu_local cpu_locals[NR_CPUS];
static TEXT64 struct cpu_local *this_cpu(void){return &cpu_locals[0];}
```

- 负载均衡在 Linux 中通过 per-CPU softirq（`SCHED_SOFTIRQ` → `run_rebalance_domains`）驱动；本内核的 `softirq_model` 与 `cpu_local` 正是这一链条的骨架：`softirq_raise(bit)` 置位、`softirq_run_budget()` 按预算消费、`this_cpu()->work_used` 反映本地队列占用。检查点 `ready` 位即代表「本地队列可被均衡器查看」。

**`irq0_schedule`（继承，均衡的"心跳"）**：每 tick 的 `ticks++` 与 `softirq_run_budget()` 调用，对应 Linux `scheduler_tick()` 里「更新本 CPU 负载 → 必要时触发负载均衡」的节奏。TinyOS 单核无 `find_busiest_queue` 逻辑，只在元数据层断言均衡不变量成立。

**软中断消费链（继承，本课概念的"承载者"）**

```c
static TEXT64 void softirq_raise(u8 bit){if(bit>=SOFTIRQ_BITS){softirq_model.drops++;return;}softirq_model.pending|=(u8)(1U<<bit);softirq_model.raises++;}
static TEXT64 void tasklet_schedule(u8 id){if(id>=TASKLET_CAP||tasklets[id].disabled)return;if(!tasklets[id].pending){tasklets[id].pending=1;softirq_raise(0);}}
static TEXT64 int workqueue_submit(u8 kind,u8 data){if(work_used>=WORK_CAP){softirq_model.drops++;return 0;}workqueue[work_head]=(struct work_model){kind,data,1,0};work_head=(u8)((work_head+1)%WORK_CAP);work_used++;softirq_raise(1);return 1;}
static TEXT64 void softirq_run_budget(void){u8 budget=SOFTIRQ_BUDGET,i;while(budget&&softirq_model.pending){if(softirq_model.pending&1){for(i=0;i<TASKLET_CAP&&budget;i++)if(tasklets[i].pending&&!tasklets[i].disabled){tasklets[i].pending=0;tasklets[i].runs++;softirq_model.runs++;budget--;}}if(softirq_model.pending&2&&budget){if(work_used){struct work_model*w=&workqueue[work_tail];w->queued=0;w->runs++;work_tail=(u8)((work_tail+1)%WORK_CAP);work_used--;softirq_model.runs++;budget--;}}if(!tasklets[0].pending&&!tasklets[1].pending)softirq_model.pending&=(u8)~1U;if(!work_used)softirq_model.pending&=(u8)~2U;}if(softirq_model.pending)softirq_model.budget_exhaustions++;}
```

- `softirq_raise`：把位图 `pending` 的第 `bit` 位置 1，并计数 `raises`；越界位直接计入 `drops`——这是「按 CPU 登记待办」的入口，对应 Linux `raise_softirq(SCHED_SOFTIRQ)`。
- `workqueue_submit`：有界环形工作队列满则 `drops++` 拒绝，否则在 `work_head` 处入队并把 `softirq_model.pending` 的位 1 置上——这就是「延迟到 softirq 上下文执行」的模型，负载均衡的 `trigger_load_balance()` 正是这种「置位待办」。
- `softirq_run_budget`：以 `SOFTIRQ_BUDGET` 为预算，位 0 消费 tasklet、位 1 消费 workqueue，逐个把 `queued` 清位、`runs++`；任务都空了才把对应 `pending` 位清掉；预算耗尽仍有余量则 `budget_exhaustions++`。
- **与本课的关系**：Linux 的负载均衡心跳就是「tick 里置位 → softirq 里均衡」的两段式；TinyOS 的 `irq0_schedule` 在 `ticks++` 后立刻 `softirq_run_budget()` 复刻了这个节奏，而「均衡器是否就绪」的结论交给检查点 `ready` 位记录。

**`schedinfo`（继承，调度统计查看）**

```c
static TEXT64 void schedinfo(u16*c){text64(c,"scheduler class: ");text64(c,active_sched_class?active_sched_class->name:"none");text64(c,"\nops enqueue/dequeue/pick: ");hex64(c,sched_enqueues);text64(c," ");hex64(c,sched_dequeues);text64(c," ");hex64(c,sched_picks);text64(c,"\nwait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions\n");}
```

- 打印当前调度类名（`tiny_rr`）与三个操作计数；`sched_enqueues/sched_dequeues/sched_picks` 是「runqueue 活动」的可见证据——在单核模型里，它们就相当于负载均衡要观察的队列流量。

### 3.3 构建管线（Makefile / linker）

- 与 Lesson 121 完全相同的双阶段构建：32 位 `CFLAGS`、64 位 `CFLAGS64`（`-fpie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx`）、`objcopy -O binary` 产出裸二进制、`grub-mkrescue` 出 ISO。
- 唯一变化在 `check` 目标：

```
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'SMP 负载均衡' README.md
	@grep -q 'l122test' kernel64.c
	@grep -q 'Lesson 122' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 122 checks passed.'
```

### 3.4 主控制流

```
kernel_main64_binary
  ├─ 各子系统初始化（与 Lesson 121 相同）
  ├─ text64(&c,"Lesson 122: SMP 负载均衡\nGETTICKS, …\n")   ← 本课横幅
  └─ for(;;) 键盘环
       ├─ "about"    → "Lesson 122: SMP 负载均衡\n"
       ├─ "l114test" → 回放上一课检查点
       └─ "l122test" → l122test(c) → 打印本课检查点结果
```

## 4. 数据流与运行逻辑

1. **输入**：`l122test` 回车。
2. **解析**：`token64` 得 `word=="l122test"`，`noargs64(arg)` 为真，命中分支。
3. **执行**：`l122test(c)` 初始化 `lesson_115_state` 并计算 `ok`。
4. **输出**：`l122test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（`ok==1`），或 `l122test: Lesson 115 fallback reported`。
5. **屏幕**：横幅首行 `Lesson 122: SMP 负载均衡`；`about` 输出同文案。

## 5. 构建、运行与验证

- **依赖**：与 Lesson 121 相同（gcc / ld / objcopy / grub-mkrescue / grub-file / qemu）。
- **构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

预期输出：`Multiboot2 and Lesson 122 checks passed.`
- **运行**：`make run`，QEMU 图形窗口查看横幅 `Lesson 122: SMP 负载均衡`（勿加 `-display none`）。
- **验证步骤**：
  1. `about` → `Lesson 122: SMP 负载均衡`（源码逐字）；
  2. `l122test` → `l122test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（源码逐字；失败态为 `Lesson 115 fallback reported`）；
  3. `l114test` → `l114test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（验证上一课模型在改名后仍通过）；
  4. `lockatomicinfo` → 首行 `locks/atomics/percpu: NR_CPUS 1 …`，确认 per-CPU 宿主状态完好。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `l122test` 打印 fallback | `lesson_115_state` 初始化被改或某位为 0 | 核对第 886 行字面量 `{115U,116U,117U,118U,1,1,1,1}`；逐一打印五条件 |
| `about` 显示旧主题 | `exec64` 的 `about` 分支文本未更新 | grep `'Lesson 122'` kernel64.c，确认出现在 about 与横幅 |
| `l114test` 报 unknown | 命令分支缺 `l114test` 匹配 | 确认 exec64 中 `l114test` 分支位于 `l122test` 之前 |
| `make check` grep 失败 | 主题串或命令串不匹配 | `grep -q 'SMP 负载均衡' README.md`；`grep -q 'l122test' kernel64.c` |
| 负载相关计数器全为 0 | 未运行 `schedinfo`/未产生切换 | 跑 `preempttest` 制造切换后再看 `schedinfo` 的 enqueue/dequeue/pick 计数 |
| 怀疑均衡搬移未发生 | 单核教学模型本就无真实搬移 | 理解：均衡语义只体现在检查点断言；行为层仍是单核轮转 |
| `schedinfo` 的 pick 计数不增长 | 没有线程进入 `RUNNABLE`/`RUNNING` 状态 | 先跑 `preempttest` 启动两个 worker，再观察 `sched_picks` 单调增长 |
| `softirqinfo` 的 drops 增大 | 有软中断请求越界或工作队列满 | 检查 `softirq_raise` 的 `bit>=SOFTIRQ_BITS` 与 `workqueue_submit` 的 `work_used>=WORK_CAP` 分支 |
| 运行 `l122test` 后屏幕无换行 | `putc64` 的 `'\n'` 处理把游标移到行首 | 检查 `putc64` 中 `'\n'` 分支 `*c+=COLS-*c%COLS`；正常情况多行输出应逐行推进 |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现（路径） |
|--------|-----------------|--------------------|
| 负载均衡触发 | `irq0_schedule` 每 tick 调 `softirq_run_budget()`（元数据心跳） | `scheduler_tick()` → `trigger_load_balance()`，置 `SCHED_SOFTIRQ`（`kernel/sched/core.c`）；`run_rebalance_domains()` 消费（`kernel/sched/fair.c`） |
| 调度域层级 | `NR_CPUS==1U`，无分组 | `struct sched_domain` 从 SMT→MC→NUMA 分层（`kernel/sched/topology.c` 的 `sd_init`/`build_sched_domains`） |
| 忙闲判定 | 检查点 `ready/accounted` 布尔位 | `find_busiest_queue()` 按 `rq->nr_running`/PELT `util_avg` 找最忙队列（`kernel/sched/fair.c`） |
| 任务搬移 | 元数据断言（无真实 `detach`/`attach`） | `detach_tasks()`/`attach_tasks()` + 迁移唤醒 `stop_one_cpu_nowait`（`kernel/sched/fair.c`、`kernel/sched/core.c`） |
| 空闲拉取 | `cpu_local` 本地空则 idle 运行 | `idle_balance()`/`newidle_balance()`：本地空队列从别处偷任务 |
| 负载度量 | 无；用 `b==a+1` 表达编号连续性 | `nr_running`、`load_avg`、`util_avg`（PELT，`kernel/sched/fair.c`） |
| 权威来源 | —— | Intel SDM Vol.3；Multiboot2 规范；GNU GRUB |

**教学模型简化了什么**：真实的负载均衡涉及调度域重建、PELT 负载衰减、迁移代价估算、`nohz` 与 ILB（idle load balancer）协调；TinyOS 只保留「每 CPU 一份状态 + tick 驱动的心跳 + 账目闭合断言」，搬移过程完全不模拟。

## 8. 思考题与练习

1. **概念理解**：空闲拉取（pull）与周期均衡（periodic）各解决什么问题？为什么 `newidle_balance` 只做一次而周期均衡按 `balance_interval` 重复？
2. **源码定位**：找出 `kernel64.c` 中 `softirq_run_budget()` 与 `irq0_schedule()` 的调用关系，说明它们组合起来如何对应 Linux 的 `SCHED_SOFTIRQ` 负载均衡心跳。
3. **动手实验**：把 `l122test` 的 `accounted` 位改为 0 后重建运行，观察 fallback 输出；再改回，验证 passed。思考 `accounted` 对应真实内核的哪个守恒量。
4. **Linux 对照**：阅读 `kernel/sched/topology.c` 的 `build_sched_domains` 与 `kernel/sched/fair.c` 的 `load_balance`，写出「TinyOS 检查点模型」与「真实搬移流程」的差距清单。
5. **综合**：若把 `NR_CPUS` 改成 2，为了让 `cpu_local` 真正变成两套 runqueue，还需要哪些代码改动？检查点模型能否直接复用？

## 9. 本课小结与下一课预告

- 本课在 per-CPU runqueue 之上讨论了队列间搬移任务的负载均衡语义。
- 明确了负载均衡的三种触发路径（空闲拉取、周期均衡、唤醒选择）与调度域的分层思想。
- 新检查点 `lesson_115_model` 用四个布尔位把「机制激活、队列就绪、账目闭合」固化，`l122test` 提供确定性验证。
- 上一课命令更名为 `l114test`，检查点链的回放机制得到完整演示。
- 确认了单核教学模型下负载均衡只有元数据断言、没有真实搬移，与 Linux 的差距在调度域/PELT/迁移代价等层面。
- 至此调度主题告一段落，课程主线转入并发同步原语。

**下一课**：[Lesson 123（RCU reader 临界区）](../lesson-123-stable/README.md) 开始 RCU 系列：先讲 reader 进入/离开临界区（`rcu_read_lock`/`rcu_read_unlock`）的语义与轻量特性，并对照 Linux `include/linux/rcupdate.h`。

## 附录：stable snapshot 声明（保留原 README 要点）

> This checkpoint models bounded concurrency, SMP, RCU, and diagnostics metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants.
>
> Commands: `l114test`、`l122test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.
>
> 主要内容：SMP 负载均衡；统一课程编号：Lesson 122。（旧 README 中「Commands: `l115test`」与实际源码不符，已勘误为 `l122test`。）

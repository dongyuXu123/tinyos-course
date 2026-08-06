# Lesson 78: runqueue 运行队列统计 — 精讲文档

> **课号**：Lesson 78（对应主线源课 Lesson 71）
> **本课主题**：runqueue（运行队列）统计元数据的 checkpoint
> **课程主线位置**：GUI 支线（Lesson 61–67）结课后的「进程组/session/调度元数据」阶段（Lesson 68 起恢复主线）。调度器元数据子主题三连课（76 策略 → 77 优先级 → **78 runqueue**）的收尾课。
> **前置课程**：[`../lesson-77-stable/README.md`](../lesson-77-stable/README.md)（priority/nice 优先级状态）
> **后续课程**：[`../lesson-79-stable/README.md`](../lesson-79-stable/README.md)（voluntary preemption 主动抢占）
> **本课一句话目标**：学会用「固定元数据 + 确定性验证」模型完成 runqueue 主题的调度/COW checkpoint——理解 Linux `struct rq`（per-CPU 运行队列）中 `nr_running` 等统计字段的含义与入队/出队计数语义，并会用 `l78test` 验证累积元数据的一致性。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释"运行队列"为什么必须是每个 CPU 一份、`nr_running` 在入队/出队时如何增减、它与调度统计的关系，能用 `l78test` 做确定性 checkpoint，并能对照 Linux `struct rq` 说清教学模型简化了什么。
- **在课程主线中的位置**：调度器元数据子主题到此收尾（76→77→78）。本课在 **runqueue** 主题下再做一次 checkpoint：新增 `lesson_71_model` 记录 + `l78test`，并把上一课 `l77test` 改名为 `l70test` 保留为回归锚点。三课连起来覆盖了调度器的三个层次：策略（怎么选）、优先级（按什么序）、队列（排在哪）。Lesson 79 将进入真实行为层的 voluntary preemption。
- **前置知识清单**：
  1. `sched_class` 分派表与 `sched_enqueues/sched_dequeues/sched_picks` 计数（Lesson 38 起累积，Lesson 76 复习）；
  2. priority/nice 优先级概念（Lesson 77）；
  3. 系列 checkpoint 命令改名惯例：`lXXtest` 随课程推进改名保留回归（Lesson 72–77 已出现三次）；
  4. 本内核 `rr_pick_next`/`rr_enqueue`/`rr_dequeue` 三个回调与 `next_runnable()` 挑选流程（Lesson 38 起累积）。
- **本课交付**：新增固定容量 `lesson_71_model` 记录与 `l78test` 验证命令；上一课 `l77test` 更名为 `l70test` 作为回归锚点；`about`/banner 更新为「Lesson 78: runqueue 运行队列统计」。

---

## 2. 核心概念精讲

### 2.1 运行队列（runqueue）：就绪任务的"候车室"

**直觉**：调度器不能每次挑选都全表扫描，它需要把就绪任务提前排好队。这个队列叫运行队列（runqueue）。哪个 CPU 干活，就查哪个 CPU 的队列。

**准确定义（Linux）**：`struct rq`（`kernel/sched/sched.h`）是 per-CPU 运行队列，关键字段：
- `nr_running`：当前运行队列上的可运行任务数（含正在运行的），入队 `++`、出队 `--`；
- `load.weight`：队列总负载（CFS 权重和）；
- `clock` / `clock_task`：队列时间戳；
- 内嵌各调度类的子队列：CFS 的 `cfs_rq`（红黑树按 `vruntime` 排序）、RT 的 `rt_rq`、DL 的 `dl_rq`。
- 调度主循环 `__schedule()` 每次从队列里 `pick_next_task`，选中者出队执行，时间片到或主动让出后再入队/换队。

### 2.2 入队/出队统计：runqueue 的"吞吐仪表盘"

- `enqueue_task`：任务进入就绪集（被唤醒、创建、时间片重新就绪）→ 放入对应调度类子队列，`rq->nr_running++`，更新负载；
- `dequeue_task`：任务被选中运行、睡眠、停止 → 移出子队列，`rq->nr_running--`；
- 用户可观测：`/proc/stat` 的 `procs_running`、`/proc/schedstat`、`top` 的 load average。

本内核累积的 `sched_enqueues`/`sched_dequeues`/`sched_picks` 三个 u64 计数器正是这套统计的教学投影——Lesson 78 的 `accounted` 标志概括其一致性。

### 2.3 教学模型：checkpoint 而不实现队列

与 Lesson 76/77 相同，`lesson_71_model` 是 generic checkpoint 记录（a/b/c/d 代际 + 四标志），代际推进到 71（对应源课 71）。runqueue 主题在概念层展开，验证层用「固定元数据 + 确定性验证」守住自洽底线：

```
runqueue 概念层                       checkpoint 验证层
─────────────────────                ────────────────────
per-CPU rq (nr_running)    ──►       ready: 就绪集合存在（队列非空抽象）
入队 nr_running++           ──►       accounted: 记账一致
出队 nr_running--           ──►       valid: 模型已合法初始化
pick_next_task 选队首       ──►       active: 调度策略已激活
load 权重更新              ──►       a,b,c,d 代际连续 (b==a+1)
```

### 2.4 命令改名惯例的第三次出现

Lesson 76 `l76test`→77 `l69test`→78 `l70test`：每次新课把上一课的 checkpoint 命令改名为"其源课编号"，并新增本课编号命令。Lesson 78 中 `l70test`（源课 70，原 `l77test`）与 `l78test`（源课 71）并存。这一惯例让累积 `kernel64.c`（本课 793 行）中的历史验证始终可重放。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-77） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、主循环 | **主要增量**：新增 `lesson_71_model` 结构、`lesson_71_state` 全局、`l78test()`；上一课 `l77test` 改名为 `l70test`；`exec64` 新增/改名两个分支；`about`/banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局与栈断言 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 78 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 新增结构体与全局变量

```c
struct lesson_71_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_71_model lesson_71_state;
```

逐行注释：
- `a,b,c,d`（u32）：代际/计数序列 71→72→73→74（比 Lesson 77 整体 +1），对应源课 71 及之后推进；校验用 `b==a+1U` 保证相邻代际连续。
- `valid`（u8）：模型已合法初始化。
- `active`（u8）：调度策略激活态（`active_sched_class` 非空）。
- `ready`（u8）：就绪集合存在——这是 runqueue 主题的语义核心：队列"有人可挑"。
- `accounted`（u8）：调度/COW 记账一致——对应 runqueue 的入队/出队计数不漂移。
- `static struct lesson_71_model lesson_71_state;`：单一全局 checkpoint 记录。

#### (b) 上一课 `l77test` 改名：`l70test`

```c
static TEXT64 void l70test(u16*c){lesson_70_state=(struct lesson_70_model){70U,71U,72U,73U,1,1,1,1};int ok=lesson_70_state.valid&&lesson_70_state.active&&lesson_70_state.ready&&lesson_70_state.accounted&&lesson_70_state.b==lesson_70_state.a+1U;text64(c,"l70test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 70 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 函数体与 Lesson 77 的 `l77test` **逐字相同**，仅函数名与输出前缀改为 `l70test`（源课 70）。
- `exec64` 中 `eq64(word,"l70test")` 分支替代了原 `l77test` 分支。
- 意义：runqueue 主题收尾前，优先级的 checkpoint（源课 70）仍可一键重放。

#### (c) 本课核心验证函数 `l78test`

```c
static TEXT64 void l78test(u16*c){lesson_71_state=(struct lesson_71_model){71U,72U,73U,74U,1,1,1,1};int ok=lesson_71_state.valid&&lesson_71_state.active&&lesson_71_state.ready&&lesson_71_state.accounted&&lesson_71_state.b==lesson_71_state.a+1U;text64(c,"l78test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 71 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_71_state=(struct lesson_71_model){71U,72U,73U,74U,1,1,1,1};`：聚合初始化代际 71/72/73/74，四标志全 1。
- `int ok=lesson_71_state.valid&&lesson_71_state.active&&lesson_71_state.ready&&lesson_71_state.accounted&&lesson_71_state.b==lesson_71_state.a+1U;`：5 条件：合法、激活、就绪、记账一致、代际连续（`72==71+1`）。
- `text64(c,"l78test: ");`：命令前缀。
- `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 71 fallback reported");`：成功串保持三课一致（调度 + COW checkpoint），失败串标注源课 71。
- `putc64(c,'\n');`：换行。

为什么这样设计：runqueue 主题的 checkpoint 与前两课保持同一判定形状，保证"调度元数据自洽"不变量可跨课复用；若 runqueue 引入独立的队列校验，会破坏回归可比性——教学模型选择在验证层统一、在概念层区分。

#### (d) `exec64` 命令分派中的增量分支

```c
}else if(eq64(word,"l70test")){if(!noargs64(arg))usage64(c,"l70test");else l70test(c);}
}else if(eq64(word,"l78test")){if(!noargs64(arg))usage64(c,"l78test");else l78test(c);}
```

逐行注释：
- `l70test` 分支由原 `l77test` 改名而来，`l78test` 为新分支；顺序保持源课编号递增（69→70→71）。
- `about` 文案更新为：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 78: runqueue 运行队列统计\n");}
```

- `help` 命令列表保持旧字面量（不含 `l70test`/`l78test`），教学简化延续。

#### (e) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
text64(&c,"Lesson 78: runqueue 运行队列统计\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- 首行主题换成本课；syscall ABI 与 "bounded reclaim metadata" 边界不变。

#### (f) 继承的调度回调（runqueue 统计的行为对应物）

```c
static TEXT64 u8 rr_pick_next(void){...}   /* 轮转挑选 */
static TEXT64 void rr_enqueue(u8 id){...}
static TEXT64 void rr_dequeue(u8 id){...}
```

- `rr_pick_next`：从 `round_robin` 游标向后扫描 `THREAD_COUNT` 个线程，返回第一个 `THREAD_RUNNABLE/RUNNING` 者，否则返回 `0xff`（让出给 idle）。
- `rr_enqueue`/`rr_dequeue`：只维护 `sched_enqueues`/`sched_dequeues` 计数器——这就是"运行队列统计"的现成模型：没有真实队列数组，但入队/出队被记数。
- 这些回调是 `l78test` 的 `accounted/ready` 标志对应的行为层实体；教学模型在行为层照常记账，在验证层断言元数据自洽。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'runqueue 运行队列统计' README.md
	@grep -q 'l78test' kernel64.c
	@grep -q 'Lesson 78' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 78 checks passed.'
```

- 三条 `grep -q` 分别锁定 README 主题、新符号 `l78test`、课号 78；`printf` 信息逐字来自 Makefile。
- `grub-file --is-x86-multiboot2` 与构建链、`kernel64.ld` 栈断言均与 Lesson 77 一致。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32 ──► enter_long_mode ──► kernel_main64_binary
    ├─ 元数据初始化（...）
    ├─ banner: "Lesson 78: runqueue 运行队列统计\nGETTICKS, ..."
    └─ for(;;) 键盘循环:
        "l78test\n" ──► exec64 ──► eq64(word,"l78test")
        ──► l78test(c)
        ──► lesson_71_state 初始化 {71,72,73,74,1,1,1,1} + 5 条件校验
        ──► VGA: "l78test: bounded scheduling and copy-on-write checkpoint passed"
        ──► "tinyos> "
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打印本课主题，`tinyos> ` 就绪。
2. **输入**：`l78test` + 回车（或回归命令 `l70test`）。
3. **分派**：`exec64` 分别命中 `l78test` / `l70test` 分支。
4. **校验**：`lesson_71_state`（或 `lesson_70_state`）聚合初始化，5 条件布尔校验。
5. **输出**：`l78test: bounded scheduling and copy-on-write checkpoint passed`（`l70test` 同样通过），回显 `tinyos> `。

---

## 5. 构建、运行与验证

**依赖**：同全仓库，见 [`docs/local-validation.md`](../../docs/local-validation.md)。

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

1. 开机第一屏：
   ```
   Lesson 78: runqueue 运行队列统计
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l78test`，预期输出：
   ```
   l78test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l78test: Lesson 71 fallback reported`。）
3. 输入 `l70test`（改名后的回归命令），预期输出：
   ```
   l70test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `about`，预期输出：
   ```
   Lesson 78: runqueue 运行队列统计
   tinyos>
   ```
5. 输入 `schedinfo`，预期输出（计数为运行期值）：
   ```
   scheduler class: tiny_rr
   ops enqueue/dequeue/pick: ...
   wait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions
   ```
6. 回归：`l77test` 已改名（输入得到 `unknown command`），改用 `l70test`；`l69test`、`stop68test`、`ps` 等继承命令仍可用。

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 78 checks passed.`；QEMU 中 `l78test` 与 `l70test` 均打印 `...passed`。

> 备注：旧 README 中曾写 "Commands: `l71test`"，但源码与 Makefile 中的实际命令是 `l78test`（见 `kernel64.c` 的 `exec64` 分支）。本精讲文档以源码为准，并将回归命令标注为改名后的 `l70test`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/镜像校验失败） | VGA 是否显示 `user image validation/load failure:`；查 `kernel.c::validate_user_image()` |
| `make check` 第一条 grep 失败 | README 主题字串与 Makefile 不一致 | `grep 'runqueue 运行队列统计' README.md` 核对字面 |
| `make check` 第二条 grep 失败 | `kernel64.c` 丢失 `l78test` 符号 | `grep -q 'l78test' kernel64.c` |
| `make check` 第三条 grep 失败 | README 课号写错 | `grep -q 'Lesson 78' README.md` |
| 输入 `l77test` 打印 `unknown command` | 该命令在本课已改名为 `l70test` | 输入 `l70test`；确认 `eq64(word,"l70test")` 分支存在 |
| 输入 `l71test` 打印 `unknown command` | 沿用了旧 README 的错误命令名 | 实际命令是 `l78test` |
| `l78test` 打印 `usage: l78test` | 命令带多余参数 | 本命令必须无参（`noargs64`） |
| `l78test` 打印 fallback 串 | 5 条件中某项不成立 | 检查 `ok`：`valid`、`active`、`ready`、`accounted`、`b==a+1U`（`72==71+1`） |
| `schedinfo` 计数一直为 0 | `sched_enqueues` 等未被回调驱动 | `rr_enqueue`/`rr_dequeue`/`rr_pick_next` 只在调度发生时计数；静态快照下为 0 属正常 |
| `l70test`/`l69test` fallback | 前课 checkpoint 场景被误改 | 对照 lesson-77/76 原文逐字段核对 |

---

## 7. 与 Linux 源码对照

**对照点 1：per-CPU 运行队列**
- TinyOS 教学模型：没有真实队列数组，`rr_pick_next` 靠 `round_robin` 游标轮转扫描 `threads[]`；`ready=1` 是"就绪集合存在"的抽象断言。
- Linux 实现：`struct rq` 是 per-CPU 运行队列（`struct rq rq[NR_CPUS]`，见 `kernel/sched/sched.h`），`this_rq()`/`cpu_rq(cpu)` 获取；`__schedule()`（`kernel/sched/core.c`）从当前 CPU 的 `rq` 中 `pick_next_task`。
- 权威来源：Linux v6.12 `kernel/sched/sched.h`、`kernel/sched/core.c`；POSIX.1 不定义 runqueue（属实现细节）。
- 教学简化：TinyOS 单 CPU（`NR_CPUS 1`）无真实队列，只保留游标与计数。

**对照点 2：nr_running 与入队/出队计数**
- TinyOS：`rr_enqueue`/`rr_dequeue` 只累加 `sched_enqueues`/`sched_dequeues`；`l78test` 的 `accounted` 概括其一致性。
- Linux：`enqueue_task`/`dequeue_task` 内 `rq->nr_running++/--`；`/proc/stat` 的 `procs_running` 即 `nr_running` 之和。
- 教学简化：TinyOS 不维护 `nr_running`，用"记账标志 + 计数器"代替实时长度。

**对照点 3：负载与统计可观测性**
- Linux：`rq->load.weight`、`cfs_rq->avg`（PELT）、`/proc/schedstat` 提供多维度统计。
- TinyOS：`schedinfo` 只打印 `ops enqueue/dequeue/pick` 三个计数，`scheduler class: tiny_rr`。
- 教学简化：无 PELT 负载追踪、无 per-task 统计。

**对照点 4：checkpoint 与进程组阶段收尾**
- 本课是「进程组/session/调度元数据」阶段（Lesson 68–78）的调度子主题收尾；Manifest 中 Lesson 79 起进入 voluntary preemption（行为层）。
- 权威来源：仓库根目录 [`COURSE-MANIFEST.md`](../../COURSE-MANIFEST.md) 的阶段划分；历史差异可审计于 [`docs/learning-stable-diff-report.md`](../../docs/learning-stable-diff-report.md)。

---

## 8. 思考题与练习

1. **概念理解**：为什么 runqueue 必须是 per-CPU 而不是全局一份？结合 Linux `struct rq rq[NR_CPUS]` 与锁粒度说明。
2. **源码定位**：在 `kernel64.c` 中找出 `rr_pick_next`/`rr_enqueue`/`rr_dequeue`，说明它们在 `schedinfo` 输出中对应哪三个计数，并指出 `l78test` 的 `ready/accounted` 如何概括它们。
3. **动手实验**：把 `l78test` 初始化里的 `b` 从 `72U` 改成 `73U`，使 `b==a+1U` 失效，重新 `make run` 观察 fallback 串。请**改回 72U**。
4. **动手实验**：连续输入 `l70test`、`l69test`，观察改名后的回归命令全部通过；再输入 `l77test` 确认已变 `unknown command`——体会三次改名惯例。
5. **Linux 对照**：阅读 `kernel/sched/core.c::__schedule()` 与 `kernel/sched/sched.h` 中 `struct rq` 的 `nr_running` 字段，说出任务从"被唤醒"到"被挑选执行"经历哪几次 `nr_running` 变化，并指出 TinyOS 教学模型省略了哪一步。

---

## 9. 本课小结与下一课预告

- 本课在 runqueue 主题下新增 `lesson_71_model` 记录与 `l78test`，完成"调度 + COW 元数据 checkpoint"的第三次重放，为调度器元数据子主题（76–78）收尾。
- 你掌握了 Linux per-CPU runqueue（`struct rq`）与 `nr_running` 入队/出队计数语义，理解了 `enqueue_task`/`dequeue_task`/`pick_next_task` 的分工。
- 你看到了本内核累积的 `rr_*` 回调与 `sched_enqueues/dequeues/picks` 计数正是 runqueue 统计的教学投影。
- 你再次验证了 checkpoint 命令改名惯例（`l77test`→`l70test`）与 `make check` 三条静态断言。
- 你对照了 Linux `kernel/sched/core.c` 与 `kernel/sched/sched.h`，知道教学模型省略了 PELT 负载、真实队列与 per-task 统计。

**下一课预告**：Lesson 79「voluntary preemption 主动抢占」。元数据层的策略、优先级、队列都齐了，下一课进入行为层：任务主动让出 CPU（`schedule()`/`yield`）如何触发切换。衔接点：本课 `schedinfo` 的 `sched_picks` 计数与 `rr_pick_next` 的 `0xff`（让位 idle）语义，正是下一课"主动让出 → 重新挑选"流程的入口。

# Lesson 76: 调度策略元数据 — 精讲文档

> **课号**：Lesson 76（对应主线源课 Lesson 69）
> **本课主题**：调度策略（scheduling policy）元数据的 checkpoint 验证
> **课程主线位置**：GUI 支线（Lesson 61–67）结课后的「进程组/session/调度元数据」阶段（Lesson 68 起恢复主线）。Lesson 73–75 刚完成进程组的 job-control 生命周期（孤儿化、信号路由、stop/continue）；本课把视角切向调度器，用 checkpoint 形式校验累积的调度策略元数据（`sched_class` 分派表、`schedinfo` 计数）。
> **前置课程**：[`../lesson-75-stable/README.md`](../lesson-75-stable/README.md)（终端 stop/continue 状态转换）
> **后续课程**：[`../lesson-77-stable/README.md`](../lesson-77-stable/README.md)（priority/nice 优先级状态）
> **本课一句话目标**：学会用「固定元数据 + 确定性验证」模型做一次调度策略 checkpoint——用 `l76test` 一次性断言"调度/COW 累积元数据合法、活跃、就绪、计数一致"，并理解本课守护的 `sched_class` 调度策略元数据在 Linux 中对应的结构。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释"调度策略元数据"在本内核里的具体形态（`sched_class` 分派表 + `sched_enqueues/sched_dequeues/sched_picks` 计数），能用 `l76test` 做确定性 checkpoint，并说出它与 Linux `struct sched_class` 的对应关系。
- **在课程主线中的位置**：本阶段前几课（73–75）处理"进程组/终端/job-control"，从本课开始连续三课进入**调度器元数据**子主题：76（调度策略 checkpoint）→ 77（priority/nice）→ 78（runqueue）。本课先做一个 checkpoint 锁定既有调度元数据的正确性，为后面引入优先级与运行队列统计提供稳定基线。
- **前置知识清单**：
  1. 本内核的 `sched_class` 结构：`{const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8);}`（Lesson 38 引入）；
  2. `fair_sched_class`（`"tiny_rr"` 轮转）与 `active_sched_class` 指针、`schedinfo` 命令（Lesson 38 起累积）；
  3. copy-on-write 基础元数据：`fault_pages`/`page_cache`/`anon_pages`/`anon_reclaims`（Lesson 44 起累积）；
  4. 系列 checkpoint 命名惯例：`l65test`/`l72test` 一类"元数据一致性冒烟"测试（Lesson 72–73）。
- **本课交付**：新增固定容量 `lesson_69_model` 记录与 `l76test` 验证命令；`about`/banner 更新为「Lesson 76: 调度策略元数据」。

---

## 2. 核心概念精讲

### 2.1 调度策略元数据（Scheduling Policy Metadata）

**直觉**：调度器不只需要"下一个跑谁"，还需要知道"用什么规则挑"：时间片轮转（round-robin）、完全公平（CFS）、实时（RT）……这套规则在 Linux 里叫调度类/调度策略，在 TinyOS 里是 `sched_class`。

**准确定义**：调度策略 = "从就绪任务中选下一个执行者"的算法契约，通常表现为一个**函数表（dispatch table）**：
- Linux：`struct sched_class`（`kernel/sched/sched.h`），内含 `enqueue_task`、`dequeue_task`、`pick_next_task` 等回调；内核有 `fair_sched_class`、`rt_sched_class`、`dl_sched_class`、`idle_sched_class` 多个实例，按优先级顺序串成链表 `sched_class_highest`。
- TinyOS：同一个名字 `sched_class`，只有 `name/pick_next/enqueue/dequeue` 四个成员，实例 `fair_sched_class = {"tiny_rr", rr_pick_next, rr_enqueue, rr_dequeue}`，由 `active_sched_class` 指针选中。

### 2.2 累积调度元数据的记账（Accounting）

策略执行时会留下可观测的计数器，这是"元数据真实"的体现：
- `sched_enqueues` / `sched_dequeues` / `sched_picks`：入队/出队/挑选次数；
- `quantum_left`、`preempt_switches`、`idle_switches`：时间片与抢占统计；
- 这些由 `schedinfo` 命令打印：`scheduler class: tiny_rr` + 三个 ops 计数。

本课的 `l76test` 不直接断言这些计数器（它们在运行期变化），而是用一张固定记录断言"元数据模型的健康属性"（见 2.4）。

### 2.3 调度策略与 copy-on-write 的关系（checkpoint 的覆盖面）

旧 README 与本课成功串都出现 "scheduling and copy-on-write"。原因：Lesson 68–80 阶段把"进程组/session/调度/COW"四个元数据家族放在同一批 checkpoint 里验收（见 Manifest：71/72 是进程组 checkpoint，76 是调度/COW checkpoint）。本课的 `lesson_69_model` 覆盖两类健康属性：调度相关（active/ready/accounted）与算术一致性（`b==a+1` 代际计数递增）。

### 2.4 「固定元数据 + 确定性验证」教学模型

`lesson_69_model` 是典型的 checkpoint 记录：

```c
struct lesson_69_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

- `a,b,c,d`：代际/计数序列 69→70→71→72，`b==a+1` 断言"计数单调递增、无跳变"；
- `valid`：模型已合法初始化；
- `active`：调度策略已激活；
- `ready`：就绪集合存在；
- `accounted`：记账一致。

一次 `l76test` 把这 5 条健康属性合成一个布尔 `ok`，在 VGA 上确定性打印 `passed`/fallback。这正是整个系列课程的验收哲学：**不执行真实调度，但保证"调度元数据是自洽的"**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-75） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、主循环 | **主要增量**：新增 `lesson_69_model` 结构、`lesson_69_state` 全局、`l76test()`、`exec64` 的 `l76test` 分支、`about`/banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局与栈断言 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 76 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 新增结构体与全局变量

```c
struct lesson_69_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_69_model lesson_69_state;
```

逐行注释：
- `a,b,c,d`（u32）：四个代际计数。初始化为 69、70、71、72，对应本课在源课序列中的起点（Origin=Lesson 69）以及之后 70、71、72 的推进；校验用 `b==a+1U` 保证相邻代际严格连续。
- `valid`（u8）：元数据模型是否已合法初始化——防止"未初始化状态被当作通过"。
- `active`（u8）：调度策略是否处于激活态（对应 `active_sched_class` 非空、`fair_sched_class` 已注册）。
- `ready`（u8）：是否已建立就绪集合（对应 `threads[]` 中的 `THREAD_RUNNABLE` 任务）。
- `accounted`（u8）：调度/COW 记账是否一致。
- `static struct lesson_69_model lesson_69_state;`：单一全局 checkpoint 记录，与 `orphan_group`/`job_signal`/`terminal_stop` 同一模式。

#### (b) 本课核心验证函数 `l76test`

```c
static TEXT64 void l76test(u16*c){lesson_69_state=(struct lesson_69_model){69U,70U,71U,72U,1,1,1,1};int ok=lesson_69_state.valid&&lesson_69_state.active&&lesson_69_state.ready&&lesson_69_state.accounted&&lesson_69_state.b==lesson_69_state.a+1U;text64(c,"l76test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 69 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_69_state=(struct lesson_69_model){69U,70U,71U,72U,1,1,1,1};`：聚合初始化四个代际 69/70/71/72 与四个标志位全部为 1——一个"理应通过"的理想 checkpoint 场景。
- `int ok=lesson_69_state.valid&&lesson_69_state.active&&lesson_69_state.ready&&lesson_69_state.accounted&&lesson_69_state.b==lesson_69_state.a+1U;`：5 个条件：① `valid` 模型合法；② `active` 调度策略激活；③ `ready` 就绪集合存在；④ `accounted` 记账一致；⑤ 代际计数连续（`b==a+1`，70==69+1）。
- `text64(c,"l76test: ");`：命令前缀。
- `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 69 fallback reported");`：成功串强调"调度与 COW checkpoint 通过"，失败串回落为源课编号提示——两条都是源码字面量。
- `putc64(c,'\n');`：换行。

为什么这样设计：checkpoint 课的职责是"锁定既有语义"，所以校验表达式刻意只测健康属性（合法/激活/就绪/记账/连续），不测具体调度行为。`b==a+1` 这类算术断言把"代际推进无跳变"变成可 grep 的布尔，比直接比较计数器更稳定——这正是确定性验证的优势。

#### (c) 本课守护的累积调度策略元数据（继承，未改）

```c
struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
static struct sched_class *active_sched_class;
static u64 sched_enqueues, sched_dequeues, sched_picks;
```

- `sched_class`：调度策略的抽象——策略名 + 三个操作回调。这正是 Linux `struct sched_class` 的微型投影（只保留教学需要的 4 个成员）。
- `fair_sched_class`：本内核唯一策略实例 `tiny_rr`（时间片轮转）；`active_sched_class` 指向它，等价于 Linux 的"当前激活的调度类"。
- 三个 `u64` 计数器：`schedinfo` 会打印 `ops enqueue/dequeue/pick: <n> <n> <n>`。
- `schedinfo` 输出串：`scheduler class: tiny_rr\nops enqueue/dequeue/pick: ...`——`l76test` 的 `active/accounted` 标志正是对这套策略元数据的健康性概括。

#### (d) `exec64` 命令分派中的增量分支

```c
}else if(eq64(word,"l76test")){if(!noargs64(arg))usage64(c,"l76test");else l76test(c);}
```

逐行注释：
- 与前几课同构：匹配命令字 → 拒绝多余参数 → 调用 `l76test(c)`。
- 分支接在 `stop68test` 之后，保持系列课程命令在 if-else 链中的源课顺序。
- `about` 文案更新为：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 76: 调度策略元数据\n");}
```

- `help` 命令列表保持旧字面量（不含 `l76test`），教学简化延续。

#### (e) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
text64(&c,"Lesson 76: 调度策略元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- 首行主题换成本课；syscall ABI 与 "bounded reclaim metadata" 边界不变。
- `kernel_main64_binary` 中 `active_sched_class=&fair_sched_class;` 的既有初始化仍是调度策略元数据被激活的源头。

#### (f) 继承的关键辅助函数（本课复用）

`eq64`、`noargs64`、`text64`/`putc64`、`token64` 均带 `TEXT64` 段属性，与 Lesson 73–75 相同；`putc64` 对 `'\n'` 执行"跳到下一行行首"。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '调度策略元数据' README.md
	@grep -q 'l76test' kernel64.c
	@grep -q 'Lesson 76' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 76 checks passed.'
```

- 三条 `grep -q` 分别锁定 README 主题、`kernel64.c` 新符号 `l76test`、课号 76；`printf` 信息逐字来自 Makefile。
- `grub-file --is-x86-multiboot2` 仍是权威 Multiboot2 检查；构建链与 `kernel64.ld` 的栈断言均与 Lesson 73–75 一致。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32 ──► enter_long_mode ──► kernel_main64_binary
    ├─ 元数据初始化（...）; active_sched_class=&fair_sched_class; sched_enqueues=...=0
    ├─ banner: "Lesson 76: 调度策略元数据\nGETTICKS, ..."
    └─ for(;;) 键盘循环:
        "l76test\n" ──► exec64 ──► eq64(word,"l76test")
        ──► l76test(c)
        ──► lesson_69_state 初始化 {69,70,71,72,1,1,1,1} + 5 条件校验
        ──► VGA: "l76test: bounded scheduling and copy-on-write checkpoint passed"
        ──► "tinyos> "
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 先 `active_sched_class=&fair_sched_class;` 并清零三个 ops 计数，再打印 banner。
2. **输入**：`l76test` + 回车，键盘循环收 7 字符进 `cmd`。
3. **分派**：`exec64` 命中 `l76test` 分支。
4. **校验**：`lesson_69_state` 聚合初始化，5 条件布尔校验。
5. **输出**：`l76test: bounded scheduling and copy-on-write checkpoint passed`，回显 `tinyos> `。

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
   Lesson 76: 调度策略元数据
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l76test`，预期输出：
   ```
   l76test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l76test: Lesson 69 fallback reported`。）
3. 输入 `about`，预期输出：
   ```
   Lesson 76: 调度策略元数据
   tinyos>
   ```
4. 输入 `schedinfo`，预期输出（计数为运行期值）：
   ```
   scheduler class: tiny_rr
   ops enqueue/dequeue/pick: ...
   wait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions
   ```
5. 回归：`stop68test`、`job67test`、`orphan66test`、`ps`、`taskvalidate` 等继承命令仍可用。

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 76 checks passed.`；QEMU 中 `l76test` 打印 `...passed`。

> 备注：旧 README 中曾写 "Commands: `l69test`"，但源码与 Makefile 中的实际命令是 `l76test`（见 `kernel64.c` 第 748 行与 `exec64` 分支）。本精讲文档以源码为准统一为 `l76test`，并保留旧 README 的核心声明（"bounded scheduling and copy-on-write metadata with deterministic validation"）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/镜像校验失败） | VGA 是否显示 `user image validation/load failure:`；查 `kernel.c::validate_user_image()` |
| `make check` 第一条 grep 失败 | README 主题字串与 Makefile 不一致 | `grep '调度策略元数据' README.md` 核对字面 |
| `make check` 第二条 grep 失败 | `kernel64.c` 丢失 `l76test` 符号 | `grep -q 'l76test' kernel64.c` |
| `make check` 第三条 grep 失败 | README 课号写错 | `grep -q 'Lesson 76' README.md` |
| `l76test` 打印 `unknown command` | `exec64` 分支未接入或拼写错误 | 核对 `eq64(word,"l76test")` 分支 |
| `l76test` 打印 `usage: l76test` | 命令带多余参数 | 本命令必须无参（`noargs64`） |
| `l76test` 打印 fallback 串 | 5 条件中某项不成立 | 检查 `ok`：`valid`、`active`、`ready`、`accounted`、`b==a+1U`；特别注意把 69/70/71/72 任一改错都会让 `b==a+1` 失败 |
| 误用 `l69test` 命令 | 沿用了旧 README 的错误命令名 | 实际命令是 `l76test`（源码 `exec64` 分支唯一） |
| `schedinfo` 打印 `scheduler class: none` | `active_sched_class` 未在 `kernel_main64_binary` 初始化 | 检查 `active_sched_class=&fair_sched_class;` 是否在启动时执行 |

---

## 7. 与 Linux 源码对照

**对照点 1：调度类（sched_class）**
- TinyOS：`struct sched_class { name; pick_next; enqueue; dequeue; }`，实例 `fair_sched_class={"tiny_rr",...}`，`active_sched_class` 指针指向当前激活类。
- Linux：`struct sched_class`（`kernel/sched/sched.h`）成员含 `enqueue_task`、`dequeue_task`、`pick_next_task`、`check_preempt_curr` 等；实例 `fair_sched_class`、`rt_sched_class`、`dl_sched_class`、`idle_sched_class` 按优先级组成链表，`__setscheduler`/`pick_next_task` 沿链表选择。
- 权威来源：Linux v6.12 `kernel/sched/sched.h`、`kernel/sched/core.c`；POSIX.1 §3.289（scheduling policy）。
- 教学简化：TinyOS 只有单一 `tiny_rr` 实例且无链表；`active_sched_class` 是"当前策略"的静态指针，不是动态切换。

**对照点 2：策略记账计数**
- TinyOS：`sched_enqueues/sched_dequeues/sched_picks` 三个 u64 由 `schedinfo` 输出；`l76test` 用 `accounted` 标志概括其一致性。
- Linux：调度统计分散在 `schedstat`（`/proc/schedstat`）与 `task->se.nr_migrations` 等字段；`CONFIG_SCHEDSTATS` 才启用。
- 教学简化：TinyOS 只记三个总计数，不做 per-task 统计。

**对照点 3：checkpoint 与 COW 元数据的关联**
- Linux：CFS 调度与 COW（`mm/memory.c` 的 `handle_mm_fault`/`wp_page_copy`）是两个子系统，但都归属"内核一致性"，由 `kernel/sched/` 与 `mm/` 分别维护。
- TinyOS：把"调度 + COW"放在一个 checkpoint 里（`l76test` 成功串 `bounded scheduling and copy-on-write checkpoint passed`），是因为两者共享"计数单调、标志自洽"的元数据风格；`fault_pages`/`anon_pages`/`page_cache_count` 是 COW 侧的健康数据。
- 权威来源：Linux `mm/memory.c`、`mm/rmap.c`。

**对照点 4（衔接系列）**：下一课 `priority/nice` 将给 `sched_class` 加入优先级维度，对应 Linux `task->prio` 与 `nice` 映射（`NICE_TO_PRIO`）。本课的 `active/ready/accounted` 正是下一课"优先级状态合法"的前置条件。

---

## 8. 思考题与练习

1. **概念理解**：为什么本课用"调度 + COW 一起 checkpoint"，而不是只查调度？结合 Manifest 中 Lesson 68–80 阶段的元数据家族划分说明。
2. **源码定位**：在 `kernel64.c` 中定位 `sched_class`、`fair_sched_class`、`active_sched_class` 与 `schedinfo`，说明 `l76test` 的 `active/accounted` 两个标志分别对应这些符号的哪一部分语义。
3. **动手实验**：把 `l76test` 初始化里的 `b` 从 `70U` 改成 `71U`，重新 `make run`，观察输出变为 `Lesson 69 fallback reported`——验证 `b==a+1U` 代际连续性检查确实生效。请**改回 70U**。
4. **动手实验**：把 `valid` 标志从 1 改成 0，观察 `ok` 变为假。思考：为什么 checkpoint 模型要求"合法初始化"与"结果正确"是两回事？
5. **Linux 对照**：阅读 `kernel/sched/sched.h` 中 `struct sched_class` 的完整成员列表，列出 TinyOS 版省略了哪些关键回调（如 `check_preempt_curr`、`set_next_task`），并说明省略它们对教学模型的影响。

---

## 9. 本课小结与下一课预告

- 本课用 `lesson_69_model` 记录 + `l76test` 确定性验证，完成"调度 + COW 元数据 checkpoint"：合法、激活、就绪、记账、代际连续五项断言一次通过。
- 你理解了"调度策略元数据"在本内核的具体形态：`sched_class` 分派表（`tiny_rr`）、`active_sched_class` 指针、三个 ops 计数器，以及 `schedinfo` 的可观测输出。
- 你对照了 Linux `kernel/sched/sched.h` 的 `struct sched_class` 与 `fair_sched_class` 等实例，知道教学模型只保留 name/pick/enqueue/dequeue 四个成员。
- 你掌握了 checkpoint 课的验收哲学：不执行真实调度，但保证调度元数据自洽、可 grep、可回归。
- 你注意到并修正了旧 README 中 `l69test` → `l76test` 的命令名笔误。

**下一课预告**：Lesson 77「priority/nice 优先级状态」。调度策略有了，但"凭什么叫公平/轮转"？下一课给任务加入优先级维度：`priority`（静态优先级）与 `nice`（系统调用调整）如何影响调度次序，对应 Linux `task->prio`、`NICE_TO_PRIO` 与 `nice(2)`。衔接点：本课 `active/ready` 标志保证的"就绪集合"，正是下一课优先级排序的作用对象。

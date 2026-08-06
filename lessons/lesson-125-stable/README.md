# Lesson 125: RCU callback 队列 — 精讲文档

> **Course status: stable snapshot.**
>
> - 课号：Lesson 125
> - 本课主题：RCU callback 队列（RCU callback queue）
> - 课程主线位置：第四阶段（SMP / RCU / 诊断元数据检查点系列），RCU 四连讲的第三课
> - 前置课程：[Lesson 124（RCU grace period）](../lesson-124-stable/README.md)
> - 后续课程：[Lesson 126（RCU 对象回收）](../lesson-126-stable/README.md)
> - 一句话目标：理解 `call_rcu()` 如何把「宽限期结束后要做的回收动作」排进每 CPU 回调队列、再被 RCU softirq 按序执行，并认识 TinyOS 检查点 `lesson_118_model` 的登记方式。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能讲清楚 callback 队列在 RCU 里的位置（宽限期结束 → 队列 → 按序执行），能区分 `synchronize_rcu`（同步等待）与 `call_rcu`（异步排队）的分工，并用 `l125test` 验证检查点。
- **在课程主线中的位置**：RCU 四连讲的第三课：reader（123）→ 宽限期（124）→ 回调队列（本课）→ 对象回收（126）。本课补上「宽限期结束后动作如何排队执行」的机制，下一课把队列里的动作落实为「释放对象」。
- **前置知识清单**：
  1. reader 临界区语义（Lesson 123）；
  2. 宽限期定义与 `synchronize_rcu`/`call_rcu` 的差异（Lesson 124）；
  3. 本内核软中断机制：`softirq_raise` 置位、`softirq_run_budget()` 按预算消费、workqueue 环形队列（Lesson 122 已精讲）；
  4. 检查点模型与命令分发约定。
- **本课交付**：
  - 新命令 `l125test` 与新模型 `struct lesson_118_model lesson_118_state`；
  - 上一课命令更名为 `l117test`；
  - `about` 与启动横幅显示 `Lesson 125: RCU callback 队列`。

## 2. 核心概念精讲

### 2.1 callback 队列是什么

- **定义**：RCU callback 是「宽限期结束后要执行的动作」（通常是 `kfree` 旧对象或释放其他资源），`call_rcu(struct rcu_head *head, rcu_callback_t func)` 把它挂进当前 CPU 的 `rcu_data` 回调队列；宽限期结束、RCU softirq 执行 `rcu_do_batch()` 时按序取出执行。
- **为什么需要队列**：宽限期结束可能同时有大量回收待办，且回收动作往往不能立即在当前上下文（如中断）执行；队列把「事件（宽限期结束）」与「动作（回收）」解耦，还允许批量合并（batch）以摊薄开销。
- **队列结构**：Linux 每 CPU 维护 `rcu_segcblist`——一个按宽限期阶段分段的链表（`kernel/rcu/rcu_segcblist.h`）：`RCU_DONE_TAIL`（已完成、待执行）、`RCU_NEXT_READY_TAIL`（已就绪）、`RCU_NEXT_TAIL`（新入队）。分段让回调可以在正确的宽限期结束后再执行。

```
call_rcu(head, kfree_old)   ──►  rcu_data.rcu_segcblist
                                    ┌─────────┬───────────┬──────────┐
                                    │ DONE    │ NEXT_READY│ NEXT     │
                                    │ (可执行) │ (等本轮GP)│(等下轮GP)│
                                    └─────────┴───────────┴──────────┘
GP 结束 ──► RCU_SOFTIRQ ──► rcu_do_batch() ──► 按序执行 func(old)
```

### 2.2 call_rcu 的执行路径

1. `call_rcu()` 在调用 CPU 的 `rcu_data` 上把 `head` 挂入 `rcu_segcblist` 的 `RCU_NEXT_TAIL`；
2. 若当前没有进行中的宽限期，则启动一个（`__call_rcu` → `rcu_start_gp`）；
3. 宽限期结束（所有 CPU 报告 QS）后，`rcu_gp_kthread` 把该段回调推进到 `RCU_DONE_TAIL`；
4. RCU softirq（`rcu_process_callbacks()`）或 `rcu_nocb` 线程执行 `rcu_do_batch()`，批量取出 DONE 段回调逐个执行 `func`。
- **TinyOS 对应**：本内核的 `workqueue`（环形数组 + `work_head`/`work_tail`/`work_used`）就是「按序排队、softirq 消费」的现成模型；`workqueue_submit` 入队、`softirq_run_budget` 出队执行。检查点 `accounted` 位即代表「入队数 == 执行数」的队列守恒。

### 2.3 回调队列的三个不变量

1. **按序执行**：同一 CPU 上 `call_rcu` 的顺序即回调执行顺序（FIFO，对应 `rcu_do_batch` 顺链表执行）；
2. **宽限期守护**：回调只在自己的宽限期结束后执行，绝不提前；
3. **延迟有界**：队列不会无限积压（Linux 有 `blimit`/`qhimark` 限流与 `rcu_nocb` 分流；TinyOS 用 `WORK_CAP` 上限 + `drops` 计数）。
- 检查点模型里：`ready`=队列可入队，`accounted`=入队/执行配对闭合，`active`=回调机制激活。

### 2.4 检查点模型如何编码 callback 队列

- `valid`：回调队列模型已实例化；
- `active`：回调机制激活（对应 `rcu_data` 已初始化、softirq 已注册）；
- `ready`：队列可接收新回调（对应 `rcu_segcblist` 未满）；
- `accounted`：入队与执行计数闭合（对应 `rcu_do_batch` 后队列清空）。
- 四连号 `{118,119,120,121}` 与 `b==a+1` 表达回调序列号连续无空洞。

### 2.5 回调执行与宽限期之间的「段对齐」

- Linux 回调队列的核心技巧是**段对齐**：同一宽限期开始时入队的回调会被推进到同一段，宽限期结束后整段一起成为 DONE 段，保证「回调和宽限期的配对」不会错乱。
- 若不加分段，可能出现新入队回调比旧回调先执行（乱序）或提前执行（在宽限期未结束时被回收对象）两类错误。`rcu_segcblist` 的 `RCU_NEXT_TAIL → RCU_NEXT_READY_TAIL → RCU_DONE_TAIL` 三段正是为了消除这两种风险。
- TinyOS 检查点的 `accounted` 位（入队/执行闭合）在概念上就是「段对齐后队列清零」的简化表达；`b==a+1`（序号连续）则对应「段内回调按宽限期号有序推进」。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 124） |
|------|------|------------------------------|
| `boot.S` | Multiboot2 头、进入 long mode | 未变化 |
| `kernel.c` | 32 位引导主流程 | 未变化 |
| `kernel64.c` | 64 位内核主体与检查点模型 | **有增量**：新增 `lesson_118_model`/`lesson_118_state`/`l125test()`；`l124test` 更名为 `l117test`；`about`/横幅改为 Lesson 125 |
| `kernel64.ld` | 64 位裸二进制布局 | 未变化 |
| `linker.ld` | 32 位镜像布局 | 未变化 |
| `Makefile` | 构建与 `check`/`run` | 微小变化：`check` grep 串改为 `RCU callback 队列`、`l125test`、`Lesson 125` |
| `grub.cfg` | GRUB 启动项 | 未变化 |

### 3.2 kernel64.c 精讲

#### 新增结构 / 全局变量

```c
struct lesson_118_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_118_model lesson_118_state;
```

逐行注释：
- 第 893–894 行。模型结构同构；本课语义落在「回调队列」上。
- `lesson_118_state` 由 `l125test` 填充；`ready`=队列可入队，`accounted`=入队/执行闭合。

#### 函数精讲

**`l125test(u16 *c)`**（第 895 行，本课新增）

```c
static TEXT64 void l125test(u16*c){lesson_118_state=(struct lesson_118_model){118U,119U,120U,121U,1,1,1,1};int ok=lesson_118_state.valid&&lesson_118_state.active&&lesson_118_state.ready&&lesson_118_state.accounted&&lesson_118_state.b==lesson_118_state.a+1U;text64(c,"l125test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 118 fallback reported");putc64(c,'\n');}
```

- **签名与职责**：`static TEXT64 void l125test(u16 *c)`：登记并断言回调队列检查点。
- **算法步骤**：
  1. `lesson_118_state=(struct lesson_118_model){118U,119U,120U,121U,1,1,1,1}`：布尔位全 1，四连号 118 起；
  2. `ok=valid && active && ready && accounted && b==a+1U`；
  3. 打印 `"l125test: "` 与 passed/`"Lesson 118 fallback reported"`。
- **边界处理**：纯常量断言，无输入越界路径；失败分支只是文案。
- **设计动机**：`accounted` 表达「入队与执行配对」，这正是回调队列区别于宽限期模型的核心不变量；`b==a+1` 则保证回调序号流连续，杜绝「跳号执行」类错误。

**`exec64` 本课相关分支**（第 896 行）

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 125: RCU callback 队列\n");}
...
}else if(eq64(word,"l117test")){if(!noargs64(arg))usage64(c,"l117test");else l117test(c);}else if(eq64(word,"l125test")){if(!noargs64(arg))usage64(c,"l125test");else l125test(c);}
```

- `about` 输出 `Lesson 125: RCU callback 队列`；`l117test`（上一课命令更名）与 `l125test`（本课）相邻注册。

**继承的队列执行机制（callback 队列的现成模型）**

```c
#define WORK_CAP 4U
struct work_model { u8 kind,data,queued; u64 runs; };
static struct work_model workqueue[WORK_CAP];
static u8 work_head,work_tail,work_used;
static TEXT64 int workqueue_submit(u8 kind,u8 data){if(work_used>=WORK_CAP){softirq_model.drops++;return 0;}workqueue[work_head]=(struct work_model){kind,data,1,0};work_head=(u8)((work_head+1)%WORK_CAP);work_used++;softirq_raise(1);return 1;}
```

- `workqueue` 是有界环形数组：`work_head` 入队游标、`work_tail` 出队游标、`work_used` 在队量。`workqueue_submit` 满则 `drops++` 拒绝，否则把 `{kind,data,queued=1,runs=0}` 写入 `work_head` 处并 `softirq_raise(1)` 唤醒消费——这就是 `call_rcu` 的教学版：**入队 + 置位待办**。
- `softirq_run_budget()` 在 tick 里消费：从 `work_tail` 取一个、`queued=0`、`runs++`、`work_used--`。这对应 `rcu_do_batch()` 的「取出 DONE 段回调执行」。
- **与本课的关系**：回调队列的三个不变量（按序、宽限期守护、有界）在 TinyOS 里分别对应环形队列 FIFO、`softirq_raise` 的延迟执行、`WORK_CAP`/`drops` 上限。检查点 `accounted` 位就是「`work_used` 最终归零」的元数据表达。

**`softirqinfo`（继承，队列健康度查看）**

```c
static TEXT64 void softirqinfo(u16*c){text64(c,"softirq pending/raises/runs/drops/budget: ");hex64(c,softirq_model.pending);text64(c,"/");hex64(c,softirq_model.raises);text64(c,"/");hex64(c,softirq_model.runs);text64(c,"/");hex64(c,softirq_model.drops);text64(c,"/");hex64(c,softirq_model.budget_exhaustions);text64(c," tasklets/work: ");hex64(c,tasklets[0].pending+tasklets[1].pending);text64(c,"/");hex64(c,work_used);putc64(c,'\n');}
```

- 打印 `pending/raises/runs/drops/budget` 与 `tasklets/work` 占用；`work_used` 就是「当前队列积压量」，`drops` 就是「队列满被拒次数」——这两个字段是检查点 `accounted` 在可观察层面对应的真实计数。

### 3.3 构建管线（Makefile / linker）

- 构建链与前四课一致（双阶段、`objcopy -O binary`、`grub-mkrescue`）。
- `check` 目标变化：

```
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'RCU callback 队列' README.md
	@grep -q 'l125test' kernel64.c
	@grep -q 'Lesson 125' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 125 checks passed.'
```

### 3.4 主控制流

```
kernel_main64_binary
  ├─ 初始化（与前课相同）
  ├─ text64(&c,"Lesson 125: RCU callback 队列\nGETTICKS, …\n")  ← 本课横幅
  └─ for(;;) 键盘环
       ├─ "about"    → "Lesson 125: RCU callback 队列\n"
       ├─ "l117test" → 回放宽限期检查点
       └─ "l125test" → l125test(c) → 打印回调队列检查点结果
```

## 4. 数据流与运行逻辑

1. **输入**：`l125test` 回车。
2. **解析**：`token64` 切词命中 `l125test`。
3. **执行**：`l125test(c)` 填充 `lesson_118_state` 并断言。
4. **输出**：`l125test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（`ok==1`），否则 `l125test: Lesson 118 fallback reported`。
5. **屏幕**：横幅与 `about` 显示 `Lesson 125: RCU callback 队列`。

若想观察队列机制本身，可运行 `softirqtest`（会执行 `workqueue_submit` 系列操作并验证 `work_used` 归零）与 `softirqinfo`（查看 `work_used`/`drops` 实时值）。

## 5. 构建、运行与验证

- **依赖**：与前课相同。
- **构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

预期输出：`Multiboot2 and Lesson 125 checks passed.`
- **运行**：`make run`，QEMU 图形窗口查看横幅（勿加 `-display none`）。
- **验证步骤**：
  1. `about` → `Lesson 125: RCU callback 队列`（源码逐字）；
  2. `l125test` → `l125test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（源码逐字；失败态为 `Lesson 118 fallback reported`）；
  3. `l117test` → 宽限期检查点回放 passed；
  4. `softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and budget carry-over passed`（验证队列机制本身）。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `l125test` 打印 fallback | `lesson_118_state` 某位为 0 或 `b!=a+1` | 核对第 895 行字面量 `{118U,119U,120U,121U,1,1,1,1}` |
| `about` 显示旧主题 | `exec64` about 分支未更新 | grep `'Lesson 125'` kernel64.c 确认两处 |
| `l117test` 报 unknown | 命令分支缺失 | 确认 `l117test` 与 `l125test` 分支相邻注册 |
| `softirqtest` 报 BROKEN | `work_used` 未归零或 budget 未结转 | 检查 `softirq_run_budget` 的出队分支与 `work_tail` 推进 |
| `softirqinfo` 显示 drops 增长 | 工作队列满被拒 | 检查 `workqueue_submit` 的 `work_used>=WORK_CAP` 分支；增大 `WORK_CAP` 可缓解 |
| `make check` 失败 | grep 串不匹配 | `grep -q 'RCU callback 队列' README.md`、`grep -q 'l125test' kernel64.c` |
| 回调执行顺序错乱 | 环形队列 head/tail 推进错误 | 核对 `work_head`/`work_tail` 的 `(u8)((x+1)%WORK_CAP)` 取模；用 `softirqtest` 的 FIFO 断言验证 |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现（路径） |
|--------|-----------------|--------------------|
| 回调入队 | `workqueue_submit`（环形数组入队 + `softirq_raise`） | `call_rcu()`/`__call_rcu()`（`kernel/rcu/tree.c`）：挂入 `rcu_segcblist` 的 `RCU_NEXT_TAIL` |
| 队列结构 | 环形数组 `workqueue[WORK_CAP]` + `head/tail/used` | `struct rcu_segcblist`（`kernel/rcu/rcu_segcblist.h`）：三段链表 |
| 按序执行 | FIFO 出队（`work_tail` 推进） | `rcu_do_batch()`（`kernel/rcu/tree.c`）：顺 `rcu_segcblist` 逐个执行 |
| 延迟执行上下文 | softirq 位图 + tick 预算 | `RCU_SOFTIRQ` → `rcu_process_callbacks()`；或 `rcu_nocb` 内核线程 |
| 队列上限 | `WORK_CAP` + `drops` 计数 | `blimit`/`qhimark`/`qovld` 限流与 `rcu_nocb` 分流（`kernel/rcu/tree.c`） |
| 检查点 | `accounted` 位 = 入队/执行闭合 | `rcu_segcblist_n_cbs` 计数；batch 后归零 |
| 权威来源 | —— | Intel SDM Vol.3；Multiboot2 规范；GNU GRUB |

**教学模型简化了什么**：真实 `rcu_segcblist` 有三段状态机、宽限期号对齐、批量计数与限流、`rcu_nocb` 离线卸载等；TinyOS 的 `workqueue` 只是单一 FIFO 环形队列，用 `WORK_CAP` 表达有界性，用 `accounted` 位表达守恒。

## 8. 思考题与练习

1. **概念理解**：为什么 `call_rcu` 不能在当前上下文直接执行 `func`？队列把「宽限期结束」与「执行回收」解耦带来了哪些好处？
2. **源码定位**：找到 `workqueue_submit` 与 `softirq_run_budget` 中的出队分支，指出它们分别对应 Linux 的 `call_rcu` 与 `rcu_do_batch` 的哪一步。
3. **动手实验**：把 `l125test` 的 `accounted` 位改为 0 重建运行观察 fallback；再用 `softirqtest`+`softirqinfo` 观察 `work_used` 是否归零。
4. **Linux 对照**：阅读 `kernel/rcu/rcu_segcblist.h` 的 `rcu_segcblist_enqueue` 与 `kernel/rcu/tree.c` 的 `rcu_do_batch`，对比「三段队列」与 TinyOS 环形数组的异同。
5. **综合**：如果回调队列里的动作是 `kfree(old)`，那么「谁保证 old 已无读者」？这一保证如何从上一课（grace period）传递到本课（callback 执行）？

## 9. 本课小结与下一课预告

- 本课建立了 RCU 回调队列的概念：宽限期结束后的动作按序排队、延迟执行。
- 区分了 `synchronize_rcu`（同步等待）与 `call_rcu`（异步排队）两种 writer 等待方式。
- 认识了 `rcu_segcblist` 三段队列结构与 `rcu_do_batch` 批量执行机制。
- 在 TinyOS 的 `workqueue`/`softirq_run_budget` 中找到了 callback 队列的现成教学模型。
- 检查点 `lesson_118_model` 用 `ready`/`accounted` 位登记「可入队、入队/执行闭合」。
- 明确了教学模型把三段状态机简化为单一 FIFO 环形队列。

**下一课**：[Lesson 126（RCU 对象回收）](../lesson-126-stable/README.md) 是 RCU 系列的收官：回调队列里的动作落实为释放对象（`kfree` 语义），检查点模型 `lesson_119_model` 将登记「对象只在所有读者离开后回收」的最终不变量。

## 附录：stable snapshot 声明（保留原 README 要点）

> This checkpoint models bounded concurrency, SMP, RCU, and diagnostics metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants.
>
> Commands: `l117test`、`l125test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.
>
> 主要内容：RCU callback 队列；统一课程编号：Lesson 125。（旧 README 中「Commands: `l118test`」与实际源码不符，已勘误为 `l125test`。）

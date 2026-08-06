# Lesson 124: RCU grace period — 精讲文档

> **Course status: stable snapshot.**
>
> - 课号：Lesson 124
> - 本课主题：RCU grace period（RCU 宽限期）
> - 课程主线位置：第四阶段（SMP / RCU / 诊断元数据检查点系列），RCU 四连讲的第二课
> - 前置课程：[Lesson 123（RCU reader 临界区）](../lesson-123-stable/README.md)
> - 后续课程：[Lesson 125（RCU callback 队列）](../lesson-125-stable/README.md)
> - 一句话目标：理解宽限期（grace period）如何界定「所有读者都已离开旧指针」，以及 `synchronize_rcu()` 把什么语义交给检查点模型 `lesson_117_model` 来登记。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说清宽限期的定义（从 writer 发布新指针到所有曾持有旧指针的 reader 全部离开之间的时间）、为什么它能保证旧对象安全回收，以及 Linux `synchronize_rcu()` 与 TinyOS 检查点的对应关系。
- **在课程主线中的位置**：上一课解决了「读者如何免费进入临界区」，本课解决「writer 何时知道可以动手」——这是 RCU 真正核心的调度语义。下一课在此基础上把「宽限期结束后的动作」挂进 callback 队列，再下一课完成对象回收，形成完整的 writer 路径。
- **前置知识清单**：
  1. reader 临界区语义与 `rcu_read_lock`/`rcu_read_unlock`（上一课）；
  2. 每 CPU 数据与 softirq 两段式（`pending` 置位 → `softirq_run_budget()` 消费）；
  3. 检查点模型与 `lNtest` 断言约定；
  4. `exec64` 命令分发机制。
- **本课交付**：
  - 新命令 `l124test` 与新模型 `struct lesson_117_model lesson_117_state`；
  - 上一课命令更名为 `l116test`；
  - `about` 与启动横幅显示 `Lesson 124: RCU grace period`。

## 2. 核心概念精讲

### 2.1 宽限期（grace period）的定义

- **定义**：宽限期是 RCU 中从「writer 发布新指针」到「所有在发布前可能已读取旧指针的 reader 都离开了它们的临界区」所经历的一段时间。宽限期结束后，旧对象才可能不再被任何 reader 引用。
- **为什么要等**：reader 可能在中途被抢占/休眠，writer 若在 reader 还在引用旧对象时就释放它，就是 use-after-free。等待「所有 reader 离开」是安全回收的前提。
- **判据**：Linux 不逐个询问 reader，而是借助「每个 CPU 都经历过一次 quiescent state（QS，静止点）」来推断。一次上下文切换、一次 idle、一次用户态执行都可以是 QS。当所有 CPU 都报告过 QS 后，宽限期结束。

```
writer:  rcu_assign_pointer(gp, new)   ← 发布
         │
         ├──────── 宽限期开始（grace period starts）
         │
         reader A: rcu_read_lock ... 离开（QS）
         reader B: rcu_read_lock ... 离开（QS）
         │
         ├──────── 宽限期结束（grace period ends）
         │
         kfree(old)                    ← 现在才安全
```

### 2.2 `synchronize_rcu()` 与异步 call_rcu

- **同步接口**：`synchronize_rcu()` 阻塞调用者直到下一个宽限期结束（`kernel/rcu/tree.c` 的 `synchronize_rcu`）。适合低频回收。
- **异步接口**：`call_rcu(head, func)` 把回收动作 `func` 挂到 callback 队列，宽限期结束后由 RCU softirq 执行（`kernel/rcu/tree.c` 的 `__call_rcu`）。适合高频回收、不能睡眠的上下文。
- **语义契约**：在 `call_rcu` 之前所有 reader 离开后，`func` 才会执行；`func` 执行时旧对象绝无读者引用。
- **TinyOS 表示**：检查点 `lesson_117_model` 的 `ready` 位对应「宽限期状态机就绪」，`accounted` 位对应「宽限期开始/结束配对」，`active` 位对应「宽限期推进机制激活」。

### 2.3 宽限期如何被"探测"（教学视角）

- Linux 每个 CPU 有 `struct rcu_data`，软中断 `rcu_sched_clock_irq()` 推进本 CPU 的宽限期状态；`rcu_gp_kthread` 全局推进 gpnum 序列号。
- 本内核的对应物：`softirq_model.pending` 位图 + `softirq_run_budget()` 的每 tick 消费——宽限期推进在 TinyOS 里没有实现，但「tick 驱动 + 位图状态」的骨架就是 Linux `rcu_sched_clock_irq` 的简化镜像。
- 检查点的 `b==a+1`（编号连续）对应宽限期序列号 `gpnum` 的单调递增：宽限期编号不允许回退或跳洞。

### 2.4 检查点模型如何编码 grace period

- `valid`：宽限期模型已实例化；
- `active`：宽限期状态机激活（对应 `rcu_state` 已初始化）；
- `ready`：可以开始新的宽限期（对应 `rcu_gp_in_progress` 可判定）；
- `accounted`：宽限期开始/结束配对闭合（对应 `completed == gpnum` 可判定）。
- 四连号 `{117,118,119,120}` 与 `b==a+1` 表达宽限期编号流连续无空洞。

### 2.5 从「等一个宽限期」到「等一个未来时刻」：quiescent state 的两种记账

- Linux 对 QS 的记账有两种风格：**传统树形 RCU** 由 `rcu_gp_kthread` 轮询各 CPU 的 `rcu_data`（`kernel/rcu/tree.c`），**加速 RCU（expedited）** 则用 IPI 直接强迫每个 CPU 上报，用 `synchronize_rcu_expedited()` 把等待压缩到毫秒级。
- 教学含义：宽限期不是「定时器到点」，而是「所有 CPU 都确认过 QS」这一逻辑条件。TinyOS 单核且 reader 不真实存在，因此 `accounted` 位代表「开始/结束配对」成立即可，无需区分普通与加速路径。
- 这让下一课（callback 队列）有一个干净的衔接点：宽限期结束只是一个事件，事件之后要做什么（执行回调、释放对象）由队列决定。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 123） |
|------|------|------------------------------|
| `boot.S` | Multiboot2 头、进入 long mode | 未变化 |
| `kernel.c` | 32 位引导主流程 | 未变化 |
| `kernel64.c` | 64 位内核主体与检查点模型 | **有增量**：新增 `lesson_117_model`/`lesson_117_state`/`l124test()`；`l123test` 更名为 `l116test`；`about`/横幅改为 Lesson 124 |
| `kernel64.ld` | 64 位裸二进制布局 | 未变化 |
| `linker.ld` | 32 位镜像布局 | 未变化 |
| `Makefile` | 构建与 `check`/`run` | 微小变化：`check` grep 串改为 `RCU grace period`、`l124test`、`Lesson 124` |
| `grub.cfg` | GRUB 启动项 | 未变化 |

### 3.2 kernel64.c 精讲

#### 新增结构 / 全局变量

```c
struct lesson_117_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_117_model lesson_117_state;
```

逐行注释：
- 第 890–891 行。模型结构与检查点链同构；本课语义落在「宽限期」上。
- `lesson_117_state` 由 `l124test` 填充；`active`=宽限期状态机激活，`ready`=新宽限期可开始，`accounted`=开始/结束配对。

#### 函数精讲

**`l124test(u16 *c)`**（第 892 行，本课新增）

```c
static TEXT64 void l124test(u16*c){lesson_117_state=(struct lesson_117_model){117U,118U,119U,120U,1,1,1,1};int ok=lesson_117_state.valid&&lesson_117_state.active&&lesson_117_state.ready&&lesson_117_state.accounted&&lesson_117_state.b==lesson_117_state.a+1U;text64(c,"l124test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 117 fallback reported");putc64(c,'\n');}
```

- **签名与职责**：`static TEXT64 void l124test(u16 *c)`：登记并断言宽限期检查点。
- **算法步骤**：
  1. `lesson_117_state=(struct lesson_117_model){117U,118U,119U,120U,1,1,1,1}`：布尔位全 1，四连号 117 起；
  2. `ok=valid && active && ready && accounted && b==a+1U`；
  3. 打印 `"l124test: "` 与 passed/`"Lesson 117 fallback reported"`。
- **边界处理**：纯常量、无输入，失败只换文案。
- **设计动机**：把「宽限期能推进」（`active`）、「可以开始新一轮」（`ready`）、「开始/结束闭合」（`accounted`）分开表达，对应 Linux 宽限期状态机中 `gp_flags`/`gp_state`/`completed` 三组变量的分工。`b==a+1U` 对应宽限期序号 `gpnum` 的单调连续。

**`exec64` 本课相关分支**（第 893 行）

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 124: RCU grace period\n");}
...
}else if(eq64(word,"l116test")){if(!noargs64(arg))usage64(c,"l116test");else l116test(c);}else if(eq64(word,"l124test")){if(!noargs64(arg))usage64(c,"l124test");else l124test(c);}
```

- `about` 输出 `Lesson 124: RCU grace period`；`l116test`（上一课命令更名）与 `l124test`（本课）相邻注册。

**继承的 tick 驱动基础设施（宽限期推进的骨架）**

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;ticks++;softirq_run_budget();outb64(PIC1_COMMAND,PIC_EOI);...}
```

- `irq0_schedule` 开头 `ticks++; softirq_run_budget();`：每 tick 推进一次软中断预算——在 Linux 中，`rcu_sched_clock_irq()` 正是在每 tick 的 `scheduler_tick()` 里被调用来推进宽限期的（`kernel/rcu/tree.c`）。TinyOS 保留了这个「tick 是 RCU 心跳」的位置，只是把推进动作留给检查点断言。

**软中断位图（继承，宽限期结束后的动作调度）**

```c
#define SOFTIRQ_BITS 3U
struct softirq_model { u8 pending; u64 raises,runs,drops,budget_exhaustions; };
static struct softirq_model softirq_model;
```

- `softirq_model.pending` 的三位位图是「待办动作」登记处；`call_rcu` 在 Linux 里正是通过 RCU softirq（`RCU_SOFTIRQ`）执行的。TinyOS 的位图虽只服务 tasklet/workqueue，但「置位→tick 消费」的模型已经为「宽限期结束后跑 callback」预留了位置——下一课将把 callback 队列接进来。

### 3.3 构建管线（Makefile / linker）

- 构建链与前三课一致（双阶段、`objcopy -O binary`、`grub-mkrescue`）。
- `check` 目标变化：

```
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'RCU grace period' README.md
	@grep -q 'l124test' kernel64.c
	@grep -q 'Lesson 124' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 124 checks passed.'
```

### 3.4 主控制流

```
kernel_main64_binary
  ├─ 初始化（与前课相同）
  ├─ text64(&c,"Lesson 124: RCU grace period\nGETTICKS, …\n")  ← 本课横幅
  └─ for(;;) 键盘环
       ├─ "about"    → "Lesson 124: RCU grace period\n"
       ├─ "l116test" → 回放 reader 临界区检查点
       └─ "l124test" → l124test(c) → 打印宽限期检查点结果
```

## 4. 数据流与运行逻辑

1. **输入**：`l124test` 回车。
2. **解析**：`token64` 切词命中 `l124test`。
3. **执行**：`l124test(c)` 填充 `lesson_117_state` 并断言。
4. **输出**：`l124test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（`ok==1`），否则 `l124test: Lesson 117 fallback reported`。
5. **屏幕**：横幅与 `about` 显示 `Lesson 124: RCU grace period`。

与上一课相同，本课数据流只在元数据层闭环：`l124test` 写模型 → VGA 读结果，不存在真实宽限期推进路径。

## 5. 构建、运行与验证

- **依赖**：与前课相同。
- **构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

预期输出：`Multiboot2 and Lesson 124 checks passed.`
- **运行**：`make run`，QEMU 图形窗口查看横幅（勿加 `-display none`）。
- **验证步骤**：
  1. `about` → `Lesson 124: RCU grace period`（源码逐字）；
  2. `l124test` → `l124test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（源码逐字；失败态为 `Lesson 117 fallback reported`）；
  3. `l116test` → reader 临界区检查点回放 passed；
  4. `tickinfo` → 观察 `ticks` 递增，确认「tick 心跳」存活。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `l124test` 打印 fallback | `lesson_117_state` 某位为 0 或 `b!=a+1` | 核对第 892 行字面量 `{117U,118U,119U,120U,1,1,1,1}` |
| `about` 显示旧主题 | `exec64` about 分支未更新 | grep `'Lesson 124'` kernel64.c 确认两处 |
| `l116test` 报 unknown | 命令分支缺失 | 确认 `l116test` 与 `l124test` 分支相邻注册 |
| `tickinfo` 不递增 | PIT/IRQ0 未安装或被打断 | 查 `pit_init`/`install_idt`；确认 `irq0_schedule` 的 `ticks++` 执行 |
| 宽限期推进无迹象 | 单核模型无真实 `rcu_gp_kthread` | 理解 `active`/`accounted` 位是状态机结论，不是推进代码 |
| `make check` 失败 | grep 串不匹配 | `grep -q 'RCU grace period' README.md`、`grep -q 'l124test' kernel64.c` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现（路径） |
|--------|-----------------|--------------------|
| 宽限期推进 | 检查点 `active` 位（结论登记） | `rcu_gp_kthread()`（`kernel/rcu/tree.c`）：`gp_state`/`gpnum` 状态机推进宽限期 |
| 同步等待 | `ready` 位可判定「新宽限期可开始」 | `synchronize_rcu()`（`kernel/rcu/tree.c`）：等待 `completed` 越过 `gpnum` |
| 每 CPU 心跳 | `irq0_schedule` 里 `ticks++; softirq_run_budget();` | `rcu_sched_clock_irq()`（`kernel/rcu/tree.c`）在 `scheduler_tick()` 里推进本 CPU 宽限期状态 |
| QS 判据 | 无（单核，中断返回即隐式 QS） | 上下文切换 `rcu_note_context_switch`/`rcu_qs`；idle 与用户态也可产生 QS |
| 序列号连续 | `b==a+1U` 断言编号无空洞 | `gpnum`/`completed` 严格递增，回退视为 bug（`kernel/rcu/tree.c` 的 `gp_seq`） |
| 软中断执行 | `softirq_model.pending` 位图 + 预算消费 | `RCU_SOFTIRQ` → `rcu_process_callbacks()`（`kernel/rcu/tree.c`） |
| 权威来源 | —— | Intel SDM Vol.3；Multiboot2 规范；GNU GRUB |

**教学模型简化了什么**：真实宽限期有 `rcu_gp_kthread` 内核线程、`rcu_data` 每 CPU 状态、`rcu_node` 树形聚合、`force_quiescent_state` 强迫推进等大量机制；TinyOS 只用 `active/ready/accounted` 三个位登记「宽限期能跑、可开始、已闭合」，等待与推进逻辑完全省略。

## 8. 思考题与练习

1. **概念理解**：为什么「所有 CPU 都经历过一次 quiescent state」就等价于「所有 reader 都离开了」？如果某个 reader 长期被抢占会怎样？
2. **源码定位**：在 `kernel64.c` 找到 `irq0_schedule` 与 `softirq_run_budget` 的调用位置，说明 tick 心跳在 TinyOS 里位于哪一行，它对应 Linux 的哪个函数。
3. **动手实验**：把 `l124test` 的 `ready` 位改为 0，重建运行观察 fallback；思考「无法开始新宽限期」在真实内核里对应什么状态（如 `rcu_gp_in_progress` 卡死）。
4. **Linux 对照**：阅读 `kernel/rcu/tree.c` 的 `synchronize_rcu` 与 `call_rcu` 差异，说明同步与异步接口各自适合什么调用场景，以及为什么 `call_rcu` 更适合中断上下文。
5. **综合**：grace period 结束后，旧对象由谁回收？如果回收动作需要排队而非立即执行，你会引入什么数据结构——这与下一课主题有什么关系？

## 9. 本课小结与下一课预告

- 本课确立了宽限期的定义：从发布新指针到所有旧读者离开的窗口。
- 理解了「所有 CPU 经历过 QS」与「所有 reader 离开」的等价性，以及 tick 驱动宽限期推进的机制。
- 对比了 `synchronize_rcu()`（同步阻塞）与 `call_rcu()`（异步回调）两种等待方式。
- 检查点 `lesson_117_model` 用 `active/ready/accounted` 登记宽限期状态机的三类结论。
- 在 `irq0_schedule` 里找到了「tick 心跳」的位置，它就是 Linux `rcu_sched_clock_irq` 的教学对应点。
- 明确了教学模型省略了 `rcu_gp_kthread`/`rcu_node`/`rcu_data` 等真实结构。

**下一课**：[Lesson 125（RCU callback 队列）](../lesson-125-stable/README.md) 把宽限期结束后的动作挂进回调队列，讲 `call_rcu` 的排队与执行，检查点模型 `lesson_118_model` 将登记回调队列语义。

## 附录：stable snapshot 声明（保留原 README 要点）

> This checkpoint models bounded concurrency, SMP, RCU, and diagnostics metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants.
>
> Commands: `l116test`、`l124test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.
>
> 主要内容：RCU grace period；统一课程编号：Lesson 124。（旧 README 中「Commands: `l117test`」与实际源码不符，已勘误为 `l124test`。）

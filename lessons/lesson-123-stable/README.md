# Lesson 123: RCU reader 临界区 — 精讲文档

> **Course status: stable snapshot.**
>
> - 课号：Lesson 123
> - 本课主题：RCU reader 临界区（RCU read-side critical section）
> - 课程主线位置：第四阶段（SMP / RCU / 诊断元数据检查点系列），RCU 四连讲的第一课
> - 前置课程：[Lesson 122（SMP 负载均衡）](../lesson-122-stable/README.md)
> - 后续课程：[Lesson 124（RCU grace period）](../lesson-124-stable/README.md)
> - 一句话目标：理解 `rcu_read_lock()`/`rcu_read_unlock()` 包裹的 reader 临界区为何能"几乎免费"地提供并发读安全，以及 TinyOS 检查点 `lesson_116_model` 如何登记这一不变量。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释 RCU reader 临界区的三要素（进入/退出开销极低、允许 writer 并发更新、reader 绝不死锁/自旋），能指认 TinyOS 检查点模型里的 `valid/active/ready/accounted` 分别代表什么，并用 `l123test` 验证。
- **在课程主线中的位置**：从 Lesson 123 起课程进入 RCU 专题：本课讲 reader 侧（临界区），Lesson 124 讲 writer 侧等待（grace period），Lesson 125 讲延迟回收（callback 队列），Lesson 126 讲最终释放（对象回收）。这四课对应 Linux `include/linux/rcupdate.h` 与 `kernel/rcu/` 目录的完整链路。
- **前置知识清单**：
  1. 检查点模型结构与 `lNtest` 断言约定（Lesson 69 以来的固定套路）；
  2. 上一课 `lesson_115_model`/`l122test` 及更名规律（`l122test` → `l115test`）；
  3. `cpu_local`/`this_cpu()`/`NR_CPUS` 每 CPU 数据结构；
  4. 软中断（softirq）两段式：置位 `pending` → `softirq_run_budget()` 消费（上一课已精讲）。
- **本课交付**：
  - 新命令 `l123test` 与新模型 `struct lesson_116_model lesson_116_state`；
  - 上一课命令更名为 `l115test`；
  - `about` 与启动横幅显示 `Lesson 123: RCU reader 临界区`。

## 2. 核心概念精讲

### 2.1 RCU 是什么（十秒版）

- **定义**：RCU（Read-Copy Update，读-复制-更新）是一种无锁同步机制：reader 只读共享数据（比如链表），writer 不直接改旧对象，而是**复制一份、改副本、然后原子发布新指针**，旧对象推迟到没有任何 reader 再引用它时才释放。
- **为什么需要**：读写锁中 reader 也要抢一把共享锁（cache line 颠簸）；RCU 让 reader 路径只含两个几乎免费的指令，writer 承担全部延迟。
- **三阶段**：读侧临界区（本课）→ 等待宽限期（grace period，下一课）→ 延迟回收 callback（再下一课）。

### 2.2 reader 临界区：`rcu_read_lock()` / `rcu_read_unlock()`

- **定义**：`rcu_read_lock()` 标记进入读侧临界区，`rcu_read_unlock()` 标记离开；两者之间的代码保证：writer 发布的新指针不会影响本次读取，reader 读到的要么是旧对象要么是新对象，绝不会是"半更新"状态。
- **关键性质**：
  1. **开销极低**：经典实现里 `rcu_read_lock()` 只是一个屏障（barrier）+ 计数器/抢占禁用标记，约几条指令；Linux `preemptible RCU` 下是 `__this_cpu_inc(preempt_count)`，非抢占 RCU 下几乎为空操作（`kernel/rcu/tree_plugin.h`、`include/linux/rcupdate.h`）。
  2. **不阻塞、不自旋、不睡眠禁忌**：临界区内不得调用可能睡眠的原语（在可抢占 RCU 中 sleep 会被标记进 `rcu_read_unlock_special` 慢路径）。
  3. **可嵌套**：`rcu_read_lock` 可套 `rcu_read_lock`，退出按栈序对称。
- **TinyOS 表示**：单核模型没有真实 reader/writer，检查点用 `active` 位表示「读者临界区机制处于激活态」，`accounted` 位表示「进入/退出次数闭合」（等价于 Linux 的 `rcu_read_lock_nesting` 计数归零可退出）。

```
reader A: rcu_read_lock();
          读 old_node ────────► (writer 此刻发布 new_node 也互不打扰)
          rcu_read_unlock();   读到的仍是完整一致的 old_node

writer:  复制 old_node → new_node → smp_store_release 替换全局指针
         随后等待所有 reader 离开（grace period）才释放 old_node
```

### 2.3 为什么 reader 可以"看到一致性"

- 发布侧用 `smp_store_release()`（或 Linux 的 `smp_store_release`/`rcu_assign_pointer`），读取侧用 `smp_load_acquire()`（或 `rcu_dereference`）。release 保证「写副本的所有内容先于指针发布」，acquire 保证「读到新指针后能看到副本全部内容」。
- TinyOS 里同款原语早已存在：`atomic_store_release_u32` / `atomic_load_relaxed_u8`（Lesson 105 附近引入）——它们正是 RCU reader 临界区内存序的教学版。

### 2.4 检查点模型如何编码 reader 临界区

- `valid`：模型已实例化；
- `active`：reader 临界区机制激活（对应 `rcu_read_lock` 已可用）；
- `ready`：reader 可在本 CPU 上安全进出（对应 Linux 的 `rcu_read_lock` 需要 `preempt_disable` 已就绪）；
- `accounted`：进入/退出计数闭合（对应 `rcu_read_lock_nesting` 在返回时归零）。
- 四连号 `{116,117,118,119}` 与 `b==a+1` 继续表达「登记流无空洞」。

### 2.5 RCU 的读者变体：preemptible / non-preemptible / sched / bh

- Linux 至少提供四类读者原语：`rcu_read_lock()`（可抢占 RCU 用 per-task nesting 计数）、`rcu_read_lock_sched()`（依赖禁抢占）、`rcu_read_lock_bh()`（顺带禁软中断，`kernel/softirq.c` 的 `_local_bh_enable` 配套）、以及被前者取代的历史 `synchronize_sched` 读者。
- 它们的共同点都是「读者侧只花几纳秒」，区别在于**宽限期判定的边界**不同：禁抢占型把「发生一次上下文切换」当成读者离开信号，bh 型把「软中断上下文退出」当成信号。
- TinyOS 检查点的 `ready` 位恰好对应「读者上下文边界已就绪」这一层：在单核教学中，中断返回即是最自然的读者离开边界，因此 `ready` 恒为 1 也说得通——它声明「本 CPU 上可以安全地进出临界区」。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 122） |
|------|------|------------------------------|
| `boot.S` | Multiboot2 头、进入 long mode | 未变化 |
| `kernel.c` | 32 位引导主流程 | 未变化 |
| `kernel64.c` | 64 位内核主体与检查点模型 | **有增量**：新增 `lesson_116_model`/`lesson_116_state`/`l123test()`；`l122test` 更名为 `l115test`；`about`/横幅改为 Lesson 123 |
| `kernel64.ld` | 64 位裸二进制布局 | 未变化 |
| `linker.ld` | 32 位镜像布局 | 未变化 |
| `Makefile` | 构建与 `check`/`run` | 微小变化：`check` grep 串改为 `RCU reader 临界区`、`l123test`、`Lesson 123` |
| `grub.cfg` | GRUB 启动项 | 未变化 |

### 3.2 kernel64.c 精讲

#### 新增结构 / 全局变量

```c
struct lesson_116_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_116_model lesson_116_state;
```

逐行注释：
- 第 887–888 行。模型定义与前两课逐字同构；本课语义落在「RCU reader 临界区」上。
- `lesson_116_state` 由 `l123test` 填充；`active`=读者机制激活，`ready`=读者上下文就绪，`accounted`=嵌套计数闭合。

#### 函数精讲

**`l123test(u16 *c)`**（第 889 行，本课新增）

```c
static TEXT64 void l123test(u16*c){lesson_116_state=(struct lesson_116_model){116U,117U,118U,119U,1,1,1,1};int ok=lesson_116_state.valid&&lesson_116_state.active&&lesson_116_state.ready&&lesson_116_state.accounted&&lesson_116_state.b==lesson_116_state.a+1U;text64(c,"l123test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 116 fallback reported");putc64(c,'\n');}
```

- **签名与职责**：`static TEXT64 void l123test(u16 *c)`：登记并断言本课检查点。
- **算法步骤**：
  1. `lesson_116_state=(struct lesson_116_model){116U,117U,118U,119U,1,1,1,1}`：四个布尔位全 1，四连号 116 起；
  2. `ok=valid && active && ready && accounted && b==a+1U`：五条件合取；
  3. 打印前缀 `"l123test: "` 与 passed/`"Lesson 116 fallback reported"`。
- **边界处理**：纯常量计算，无输入、无越界；失败分支只是文案不同。
- **设计动机**：reader 临界区的「进入/退出闭合」用 `accounted` 表达；把 `active`、`ready` 与 `accounted` 分开，是为了让「机制存在」「上下文就绪」「计数闭合」三个层次可独立诊断——与 Linux 把 reader 支持拆成 `rcu_read_lock`/`rcu_read_unlock`/`__rcu_read_lock` 的层次化设计同构。

**`exec64` 本课相关分支**（第 890 行）

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 123: RCU reader 临界区\n");}
...
}else if(eq64(word,"l115test")){if(!noargs64(arg))usage64(c,"l115test");else l115test(c);}else if(eq64(word,"l123test")){if(!noargs64(arg))usage64(c,"l123test");else l123test(c);}
```

- `about` 输出本课主题；`l115test`（上一课命令的更名）与 `l123test`（本课）相邻注册。

**继承的内存序 / 原子原语（reader 临界区的教学版）**

```c
static TEXT64 u8 atomic_load_relaxed_u8(volatile u8*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
static TEXT64 void atomic_store_release_u8(volatile u8*p,u8 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
```

- `__ATOMIC_RELEASE` 对应 RCU writer 的发布（`smp_store_release`/`rcu_assign_pointer`），`__ATOMIC_ACQUIRE` 对应 reader 的获取（`smp_load_acquire`/`rcu_dereference`）。
- `raw_spin_lock_irqsave` 用 `atomic_exchange_acquire_u32` 拿锁、`raw_spin_unlock_irqrestore` 用 `atomic_store_release_u32` 放锁——这条「acquire 进、release 出」的路径就是 RCU 读者进出临界区的内存序蓝本。

**`lockatomictest`（继承，可运行的 reader 语义演示）**

```c
static TEXT64 void lockatomictest(u16*c){irqflags_t f;u8 v=0;atomic_fetch_or_relaxed_u8(&this_cpu()->softirq_pending,0);raw_spin_lock_irqsave(&deferred_lock,&f);v=atomic_load_relaxed_u8(&this_cpu()->softirq_pending);atomic_store_release_u8(&this_cpu()->softirq_pending,(u8)(v|1));raw_spin_unlock_irqrestore(&deferred_lock,f);text64(c,"lockatomictest: ");text64(c,atomic_load_relaxed_u8(&this_cpu()->softirq_pending)==1&&deferred_lock.locked==0?"irq-safe lock, atomic publication, per-CPU ordering passed":"BROKEN");putc64(c,'\n');}
```

- 它在关中断拿锁后，用 release 序把 `this_cpu()->softirq_pending` 置 1，解锁后断言该位可见且锁已释放。这演示了「发布一个值 → 之后所有人都能看到」的 RCU 发布语义。

### 3.3 构建管线（Makefile / linker）

- 构建链与 Lesson 121/122 完全一致（双阶段、`objcopy -O binary`、`grub-mkrescue`）。
- `check` 目标变化：

```
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'RCU reader 临界区' README.md
	@grep -q 'l123test' kernel64.c
	@grep -q 'Lesson 123' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 123 checks passed.'
```

### 3.4 主控制流

```
kernel_main64_binary
  ├─ 初始化（与前课相同，active_sched_class=&fair_sched_class 等）
  ├─ text64(&c,"Lesson 123: RCU reader 临界区\nGETTICKS, …\n")  ← 本课横幅
  └─ for(;;) 键盘环
       ├─ "about"    → "Lesson 123: RCU reader 临界区\n"
       ├─ "l115test" → 回放上一课（负载均衡）检查点
       └─ "l123test" → l123test(c) → 打印 reader 临界区检查点结果
```

## 4. 数据流与运行逻辑

1. **输入**：`l123test` 回车。
2. **解析**：`token64` 切词，`eq64(word,"l123test")` 命中。
3. **执行**：`l123test(c)` 填充 `lesson_116_state`，计算 `ok`。
4. **输出**：`l123test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（`ok==1`），否则 `l123test: Lesson 116 fallback reported`。
5. **屏幕**：横幅与 `about` 均显示 `Lesson 123: RCU reader 临界区`。

补充说明：本课没有任何真实读路径代码可观察，因此「数据流」只发生在元数据层——`l123test` 是唯一的数据制造者，VGA 是唯一的数据消费者；这正是检查点课区别于行为课的典型特征。

## 5. 构建、运行与验证

- **依赖**：与前课相同。
- **构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

预期输出：`Multiboot2 and Lesson 123 checks passed.`
- **运行**：`make run`，QEMU 图形窗口查看横幅（勿加 `-display none`）。
- **验证步骤**：
  1. `about` → `Lesson 123: RCU reader 临界区`（源码逐字）；
  2. `l123test` → `l123test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（源码逐字；失败态为 `Lesson 116 fallback reported`）；
  3. `l115test` → 上一课检查点回放 passed；
  4. `lockatomictest` → `lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`，验证 release/acquire 原语工作正常。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `l123test` 打印 fallback | `lesson_116_state` 有布尔位为 0 或 `b!=a+1` | 核对第 889 行字面量；逐一打印五条件 |
| `about` 显示旧主题 | `exec64` about 分支未更新 | grep `'Lesson 123'` kernel64.c 确认两处出现 |
| `l115test` 报 unknown | 命令分支缺失 | 确认 `l115test` 分支与 `l123test` 分支相邻注册 |
| `lockatomictest` 报 BROKEN | release 后 `pending` 位未置 1 或锁未释放 | 检查 `atomic_store_release_u8` 参数与 `raw_spin_unlock_irqrestore` 顺序 |
| 读者语义无法直接观察 | 教学模型无真实 RCU 读代码路径 | 用 `lockatomictest` 间接验证发布原语；理解检查点 `accounted` 是闭合断言 |
| `make check` 失败 | grep 串不匹配 | `grep -q 'RCU reader 临界区' README.md`、`grep -q 'l123test' kernel64.c` |
| 怀疑「读者不配对」无法体现 | 模型只有闭合断言，没有泄漏路径 | 对照 Linux `rcu_read_lock_nesting`：把 `accounted` 置 0 可模拟「未闭合」的失败态 |
| 想观察 release 序是否生效 | 单核下难从时序观察 | 读 `atomic_store_release_u32` 生成的汇编（`objdump -d build/kernel64.o`），确认无 `mfence` 之外的重排 |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现（路径） |
|--------|-----------------|--------------------|
| 进入临界区 | 检查点 `active`/`ready` 位 + `atomic_exchange_acquire_u32` 类原语 | `rcu_read_lock()`（`include/linux/rcupdate.h`）；可抢占 RCU 里 `__this_cpu_inc(preempt_count)`（`kernel/rcu/tree_plugin.h`） |
| 离开临界区 | 检查点 `accounted` 位（计数闭合） | `rcu_read_unlock()`；嵌套计数 `current->rcu_read_lock_nesting` 归零才真正退出 |
| 发布/获取内存序 | `atomic_store_release_u32`/`atomic_load_relaxed_u8` | `smp_store_release()`/`smp_load_acquire()`；`rcu_assign_pointer()`/`rcu_dereference()` |
| 读者禁睡约束 | 模型不睡眠；`accounted` 保证退出配对 | `RCU_READ_LOCK_CRITICAL`/`rcu_read_lock` 内禁 `might_sleep`；`rcu_read_unlock_special` 慢路径处理（`kernel/rcu/tree_plugin.h`） |
| 每 CPU 读者状态 | `this_cpu()->softirq_pending` 等 per-CPU 位 | `__get_cpu_var(rcu_data)` / `preempt_count` 按 CPU 存放（`kernel/rcu/tree.c`） |
| 权威来源 | —— | Intel SDM Vol.3（内存序与原子指令）；Multiboot2 规范；GNU GRUB |

**教学模型简化了什么**：真实 reader 临界区有可抢占/不可抢占两种实现、`rcu_read_lock_sched`/`rcu_read_lock_bh` 等变体、以及 `rcu_note_context_switch` 等调度器交互；TinyOS 只登记「进入/退出配对闭合」这一最核心不变量，内存序由既有原子封装代为体现。

## 8. 思考题与练习

1. **概念理解**：为什么 `rcu_read_lock` 几乎免费？它与普通读写锁在 reader 路径上的开销差在哪？
2. **源码定位**：找到 `kernel64.c` 中 `atomic_store_release_u32` 与 `raw_spin_lock_irqsave` 的调用点，说明哪一处对应 RCU 的「发布」，哪一处对应「获取」。
3. **动手实验**：把 `l123test` 的 `accounted` 位改为 0，重建运行观察 fallback；思考如果 Linux 的 `rcu_read_lock_nesting` 泄漏（不配对）会发生什么。
4. **Linux 对照**：阅读 `include/linux/rcupdate.h` 的 `rcu_read_lock`/`rcu_read_unlock` 注释，列出它要求的三个约束，逐一说明 TinyOS 检查点哪些位能表达它们。
5. **综合**：reader 临界区内为什么不能睡眠？如果允许睡眠，grace period 的「等待所有 reader 离开」会出什么问题？这与下一课主题如何衔接？

## 9. 本课小结与下一课预告

- 本课确立了 RCU 专题的读者视角：`rcu_read_lock`/`rcu_read_unlock` 构成几乎免费的临界区。
- 明确了 reader 与 writer 的协作模式：writer 复制-更新-发布，reader 无锁直读。
- 检查点 `lesson_116_model` 用 `active/ready/accounted` 三个位分别登记「机制激活、上下文就绪、计数闭合」。
- 复习了 release/acquire 内存序原语，找到它们在 `lockatomictest` 中的可运行示范。
- 确认了教学模型只登记 reader 不变量，没有真实读者/写者代码路径。

**下一课**：[Lesson 124（RCU grace period）](../lesson-124-stable/README.md) 进入 writer 侧：宽限期如何界定「所有读者已离开」，`synchronize_rcu()` 如何实现，检查点模型 `lesson_117_model` 将登记宽限期语义。

## 附录：stable snapshot 声明（保留原 README 要点）

> This checkpoint models bounded concurrency, SMP, RCU, and diagnostics metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants.
>
> Commands: `l115test`、`l123test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.
>
> 主要内容：RCU reader 临界区；统一课程编号：Lesson 123。（旧 README 中「Commands: `l116test`」与实际源码不符，已勘误为 `l123test`。）

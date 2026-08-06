# Lesson 114: 原子操作与内存序 — 精讲文档

> **课号**：Lesson 114（检查点课 / Checkpoint Lesson，可执行课）
> **主题**：原子操作与内存序（Atomic Operations and Memory Ordering）
> **课程主线位置**：第 5 阶段检查点主线（约 100–114）并发站的收官课。113 课讲了
> spinlock/mutex 竞争；本课把支撑锁的「原子操作 + acquire/release/relaxed 内存序」本身
> 讲透。114 之后主线继续向后续阶段推进。
> **前置课程**：[`lesson-113-stable/README.md`](../lesson-113-stable/README.md)
> **后续课程**：第 5 阶段后续（检查点主线下一站，见 §9 预告）
> **一句话目标**：理解 TinyOS 五个 `__atomic_*` 内建各自编译出什么指令、acquire/release/
> relaxed 三种序分别承诺什么、为什么「拿锁用 acquire、放锁用 release、计数用 relaxed」，
> 以及 `l114test` 如何用 `lesson_107_model` 重申并发检查点。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能逐条解释 `atomic_load_relaxed_u8`/`atomic_store_release_u8`/
`atomic_fetch_or_relaxed_u8`/`atomic_exchange_acquire_u32`/`atomic_store_release_u32`
五个函数「为什么这样命名、用在什么场景、违背序会出什么问题」。`l114test` 输出
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed` 即通过。

- **在课程主线中的位置**：第 5 阶段检查点主线并发站（113–114）的终点。本课主题
  「原子操作与内存序」是 113 课「锁竞争」的底层：锁只是原子交换 + acquire/release 的
  组合，本课把这层组合拆开讲。
- **责任边界**：本课**不新增**原子原语（五个 `__atomic_*` 内建自 lesson-50 继承）。
  新增的只有 `struct lesson_107_model`、`lesson_107_state`、`l114test()` 与主题串。
- **前置知识清单**：① C11 `__atomic_*` 内建与 `__ATOMIC_RELAXED/ACQUIRE/RELEASE`
  常量；② `raw_spin_lock_irqsave` 的 acquire 拿锁 / release 放锁配对（lesson-50/113）；
  ③ `sem_down/sem_up` 的阻塞同步；④ `lockatomicinfo` 输出的
  `memory order: acquire/release/relaxed` 行。
- **本课交付**：命令 `l114test`（新增）与 `l106test`（由 113 课 `l113test` 更名）；
  `about`/横幅显示 `Lesson 114: 原子操作与内存序`；
  `make check` 输出 `Multiboot2 and Lesson 114 checks passed.`。

---

## 2. 核心概念精讲

### 2.1 概念一：原子操作 —— 不可分割的读-改-写

定义：原子操作是对内存位置执行「读 + 修改 + 写」且全程不可被其他执行流插队的操作。
在 x86 上由 `LOCK` 前缀指令（`lock xchg`、`lock cmpxchg`、`lock xadd`）保证。

为什么需要：`threads[i].state=THREAD_RUNNABLE` 这类普通赋值在并发下会丢更新（两个线程
同时「读-改-写」同一变量时互相覆盖）。原子操作让每次「读-改-写」都像一条指令一样完成。

工作机制：C11 `__atomic_load_n(p,__ATOMIC_RELAXED)` 是原子读，
`__atomic_store_n(p,v,__ATOMIC_RELEASE)` 是原子写，
`__atomic_exchange_n(p,v,__ATOMIC_ACQUIRE)` 是原子交换（写 v、返回旧值），
`__atomic_fetch_or(p,v,__ATOMIC_RELAXED)` 是原子 OR（`*p|=v` 并返回旧值）。

### 2.2 概念二：内存序（Memory Ordering）——reorder 的自由度

定义：编译器与 CPU 都会重排（reorder）无依赖的内存访问。内存序参数告诉它们**哪些重排
不被允许**。TinyOS 用到三种：

- `relaxed`（最弱）：只保证原子性，不限制重排。用于计数器、标志位的单次读/写——顺序
  无关紧要时用它最省。
- `acquire`（获取）：保证「acquire 操作**之后**的内存访问不会重排到它之前」。用于拿锁——
  拿锁成功后，临界区里的读写必须真的发生在临界区内。
- `release`（释放）：保证「release 操作**之前**的内存访问不会重排到它之后」。用于放锁/
  发布数据——临界区的写入在解锁前必须对其他观察者可见。

配对规则：`acquire` 与 `release` 必须成对出现在「生产者/消费者」两端。生产者
`release` 发布数据，消费者 `acquire` 消费数据，中间的普通读写才能正确「随锁流动」。

### 2.3 概念三：五个 `__atomic_*` 内建的语义地图

```c
atomic_load_relaxed_u8       —— 原子读 u8（relaxed）
atomic_store_release_u8      —— 原子写 u8（release，发布）
atomic_fetch_or_relaxed_u8   —— 原子置位 u8（relaxed）
atomic_exchange_acquire_u32  —— 原子交换 u32（acquire，拿锁核心）
atomic_store_release_u32     —— 原子写 u32（release，放锁核心）
```

为什么需要：TinyOS 只有 `raw_spin_lock_irqsave` 一个锁消费者（`deferred_lock`），但
`lockatomictest` 需要完整展示「锁内发布数据 + per-CPU 标志」的范式。五个内建正好覆盖
「读/写/置位/交换」四种操作 ×「relaxed/acquire/release」三种序的最小组合。

### 2.4 概念四：从原子操作到锁 —— acquire/release 配对

`raw_spin_lock_irqsave` 拿锁 = `atomic_exchange_acquire_u32(&l->locked,1)`：交换本身是
原子 RMW，`acquire` 保证进入临界区后不再被重排出界。
`raw_spin_unlock_irqrestore` 放锁 = `atomic_store_release_u32(&l->locked,0)`：写 0 是
原子写，`release` 保证临界区内的写入先于解锁可见。

如果拿锁用 relaxed：另一个 CPU 可能看到「临界区写操作先于锁获取」，临界区被撕开。
如果放锁用 relaxed：临界区写操作可能被延迟到解锁之后才可见，锁失去屏障意义。
这正是「锁 = 原子 + 内存序」的教学结论。

### 2.5 概念五：`lesson_107_model` 检查点模型

```c
struct lesson_107_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_107_model lesson_107_state;
```

与 106 模型同构；`l114test` 注入 `{107U,108U,109U,110U,1,1,1,1}` 并断言
`valid&&active&&ready&&accounted&&b==a+1U`。成功串保持并发措辞
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-113） |
|---|---|---|
| `boot.S` | Multiboot2 header + 长模式引导 | 未变化 |
| `kernel.c` | 32 位阶段 | 未变化 |
| `kernel64.c` | 64 位内核主体 | `l113test`→`l106test` 更名；新增 `struct lesson_107_model`/`lesson_107_state`/`l114test`；exec64 增 `l114test` 分支；`about` 与横幅改「Lesson 114: 原子操作与内存序」 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 + check | check grep 更新为 `原子操作与内存序`/`l114test`/`Lesson 114` |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 本课增量（源码逐字）

更名命令 `l106test`（即 113 课的 `l113test`）：

```c
static TEXT64 void l106test(u16*c){lesson_106_state=(struct lesson_106_model){106U,107U,108U,109U,1,1,1,1};int ok=lesson_106_state.valid&&lesson_106_state.active&&lesson_106_state.ready&&lesson_106_state.accounted&&lesson_106_state.b==lesson_106_state.a+1U;text64(c,"l106test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 106 fallback reported");putc64(c,'\n');}
```

- 函数名改为 `l106test`（= 模型号 106），逻辑与成功串逐字未变。

新增结构与命令：

```c
struct lesson_107_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_107_model lesson_107_state;
static TEXT64 void l114test(u16*c){lesson_107_state=(struct lesson_107_model){107U,108U,109U,110U,1,1,1,1};int ok=lesson_107_state.valid&&lesson_107_state.active&&lesson_107_state.ready&&lesson_107_state.accounted&&lesson_107_state.b==lesson_107_state.a+1U;text64(c,"l114test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 107 fallback reported");putc64(c,'\n');}
```

逐行拆解：

1. 结构 `lesson_107_model`：与 106 同构，4 个 u32 计数 + 4 个 u8 状态位；
2. 全局 `lesson_107_state`：默认全零，仅 `l114test` 注入；
3. `l114test` 赋值 `{107U,108U,109U,110U,1,1,1,1}`：四状态位全 1；
4. 断言 `ok=`：五条件 AND（`valid/active/ready/accounted` + `b==a+1U`）；
5. 输出：`l114test: ` + 并发措辞成功串或 `Lesson 107 fallback reported`。

exec64 新增分支（源码逐字）：

```c
else if(eq64(word,"l114test")){if(!noargs64(arg))usage64(c,"l114test");else l114test(c);}
```

主题横幅与 about（源码逐字）：

```c
text64(&c,"Lesson 114: 原子操作与内存序\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

```c
text64(c,"Lesson 114: 原子操作与内存序\n");
```

### 3.3 继承机制精讲——原子原语与内存序的源码基础（本课主题的实质内容）

五个原子内建（源码逐字）：

```c
static TEXT64 u8 atomic_load_relaxed_u8(volatile u8*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
static TEXT64 void atomic_store_release_u8(volatile u8*p,u8 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 u8 atomic_fetch_or_relaxed_u8(volatile u8*p,u8 v){return __atomic_fetch_or(p,v,__ATOMIC_RELAXED);}
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
```

- 全部接收 `volatile` 指针：防编译器把原子访问当作普通变量优化/合并；
- `relaxed` 两个（load、fetch_or）：用于「顺序无关」的读与置位；
- `release` 两个（store u8、store u32）：用于「发布数据 / 放锁」；
- `acquire` 一个（exchange u32）：拿锁核心，read-modify-write 且带 acquire 屏障；
- 命名规范 `atomic_<操作>_<序>_<宽度>`：读者一眼可知语义，这是并发代码可维护性的关键。

锁的构建（源码逐字）：

```c
static TEXT64 void raw_spin_lock_irqsave(raw_spinlock_t*l,irqflags_t*f){*f=irq_save64();while(atomic_exchange_acquire_u32(&l->locked,1)){} }
static TEXT64 void raw_spin_unlock_irqrestore(raw_spinlock_t*l,irqflags_t f){atomic_store_release_u32(&l->locked,0);irq_restore64(f);}
```

- 拿锁：`atomic_exchange_acquire_u32(&l->locked,1)` 原子地「写 1 并返回旧值」；旧值 1
  表示已被占，`while` 自旋；旧值 0 表示到手，`acquire` 使临界区读写不得上浮；
- 放锁：`atomic_store_release_u32(&l->locked,0)` 写 0，`release` 使临界区写入先于解锁
  可见——acquire/release 在此配对；
- `irq_save64`/`irq_restore64` 负责关/恢复中断，见 §2 与 lesson-113。

`lockatomicinfo` 输出内存序摘要（源码逐字）：

```c
static TEXT64 void lockatomicinfo(u16*c){text64(c,"locks/atomics/percpu: NR_CPUS ");hex64(c,NR_CPUS);text64(c," lock ");hex64(c,deferred_lock.locked);text64(c," cpu id/pending/work: ");hex64(c,this_cpu()->id);text64(c,"/");hex64(c,this_cpu()->softirq_pending);text64(c,"/");hex64(c,this_cpu()->work_used);text64(c," memory order: acquire/release/relaxed\n");}
```

- 末尾固定串 `memory order: acquire/release/relaxed` 是本课主题的官方注脚；
- 同时展示 `deferred_lock.locked`、per-CPU 的 `softirq_pending`/`work_used`——
  「锁 + per-CPU + 内存序」三者一起展示。

`lockatomictest` 的发布范式（源码逐字，见 lesson-113 §3.3 全文）：

```c
v=atomic_load_relaxed_u8(&this_cpu()->softirq_pending);
atomic_store_release_u8(&this_cpu()->softirq_pending,(u8)(v|1));
```

- 锁内先 relaxed 读、再 release 写：`release` 保证该标志的发布对其他 CPU 可见时才解锁；
- 断言 `softirq_pending==1 && deferred_lock.locked==0` 验证「发布生效且锁干净」。

### 3.4 构建管线

与 lesson-113 完全一致（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse* -Werror`
编译，`ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary`，boot.S `.incbin`，
`grub-mkrescue`）。`check` 目标：`grub-file --is-x86-multiboot2` + grep
`原子操作与内存序`/`l114test`/`Lesson 114`，通过打印
`Multiboot2 and Lesson 114 checks passed.`。`run` 用 `-accel tcg -serial stdio` 的 QEMU。

### 3.5 主控制流

```text
GRUB → kernel_main32 → 长模式 → kernel_main64_binary
  ├─ module_init_model(); init_model_start(); wait_model_start();
  │  adoption_start(); resource_start(); pmm_init(h); vma_init();
  │  reclaim_init(); vfs_init(); address_space_init(...)
  ├─ ... framebuffer_init(h); install_idt(h); pit_init(); pic_init()
  ├─ 横幅 "Lesson 114: 原子操作与内存序\nGETTICKS, ... bounded reclaim metadata\n"
  └─ 键盘循环 → exec64
        ├─ l106test → 复验 lesson_106 检查点（更名命令）
        ├─ l114test → 复验 lesson_107 检查点（本课新增）
        ├─ lockatomicinfo → 打印 acquire/release/relaxed 摘要
        └─ lockatomictest → 演示锁内 release 发布
```

---

## 4. 数据流与运行逻辑

```text
输入 "l114test" → exec64 → l114test(c)
  → lesson_107_state = {107,108,109,110, 1,1,1,1}
  → ok = valid(1)&&active(1)&&ready(1)&&accounted(1)&&b(108)==a(107)+1
  → "l114test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed"

原子/内存序证据链：
  lockatomicinfo: "lock 0000000000000000 cpu id/pending/work: ... memory order: acquire/release/relaxed"
  lockatomictest: raw_spin_lock_irqsave → release 发布 softirq_pending=1 → unlock
                → "lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed"
  pctest/pcgo:   信号量竞争 → pcinfo "P errors/ok: ... yes"
```

---

## 5. 构建、运行与验证

### 5.1 依赖

`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 114 checks passed.`（README 必须含
`原子操作与内存序` 与 `Lesson 114`，kernel64.c 必须含 `l114test`）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 114: 原子操作与内存序\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字抄录）：

```bash
l114test
```

预期：

```text
l114test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed
```

```bash
about
```

预期：`Lesson 114: 原子操作与内存序`

```bash
lockatomicinfo
```

预期末行：` memory order: acquire/release/relaxed`（源码逐字，整行前缀是
`locks/atomics/percpu: NR_CPUS 0000000000000001 lock 0000000000000000 cpu id/pending/work: ...`）。

```bash
lockatomictest
```

预期：`lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`

继承回归：`l106test`、`pctest`/`pcgo`/`pcinfo`、`softirqtest`、`preempttest` 行为不变。

### 5.4 课程实测记录（稳定快照）

旧 README 声明「Commands: `l107test`」——**命令名以源码为准勘误**：本课源码可执行命令是
`l106test`（更名）与 `l114test`（新增）；`l107test` 是模型编号（lesson_107），不是命令。
旧 README 的「bounded concurrency, SMP, RCU, and diagnostics」描述与源码 `l114test`
成功串一致。`make check` 复验输出 `Multiboot2 and Lesson 114 checks passed.`，
`l114test` 显示 passed，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l114test` 输出 `Lesson 107 fallback reported` | `lesson_107_state` 未注入或断言不成立 | 核对 `l114test` 赋值 `{107U,108U,109U,110U,1,1,1,1}` 与 `ok` 五条件 |
| `lockatomictest` 输出 `BROKEN` | `deferred_lock.locked` 未归零或 `softirq_pending` 未发布 | 检查 unlock 是否执行 `atomic_store_release_u32`；发布目标是否为 `this_cpu()->softirq_pending` |
| `lockatomicinfo` 不显示 `acquire/release/relaxed` | 输出串被改写 | grep `memory order: acquire/release/relaxed` kernel64.c |
| 自旋锁拿不到（`pctest` 卡死） | `sem_up` 未唤醒或 `pc_start_event` 未广播 | 确认顺序 `pctest`→`pcgo`；`pcinfo` 查 `E sig/wait:` 与 `S/I count/wait:` |
| `make check` 报错 | README 缺 `原子操作与内存序`/`Lesson 114` 或 kernel64.c 缺 `l114test` | 对照 Makefile 三条 grep |
| `about` 显示旧课号 | 主题串未更新 | grep `Lesson 114: 原子操作与内存序` kernel64.c |
| help 列表找不到 `l114test` | 已知小瑕疵：help 串未追加检查点命令 | 用 `about`/README 发现命令 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `atomic_load_relaxed_u8`/`atomic_store_release_u8` | `include/linux/atomic.h`：`atomic_read`/`atomic_set`；`include/asm-generic/atomic.h` | Linux 提供原子 API 家族；内存序变体用 `smp_load_acquire`/`smp_store_release` |
| `atomic_exchange_acquire_u32`（拿锁） | `arch/x86/include/asm/spinlock.h` `arch_spin_lock`；`atomic_xchg_acquire` | x86 用 `lock xchg` 原子交换；语义一致 |
| `atomic_store_release_u32`（放锁） | `smp_store_release`（`include/asm-generic/barrier.h`） | release 写 + 屏障；x86 上 `xchg` 自带全屏障 |
| acquire/release/relaxed 三种序 | `include/linux/compiler.h`：`READ_ONCE`/`WRITE_ONCE`；`smp_load_acquire`/`smp_store_release`；`include/asm-generic/barrier.h` | TinyOS 用 C11 `__atomic_*`；Linux 用宏+架构屏障 |
| `lockatomictest` 锁内 release 发布 | Linux 无锁发布范式（`WRITE_ONCE` + `smp_store_release`） | 教学模型用 per-CPU `softirq_pending` 演示同一范式 |
| `raw_spin_lock_irqsave` 关中断 + 自旋 | `include/linux/spinlock.h` `raw_spin_lock_irqsave`；`arch/x86/include/asm/irqflags.h` | 先保存 IF 再关中断再自旋，完全一致 |

**权威来源**：C11 标准 §7.17.2/§7.17.3（memory_order 与原子类型）；Linux v6.x
（`include/linux/atomic.h`、`include/asm-generic/atomic.h`、`include/linux/spinlock.h`、
`include/asm-generic/barrier.h`、`arch/x86/include/asm/spinlock.h`）；Intel SDM
（`LOCK` 前缀与 `XCHG` 的原子语义）。

**教学模型简化了什么**：单核 + 关中断，无真实 SMP 缓存一致性流量；relaxed/acquire/
release 在 x86 单核下行为等价（测试命令无并发也能过，教学上靠「命名 + 注释」而非
「复现数据竞争」）；无 `seq_cst`（最强序）演示；无真实 RCU 实现（主题措辞提及）。

---

## 8. 思考题与练习

1. **概念理解**：为什么拿锁用 `atomic_exchange_acquire_u32` 而不是
   `atomic_load_relaxed_u8` 再单独判断？「读-改-写」为什么要原子？
2. **源码定位**：`lockatomicinfo` 的输出串里哪一段逐字是 `memory order:
   acquire/release/relaxed`？它对应哪三种 C11 内存序？
3. **动手实验**：把 `raw_spin_unlock_irqrestore` 的
   `atomic_store_release_u32(&l->locked,0)` 改成 `atomic_store_n(&l->locked,0,
   __ATOMIC_RELAXED)`（保持语义破坏序），`make run` 后输入 `lockatomictest` 观察是否
   仍 passed——理解单核无并发时 relaxed/release 可互换，然后改回（勿提交）。
4. **Linux 对照**：读 `include/asm-generic/barrier.h` 的 `smp_store_release` 定义，
   对照本课 `atomic_store_release_u32`，说明 Linux 在 ARM 上如何用 `dmb` 实现
   release 而 x86 上几乎免费。
5. **设计思考**：`l114test` 成功串与 113 课完全相同（`bounded concurrency, SMP, RCU,
   and diagnostics checkpoint passed`）。检查点课里「成功串不变、只换模型号」与「换成功串
   标志阶段切换」（113 课那次）两种做法各自表达什么意图？

---

## 9. 本课小结与下一课预告

**小结**：本课主题「原子操作与内存序」，是检查点主线并发站的收官课。源码增量依旧是
「更名 + 新模型 + 新命令 + 主题串」：`l113test`→`l106test`、新增
`struct lesson_107_model`/`lesson_107_state`/`l114test`、exec64 增分支、about/横幅更新。
主题机制全部继承：五个 `__atomic_*` 内建覆盖「读/写/置位/交换 × relaxed/acquire/
release」；`raw_spin_lock_irqsave`/`raw_spin_unlock_irqrestore` 是
「acquire 拿锁 + release 放锁」的完整配对；`lockatomictest` 演示锁内 release 发布
per-CPU 标志；`lockatomicinfo` 打印官方注脚 `memory order: acquire/release/relaxed`。
`l114test` 用 `{107U,108U,109U,110U,1,1,1,1}` 重申
`bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。

**下一课预告**：第 5 阶段检查点主线后续课程将在本课（并发终点）基础上继续向新的主题
推进（具体课号以主线规划为准）。本课与 113 课共同构成「锁（spinlock/mutex）→ 原子操作
与内存序」的并发两连站：先看锁怎么用，再看锁的原子/内存序底座。检查点模型序列
（lesson_101→lesson_107）随课号递增，命令名逐一收敛到模型号，`make check` 的三条 grep
持续把关主题串、命令与课号。

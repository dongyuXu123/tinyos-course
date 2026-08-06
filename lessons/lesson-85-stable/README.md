# Lesson 85: fork 内存屏障与一致性 — 精讲文档

> **课号**：Lesson 85 ｜ **主题**：fork 内存屏障与一致性（fork memory barrier and consistency）
> **课程主线位置**：进程/调度/COW 元数据阶段（Lesson 71–87 检查点序列，本课为 Lesson 78 原型的检查点）
> **前置课程**：[`../lesson-84-stable/README.md`](../lesson-84-stable/README.md)（共享页生命周期）
> **后续课程**：[`../lesson-86-stable/README.md`](../lesson-86-stable/README.md)（调度公平性验证）
> **一句话目标**：用「检查点状态 + 元数据模型」验证 fork 语义下 copy/share 边界、原子发布与内存屏障（acquire/release）带来的一致性保证，并确认内核不执行任何真实子进程指令。

本课是稳定快照（stable snapshot）检查点：`kernel64.c` 相对上一课只做了一处增量——把上一课的 `l84test` 恢复为 `l77test`，新增 `lesson_78_model` 状态与 `l85test` 检查点测试，并更新 `about`/开机横幅为本课主题。所有继承的进程、GUI、子系统回归保持有效；会话不变量（session invariants）继续保留。

---

## 1. 课程定位（Mission）

**学完本课你能**：看懂 fork 在「只做元数据、不执行子进程」教学模型下的语义边界；说清为什么 fork 的 copy/share 决策与「先发布、后读取」的内存屏障（acquire/release）共同构成一致性保证；能运行 `l85test` 检查点并在 VGA 上读出 `bounded scheduling and copy-on-write checkpoint passed`。

**在课程主线中的位置**：本课处于进程/调度/COW 元数据阶段（Lesson 71–87 检查点序列）的末端附近，其课程原型是 Lesson 78「fork 内存屏障与一致性」。上一课（Lesson 84）讲共享页生命周期，把 COW 页从「建立」推进到「共享与生命周期」；本课把 fork 侧的一致性视角收束为可验证的检查点，下一课（Lesson 86）转向调度公平性验证。之后课程进入 VFS 主线（Lesson 88 起）。

**前置知识清单**（学本课前必须掌握）：
1. fork/clone 的元数据模拟模型：`struct fork_model`、`RESOURCE_COPIED`/`RESOURCE_SHARED` 两种资源策略、`fork_model_run`/`fork_model_validate`（Lesson 39–41）。
2. 内存序基础：GCC `__atomic` 内建函数与 `__ATOMIC_RELAXED`/`__ATOMIC_ACQUIRE`/`__ATOMIC_RELEASE` 的含义，以及 `raw_spin_lock_irqsave` 用 acquire 换锁、release 放锁的写法（Lesson 45 锁/原子/percpu）。
3. COW 与共享页：匿名页、页缓存、`fault_pages`/`page_cache` 的引用计数与回收（Lesson 75–76、Lesson 84）。
4. 中断保存/恢复原语 `irq_save64`/`irq_restore64` 与 `ticks` 时钟源。

**本课交付（可见结果）**：
- 开机横幅更新为 `Lesson 85: fork 内存屏障与一致性`；
- `about` 命令输出同一主题串；
- 新命令 `l85test` 在 VGA 上输出 `l85test: bounded scheduling and copy-on-write checkpoint passed`（或其 fallback）；
- 继承的全部历史命令（`forktest`、`forklifecycle`、`lockatomictest`、`reclaimtest` 等）继续可用。

---

## 2. 核心概念精讲

### 2.1 内存屏障（memory barrier）与内存序（memory ordering）

**直觉**：CPU 和编译器都可能重排内存操作——只要单线程视角下结果不变。但在多 CPU/中断场景下，另一个执行者（另一 CPU 的中断处理、另一个核）看到的顺序可能与代码书写顺序不同。内存屏障就是「钉住顺序」的指令：它不允许两侧的内存访问跨越它。

**准确定义**（Intel SDM Vol.3A §8.2 / C11 内存模型）：
- 编译器屏障：`__asm__ volatile("":::"memory")` 只阻止编译器重排，不阻止 CPU 重排。
- 硬件屏障：`mfence`/`lfence`/`sfence` 阻止 CPU 重排（本课没有显式使用，因为 x86 TSO 在单核 + 锁前缀下足够）。
- 内存序（memory order）是抽象层：`__ATOMIC_RELAXED`（无顺序约束）、`__ATOMIC_ACQUIRE`（本操作之前的读取不得被移动到它之后）、`__ATOMIC_RELEASE`（本操作之前的写入不得被移动到它之后）。

**为什么 fork 需要它**：fork 的典型流程是「父进程先构造子进程的地址空间元数据（copy 或共享标记），再向父进程返回子 pid」。如果构造动作与「对外可见」的动作之间没有正确排序，另一个执行者可能看到「半初始化」的子进程元数据。教学模型用 release/acquire 这对序来模拟这一保证：写方 release「发布」，读方 acquire「获取」，中间是一条 acquire-release 同步链（happens-before）。

### 2.2 fork 的 copy-vs-share 语义

`enum resource_policy { RESOURCE_COPIED=1, RESOURCE_SHARED=2 }` 把 Linux fork 的资源决策压缩成两个标志：

| 标志 | 含义 | Linux 对照 |
|---|---|---|
| `copied_metadata` = `RESOURCE_COPIED` | 进程可见元数据（identity、地址空间记录）是复制出来的 | fork 复制 `task_struct`、`mm_struct`（`copy_mm`） |
| `shared_resources` = `RESOURCE_SHARED` | 内核级资源（console、PIT、PMM 策略）显式共享 | 打开的文件表 `copy_files` 后共享 `file`；CLONE_FILES 共享 fd 表 |

`fork_model_validate` 正是检查这两个边界的布尔式：
```c
int ok=fork_model.valid&&fork_model.child_pid>fork_model.parent_pid&&fork_model.child_tid!=fork_model.parent_tid&&fork_model.parent_address_space!=fork_model.child_address_space&&fork_model.copied_metadata==RESOURCE_COPIED&&fork_model.shared_resources==RESOURCE_SHARED;
```
可见性要求：子地址空间记录必须「不同于」父记录（copy），而共享资源位必须是 SHARED（share）——这就是本课标题中「一致性」的检查对象。

### 2.3 发布/获取（release/acquire）一致性

教学内核里锁与原子操作是最具体的屏障载体：

```c
static TEXT64 void raw_spin_lock_irqsave(raw_spinlock_t*l,irqflags_t*f){*f=irq_save64();while(atomic_exchange_acquire_u32(&l->locked,1)){} }
static TEXT64 void raw_spin_unlock_irqrestore(raw_spinlock_t*l,irqflags_t f){atomic_store_release_u32(&l->locked,0);irq_restore64(f);}
```
- 加锁用 `__ATOMIC_ACQUIRE` 的 xchg：读锁后，锁内保护的数据读取不会被重排到加锁前；
- 解锁用 `__ATOMIC_RELEASE` 的 store：解锁前对数据的写入不会被重排到解锁后。
因此「写数据 → release 解锁」与「acquire 加锁 → 读数据」构成 happens-before。这正是 fork 元数据发布所需的一致性模型：先填好 `fork_model` 全部字段，再让观察者可见。

### 2.4 与 COW/共享页的衔接

本课是 Lesson 84（共享页生命周期）的直接延续。共享页/页缓存侧的引用计数与 `refs` 语义在 `reclaim_one`/`page_cache_get` 中已经体现「一个物理页可以被多个映射共享」，而本课的 fork 侧则回答「共享还是复制由谁决定」——由 fork 的资源策略位决定。二者合起来才是完整的 COW 生命周期：fork 时把页标记为共享，写缺页时才触发复制（Lesson 76 的统计模型）。

### 2.5 检查点模型：lesson_78_model

本课新增的 `struct lesson_78_model` 是标准检查点状态机：四个 `u32` 计数器 + 四个 `u8` 布尔位（valid/active/ready/accounted）。`l85test` 一次性初始化并断言 `b == a + 1U`，用一个简单不变量代表「上一课与这一课之间状态连续」。它是教学的确定性验证桩，不执行任何真实调度或 fork。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：PMM、VM、进程/线程/信号、调度、VFS、GUI、检查点 | 恢复 `l77test`；新增 `lesson_78_model`/`lesson_78_state`/`l85test`；`exec64` 增加 `l85test` 分支；`about` 与开机横幅更新为本课主题 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、长期模式交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（`__idle_guard`/`rsp0`/`ist1` 栈守护） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本（Multiboot2 头部、`.text64` 段） | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 目标课程串更新（`fork 内存屏障与一致性`/`Lesson 85`/`l85test`） |
| `grub.cfg` | GRUB menuentry | 未变化（仍显示 Lesson 52 标题） |

### 3.2 kernel64.c（累积内核主体，本课只精讲增量与直接相关部分）

#### 3.2.1 本课新增的数据结构与检查点函数

```c
struct lesson_78_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_78_model lesson_78_state;
static TEXT64 void l85test(u16*c){lesson_78_state=(struct lesson_78_model){78U,79U,80U,81U,1,1,1,1};int ok=lesson_78_state.valid&&lesson_78_state.active&&lesson_78_state.ready&&lesson_78_state.accounted&&lesson_78_state.b==lesson_78_state.a+1U;text64(c,"l85test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 78 fallback reported");putc64(c,'\n');}
```

逐行分析：
1. **结构定义**：`a,b,c,d` 四个 32 位计数器模拟本课检查点的「延续计数」；`valid/active/ready/accounted` 四个布尔位是检查点必须同时成立的属性。这与 `lesson_77_model` 等前辈结构完全同构，保证历史检查点可对齐。
2. **初始化**：`lesson_78_state=(struct lesson_78_model){78U,79U,80U,81U,1,1,1,1}` 把四个计数器定为连续序列 78→81，并把四个布尔位全部置 1。78 是课程原型编号（COURSE-MANIFEST 中 Lesson 85 的 Origin 为 Lesson 78）。
3. **断言**：`ok=...valid&&active&&ready&&accounted&&b==a+1U`——四属性 + 「相邻计数差一」不变量。这是确定性检查点的核心：只要状态被正确初始化且未被破坏，`b==a+1U` 恒真。
4. **输出**：通过 `text64(c,"l85test: ")` 打前缀，再按 `ok` 三目选择输出串。**预期成功串**（从源码逐字抄录）：
   `l85test: bounded scheduling and copy-on-write checkpoint passed`
   fallback 串：`Lesson 78 fallback reported`。
5. **设计动机**：这是「验证锚点」而不是功能实现——它不触发任何真实 fork/COW，只是让维护者与学习者用一条命令确认内核镜像仍是本课的正确版本，防止 stale snapshot 混入。

#### 3.2.2 fork 模型相关函数（本课主题的直接载体）

`fork_model_run`（`exec64` 的 `forktest`/`cloneinfo` 分支调用）：
```c
static TEXT64 int fork_model_run(u16*c,int clone){fork_attempts++;if(fork_model.valid){text64(c,"forktest: bounded child already exists\n");return 0;}fork_model.parent_pid=user_process.pid;fork_model.parent_tid=user_thread.tid;fork_model.child_pid=SECOND_PID+1;fork_model.child_tid=SECOND_PID+1;fork_model.parent_address_space=(u64)(unsigned long)user_process.address_space;fork_model.child_address_space=(u64)(unsigned long)&user_address_spaces[1];fork_model.copied_metadata=RESOURCE_COPIED;fork_model.shared_resources=RESOURCE_SHARED;fork_model.is_clone=(u8)clone;fork_model.valid=1;fork_successes++;if(clone)clone_successes++;text64(c,clone?"cloneinfo: metadata-only clone created\n":"forktest: metadata-only child created\n");return 1;}
```

逐行分析：
1. **单调尝试计数**：`fork_attempts++` 每次调用都累加；若 `fork_model.valid` 已置位则拒绝重复创建（`bounded child already exists`）——一次性语义，保证检查点可重复但状态不重复生效。
2. **身份派生**：`child_pid=SECOND_PID+1`（=3）、`child_tid=SECOND_PID+1`，与父进程 `FIXED_PID`/`SECOND_PID` 严格区分；`parent_address_space` 取当前进程的地址空间指针，`child_address_space` 取 `user_address_spaces[1]`——两者必然不同，满足 `fork_model_validate` 的 copy 边界检查。
3. **策略位**：`copied_metadata=RESOURCE_COPIED`、`shared_resources=RESOURCE_SHARED` 是本课「一致性」的断言依据；`is_clone` 区分 fork/clone 两种系统调用语义。
4. **无执行**：函数只写元数据并返回 1，**从不**为子进程构造指令指针、从不跳转——这是教学模型与真实 fork 的最大区别，代码注释也明确写着 `no real child execution`。

`fork_model_validate` 与 `forkinfo`（见 2.2 节引用）分别给出布尔验证与 VGA 报告，`forkinfo` 输出 `fork model: fork/clone`、父子 pid/tid、两份地址空间、两个策略位以及 `attempts/success/fork/clone` 统计。

#### 3.2.3 原子与锁原语（内存屏障的直接体现）

```c
static TEXT64 u8 atomic_load_relaxed_u8(volatile u8*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
static TEXT64 void atomic_store_release_u8(volatile u8*p,u8 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 u8 atomic_fetch_or_relaxed_u8(volatile u8*p,u8 v){return __atomic_fetch_or(p,v,__ATOMIC_RELAXED);}
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
```
- 五个原语覆盖三类内存序：`RELAXED`（无顺序约束，用于统计）、`ACQUIRE`（读侧同步，用于取锁）、`RELEASE`（写侧同步，用于放锁/发布）。
- 它们都被编译为带 `lock` 前缀或 `xchg` 的指令，在单核 + 关中断的场景下退化为纯编译器屏障，但在教学上保留了完整的语义标注——这正是「内存屏障」概念最小可用的可读载体。
- 用法上：`raw_spin_lock_irqsave` 先 `irq_save64()`（`pushfq; popq; cli`）再 acquire-xchg 轮询；`raw_spin_unlock_irqrestore` 先 release-store 再按保存的 IF 位恢复中断。`lockatomictest` 验证这条链：
  `lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`。

#### 3.2.4 exec64 命令分发表（本课增量分支）

`exec64` 是约 600 行的命令分发表。本课变化最小：
- `about` 分支输出 `Lesson 85: fork 内存屏障与一致性\n`；
- 检查点分支由 `l84test` 改为 `l77test` 与新增的 `l85test`：
```c
else if(eq64(word,"l77test")){if(!noargs64(arg))usage64(c,"l77test");else l77test(c);}else if(eq64(word,"l85test")){if(!noargs64(arg))usage64(c,"l85test");else l85test(c);}
```
- `help` 文本保持不变（继承命令集），唯一新命令是 `l85test`。

#### 3.2.5 开机横幅

```c
text64(&c,"Lesson 85: fork 内存屏障与一致性\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```
第一行即课程主题；第二行说明继承的 syscall ABI（四个系统调用 + `-ENOSYS`）与回收元数据约束，均未变化。

### 3.3 构建管线（Makefile / linker）

- `CFLAGS64 := -m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx ...`：64 位 freestanding 编译，关闭 red zone 与所有 SIMD（避免编译器在异常/中断路径生成 SSE 保存），`-fpie` 配合 `kernel64.ld` 的零 VMA raw 镜像。
- `kernel64.o → kernel64.elf → kernel64.bin`：先用 `kernel64.ld` 链接出 ELF，再用 `objcopy -O binary` 剥离为裸二进制；`boot.S` 的 `.text64` 段通过 `.incbin "build/kernel64.bin"` 内嵌。
- 32 位侧：`boot.o + kernel.o` 用 `linker.ld`（`-T linker.ld -nostdlib`）链接成 `kernel.elf`，GRUB 加载它并按 Multiboot2 规范引导。
- `make check`：`grub-file --is-x86-multiboot2` 验证 Multiboot2 头部，再 grep README 与源码的课程标记（本课为 `fork 内存屏障与一致性`、`Lesson 85`、`l85test`）。
- 本课相对上一课无新增编译步骤，仅 `check` 目标课程串变化。

### 3.4 主控制流

```
_start (boot.S)
 └─ kernel_main32 (kernel.c)：Multiboot2 解析、分页表构造、user 镜像加载
     └─ enter_long_mode (boot.S)：CR4.PAE → EFER.LME → CR0.PG → far jump
         └─ kernel_main64_binary (kernel64.c)
             ├─ 初始化：pmm_init/vma_init/vfs_init/…/fork 相关状态（本课用 l85test 验证）
             ├─ 打印横幅 "Lesson 85: fork 内存屏障与一致性\n…"
             └─ 主循环：kbd_dequeue → 命令 → exec64 分发表
                 ├─ l85test → l85test() → VGA 输出检查点串
                 ├─ forktest/cloneinfo → fork_model_run() → VGA 输出创建结果
                 └─ forklifecycle → fork_model_validate() → VGA 输出边界验证
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 清屏后打印横幅，随后提示符 `tinyos> `。
2. **`l85test`** → 命中 `eq64(word,"l85test")` 分支 → `l85test(c)` → 初始化 `lesson_78_state`，断言四属性与 `b==a+1U` → 打印 `l85test: bounded scheduling and copy-on-write checkpoint passed`。
3. **`forktest`** → `fork_model_run(c,0)` → 若首次创建打印 `forktest: metadata-only child created`；重复调用打印 `forktest: bounded child already exists`。
4. **`forklifecycle`** → `fork_model_validate(c)` → 打印 `fork lifecycle: passed (identity, parent, copy/share boundaries, no execution)`（或 `BROKEN`）。
5. **`lockatomictest`** → 走一遍 acquire 取锁 / release 放锁 / relaxed 读取 → 打印 `lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed`。
6. **`about`** → 打印 `Lesson 85: fork 内存屏障与一致性`。
7. **`reclaimtest`/`anoninfo`**（继承回归）→ 打印 `anonymous reclaim and page-cache hit model passed` 等，证明共享页/回收子系统未回归。

---

## 5. 构建、运行与验证

**依赖**：Ubuntu/Debian 需 `build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`（见 `docs/local-validation.md`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 85 checks passed.
```

**运行**：
```bash
make run
```
注意：成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（在 `tinyos> ` 提示符后输入，预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 85: fork 内存屏障与一致性` 横幅 |
| `l85test` | `l85test: bounded scheduling and copy-on-write checkpoint passed` |
| `forktest` | `forktest: metadata-only child created` |
| `forklifecycle` | `fork lifecycle: passed (identity, parent, copy/share boundaries, no execution)` |
| `lockatomictest` | `lockatomictest: irq-safe lock, atomic publication, per-CPU ordering passed` |
| `about` | `Lesson 85: fork 内存屏障与一致性` |
| `reclaimtest` | `reclaimtest: anonymous reclaim and page-cache hit model passed` |
| `ps` / `threadinfo` | 继承的线程表/调度统计输出，均保持可用 |

判定成功：横幅为本课主题、`l85test` 通过、历史命令无 `BROKEN`/fallback 之外的异常输出，QEMU 无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l85test` 输出 `Lesson 78 fallback reported` | `lesson_78_state` 未正确初始化或 `b==a+1U` 断言失败（stale 镜像） | 确认 `kernel64.c` 中 `lesson_78_state=(struct lesson_78_model){78U,79U,80U,81U,1,1,1,1}` 存在；`grep -n "l85test" kernel64.c` |
| 开机横幅仍是 `Lesson 84: 共享页生命周期` | 误加载上一课镜像/未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 85' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记串 | 确认 README 包含 `fork 内存屏障与一致性` 与 `Lesson 85`；kernel64.c 包含 `l85test` |
| `forktest` 第二次执行报 `bounded child already exists` | `fork_model.valid` 一次性语义生效（设计如此，非错误） | 对照 `fork_model_run` 首行的 `if(fork_model.valid)return 0` |
| `lockatomictest` 输出 `BROKEN` | 锁/原子序使用错误或 IF 位恢复异常 | 检查 `raw_spin_lock_irqsave`/`raw_spin_unlock_irqrestore` 配对；用 `lockatomicinfo` 看 `deferred_lock.locked` |
| `forklifecycle` 输出 `BROKEN` | `parent_address_space==child_address_space` 或策略位错误 | 检查 `fork_model_run` 中 `&user_address_spaces[1]` 与两个策略位赋值 |
| QEMU 启动黑屏或 triple fault | 镜像构建/引导失败 | `make check` 必须通过；`grub-file --is-x86-multiboot2 build/kernel.elf` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `fork_model` 只写元数据、无子进程执行 | `kernel/fork.c`：`copy_process()` 创建完整 `task_struct` 并让子进程真正运行 | 模型只保留身份/地址空间/策略位，删除 `copy_thread`、`dup_mmap` 等全部真实内存复制与指令执行 |
| `copied_metadata=RESOURCE_COPIED` | `kernel/fork.c`：`copy_mm()` 复制 `mm_struct`（深拷贝页表描述，写时复制） | 模型不复制页表，仅用一个标志位表示「copy」边界 |
| `shared_resources=RESOURCE_SHARED` | `kernel/fork.c`：`copy_files()` 递增 `file` 引用；`CLONE_FILES` 直接共享 fd 表 | 模型用一位标志概括所有共享资源 |
| `atomic_exchange_acquire_u32`/`atomic_store_release_u32`（spinlock） | `include/linux/spinlock.h` + `arch/x86/include/asm/spinlock.h`（xchg 取锁）、`arch/x86/include/asm/barrier.h` | 模型在单核 + 关中断下运行，屏障语义靠标注保留，不做真实多核竞争 |
| `__ATOMIC_RELAXED/ACQUIRE/RELEASE` 五种原语 | C11 `stdatomic.h`；Linux `Documentation/memory-barriers.txt` 的 ACQUIRE/RELEASE 章节 | 模型把序关系显式写死，不涉及多核缓存一致性协议 |
| `forkinfo` 报告 `copied/shared` 位 | `kernel/fork.c` 注释与 `mm_struct` 的 `mm_users`/`mm_count` 语义 | 模型用整数位代替 Linux 的引用计数与 RCU 保护 |
| `l85test` 确定性断言 | 无直接对应（Linux 用 kselftests，如 `tools/testing/selftests/`） | 模型内嵌检查点，把验证固化进内核 |

**权威来源**：Intel SDM Vol.3A §8.2（内存序）、C11 §7.17.4 与 GCC 内建 `__atomic_*` 文档、Multiboot2 规范（引导协议）、GNU GRUB 手册。Linux 内核源码仅作工程对照。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `fork_model_run` 用 `copied_metadata=RESOURCE_COPIED` + `shared_resources=RESOURCE_SHARED` 两个位就能表达 fork 的资源策略？与 `CLONE_VM`/`CLONE_FILES` 的差别对应吗？
2. **源码定位**：在 `kernel64.c` 中找到 `atomic_exchange_acquire_u32` 的**全部**调用点，说明每个调用点为何需要 acquire/release 语义。
3. **动手实验**：把 `l85test` 的初始化改为 `{78U,79U,80U,81U,1,1,1,0}`（`accounted=0`），重新构建，观察输出是否变为 fallback 串；解释断言失败路径。
4. **动手实验**：在 `fork_model_run` 中把 `child_address_space` 改成 `user_process.address_space`，运行 `forklifecycle`，观察 `BROKEN`；说明这违反了哪个一致性边界。
5. **Linux 对照**：阅读 `kernel/fork.c` 中 `copy_mm()` 与 `dup_mmap()`，对比教学模型删除掉的页表复制，指出 Linux 靠什么机制实现「复制但几乎不花内存」（提示：COW）。

---

## 9. 本课小结与下一课预告

1. 本课是进程/调度/COW 阶段的一个检查点：源码增量很小（一个 `lesson_78_model` 检查点 + 文本更新），但主题把「fork 内存屏障与一致性」落到可验证的断言上。
2. fork 的资源策略被压缩为 copy/share 两个位，`fork_model_validate` 用布尔式检查地址空间相异与策略位正确。
3. 内存屏障概念由五种 `__atomic_*` 原语与 `raw_spin_lock_irqsave`/`raw_spin_unlock_irqrestore` 承载，acquire/release 构成 happens-before，正是元数据发布的一致性基础。
4. 教学模型与 Linux `kernel/fork.c` 的本质差异是「无真实子进程执行、无页表复制」，一切以元数据与检查点验证为边界。
5. 共享页/回收、GUI、进程组等全部继承命令在 `l85test` 之外继续回归可用。
6. 下一课（Lesson 86）将主题切换到**调度公平性验证**，用同样的检查点手法验证时间片、抢占与公平轮转，并复用本课讲过的调度器类与 `irq0_schedule` 路径。

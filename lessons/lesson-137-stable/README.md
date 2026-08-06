# Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint — 精讲文档

> **课号**：Lesson 137（可执行课，checkpoint 快照）
> **主题**：并发、SMP、RCU、诊断综合 checkpoint——把前四课（异常/故障分类、内存
> 压力、调度并发、SMP/RCU 原语）的机制与诊断命令汇总成一张「综合回归」视图，并
> 追加本课自己的 checkpoint 模型 `lesson_130_model`。
> **课程主线位置**：诊断/网络主题的「检查点课」序列（Lesson 133–138），位于
> Lesson 136（SMP/RCU 回归验证）之后、Lesson 138（网络 buffer pool，阶段 6 网络
> 主题起点）之前。
> **前置课程**：[`lesson-136-stable/README.md`](../lesson-136-stable/README.md)
> **后续课程**：[`lesson-138-stable/README.md`](../lesson-138-stable/README.md)
> **一句话目标**：学完本课你能把 Lesson 133–136 四张机制图拼成一张「并发与诊断
> 全景图」，说明每个机制家族的回归断言（test 命令）与诊断命令（info 命令）如何
> 配合，以及本课 checkpoint 模型 `l137test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：以「综合 checkpoint」视角回看四族机制——①异常/故障分类
（#BP/#UD/#PF 路径 + `pf_classify`）；②内存压力诊断（PMM/reclaim/page cache）；
③调度与并发（`irq0_schedule`/轮转/信号量/生产者-消费者）；④SMP/RCU 相关原语
（per-CPU/原子/自旋锁/softirq 预算）——掌握各族的 test/info 命令对，并理解新增
`lesson_130_model`/`l137test` 与 `l129test`/`l137test` 命令接线。

- **在课程主线中的位置**：本课是 Lesson 133–137 检查点序列的**收尾综合课**。
  `kernel64.c` 相对 Lesson 136 仅 3 处增量：`l136test`→`l129test` 改名、新增
  `struct lesson_130_model` 与 `l137test`、exec64/about/banner 文案换成
  「并发、SMP、RCU、诊断综合 checkpoint」。全部机制继承自前四课，本课不新增机制，
  只做「综合回归」的讲解视角。
- **前置知识清单**：
  1. Lesson 133：异常路径（stub + reporter）与故障分类（`pf_classify`/`pfmodel`）；
  2. Lesson 134：`fault_insert`/`page_cache_get`/`reclaim_one` 与 `meminfo`/
     `anoninfo`/`reclaimtest`；
  3. Lesson 135：`irq0_schedule`/`rr_pick_next`/信号量/事件与 `threadinfo`/
     `pcinfo`/`ps`/`schedinfo`；
  4. Lesson 136：per-CPU/原子/自旋锁/softirq 与 `lockatomictest`/`softirqtest`/
     `lockatomicinfo`/`softirqinfo`。
- **本课交付**：一张「机制家族 × test/info 命令」对照表；`l129test`、`l137test`
  两个 checkpoint 测试；对「综合 checkpoint」概念的准确理解（消息文本是覆盖标签，
  真实验证靠命令矩阵）。

---

## 2. 核心概念精讲

### 2.1 概念一：综合 checkpoint（comprehensive checkpoint）

**直觉**：单课 checkpoint（`l134test` 等）验证「上一课模型没被改坏」；综合
checkpoint 则要求**所有**累积机制自洽。Lesson 137 正是这个角色：它不加机制，而是
站在四族机制之上，用一句话断言 + 命令矩阵宣告「整个并发与诊断子系统仍然成立」。

**机制家族 × 命令对照**：

| 家族 | test 命令（断言） | info 命令（状态） | 来源 |
|------|-------------------|-------------------|------|
| 异常/故障分类 | `pfmodel`、`signaltest`、`bptest` | `vmainfo`、`idtinfo` | L133 |
| 内存压力 | `reclaimtest` | `meminfo`、`anoninfo` | L134 |
| 调度/并发 | `preempttest`、`pctest`+`pcgo` | `threadinfo`、`pcinfo`、`ps`、`schedinfo` | L135 |
| SMP/RCU 原语 | `lockatomictest`、`softirqtest` | `lockatomicinfo`、`softirqinfo` | L136 |
| checkpoint | `l129test`、`l137test` | `about` | 各课 |

**为什么**：test 命令给出「真/假」，info 命令给出「数值证据」——两者配对才能
定位回归。综合 checkpoint 的意义不是写新代码，而是给学习者一张「验收清单」。

### 2.2 概念二：四族机制的一分钟回顾

1. **异常路径**：`exception_bp`（可恢复 trap gate）/`exception_ud`/`exception_pf`
   （终止型 interrupt gate + IST1），现场统一进 `exception_frame`，报告后打印
   vector/error/rip/CR2。
2. **内存压力**：`pmm_alloc` 是唯一物理来源；`fault_insert` 登记匿名页；
   `page_cache_get` 缓存命中；`reclaim_one` 按 `live && reclaimable && refs==1`
   回收。
3. **调度与并发**：IRQ0 每 tick 调 `irq0_schedule`，时间片耗尽后 `rr_pick_next`
   顺时针选线程；wait queue/event/semaphore 提供阻塞唤醒；生产者-消费者用双
   信号量保证顺序。
4. **SMP/RCU 原语**：`cpu_local[NR_CPUS=1]` per-CPU 抽象；`__atomic_*` 封装
   relaxed/acquire/release 内存序；`raw_spin_lock_irqsave` = 关中断 + XCHG 自旋；
   `softirq_run_budget` 用 `SOFTIRQ_BUDGET=2` 预算消费 tasklet/workqueue。

### 2.3 概念三：本课 checkpoint 模型

`struct lesson_130_model` 的 `a` 从 `130U` 起头 = 课号 137 − 7，回锚到 Lesson 130
检查点；`l137test` 五连断言（valid/active/ready/accounted/b==a+1）输出恒真串。
**如实说明**：成功串里的 "concurrency, SMP, RCU" 是四族机制的覆盖标签，断言本身
只校验元数据；「综合验证」的真实载体是 §2.1 命令矩阵里各族的 test 命令。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 136） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（959 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l136test`→`l129test`；新增 `struct lesson_130_model`、`l137test`；exec64 增加 `l129test`/`l137test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `并发、SMP、RCU、诊断综合 checkpoint`、`l137test`、`Lesson 137` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：综合 checkpoint 视角精讲

#### 3.2.1 本课新增 checkpoint：lesson_130_model 与 l137test

```c
struct lesson_130_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_130_model lesson_130_state;
static TEXT64 void l137test(u16*c){lesson_130_state=(struct lesson_130_model){130U,131U,132U,133U,1,1,1,1};int ok=lesson_130_state.valid&&lesson_130_state.active&&lesson_130_state.ready&&lesson_130_state.accounted&&lesson_130_state.b==lesson_130_state.a+1U;text64(c,"l137test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 130 fallback reported");putc64(c,'\n');}
```

- 字段语义：4 个 u32 连续编号（a=130、b=131、c=132、d=133）+ 4 个状态位
  （valid/active/ready/accounted）。`a` 从 `130U` 起头 = 课号 137 − 7，回锚到
  Lesson 130 检查点；这也是整个检查点序列的「累计回锚」：L133→L126、L134→L127、
  L135→L128、L136→L129、L137→L130、L138→L131，模型编号随课号等距前进。
- 断言逻辑：`ok` 五连真（四个状态位 + `b==a+1`）输出成功串 `bounded concurrency,
  SMP, RCU, and diagnostics checkpoint passed`，否则失败串 `Lesson 130 fallback
  reported`。
- 为什么：作为本序列最后一枚元数据探针，任何相邻课改坏模型字段都会让输出翻转为
  fallback。它不执行任何并发/RCU 代码——「综合」二字靠命令矩阵兑现（§2.1）。

#### 3.2.2 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l129test")){if(!noargs64(arg))usage64(c,"l129test");else l129test(c);}else if(eq64(word,"l137test")){if(!noargs64(arg))usage64(c,"l137test");else l137test(c);}
```

- 本课把上一课的 `l136test` 分支改名 `l129test`（复用 `lesson_129_model`），新增
  `l137test` 分支。**勘误**：旧 README 写的 `Commands: l130test` 与源码不符，源码中
  可用的 checkpoint 命令是 `l129test` 与 `l137test`。
- about：`else text64(c,"Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint\n");`；
  开机横幅：`text64(&c,"Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint\nGETTICKS,
  GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。

#### 3.2.3 综合命令矩阵源码定位（四族机制各取代表）

- **异常/故障**：`pfmodel` 分支 → `pf_classify` 三分类 + `fault_insert` 插页，
  输出 `pfmodel: not-present/protection/unmapped classified; bounded page
  inserted`；`vmainfo` 打印 VMA 表与 fault 计数器。
- **内存压力**：`reclaimtest` 分支 → `fault_insert` + `page_cache_get`×2 +
  `reclaim_one`，输出 `reclaimtest: anonymous reclaim and page-cache hit model
  passed`；`anoninfo`/`meminfo` 打印账目。
- **调度/并发**：`pctest`（mode 3 启动两个 worker）→ `pcgo`（`event_set` 广播）→
  `pcinfo`（`P prod/cons` 与 `P errors/ok`）；`threadinfo`/`ps`/`schedinfo` 观察
  调度状态。
- **SMP/RCU 原语**：`lockatomictest` 输出 `lockatomictest: irq-safe lock, atomic
  publication, per-CPU ordering passed`；`softirqtest` 输出 `softirqtest: tasklet
  coalescing, FIFO work, and budget carry-over passed`；`softirqinfo`/
  `lockatomicinfo` 打印计数。

（四族函数体的逐行讲解见各来源课：L133 §3.2、L134 §3.2、L135 §3.2、L136 §3.2；
本课不再重复贴码，只做矩阵化收束。）

#### 3.2.4 综合回归的「顺序」建议（shell 验证路径）

```text
about → l129test → l137test         # 元数据层
pfmodel → reclaimtest → meminfo      # 内存层
preempttest → threadinfo → ps        # 调度层
pctest → pcgo → pcinfo               # 并发层
lockatomictest → softirqtest         # 原语层
```

- 每层先跑 test（真/假断言）再跑 info（数值证据），异常时用 info 计数定位。
- 该路径与 `kernel_main64_binary` 的初始化顺序一致：PMM/VMA 先行（内存层），
  IRQ0/PIT 驱动调度（调度层），原子/锁贯穿其中。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`并发、SMP、RCU、诊断综合 checkpoint`、`l137test`、`Lesson 137`）——README 里
  这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals[0].id=0 → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class=&fair_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint\n..." → shell 循环
  shell 命令 → 内存层：pfmodel/reclaimtest/meminfo/anoninfo
             → 调度层：preempttest/threadinfo/ps/schedinfo
             → 并发层：pctest→pcgo→pcinfo
             → 原语层：lockatomictest/softirqtest/softirqinfo/lockatomicinfo
             → 元数据层：l129test/l137test
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，按综合验证顺序：

1. **`l137test`** → `l137test(c)` 对 `lesson_130_state` 赋值并五连断言 → 输出
   `l137test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。
2. **`reclaimtest`** → `fault_insert`（PMM 出帧）→ `page_cache_get` 建项/命中 →
   `reclaim_one` 回收 → 输出 `reclaimtest: anonymous reclaim and page-cache hit
   model passed`。
3. **`pctest`+`pcgo`** → 双 worker 阻塞于 start event → `event_set` 广播 → IRQ0
   抢占驱动 4 步生产/消费 → `pcinfo` 输出 `P errors/ok: ... yes`。
4. **`softirqtest`** → 两轮 `softirq_run_budget` 消费 2 tasklet + 4 work → 输出
   `softirqtest: tasklet coalescing, FIFO work, and budget carry-over passed`。

数据流要点：所有 test 命令共享同一组全局状态（PMM 账目、线程表、`pc_*` 计数器、
`softirq_model`），因此**顺序敏感**——综合回归必须按 §3.2.4 的顺序跑，避免一个
test 污染下一个的基线。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-137-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `并发、SMP、RCU、诊断综合 checkpoint`、`l137test`、`Lesson 137` 与 kernel64.c 中的
  `l137test`，全部命中输出 `Multiboot2 and Lesson 137 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint`
  2. `l137test` → `l137test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  3. `l129test` → `l129test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  4. `pfmodel` → `pfmodel: not-present/protection/unmapped classified; bounded page
     inserted`
  5. `reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit model
     passed`
  6. `lockatomictest` → `lockatomictest: irq-safe lock, atomic publication, per-CPU
     ordering passed`
  7. `softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and budget
     carry-over passed`
  8. `pctest` → `pctest: producer and consumer blocked on start event; run pcgo`；
     `pcgo` → `pcgo: event set; broadcast wake-all issued`；随后 `pcinfo` 末行
     `P errors/ok: 0000000000000000 yes`
- **如何判断成功**：上述命令逐一打印预期串即成功；所有 test 命令不得出现
  `BROKEN` 或 fallback。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l137test` 输出 `Lesson 130 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l137test` 赋值 `{130U,131U,132U,133U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 某个 test 输出 `BROKEN`（其余正常） | 该族机制被改坏，或前一个 test 污染了共享基线 | 按 §3.2.4 顺序重跑；用对应 info 命令看计数（如 `reclaimtest` 失败看 `anoninfo`） |
| `reclaimtest` 后 `anon_pages` 非 0 | `reclaim_one` 判据或 `pmm_free_page` 返回非 `freed` | `anoninfo` 看 scans/skips；确认帧未被映射/占用 |
| `pcinfo` 的 `P errors/ok: ... no` | 生产者-消费者丢序或进度未跑完 | 确认先 `pctest` 再 `pcgo`；`ps` 看 worker progress |
| `lockatomictest` 输出 `BROKEN` | `softirq_pending` 非 1 或锁未释放 | `lockatomicinfo` 看 `cpu id/pending/work` 与 `lock` |
| 某命令提示 `unknown command` | 命令名拼错或该命令不存在于本课 exec64 | 对照 `help` 输出与 §2.1 命令表；l-test 命令以 `l129test`/`l137test` 为准 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 137: 并发、SMP、RCU、诊断综合 checkpoint`；`make check` grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **综合回归的意义**：TinyOS 用命令矩阵做回归，Linux 用 `LTP`/`kselftest`/
   `tools/testing/selftests` 跑子系统测试。本课 `l137test` 类似一个「冒烟测试」
   （smoke test），而各 test 命令类似 `selftests` 单测。
2. **异常路径**：对照 `arch/x86/kernel/traps.c`（`exc_int3`/`do_trap`）与
   `arch/x86/mm/fault.c`（`exc_page_fault`）；TinyOS 是终止型 handler + 元数据
   分类。
3. **内存压力**：对照 `mm/vmscan.c`（`shrink_page_list`）、`mm/page_alloc.c`、
   `fs/proc/meminfo.c`；TinyOS `reclaim_one` 是单页回收的 3 行版。
4. **调度与并发**：对照 `kernel/sched/core.c`（`__schedule`）、
   `kernel/sched/sched.h`（`sched_class`）、`kernel/sched/wait.c`；TinyOS 用
   `iretq` 弹栈切换代替 `switch_to`。
5. **SMP/RCU 原语**：对照 `kernel/locking/spinlock.c`、`kernel/softirq.c`、
   `arch/x86/include/asm/atomic.h`；**RCU 本身（`kernel/rcu/tree.c`）本课没有实现**
   ——"RCU" 仅作为 checkpoint 消息文本的覆盖标签，这是必须如实说明的边界。
6. **诊断接口**：TinyOS info 命令族对应 Linux `/proc` 接口（`meminfo`/`schedstat`/
   `/proc/softirqs`）；test 命令族对应内核自测（`selftests`）。

**权威来源**：Intel SDM Vol.3A、C11/C17 §7.17.3、Linux `kernel/sched`/`mm`/
`kernel/locking`/`kernel/softirq.c`/`arch/x86`。
**教学模型简化了什么**：单核、无真实 RCU、无锁竞争窗口、无真实磁盘 I/O；
综合回归靠命令矩阵而非自动测试框架。

---

## 8. 思考题与练习

1. **概念理解**：综合 checkpoint 与单课 checkpoint（如 `l134test`）的本质区别是什么？
   「综合」二字由哪一部分代码兑现，哪一部分只是主题标签？
2. **源码定位**：在 `kernel64.c` 中列出 exec64 里所有以 `l1` 开头的命令分支，指出
   本课实际可用的两个 checkpoint 命令（`l129test`/`l137test`），并说明旧 README 的
   `l130test` 为何是错的。
3. **动手实验**：按 §3.2.4 的顺序执行完整命令链，然后在 `reclaimtest` 前先执行
   一次 `palloc`，观察 `reclaimtest` 的断言是否仍通过——解释共享全局状态带来的
   顺序敏感性。
4. **动手实验**：修改 `l137test` 的赋值使 `b==a`，重跑，观察输出翻转为
   `Lesson 130 fallback reported`；再改回并确认恢复。
5. **Linux 对照**：把 §2.1 的命令矩阵映射到 Linux 的对等物（`/proc/meminfo`、
   `/proc/schedstat`、`/proc/softirqs`、`selftests` 单测），指出每行 TinyOS 简化了
   什么。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是 Lesson 133–137 检查点序列的收尾综合课，`kernel64.c` 相对 Lesson 136 只有
   3 处小增量，机制全部继承，主题由 banner/about 文案标识。
2. 综合 checkpoint 不写新机制，它给出「机制家族 × test/info 命令」验收清单。
3. 四族机制（异常/故障、内存压力、调度并发、SMP/RCU 原语）各有 test 断言与 info
   证据命令，配对使用才能定位回归。
4. 本课新增 `lesson_130_model`/`l137test`，`a` 从 `130U` 起头回锚到 Lesson 130；
   五连断言恒真，作为序列最后一枚元数据探针。
5. 综合回归是顺序敏感的：test 共享全局状态，需按内存→调度→并发→原语→元数据的
   顺序执行。
6. 「RCU」在本序列中只是 checkpoint 消息文本的覆盖标签，源码没有 RCU 原语——
   如实声明，避免误读。
7. 旧 README 的 `Commands: l130test` 已勘误为源码实际的 `l129test` 与 `l137test`。

**下一课**：[`lesson-138-stable/README.md`](../lesson-138-stable/README.md) 主题为
「网络 buffer pool」，是阶段 6（网络主题）的起点课。banner/about 宣告网络主题，
checkpoint 消息换为 `bounded networking, namespaces, cgroups, and security
checkpoint passed`（命令 `l138test`），实际的网络 buffer pool 机制（skb 式缓存）
将在后续课展开。两课的衔接点：本课继承的 `pmm_alloc`/`page_cache` 元数据模型
正是未来 buffer pool「从物理帧里要缓冲」的基础。
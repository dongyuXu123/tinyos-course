# Lesson 162: 网络、namespace、cgroup、安全综合 checkpoint — 精讲文档

> **课号**：Lesson 162（可执行课，checkpoint 快照，**全课程最终一课**）
> **主题**：网络、namespace、cgroup、安全综合 checkpoint——把本主题弧线（Lesson
> 157–162：资源限制与回收、capability 权限检查、syscall 安全边界、审计事件缓冲区、
> 安全策略决策）与全课程主线收束成一张「网络/namespace/cgroup/安全」综合图，追加
> 全课程最后一个确定性 checkpoint 模型 `lesson_155_model`。
> **课程主线位置**：资源/安全主题的「检查点课」序列（Lesson 157–162）的终章，位于
> Lesson 161（安全策略决策）之后；本课是**整个 TinyOS 课程体系的最终一课**，没有
> 后续课程。
> **前置课程**：[`lesson-161-stable/README.md`](../lesson-161-stable/README.md)
> **后续课程**：无（全课程完结；向前回溯见本课第 9 节主线回顾）
> **一句话目标**：学完本课你能把「网络、namespace、cgroup、安全」四大主题在 TinyOS
> 教学模型中的机制全部对上号，说清 `l162test` 校验了什么，并站在全课程高度回顾从
> 裸机启动到综合 checkpoint 的整条主线。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂本课（以及整个 157–162 主题弧线）的「主题宣告 + checkpoint
增量」模式，会用 `l154test`、`l162test` 及网络/namespace/cgroup/安全相关的既有
命令做综合验收，并能回溯全课程主线（引导 → 内核地基 → 调度进程 → 内存文件 →
GUI → 资源安全）。

- **在课程主线中的位置**：与 Lesson 157–161 同属「资源/安全主题的检查点课」，
  相邻课 `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 161 仅 4 处改动：
  `l161test`→`l154test` 改名、新增 `struct lesson_155_model` 与 `l162test`、
  exec64/about/banner 文案）。与前面五课一样，**主题机制代码全部继承自早期课程**，
  本课不改任何机制，只追加最终 checkpoint。
- **全课程主线回顾**（最终课视角）：
  1. **引导阶段**（Lesson 00–0.10）：Multiboot2 头、GRUB、32 位 `kernel_main32`
     解析 MBI/建页表 → `boot.S` 进 long mode → `kernel_main64_binary`；
  2. **内核地基**（Lesson 01–33）：PMM 位图、IDT/GDT/TSS、异常路径、IRQ/PIT/键盘、
     线程与抢占式调度；
  3. **进程与 syscall**（Lesson 34–49）：CPL3 用户态、`int 0x80` syscall、
     信号、fork/exec/wait、进程组/会话与孤儿收养；
  4. **内存与文件**（Lesson 50–80）：VMA、缺页分类、页回收、ramfs/VFS/fd、
     管道与 poll；
  5. **GUI 与并发**（Lesson 81–129）：framebuffer/canvas/窗口、输入事件环、
     softirq/workqueue、锁与原子、模块、时钟定时器；
  6. **资源/安全主题弧线**（Lesson 157–162）：资源限制与回收 → capability 权限检查
     → syscall 安全边界 → 审计事件缓冲区 → 安全策略决策 → 本课综合 checkpoint。
- **前置知识清单**：
  1. 主题弧线五课的既有机制：`resource_ledger`/`reclaim_one`（157）、VMA 权限位/
     `uaccess_validate`（158）、`int 0x80`/`syscall_dispatch`/`user_context_valid`
     （159）、有界环形事件队列（160）、`pf_classify` 等裁决函数（161）；
  2. 进程组/会话/孤儿收养模型（`process_group`、`session_jobs`、`adoption_model`、
     `orphan_group_model`）——namespace 的教学近似；
  3. 管道与 poll 模型（`pipe_model`、`pipe_poll`、`polltest`）——网络/通信的教学
     近似；
  4. checkpoint 课固定模式（`struct lesson_K_model` + `lXXtest`，Lesson 133–161）。
- **本课交付**：网络/namespace/cgroup/安全四大主题的机制汇总对照；命令 `l154test`
  （改名）与 `l162test`（新增）；`about`/banner 文案；全课程主线回顾。

---

## 2. 核心概念精讲

### 2.1 概念一：主题弧线的四条主线

**直觉**：Lesson 157–162 的横幅共用同一句英文——`bounded networking, namespaces,
cgroups, and security metadata`——它把整个主题弧线标成四个维度。TinyOS 分别用
哪些机制代表它们？

| 主题 | TinyOS 教学机制（继承） | 代表命令 | Linux 对照 |
|------|------------------------|---------|-----------|
| networking（通信） | 管道 FIFO `pipe_model`、`pipe_poll`、生产者消费者 `pc_buffer` | `pipetest`/`polltest`/`pctest` | socket/网络栈 |
| namespaces（命名空间） | 进程组/会话/孤儿收养 `process_group`/`session_jobs`/`adoption_model` | `pgtest`/`sessiontest`/`reparenttest` | `CLONE_NEW*` 命名空间 |
| cgroups（资源分组） | 资源台账 `resource_ledger`、匿名页回收 `reclaim_one` | `teardowntest`/`reclaimtest` | cgroup v2 控制器 |
| security（安全） | 权限裁决 `uaccess_validate`/`pf_classify`、syscall 边界、审计缓冲、策略决策 | `ptrtest`/`pfmodel`/`l15Xtest` | LSM/capability/audit |

**为什么需要**：这四个维度是当代内核「隔离 + 限额 + 审计」的三大支柱——namespace
提供隔离、cgroup 提供限额、security/audit 提供审计。TinyOS 用元数据模型把每一根
柱子压成一个可验证的小结构。

### 2.2 概念二：通信（networking）的本地模型——管道与 poll

**直觉**：内核里「进程间通信」最朴素的形式就是管道：一段有界缓冲区 + 读写双方 +
阻塞/唤醒。TinyOS 的 `pipe_model` 就是 4 字节环形 FIFO，`pipe_poll` 用
`POLL_IN/POLL_OUT` 位表达可读可写状态。

```c
struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };
```

- 核心状态：`head/tail/used` 三变量 + 取模环形缓冲，`used==PIPE_CAP` 满、
  `used==0` 空；读写方通过 wait queue 阻塞/唤醒。
- `pipe_poll(mask)` 返回 `POLL_IN`（有数据可读）与 `POLL_OUT`（有空间可写）——
  与 Linux `fs/select.c` 的 poll 语义对应，`polltest` 验证
  `POLLIN/POLLOUT readiness transitions passed`。

### 2.3 概念三：namespace 的本地模型——进程组/会话/孤儿收养

**直觉**：namespace 的实质是「进程看到的全局视图被隔离」。TinyOS 的简化版本是
**进程组与会话**：`process_group` 记录 pgid/leader/session、前台/受控终端；
`adoption_model` 处理父进程退出后孤儿归属 init；`session_jobs` 管理会话里两个
作业的生命周期。

```c
struct process_group_model { u32 pgid,leader,session,member_count; u8 foreground,controlled; };
struct adoption_model { u64 init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; u8 orphaned,adopted,wait_owner; };
```

- `pgtest` 断言 `pgid==leader && session==pgid && foreground && controlled`——会话
  领导权自洽；
- `adoption_reparent` 把孤儿 `current_parent` 指向 init——对应 Linux
  `forget_original_parent`/`find_new_reaper` 的收养语义。

### 2.4 概念四：cgroup 与安全的本地模型

- **cgroup**：`resource_ledger` 的「zombie 保留 → 有序释放 → 防双重释放」
  （`teardowntest`）+ `reclaim_one` 的「live/reclaimable/refs==1 三重回收条件」
  （`reclaimtest`）对应 cgroup 内存控制器「分组记账 + 按压力回收」的极简投影；
- **security**：`uaccess_validate` 的 canonical/range/vma/permission 四查、
  `pf_classify` 的三分类、syscall 白名单与 `-ENOSYS`、有界审计缓冲、决策函数群
  （Lesson 157–161 各课主题）。

### 2.5 概念五：全课程最终 checkpoint 模型

**直觉**：与 Lesson 157–161 完全相同的模式，只是编号到顶——本课新增：

```c
struct lesson_155_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

**工作机制**：`l162test` 把 `lesson_155_state` 整体赋为 `{155U,156U,157U,158U,1,1,1,1}`
（a=155, b=156, c=157, d=158，四个状态位全 1），断言 `valid && active && ready &&
accounted && b==a+1`。字面量赋值使断言恒真，输出恒为 `bounded networking,
namespaces, cgroups, and security checkpoint passed`。模型名 `lesson_155_model`
的 155 = 162−7，是本主题弧线（150→155）「回锚」链的最后一级——五课模型名
150/151/152/153/154/155 正好衔接起「网络/namespace/cgroup/安全」六连检查点。
**教学模型：不执行任何网络/namespace/cgroup/安全代码，只校验元数据自洽。**

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 161） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（1045 行）：PMM/异常/中断/调度/进程/VFS/GUI/资源安全机制/checkpoint 模型 | `l161test`→`l154test` 改名；新增 `struct lesson_155_model`、`l162test`；exec64 增加 `l154test`/`l162test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `网络、namespace、cgroup、安全综合 checkpoint`、`l162test`、`Lesson 162` | 仅 grep 文案（Lesson 161→162） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：四大主题机制汇总精讲（继承代码）

> 说明：本课**没有**新增任何主题机制，以下函数全部继承自早期课程；本课把它们按
> 「网络/namespace/cgroup/安全」四类重新汇总精讲，作为综合 checkpoint 的机制回顾。

#### 3.2.1 网络/通信模型：管道与 poll（继承自 Lesson 33/38）

```c
static TEXT64 int pipe_try_write(u8 value){u8 id;if(pipe_model.used>=PIPE_CAP){pipe_model.blocked_writers++;return 0;}pipe_model.data[pipe_model.head]=value;pipe_model.head=(u8)((pipe_model.head+1)%PIPE_CAP);pipe_model.used++;pipe_model.writes++;if(waitq_wake_one(&pipe_read_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_readers++;return 1;}
static TEXT64 int pipe_try_read(u8*out){u8 id;if(!pipe_model.used){pipe_model.blocked_readers++;return 0;}*out=pipe_model.data[pipe_model.tail];pipe_model.tail=(u8)((pipe_model.tail+1)%PIPE_CAP);pipe_model.used--;pipe_model.reads++;if(waitq_wake_one(&pipe_write_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_writers++;return 1;}
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

逐行注释：
- `pipe_try_write`：满则 `blocked_writers++` 返回 0（阻塞策略）；否则环形入队
  （`head=(head+1)%PIPE_CAP`）、`used++`、`writes++`，并尝试唤醒一个阻塞读方
  （`waitq_wake_one(&pipe_read_wait,...)`），成功则 `wake_readers++`。
- `pipe_try_read`：空则 `blocked_readers++` 返回 0；否则从 `tail` 取出、
  `used--`、`reads++`，唤醒一个阻塞写方。
- `pipe_poll`：按掩码返回就绪位——`POLL_IN`（有数据）与 `POLL_OUT`（有空间），
  每次轮询 `poll_registrations++` 记账。这是「网络/通信」主题的核心：有界队列 +
  阻塞/唤醒 + 就绪位轮询三位一体。
- `polltest` 把「空 → 不可读可写 → 写 1 字节 → 可读 → 写满 → 不可写 → 读空」
  整条状态迁移链验证一遍，输出 `polltest: POLLIN/POLLOUT readiness transitions
  passed`。

#### 3.2.2 namespace 模型：进程组/会话/孤儿收养（继承自 Lesson 62–68）

```c
static TEXT64 void adoption_start(void){adoption_model=(struct adoption_model){1,FIXED_PID,SECOND_PID,FIXED_PID,0,0,0,0,0};}
static TEXT64 int adoption_exit_parent(void){if(adoption_model.original_parent!=FIXED_PID)return 0;adoption_model.orphaned=1;adoption_model.wait_owner=adoption_model.init_pid;return adoption_reparent();}
static TEXT64 int adoption_reparent(void){if(!adoption_model.orphaned||adoption_model.adopted)return 0;adoption_model.current_parent=adoption_model.init_pid;adoption_model.adopted=1;adoption_model.adoptions++;return 1;}
```

- `adoption_start`：init pid=1 收养者、original_parent=1、child_pid=2——一个最小
  孤儿场景。
- `adoption_exit_parent`：父进程退出 → `orphaned=1`、`wait_owner=init_pid`，随后
  `adoption_reparent` 把 `current_parent` 改成 init、`adopted=1`、`adoptions++`。
- 决策语义：`adoption_reparent` 前置条件是「确已 orphaned 且尚未 adopted」——与
  `resource_teardown` 的「zombie && !teardown_done」同一「状态即策略」模式。
- `reparenttest` 断言整个链（初始归属 → 孤儿化 → init 收养 → 所有权自洽），输出
  `reparenttest: orphan adoption, init wait ownership, and bounded reparent passed`。
- 进程组/会话侧：`pgtest`/`sessiontest`/`fgtest`（`process_group` 的 pgid/leader/
  session/foreground 四字段）、`jobtest`（两个 session job 的 exit/reap）、
  `orphan66test`/`job67test`/`stop68test`（孤儿组、作业控制信号、终端停止/继续）
  共同构成「命名空间 + 作业控制」的教学全景。

#### 3.2.3 cgroup 模型：资源台账与页回收（继承自 Lesson 57/134/157）

```c
static TEXT64 int resource_teardown(void){if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;resource_ledger.address_space=0;resource_ledger.fd_refs=0;resource_ledger.pipe_refs=0;resource_ledger.signal_refs=0;resource_ledger.timer_refs=0;resource_ledger.deferred_refs=0;resource_ledger.releases=6;resource_ledger.teardown_done=1;return 1;}
```

- 资源台账：六类资源引用 + `releases` 计数 + `zombie/teardown_done` 状态位；
  `teardowntest` 验证「zombie 保留 → 一次拆解 → 二次拒绝」协议，输出
  `teardowntest: zombie retention, ordered resource release, and double-reap guard
  passed`。
- 页回收：`reclaim_one` 的 `live && reclaimable && refs==1` 三重检查 + `pmm_free_page`
  归还；`reclaimtest` 输出 `reclaimtest: anonymous reclaim and page-cache hit model
  passed`，第二行 `page cache is metadata-only; no disk I/O or swap executed` 声明
  纯元数据。
- 对应 Linux cgroup 内存控制器（`mm/memcontrol.c` 的 `mem_cgroup_charge`/`try_charge`
  记账 + shrinker 回收），TinyOS 把「分组记账 + 回收」压缩成两张元数据表。

#### 3.2.4 安全模型：权限裁决与 syscall 边界（继承自 Lesson 133/140/158/159）

```c
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```

- 决策函数群：`pf_classify`（三分类）、`uaccess_validate`（四层放行）、
  `module_lookup`（导出符号可见性）、`task_transition`（状态机迁移）、
  `syscall_dispatch`（白名单 + `-ENOSYS`）——Lesson 158–159/161 的主题载体，
  全部「先裁决后执行」。
- 有界审计缓冲：`kbd_queue`/`input_queue`/`pipe_model`/`workqueue` 四个有界环形
  队列各带「满则丢/阻塞 + 计数」（`kbd_overflow_count`/`input_dropped`/
  `softirq_model.drops`）——Lesson 160 的主题载体。
- 综合验证命令组：`ptrtest`、`copytest`、`pfmodel`、`signaltest`、`syscallinfo`、
  `bptest` 等全部可用，与 `l154test`/`l162test` 一起构成最终验收面。

#### 3.2.5 本课新增 checkpoint：lesson_155_model 与 l162test

```c
struct lesson_155_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_155_model lesson_155_state;
static TEXT64 void l162test(u16*c){lesson_155_state=(struct lesson_155_model){155U,156U,157U,158U,1,1,1,1};int ok=lesson_155_state.valid&&lesson_155_state.active&&lesson_155_state.ready&&lesson_155_state.accounted&&lesson_155_state.b==lesson_155_state.a+1U;text64(c,"l162test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 155 fallback reported");putc64(c,'\n');}
```

- `struct lesson_155_model`：4 个 u32 + 4 个状态位，`a` 以 `155U` 起头，155 = 162−7。
  至此主题弧线六课模型名 150/151/152/153/154/155 全部就位，形成一条完整的
  「回锚」链。
- `l162test` 算法：①字面量赋值；②五连断言（valid/active/ready/accounted/b==a+1）；
  ③成功串 `bounded networking, namespaces, cgroups, and security checkpoint passed`
  或失败串 `Lesson 155 fallback reported`。
- 为什么：作为全课程最终回归探针，它与 `l150test`…`l154test` 共同覆盖「网络/
  namespace/cgroup/安全」六连检查点；不改坏任何模型字段时恒为 passed。

#### 3.2.6 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l154test")){if(!noargs64(arg))usage64(c,"l154test");else l154test(c);}else if(eq64(word,"l162test")){if(!noargs64(arg))usage64(c,"l162test");else l162test(c);}
```

- 本课把上一课的 `l161test` 分支改名 `l154test`（其模型 `lesson_154_state` 不动，
  仍是 `{154,155,156,157}`），并新增 `l162test` 分支。
- **勘误**：旧 README 写的 `Commands: l155test` 与源码不符——源码中**不存在**
  `l155test` 命令（`grep -c l155test` 为 0），可用的 checkpoint 命令是 `l154test`
  与 `l162test`。
- about 文案 `else text64(c,"Lesson 162: 网络、namespace、cgroup、安全综合
  checkpoint\n");` 与开机横幅 `text64(&c,"Lesson 162: 网络、namespace、cgroup、
  安全综合 checkpoint\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS;
  bounded reclaim metadata\n");` 一起构成主题标识——横幅里 `bounded reclaim
  metadata` 呼应全课「有界元数据模型」的核心方法论。

### 3.3 构建管线（Makefile / linker）

- `kernel64.o`：`gcc $(CFLAGS64) -c`。`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`——`-fpie` 允许 RIP 相对
  寻址（`leaq` 取 stub 地址依赖它），`-mno-red-zone` 防止中断路径踩红区。
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld -nostdlib` 再 `objcopy -O binary`；
  `kernel64.ld` 从 0 开始布局，`.data` 内用 `. = ALIGN(0x1000)` 依次划出
  idle/rsp0/ist1 三块 guard+stack，末尾三条 `ASSERT(...==0x1000)` 锁死每块栈尺寸。
- `boot.o`：`gcc $(CFLAGS)`（32 位），依赖 `build/kernel64.bin`——外层 `.text64`
  段 `kernel_main64` 以 `.incbin` 嵌入二进制。
- `kernel.iso`：`ld -m elf_i386 -T linker.ld` 链接外层 ELF32，`grub-mkrescue` 出 ISO；
  `linker.ld` 保证 `.multiboot` 在 1 MiB 起、8 字节对齐、`.text64` 紧随其后。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`网络、namespace、cgroup、安全综合 checkpoint`、`l162test`、`Lesson 162`）——
  README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**。Makefile 仅 `check` 目标的 grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) CR4.PAE → EFER.LME → CR0.PG → far jump
  → kernel_main64_binary (kernel64.c)
       module_init_model() → init_model_start() → wait_model_start()
       → adoption_start() → resource_start()
       → pmm_init() → vma_init() → reclaim_init() → vfs_init()
       → 进程/线程元数据装配 → framebuffer_init
       → stack_guards_init / runtime_gdt_tss_init / idle_init / install_idt
       → pit_init()+pic_init() → 横幅 "Lesson 162: 网络、namespace、cgroup、安全综合 checkpoint\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  exec64 分支 → 网络:pipetest/polltest/pctest
             → namespace:pgtest/sessiontest/reparenttest/jobtest
             → cgroup:teardowntest/reclaimtest/anoninfo
             → 安全:ptrtest/pfmodel/signaltest/syscallinfo
             → l154test / l162test:checkpoint 断言（最终验收）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 162: 网络、namespace、
   cgroup、安全综合 checkpoint\n")` → 屏幕打印 `Lesson 162: 网络、namespace、cgroup、
   安全综合 checkpoint`。
2. **`l162test`** → `l162test(c)` 对 `lesson_155_state` 赋值并五连断言 → 输出
   `l162test: bounded networking, namespaces, cgroups, and security checkpoint passed`
   ——全课程最终一行验收。
3. **`reclaimtest`** → `fault_insert` 插一页 → 两次 `page_cache_get` 构造
   hit/miss → `reclaim_one` 回收 → 断言 `anon_pages==0 && page_cache_count==1` →
   输出 `reclaimtest: anonymous reclaim and page-cache hit model passed`，下一行
   `page cache is metadata-only; no disk I/O or swap executed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字
黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-162-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `网络、namespace、cgroup、安全综合 checkpoint`、`l162test`、`Lesson 162` 与
  kernel64.c 中的 `l162test`，全部命中输出 `Multiboot2 and Lesson 162 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 162: 网络、namespace、cgroup、安全综合 checkpoint`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 162: 网络、namespace、cgroup、安全综合 checkpoint`
  2. `l162test` → `l162test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l154test` → `l154test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit model passed`，
     下一行 `page cache is metadata-only; no disk I/O or swap executed`
  5. `reparenttest` → `reparenttest: orphan adoption, init wait ownership, and
     bounded reparent passed`
  6. `polltest` → `polltest: POLLIN/POLLOUT readiness transitions passed`
  7. `ptrtest` → `ptrtest: canonical/range/VMA/permission checks passed`
  8. `signaltest` → `signaltest: exception notifications queued with bounded default
     actions passed`
- **如何判断成功**：上述命令逐一打印预期串即成功；`make check` 三条 grep 全命中即
  通过。这是全课程最终验收画面。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l162test` 输出 `Lesson 155 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l162test` 的赋值 `{155U,156U,157U,158U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 输入 `l155test` 报 `unknown command` | 旧 README 命令名是笔误，源码无此命令 | 源码中可用命令是 `l154test` 与 `l162test` |
| `reclaimtest` 输出 `BROKEN` 或 `anon_pages!=0` | `fault_insert` 失败或 `reclaim_one` 三重条件不满足 | 先 `vmainfo` 确认 VMA；检查 `fault_pages[i].reclaimable/refs` 是否被其他测试改脏 |
| `reparenttest` 输出 `BROKEN` | 收养链顺序错乱（未 orphaned 就 reparent） | 检查 `adoption_exit_parent`→`adoption_reparent` 的调用顺序与前置状态位 |
| `polltest` 输出 `BROKEN` | `pipe_poll` 的就绪位判定与 `used` 状态不符 | 检查 `POLL_IN/POLL_OUT` 宏（1/2）与 `pipe_poll` 的两个判定分支 |
| `ptrtest` 输出 `BROKEN` | `uaccess_validate` 某层裁决与断言不符 | 检查 `USER_CANONICAL_MAX/USER_RANGE_MAX/USER_COPY_MAX`；确认 data `rw-`、code `r-x` |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 162: 网络、namespace、cgroup、安全综合 checkpoint`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **网络（networking）**：TinyOS 用 `pipe_model`/`pipe_poll` 表达通信；Linux 用
   socket（`net/socket.c`）+ `sock_poll`（`include/net/sock.h`）+ 等待队列实现进程
   间通信。TinyOS 砍掉了协议栈、端口、路由、skb，只保留「有界缓冲 + 阻塞唤醒 +
   就绪位」的最小形态。
2. **namespace**：TinyOS 用进程组/会话/孤儿收养（`process_group`/`adoption_model`）
   模拟命名空间；Linux 有 8 种 namespace（mount/pid/net/ipc/uts/user/cgroup/time，
   见 `kernel/nsproxy.c`，创建靠 `clone` 的 `CLONE_NEW*` 标志），孤儿收养靠
   `kernel/exit.c` 的 `forget_original_parent`/`find_new_reaper`。TinyOS 只覆盖
   「归属关系」这一维，没有独立视图隔离。
3. **cgroup**：TinyOS 用 `resource_ledger`（资源引用台账）+ `reclaim_one`（页回收）
   建模；Linux cgroup v2（`kernel/cgroup/cgroup.c`、`mm/memcontrol.c`）对进程做
   分组记账、按组限额、内存压力回收。TinyOS 砍掉了层级树、控制器子系统、
   `memory.high/max` 限额语义，只保留「清点 + 回收」骨架。
4. **安全（security）**：TinyOS 的权限裁决（`uaccess_validate`/`pf_classify`）与
   syscall 边界（`-ENOSYS`、无用户指针）对应 Linux LSM（`security/security.c`）、
   capability（`kernel/capability.c`）与 `access_ok`（`include/linux/uaccess.h`）；
   有界审计缓冲对应 `kernel/audit.c` 的 backlog + `audit_log_lost`。TinyOS 没有
   钩子注册表、没有 SID/AVC、没有 errno 语义。
5. **「有界元数据」方法论**：本主题弧线所有模型都遵循同一原则——固定容量数组 +
   显式状态位 + 确定性断言；Linux 侧对应的是「硬限制 + 审计计数」的工程实践
   （`audit_backlog_limit`、`RLIMIT_*`、cgroup 限额）。两者的共同点是：**内核永远
   不能无限积压或无限信任**。

**权威来源**：Linux `kernel/nsproxy.c`、`kernel/exit.c`、`kernel/cgroup/cgroup.c`、
`mm/memcontrol.c`、`kernel/audit.c`、`security/security.c`、`include/linux/capability.h`、
Intel SDM Vol.3A（本课未改动的 GDT/TSS/IDT 机制）、Multiboot2 规范。
**教学模型简化了什么**：真实内核的网络/namespace/cgroup/安全各有数万行实现与复杂
状态机；TinyOS 每项只保留「一张元数据表 + 一组断言」，用确定性替代随机性，用
`passed/BROKEN` 二值替代真实系统的运行时错误语义。

---

## 8. 思考题与练习

1. **概念理解**：本主题弧线的四根支柱（网络、namespace、cgroup、安全）在 TinyOS
   里分别由哪些结构体代表？它们各自的「有界容量」是多少？（提示：查
   `PIPE_CAP`/`TASK_TABLE_CAP`/`VM_REGION_SLOTS`/`SIG_PENDING_MAX`）
2. **源码定位**：在 `kernel64.c` 中找出 `l162test` 使用的模型结构体名字，解释它
   为什么叫 `lesson_155_model`（与课号 162 的关系），并列出本主题弧线六课的模型名
   与命令名对照表。
3. **动手实验**：修改 `l162test` 的赋值，把 `d` 从 `158U` 改成 `158U+1`，重新构建
   运行，观察输出是否仍为 passed（断言不含 d 的连续性）；再把 `ready` 改成 `0`
   观察输出翻转。
4. **动手实验**：综合验收——在 QEMU 里依次运行 `pipetest`、`reparenttest`、
   `reclaimtest`、`ptrtest`、`l154test`、`l162test`，把六个输出串与本文 §5 预期
   串逐一比对。
5. **Linux 对照**：阅读 `kernel/nsproxy.c` 的 `copy_namespaces` 与 `mm/memcontrol.c`
   的 `mem_cgroup_charge`，对比它们与 `adoption_model`/`resource_ledger` 的粒度
   差距，指出 TinyOS 若要增加真实 cgroup 层级需要补哪些结构。

---

## 9. 本课小结与全课程主线回顾

**小结**：
1. 本课是资源/安全主题的检查点课，也是**全课程最终一课**；`kernel64.c` 相对上一课
   只有 4 处小增量，主题机制全部继承，只追加最终 checkpoint。
2. 网络（管道 + poll）、namespace（进程组/会话/孤儿收养）、cgroup（资源台账 +
   页回收）、安全（权限裁决 + syscall 边界 + 审计缓冲 + 策略决策）四根支柱在
   TinyOS 里各有清晰的元数据模型与验证命令。
3. 新 checkpoint `l162test` 用字面量赋值 + 五连断言固化最终回归探针；模型名
   `lesson_155_model` 的 155 = 162−7，与 150/151/152/153/154 五级「回锚」链合拢。
4. 旧 README 的 `Commands: l155test` 已勘误为源码实际的 `l154test` 与 `l162test`。

**全课程主线回顾**（从 Lesson 00 到 162）：
1. **引导**：Multiboot2 头 → GRUB → 32 位 MBI 解析/建页表 → long mode 切换 →
   `kernel_main64_binary`；
2. **内核地基**：PMM 位图、IDT/GDT/TSS、异常与 #BP/#UD/#PF 路径、PIT/IRQ、键盘；
3. **调度与并发**：三线程 + idle、抢占式 round-robin、wait queue/event/信号量、
   pipe、生产者消费者、softirq/workqueue、锁与原子；
4. **进程与 syscall**：CPL3 用户态、`int 0x80` 白名单、信号、fork/exec/wait、
   进程组/会话、孤儿收养；
5. **内存与文件**：VMA/缺页分类/页回收/页缓存、ramfs/VFS/fd/inode/dentry；
6. **GUI**：framebuffer/canvas/窗口/输入事件；
7. **资源/安全主题弧线**（157–162）：资源限制与回收 → capability → syscall 边界 →
   审计缓冲 → 策略决策 → 本课综合 checkpoint——一条贯穿「隔离、限额、审计、决策」
   的完整安全主线。

**方法论总结**：本课程自始至终贯彻「有界元数据模型」——用固定容量结构体 + 显式
状态位 + 确定性断言（`passed/BROKEN`）把 Linux 的复杂机制压成可验证的教学标本；
每一课的 checkpoint 命令 `lXXtest` 既是回归探针，也是把「继承机制仍然自洽」固化
下来的快照。到这里，从第一行 Multiboot2 汇编到最终的综合验收，TinyOS 的全课程
主线画上了句号。

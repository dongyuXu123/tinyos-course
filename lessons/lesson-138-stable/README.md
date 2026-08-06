# Lesson 138: 网络 buffer pool — 精讲文档

> **课号**：Lesson 138（可执行课，checkpoint 快照）
> **主题**：网络 buffer pool——阶段 6（网络主题）的起点课。banner/about 宣告网络
> 主题，checkpoint 消息切换为网络/命名空间/cgroup/安全覆盖标签，并追加 checkpoint
> 模型 `lesson_131_model`。
> **课程主线位置**：诊断/网络主题的「检查点课」序列（Lesson 133–138）的最后一课，
> 同时是阶段 6「网络主题」的起点。位于 Lesson 137（并发、SMP、RCU、诊断综合
> checkpoint）之后、Lesson 139（网络接口与链路状态）之前。
> **前置课程**：[`lesson-137-stable/README.md`](../lesson-137-stable/README.md)
> **后续课程**：[`lesson-139-stable/README.md`](../lesson-139-stable/README.md)
> **一句话目标**：学完本课你能确认阶段 6 的开场语义——本课源码里「网络 buffer
> pool」只是主题宣告（about/banner）与 checkpoint 消息文本，真正的 skb 式缓冲池
> 机制尚未出现；网络机制将自 Lesson 139 起逐步展开。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 137 只有
3 处小增量（`l137test`→`l130test` 改名、新增 `lesson_131_model`/`l138test`、
文案换成「网络 buffer pool」），**没有新增任何网络代码**；理解本课 checkpoint
模型 `l138test` 的断言与消息文本语义，以及它如何为阶段 6 网络主题立起「覆盖标签」。

- **在课程主线中的位置**：Lesson 133–138 是检查点课序列，Lesson 138 收尾该序列并
  开启网络主题。banner/about 文案「网络 buffer pool」+ checkpoint 成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 标识
  主题转向；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 137：综合 checkpoint 概念与「消息文本是覆盖标签」的诚实性约定；
  2. `pmm_alloc`/`page_cache_get` 元数据模型（Lesson 134）——未来 buffer pool
     从物理帧要缓冲的基础；
  3. exec64 命令分发与 l-test 接线模式（改名 + 新增两分支）；
  4. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定。
- **本课交付**：`l130test`、`l138test` 两个 checkpoint 命令；`about` 文案「网络
  buffer pool」；对「主题宣告课 vs 机制实现课」的准确区分。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课（theme-announcing checkpoint）

**直觉**：课程主线有时需要一个「转向点」——先宣告新主题，再在后续课里逐步实现。
Lesson 138 正是这样的转向点：文本层（about/banner/checkpoint 消息）切换到网络，
代码层不加网络机制。

**如何确认（自查清单）**：
1. `diff ../lesson-137-stable/kernel64.c kernel64.c` 只有 9 行变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `skb`/`net_device`/`tx_ring`/`buffer_pool` 等网络符号——**不存在**；
3. 唯一带 "networking" 字样的是 `l138test` 的成功串（覆盖标签）。

**为什么需要这样的课**：阶段 6 网络主题要在既有 32 位/64 位双段、PMM、调度骨架上
叠加新机制，一次性实现既难验证又难教学。先立主题、再逐课展开，是课程设计的
「声明-实现分离」。

### 2.2 概念二：网络主题的关键词——buffer pool / namespaces / cgroups / security

**直觉**：Linux 网络栈依赖四个支撑概念，本课 checkpoint 消息把它们打包成覆盖标签：

| 关键词 | Linux 对应 | 本课位置 |
|--------|-----------|---------|
| buffer pool | `net/core/skbuff.c` 的 `struct sk_buff` + `kmem_cache` 缓冲池 | 主题标签；机制见 Lesson 139+ |
| namespaces | `include/net/net_namespace.h` 的 `net` 命名空间 | 主题标签 |
| cgroups | `kernel/cgroup/cgroup.c` | 主题标签 |
| security | LSM（`security/security.c`） | 主题标签 |

**诚实边界**：本课源码**没有**上述任何实现，它们只出现在 checkpoint 消息文本里。
若后续课实现了 buffer pool，将复用本课继承的 `pmm_alloc`/`page_cache` 元数据
模式（从物理帧要缓冲、登记、回收）。

### 2.3 概念三：本课 checkpoint 模型

`struct lesson_131_model` 的 `a` 从 `131U` 起头 = 课号 138 − 7，回锚到 Lesson 131
检查点。`l138test` 五连断言（valid/active/ready/accounted/b==a+1）输出成功串
`bounded networking, namespaces, cgroups, and security checkpoint passed` 或
失败串 `Lesson 131 fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 137） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（959 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l137test`→`l130test`；新增 `struct lesson_131_model`、`l138test`；exec64 增加 `l130test`/`l138test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `网络 buffer pool`、`l138test`、`Lesson 138` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 137 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `skb`、`net_device`、发送/接收队列等网络实现符号。任何
「网络 buffer pool 已实现」的表述都是错误的——本课是主题起点课。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_131_model 与 l138test

```c
struct lesson_131_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_131_model lesson_131_state;
static TEXT64 void l138test(u16*c){lesson_131_state=(struct lesson_131_model){131U,132U,133U,134U,1,1,1,1};int ok=lesson_131_state.valid&&lesson_131_state.active&&lesson_131_state.ready&&lesson_131_state.accounted&&lesson_131_state.b==lesson_131_state.a+1U;text64(c,"l138test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 131 fallback reported");putc64(c,'\n');}
```

- 字段语义：4 个 u32 连续编号（a=131、b=132、c=133、d=134）+ 4 个状态位
  （valid/active/ready/accounted）。`a` 从 `131U` 起头 = 课号 138 − 7，回锚到
  Lesson 131 检查点——延续 L133→L126、L137→L130 的「模型编号 = 课号 − 7」回锚链。
- 断言逻辑：`ok` 五连真（四个状态位 + `b==a+1`）输出成功串 `bounded networking,
  namespaces, cgroups, and security checkpoint passed`，否则失败串 `Lesson 131
  fallback reported`。
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。它与前五课的
  "concurrency, SMP, RCU, and diagnostics" 一样，只描述「内核机制覆盖面」。断言
  本身只校验元数据自洽，不执行任何网络代码。

#### 3.2.2 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l130test")){if(!noargs64(arg))usage64(c,"l130test");else l130test(c);}else if(eq64(word,"l138test")){if(!noargs64(arg))usage64(c,"l138test");else l138test(c);}
```

- 本课把上一课的 `l137test` 分支改名 `l130test`（复用 `lesson_130_model`），新增
  `l138test` 分支。**勘误**：旧 README 写的 `Commands: l131test` 与源码不符，源码中
  可用的 checkpoint 命令是 `l130test` 与 `l138test`。
- about：`else text64(c,"Lesson 138: 网络 buffer pool\n");`；开机横幅：
  `text64(&c,"Lesson 138: 网络 buffer pool\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致（`help` 列表含
  `pipeinfo pipetest polltest ptrinfo ptrtest copytest schedinfo tasklist
  taskvalidate forkinfo forktest ... pfmodel ... meminfo ... idtinfo ...` 等），
  `l64test`–`l130test` 的 l-test 分支继续保留。

#### 3.2.3 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 将复用的三个继承件值得点明（源码在 `kernel64.c`
既有部分，非本课新增）：
1. `pmm_alloc`/`pmm_free_page`：buffer pool 的物理帧来源（Lesson 134 精讲）；
2. `page_cache_get` 的「index→phys 登记」模式：未来 `sk_buff` 缓冲登记的同构；
3. `fault_insert` 的 `live/dirty/refs` 字段约定：缓冲生命周期管理的基础。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`网络 buffer pool`、`l138test`、`Lesson 138`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 138: 网络 buffer pool\n..." → shell 循环
  exec64 命令 → l130test/l138test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 138: 网络 buffer pool\n")` → 屏幕打印
   `Lesson 138: 网络 buffer pool`。
2. **`l138test`** → `l138test(c)` 对 `lesson_131_state` 赋值并五连断言 → 输出
   `l138test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l130test`** → 复用 `lesson_130_model`（L137 的模型）→ 输出 `l130test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`——注意消息仍是
   "concurrency, SMP, RCU" 标签，证明改名只动了函数名与模型指针，消息未变。
4. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 137
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本与命令名。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-138-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `网络 buffer pool`、`l138test`、`Lesson 138` 与 kernel64.c 中的 `l138test`，
  全部命中输出 `Multiboot2 and Lesson 138 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 138: 网络 buffer pool`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 138: 网络 buffer pool`
  2. `l138test` → `l138test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  4. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。注意 `l138test` 的 passed 串
  含 "networking, namespaces, cgroups, and security"，与前三课的
  "concurrency, SMP, RCU" 不同——这是主题切换的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l138test` 输出 `Lesson 131 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l138test` 赋值 `{131U,132U,133U,134U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l130test` 输出失败串 | `lesson_130_model` 被改坏（它是 L137 的模型，本课只改函数名） | 检查 `l130test` 是否仍引用 `lesson_130_state`；比对赋值 `{130U,...}` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`skb`/`net_device` 等符号不存在 | 用 `diff ../lesson-137-stable/kernel64.c kernel64.c` 确认仅 9 行差异；`grep -i skb kernel64.c` 应无命中 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 138: 网络 buffer pool`；`make check` grep 覆盖此串 |
| 某个继承 test 输出 `BROKEN` | 该族机制回归（与网络主题无关，属前课机制） | 按 Lesson 137 §3.2.4 顺序排查对应 info 命令 |

---

## 7. 与 Linux 源码对照

1. **buffer pool 对照（预告）**：Linux 网络缓冲池在 `net/core/skbuff.c`——
   `struct sk_buff` 与 `sk_buff_head` 链表、`kmem_cache_create` 的 slab 缓冲池、
   `alloc_skb`/`kfree_skb`。TinyOS 本课未实现；后续课若实现，将复用继承的
   `pmm_alloc`/`page_cache` 元数据模式做 `struct sk_buff` 的教学版。
2. **net namespaces**：Linux `include/net/net_namespace.h` 的 `struct net`（每个
   namespace 一套路由/防火墙/协议表），本课仅在消息文本引用。
3. **cgroups**：Linux `kernel/cgroup/cgroup.c` 的资源分组与控制器（cpu/memory/
   io），本课仅在消息文本引用。
4. **安全（LSM）**：Linux `security/security.c` 的 hook 链（`security_ops`），本课
   仅在消息文本引用。
5. **诚实性对照**：Linux 文档区分「接口声明」与「实现」；本课同理——checkpoint
   消息是主题声明，实际网络机制自 Lesson 139 起逐课实现。教学模型不假装网络栈
   已存在。

**权威来源**：Linux `net/core/skbuff.c`、`include/net/net_namespace.h`、
`kernel/cgroup/cgroup.c`、`security/security.c`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。

---

## 8. 思考题与练习

1. **概念理解**：如何仅凭源码就判断「网络 buffer pool」在本课是主题标签而非已实现
   机制？列出你用的检查方法（diff、符号搜索等）。
2. **源码定位**：在 `kernel64.c` 中找出 `l138test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l130test` 使用的模型结构体名。
3. **动手实验**：修改 `l138test` 的赋值把 `b` 改成 `131U`（即 `b==a`），重跑观察
   输出翻转为 `Lesson 131 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-137-stable/kernel64.c kernel64.c | wc -l`，确认
   本课相对上一课的改动行数（预期约 9 行），并用 `grep -c skb kernel64.c` 确认
   无网络符号。
5. **Linux 对照**：浏览 `net/core/skbuff.c` 的 `alloc_skb` 与 `kfree_skb` 注释，
   推测如果 TinyOS 在 Lesson 139 之后实现教学版 buffer pool，会复用本课继承的
   哪个元数据模式（`pmm_alloc`/`page_cache_get`/`fault_insert`）做「分配-登记-回收」，
   并说明为什么。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是检查点课序列的收尾课 + 阶段 6 网络主题的起点课，`kernel64.c` 相对
   Lesson 137 只有 3 处小增量。
2. 「网络 buffer pool」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   没有网络实现符号——这是必须如实声明的边界。
3. 新增 `lesson_131_model`/`l138test`，`a` 从 `131U` 起头回锚到 Lesson 131；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l130test` 复用 L137 的 `lesson_130_model`，消息仍是 "concurrency, SMP, RCU"
   标签，证明改名不动模型。
5. 未来网络机制的种子是继承的 `pmm_alloc`/`page_cache_get`/`fault_insert` 元数据
   模式。
6. 旧 README 的 `Commands: l131test` 已勘误为源码实际的 `l130test` 与 `l138test`。

**下一课**：[`lesson-139-stable/README.md`](../lesson-139-stable/README.md) 主题为
「网络接口与链路状态」——阶段 6 网络主题的**第一堂实现课**。它将在本课宣告的
主题上落第一块网络机制（网络接口与链路状态的教学模型），把 "networking" 从标签
变成代码。两课的衔接点：本课 checkpoint 消息里的 "networking" 标签即下课的
实现契约。
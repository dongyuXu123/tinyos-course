# Lesson 140: 收发队列与包记账 — 精讲文档

> **课号**：Lesson 140（可执行课，checkpoint 快照）
> **主题**：收发队列与包记账——阶段 6（网络主题）checkpoint 课的第四课。banner/about
> 宣告「收发队列与包记账」，checkpoint 消息继续沿用网络/命名空间/cgroup/安全覆盖标签，
> 并追加 checkpoint 模型 `lesson_133_model`/`l140test`。
> **课程主线位置**：网络主题 checkpoint 序列。位于 Lesson 139（网络接口与链路状态）
> 之后、Lesson 141（loopback 接口）之前。
> **前置课程**：[`lesson-139-stable/README.md`](../lesson-139-stable/README.md)
> **后续课程**：[`lesson-141-stable/README.md`](../lesson-141-stable/README.md)
> **一句话目标**：学完本课你能准确区分「主题宣告课」与「机制实现课」——本课源码里
> 「收发队列与包记账」只是 about/banner 文案与 checkpoint 覆盖标签，源码中**没有**
> `netdev_queue`/`netif_rx`/`net_device_stats` 等任何网络实现符号；本课把 Linux 的
> 「收发队列 + 包记账」机制作为**概念模型**精讲，并掌握 `l140test` 的断言语义。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 139 只有
3 处小增量（`l139test`→`l132test` 改名、新增 `struct lesson_133_model`/`l140test`、
about/banner 文案换成「收发队列与包记账」），**没有新增任何网络代码**；理解本课
checkpoint 模型 `l140test` 的断言与消息文本语义，并把「收发队列 + 包记账」作为
Linux 网络栈数据通路的核心概念模型来学习。

- **在课程主线中的位置**：阶段 6 网络主题 checkpoint 课序列的第四课（L138 立主题、
  L139 接口/链路、L140 队列/记账、L141 loopback、L142 IPv4 地址、L143 UDP socket、
  L144 端口分配）。banner/about 文案「收发队列与包记账」+ checkpoint 成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 标识
  本课主题；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 139：网络接口（`net_device`）与链路状态（carrier）概念模型；
  2. `pipe_model` 环形缓冲（head/tail/used + 读写计数）——本课概念模型最贴切的内核
     既有骨架；
  3. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定；
  4. 「模型编号 = 课号 − 7」回锚链与 `lNNNtest`/`lesson_NNN` 命名约定。
- **本课交付**：`l132test`（改名）、`l140test`（新增）两个 checkpoint 命令；`about`
  文案「收发队列与包记账」；对 Linux「收发队列/包记账」机制的完整概念模型。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课与「滚动标签链」的延续

**直觉**：阶段 6 的每一课（L138–L144）都是 checkpoint 快照：`kernel64.c` 只换
banner/about 文案 + checkpoint 模型，机制代码不动。Lesson 140 的主题标签是「收发
队列与包记账」，是滚动标签链的第四环。

**如何确认（自查清单）**：
1. `diff ../lesson-139-stable/kernel64.c kernel64.c` 只有 6 行实质变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `netdev_queue`/`netif_rx`/`net_device_stats`/`tx_queue_len` 等网络符号
   ——**不存在**；
3. 唯一带 "networking" 字样的是 checkpoint 成功串（覆盖标签）。

**为什么需要这样的课**：收发队列与包记账是网络数据通路的骨架，但实现它需要先有
接口对象与缓冲池。课程设计选择「逐课宣告主题、文档层建立概念模型」，让学习者先
建立完整心智地图，再等待机制实现课落地。

### 2.2 概念二：收发队列（transmit/receive queues）

**定义**：每个网络接口在数据通路上有两类队列——发送侧 `netdev_queue`（存放待发送
的 `sk_buff` 链，通常带一个排队规则 qdisc），接收侧内核的 backlog 队列（
`netif_rx` 软中断投递）或驱动的 NAPI 收包环。

**为什么需要**：收发双方速率不匹配。发送方（应用程序/协议栈）可能瞬间产生大量包，
硬件一次只能发送一个；接收方（网卡中断）可能在一瞬间丢入大量包，协议栈来不及处理。
队列就是中间缓冲，让生产/消费解耦，并提供「背压」信号（队列满时丢弃或唤醒等待者）。

**工作机制**（发送方向）：`dev_queue_xmit(skb)` → `__dev_queue_xmit` 把包放入
`skb->dev` 对应 `netdev_queue` 的 qdisc → qdisc 按策略（如 FIFO/令牌桶）取出 →
`ndo_start_xmit` 交给驱动。接收方向：驱动把包交给 `netif_rx`/`netif_receive_skb`
→ 挂到 CPU 的 backlog 队列 → `net_rx_action`（`net/core/dev.c`）在软中断里逐个
交给协议栈。

### 2.3 概念三：包记账（packet accounting）

**定义**：内核为每个接口维护收发统计——`struct net_device_stats`（
`include/linux/netdevice.h`）的 `rx_packets`、`tx_packets`、`rx_bytes`、`tx_bytes`，
以及错误/丢弃计数（`rx_errors`、`tx_errors`、`rx_dropped`、`tx_dropped`）。

**为什么需要**：运维与调试离不开计数：`ifconfig`/`ip -s link`/`/proc/net/dev` 都
读这些计数；丢包率、错误率是判断链路与驱动健康度的第一手数据；`netdev_rx_stat`/
`softnet_stat` 记录每 CPU 的收包处理统计。

**工作机制**：在数据通路的关键点递增计数——驱动接收中断时 `dev->stats.rx_packets++`
并累加 `rx_bytes`；发送完成（`ndo_start_xmit` 返回或 `tx interrupt`）时递增
`tx_packets`；丢弃路径递增 `rx_dropped`/`tx_dropped`。`dev_get_stats`（
`net/core/dev.c`）聚合后供用户态读取。新版内核用 `struct rtnl_link_stats64` 提供
64 位扩展统计。

### 2.4 概念四：本课 checkpoint 模型

`struct lesson_133_model` 的 `a` 从 `133U` 起头 = 课号 140 − 7，延续回锚链
（L138→131、L139→132、L140→133）。`l140test` 五连断言输出成功串 `l140test:
bounded networking, namespaces, cgroups, and security checkpoint passed` 或失败串
`l140test: Lesson 133 fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 139） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（979 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l139test`→`l132test`；新增 `struct lesson_133_model`、`l140test`；exec64 增加 `l132test`/`l140test` 分支；about/banner 文案换为「收发队列与包记账」 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `收发队列与包记账`、`l140test`、`Lesson 140` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 139 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `netdev_queue`、`netif_rx`、`net_device_stats` 等网络实现
符号。任何「收发队列与包记账已实现」的表述都是错误的——本课是主题宣告课。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_133_model 与 l140test

```c
struct lesson_133_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_133_model lesson_133_state;
static TEXT64 void l140test(u16*c){lesson_133_state=(struct lesson_133_model){133U,134U,135U,136U,1,1,1,1};int ok=lesson_133_state.valid&&lesson_133_state.active&&lesson_133_state.ready&&lesson_133_state.accounted&&lesson_133_state.b==lesson_133_state.a+1U;text64(c,"l140test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 133 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_133_model`：延续 `{a,b,c,d,valid,active,ready,accounted}` 元数据模型；
- `lesson_133_state`：本课新增模型实例，`a=133U` = 课号 140 − 7，回锚到检查点 L133；
- 断言 `ok`：四个状态位全真 **且** `b==a+1`（`134==133+1`）才为真；
- 输出：前缀 `"l140test: "` + 三目结果——成功串 `"bounded networking, namespaces,
  cgroups, and security checkpoint passed"`，失败串 `"Lesson 133 fallback
  reported"`；
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。断言只校验
  元数据自洽，不执行任何网络代码。

#### 3.2.2 改名：l139test → l132test（保留 lesson_132_model）

```c
static TEXT64 void l132test(u16*c){lesson_132_state=(struct lesson_132_model){132U,133U,134U,135U,1,1,1,1};int ok=lesson_132_state.valid&&lesson_132_state.active&&lesson_132_state.ready&&lesson_132_state.accounted&&lesson_132_state.b==lesson_132_state.a+1U;text64(c,"l132test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 132 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 本课把上一课的 `l139test` 函数**改名**为 `l132test`，模型结构与赋值完全不变
  （`lesson_132_state`、`a=132U`）——改名只动函数名与命令名，不动模型；
- 成功串仍是 `"bounded networking, namespaces, cgroups, and security checkpoint
  passed"`，与 `l140test` 相同（同一网络主题覆盖标签）；
- 失败串 `"Lesson 132 fallback reported"` 跟随模型编号 132，符合
  「`lNNNtest` 操作 `lesson_NNN`」命名约定。

#### 3.2.3 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l132test")){if(!noargs64(arg))usage64(c,"l132test");else l132test(c);}else if(eq64(word,"l140test")){if(!noargs64(arg))usage64(c,"l140test");else l140test(c);}
```

- 本课把上一课的 `l139test` 分支改为 `l132test`，并新增 `l140test` 分支。两条分支
  都先做 `noargs64` 参数检查（带参数则 `usage64`），再调用对应函数。
- **勘误**：旧 README 写的 `Commands: l133test` 与源码不符——源码中 `l133test`
  在本课**不存在**（它要到 Lesson 141 才出现）。本课可用的网络主题 checkpoint
  命令是 **`l132test` 与 `l140test`**（连同继承的 `l130test`/`l131test` 及更早
  l-test）。
- about：`else text64(c,"Lesson 140: 收发队列与包记账\n");`；开机横幅：
  `text64(&c,"Lesson 140: 收发队列与包记账\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致。

#### 3.2.4 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 未来实现网络机制时将复用的继承件值得点明（源码在
`kernel64.c` 既有部分，非本课新增）：
1. `pipe_model` 环形缓冲（`data[PIPE_CAP]` + `head/tail/used` + `reads/writes` 计数）：
   收发队列（ring）与包记账（读写/满/空计数）最直接的教学骨架；
2. `softirq_model`/`workqueue`（`SOFTIRQ_BITS`、`work_head/work_tail/work_used`）：
   Linux 收包软中断 `net_rx_action` 与 backlog 队列的既有同构；
3. `pmm_alloc`/`pmm_free_page`：未来 `sk_buff` 缓冲池的物理帧来源。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`收发队列与包记账`、`l140test`、`Lesson 140`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 140: 收发队列与包记账\n..." → shell 循环
  exec64 命令 → l132test/l140test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 140: 收发队列与包记账\n")` → 屏幕打印
   `Lesson 140: 收发队列与包记账`。
2. **`l140test`** → `l140test(c)` 对 `lesson_133_state` 赋值并五连断言 → 输出
   `l140test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l132test`** → 复用 `lesson_132_model`（L139 的模型改名而来）→ 输出
   `l132test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`——前缀不同（`l132test:`），成功串相同，证明改名不动模型与消息。
4. **`l131test`** → 输出 `l131test: bounded networking, namespaces, cgroups, and
   security checkpoint passed`；**`l130test`** → 输出 `l130test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`——后者的消息仍是并发
   主题标签，体现滚动链从并发切换到网络的边界。
5. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 139
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本、命令名与模型编号。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-140-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `收发队列与包记账`、`Lesson 140` 与 kernel64.c 中的 `l140test`，全部命中输出
  `Multiboot2 and Lesson 140 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 140: 收发队列与包记账`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 140: 收发队列与包记账`
  2. `l140test` → `l140test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l132test` → `l132test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `l131test` → `l131test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  5. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  6. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。网络主题命令（l140test/
  l132test/l131test）的 passed 串含 "networking, namespaces, cgroups, and
  security"，而 `l130test` 仍是 "concurrency, SMP, RCU"——这是主题滚动链的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l140test` 输出 `l140test: Lesson 133 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l140test` 赋值 `{133U,134U,135U,136U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l132test` 输出失败串 | `lesson_132_model` 被改坏（它是 L139 的模型，本课只改函数名） | 检查 `l132test` 是否仍引用 `lesson_132_state`；比对赋值 `{132U,...}` |
| 输入 `l139test` 提示 unknown command | 本课把 `l139test` 改名 `l132test`，旧命令名已不存在 | `grep -n l139test kernel64.c` 应无命中；改用 `l132test`/`l140test` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`netdev_queue`/`netif_rx` 等符号不存在 | `diff ../lesson-139-stable/kernel64.c kernel64.c` 确认仅 6 行差异；`grep -n netif kernel64.c` 应无命中 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 140: 收发队列与包记账`；`make check` grep 覆盖此串 |
| 输入 `l133test` 提示 unknown command | 旧 README 写错命令名；`l133test` 属于下一课（Lesson 141） | 本课正确的 checkpoint 命令是 `l132test` 与 `l140test` |

---

## 7. 与 Linux 源码对照

1. **发送队列**：Linux `include/linux/netdevice.h` 的 `struct netdev_queue`——
   每个 `net_device` 一个（或每队列一个），内含 `struct Qdisc *qdisc`、`state`
   标志。发送主路径 `net/core/dev.c` 的 `dev_queue_xmit`/`__dev_queue_xmit`：把
   `sk_buff` 交给 `qdisc_enqueue`，再由 `qdisc_run` 取出调用 `ndo_start_xmit`。
2. **接收队列**：Linux `net/core/dev.c` 的 `netif_rx`/`netif_receive_skb`——
   `netif_rx` 把包挂到当前 CPU 的 backlog 队列（`softnet_data->input_pkt_queue`），
   `net_rx_action` 软中断逐包处理；NAPI 路径则由驱动轮询环取包后直接
   `netif_receive_skb`。
3. **包记账**：Linux `include/linux/netdevice.h` 的 `struct net_device_stats`
   （`rx_packets`/`tx_packets`/`rx_bytes`/`tx_bytes`/`rx_errors`/`tx_errors`/
   `rx_dropped`/`tx_dropped`），由 `net/core/dev.c` 的 `dev_get_stats` 聚合；
   新版经 `struct rtnl_link_stats64` 提供 64 位统计。`ip -s link`、`/proc/net/dev`
   直接读这些计数。
4. **软中断骨架**：Linux `net/core/dev.c` 的 `net_rx_action` 是 `NET_RX_SOFTIRQ`
   软中断处理函数——TinyOS 既有 `softirq_model`（`SOFTIRQ_BITS`/`softirq_run_budget`）
   是它的教学同构：tasklet/FIFO work + budget 语义对应 NAPI 与 `net_rx_action` 的
   每次唤醒预算限制。
5. **诚实性对照**：Linux 的队列与统计是「实现」（真实数据结构与计数路径），而本课
   消息里的 "networking, namespaces, cgroups, and security" 只是主题声明。教学模型
   不假装网络栈已存在。

**权威来源**：Linux `include/linux/netdevice.h`、`net/core/dev.c`、
`net/core/skbuff.c`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。概念模型中，
Linux 收发队列涉及 per-CPU backlog、NAPI、qdisc 三层复杂度，本课仅建立「生产-消费
队列 + 统计计数」的简化心智模型。

---

## 8. 思考题与练习

1. **概念理解**：为什么收发队列需要「背压」机制？队列满时 Linux 的典型处理是什么
   （丢弃并递增 `rx_dropped`/`tx_dropped`）？
2. **源码定位**：在 `kernel64.c` 中找出 `l140test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l140test` 使用的模型结构体名与 `a` 的起始值。
3. **动手实验**：修改 `l140test` 的赋值把 `b` 改成 `133U`（即 `b==a`），重跑观察
   输出翻转为 `l140test: Lesson 133 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-139-stable/kernel64.c kernel64.c | grep -c '^[<>]'`
   确认本课相对上一课的改动行数；用 `grep -n netdev kernel64.c` 确认无网络符号。
5. **Linux 对照**：比较 `pipe_model`（`kernel64.c` 既有）与 Linux 的
   `struct sk_buff_head`（`include/linux/skbuff.h`）在「生产-消费 + 计数」语义上的
   相似之处，说明若 TinyOS 实现教学版收发队列会复用哪个既有骨架。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是阶段 6 网络主题 checkpoint 课的第四课，`kernel64.c` 相对 Lesson 139 只有
   3 处小增量（改名 + 新增模型 + 文案）。
2. 「收发队列与包记账」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   没有网络实现符号——这是必须如实声明的边界。
3. 新增 `lesson_133_model`/`l140test`，`a` 从 `133U` 起头回锚到 Lesson 133（课号−7）；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l139test` 改名 `l132test`，模型 `lesson_132_model` 与赋值不变，证明改名不动模型。
5. 概念模型上，本课建立了「收发队列（netdev_queue/backlog）vs 包记账
   （net_device_stats）」的 Linux 心智模型，并点明 `pipe_model`/`softirq_model`
   是未来教学实现的两块现成骨架。
6. 旧 README 的 `Commands: l133test` 已勘误：本课源码实际的 checkpoint 命令是
   `l132test` 与 `l140test`（`l133test` 属于 Lesson 141）。

**下一课**：[`lesson-141-stable/README.md`](../lesson-141-stable/README.md) 主题为
「loopback 接口」——网络主题 checkpoint 课的第五课。它将在本课队列/记账概念之上，
把「loopback 接口（lo 设备、127.0.0.1、自环数据通路）」作为新的主题标签与概念
模型，衔接点是本课的接口与队列概念（`net_device`、发送/接收路径）。

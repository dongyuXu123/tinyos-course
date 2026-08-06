# Lesson 139: 网络接口与链路状态 — 精讲文档

> **课号**：Lesson 139（可执行课，checkpoint 快照）
> **主题**：网络接口与链路状态——阶段 6（网络主题）checkpoint 课的第三课。banner/about
> 宣告「网络接口与链路状态」，checkpoint 消息继续沿用网络/命名空间/cgroup/安全覆盖标签，
> 并追加 checkpoint 模型 `lesson_132_model`/`l139test`。
> **课程主线位置**：网络主题 checkpoint 序列（Lesson 138 立主题 → 139 起按主题逐课
> 换标签）。位于 Lesson 138（网络 buffer pool）之后、Lesson 140（收发队列与包记账）之前。
> **前置课程**：[`lesson-138-stable/README.md`](../lesson-138-stable/README.md)
> **后续课程**：[`lesson-140-stable/README.md`](../lesson-140-stable/README.md)
> **一句话目标**：学完本课你能准确区分「主题宣告课」与「机制实现课」——本课源码里
> 「网络接口与链路状态」只是 about/banner 文案与 checkpoint 覆盖标签，源码中**没有**
> `net_device`/`netif_carrier` 等任何网络实现符号；本课把 Linux 的「网络接口 + 链路
> 状态」机制作为**概念模型**精讲，并掌握 `l139test` 的断言语义。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 138 只有
3 处小增量（`l138test`→`l131test` 改名、新增 `struct lesson_132_model`/`l139test`、
about/banner 文案换成「网络接口与链路状态」），**没有新增任何网络代码**；理解本课
checkpoint 模型 `l139test` 的断言与消息文本语义，并把「网络接口与链路状态」作为
Linux 网络栈第一层机制的概念模型来学习。

- **在课程主线中的位置**：阶段 6 网络主题的 checkpoint 课序列（L138 立主题、
  L139–L144 逐课换主题标签）。banner/about 文案「网络接口与链路状态」+ checkpoint
  成功串 `bounded networking, namespaces, cgroups, and security checkpoint passed`
  标识本课主题；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 138：网络主题起点课的「主题宣告 vs 机制实现」诚实性约定与 `lesson_131_model`
     模型（`a` 从 `131U` 起头）；
  2. exec64 命令分发与 l-test 接线模式（改名 + 新增两分支）；
  3. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定；
  4. 网络主题覆盖标签 `bounded networking, namespaces, cgroups, and security` 的含义。
- **本课交付**：`l131test`（改名）、`l139test`（新增）两个 checkpoint 命令；`about`
  文案「网络接口与链路状态」；对 Linux「网络接口/链路状态」机制的完整概念模型。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课与主题标识的滚动链

**直觉**：阶段 6 的每一课（L138–L144）都是 checkpoint 快照：`kernel64.c` 只换
banner/about 文案 + checkpoint 模型，机制代码不动。Lesson 139 的主题标签是「网络
接口与链路状态」，它只是**滚动标签链**中的一环。

**如何确认（自查清单）**：
1. `diff ../lesson-138-stable/kernel64.c kernel64.c` 只有 6 行实质变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `net_device`/`netif_carrier`/`netif_running`/`ifindex` 等网络符号——
   **不存在**（唯一的 `skb` 命中是 "taskbar" 里的子串，属误报）；
3. 唯一带 "networking" 字样的是 checkpoint 成功串（覆盖标签）。

**为什么需要这样的课**：网络栈机制（接口、队列、协议、端口）要在既有 32/64 位双段、
PMM、调度骨架上叠加，一次性实现既难验证又难教学。先按主题逐课宣告、再在文档层
把每个主题讲成 Linux 概念模型，是课程设计的「声明-教学分离」。

### 2.2 概念二：网络接口（network interface）

**定义**：Linux 中网络接口是内核网络栈与「外部世界」（物理网卡、虚拟网卡、回环、
网桥/tap 等）之间的抽象端点。每个接口对应一个 `struct net_device`，有唯一的名字
（`eth0`、`lo`）与 ifindex 编号。

**为什么需要**：协议栈（TCP/UDP/IP）不该知道底层硬件细节——`net_device` 通过
`netdev_ops`（`ndo_open`/`ndo_stop`/`ndo_start_xmit`）把硬件差异封装成统一操作集，
上层只面向「接口」编程。

**工作机制**（发送方向）：`dev_queue_xmit(skb)` → 找到 `skb->dev` 对应的
`netdev_queue` → 调用 `ndo_start_xmit` 交给驱动 → 驱动写设备寄存器。接收方向：
驱动中断/NAPI → `netif_receive_skb`/`netif_rx` 把包送进协议栈。

### 2.3 概念三：链路状态（link status / carrier）

**定义**：链路状态（carrier state）描述接口与对端的物理链路是否通（`netif_carrier_ok`
返回 1 = carrier up）。它与「接口管理状态」不同：`IFF_UP` 是管理员把接口打开
（`ip link set dev eth0 up`），carrier 是物理层报告的通断（拔网线 → carrier down）。

**为什么需要**：路由与邻居子系统需要知道链路是否可用，避免向断开的链路发包；用户态
`ip link`、监控工具据此显示 `NO-CARRIER`/`LOWER_UP`。

**工作机制**：驱动探测到链路变化后调用 `netif_carrier_on/off` → 触发
`linkwatch` 事件（`net/core/link_watch.c`）与 `NETDEV_CHANGE` 通知 → 路由/邻居/
上层协议按需响应。管理员开接口则走 `dev_open`（`net/core/dev.c`），它设置
`IFF_UP` 并调 `ndo_open`。

### 2.4 概念四：本课 checkpoint 模型

`struct lesson_132_model` 的 `a` 从 `132U` 起头 = 课号 139 − 7，延续
「模型编号 = 课号 − 7」的回锚链（L138→`lesson_131_model`）。`l139test` 五连断言
（valid/active/ready/accounted/b==a+1）输出成功串 `l139test: bounded networking,
namespaces, cgroups, and security checkpoint passed` 或失败串 `l139test: Lesson 132
fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 138） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（977 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l138test`→`l131test`；新增 `struct lesson_132_model`、`l139test`；exec64 增加 `l131test`/`l139test` 分支；about/banner 文案换为「网络接口与链路状态」 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `网络接口与链路状态`、`l139test`、`Lesson 139` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 138 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `net_device`、`netif_carrier`、`dev_queue_xmit` 等网络实现
符号。任何「网络接口与链路状态已实现」的表述都是错误的——本课是主题宣告课，
机制要到后续课才以教学模型形式逐步出现。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_132_model 与 l139test

```c
struct lesson_132_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_132_model lesson_132_state;
static TEXT64 void l139test(u16*c){lesson_132_state=(struct lesson_132_model){132U,133U,134U,135U,1,1,1,1};int ok=lesson_132_state.valid&&lesson_132_state.active&&lesson_132_state.ready&&lesson_132_state.accounted&&lesson_132_state.b==lesson_132_state.a+1U;text64(c,"l139test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 132 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_132_model`：延续 `{a,b,c,d,valid,active,ready,accounted}` 的元数据
  模型，4 个 u32 连续编号 + 4 个状态位；
- `lesson_132_state`：本课新增的模型实例，`a=132U` = 课号 139 − 7，回锚到检查点
  L132——延续「模型编号 = 课号 − 7」回锚链（L138→131、L139→132）；
- 断言 `ok`：四个状态位全部为真 **且** `b==a+1`（`133==132+1`）才为真；
- 输出：先打印 `"l139test: "` 前缀，再打印三目运算结果——成功串 `"bounded
  networking, namespaces, cgroups, and security checkpoint passed"`，失败串
  `"Lesson 132 fallback reported"`；
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。断言本身只
  校验元数据自洽，不执行任何网络代码。

#### 3.2.2 改名：l138test → l131test（保留 lesson_131_model）

```c
struct lesson_131_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_131_model lesson_131_state;
static TEXT64 void l131test(u16*c){lesson_131_state=(struct lesson_131_model){131U,132U,133U,134U,1,1,1,1};int ok=lesson_131_state.valid&&lesson_131_state.active&&lesson_131_state.ready&&lesson_131_state.accounted&&lesson_131_state.b==lesson_131_state.a+1U;text64(c,"l131test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 131 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 本课把上一课的 `l138test` 函数**改名**为 `l131test`，但模型结构与赋值完全不变
  （`lesson_131_state`、`a=131U`）——改名只动函数名与命令名，不动模型；
- 成功串仍是 `"bounded networking, namespaces, cgroups, and security checkpoint
  passed"`：注意它与 `l139test` 的成功串完全相同，因为两者都属于网络主题覆盖标签；
- 失败串为 `"Lesson 131 fallback reported"`——失败串里的编号跟随模型编号（131），
  与函数名一致；这是「函数名 = 模型名」的命名约定（`lNNNtest` 操作 `lesson_NNN`）。

#### 3.2.3 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l131test")){if(!noargs64(arg))usage64(c,"l131test");else l131test(c);}else if(eq64(word,"l139test")){if(!noargs64(arg))usage64(c,"l139test");else l139test(c);}
```

- 本课把上一课的 `l138test` 分支改为 `l131test`（接线到改名后的函数），并新增
  `l139test` 分支（接线到新函数）。两条分支都先做 `noargs64` 参数检查（带参数则
  `usage64`），再调用对应函数。
- **勘误**：旧 README 写的 `Commands: l132test` 与源码不符——源码中 `l132test`
  在本课**不存在**（它要到 Lesson 140 才出现）。本课可用的网络主题 checkpoint
  命令是 **`l131test` 与 `l139test`**（连同继承的 `l130test` 及更早 l-test）。
- about：`else text64(c,"Lesson 139: 网络接口与链路状态\n");`；开机横幅：
  `text64(&c,"Lesson 139: 网络接口与链路状态\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致（`help` 列表含 `pipeinfo
  pipetest polltest ptrinfo ptrtest copytest schedinfo tasklist taskvalidate
  forkinfo forktest ... reclaimtest pfmodel ...` 等），`l64test`–`l130test` 的
  l-test 分支继续保留。

#### 3.2.4 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 未来实现网络机制时将复用的三个继承件值得点明
（源码在 `kernel64.c` 既有部分，非本课新增）：
1. `pmm_alloc`/`pmm_free_page`：未来 `struct net_device`/`sk_buff` 教学模型的物理帧来源；
2. `page_cache_get` 的「index→phys 登记」模式：接口描述符、缓冲登记的既有同构；
3. `pipe_model` 的环形缓冲（head/tail/used + 读写计数）：**收发队列与包记账**
   （Lesson 140 主题）最直接的教学骨架。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`网络接口与链路状态`、`l139test`、`Lesson 139`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 139: 网络接口与链路状态\n..." → shell 循环
  exec64 命令 → l131test/l139test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 139: 网络接口与链路状态\n")` → 屏幕打印
   `Lesson 139: 网络接口与链路状态`。
2. **`l139test`** → `l139test(c)` 对 `lesson_132_state` 赋值并五连断言 → 输出
   `l139test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l131test`** → 复用 `lesson_131_model`（L138 的模型改名而来）→ 输出
   `l131test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`——成功串与 `l139test` 相同，但前缀不同（`l131test:` vs `l139test:`），
   证明改名只动了函数名与命令名，模型与消息未变。
4. **`l130test`** → 输出 `l130test: bounded concurrency, SMP, RCU, and diagnostics
   checkpoint passed`——注意消息仍是 "concurrency, SMP, RCU" 标签，说明该命令来自
   并发主题时代，未进入网络主题。
5. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 138
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本、命令名与模型编号。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-139-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `网络接口与链路状态`、`Lesson 139` 与 kernel64.c 中的 `l139test`，全部命中输出
  `Multiboot2 and Lesson 139 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 139: 网络接口与链路状态`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 139: 网络接口与链路状态`
  2. `l139test` → `l139test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l131test` → `l131test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  5. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。注意 `l139test`/`l131test` 的
  passed 串含 "networking, namespaces, cgroups, and security"，而 `l130test` 仍是
  "concurrency, SMP, RCU"——这是主题滚动链的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l139test` 输出 `l139test: Lesson 132 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l139test` 赋值 `{132U,133U,134U,135U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l131test` 输出失败串 | `lesson_131_model` 被改坏（它是 L138 的模型，本课只改函数名） | 检查 `l131test` 是否仍引用 `lesson_131_state`；比对赋值 `{131U,...}` |
| 输入 `l138test` 提示 unknown command | 本课把 `l138test` 改名 `l131test`，旧命令名已不存在 | `grep -n l138test kernel64.c` 应无命中；改用 `l131test`/`l139test` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`net_device`/`netif_carrier` 等符号不存在 | `diff ../lesson-138-stable/kernel64.c kernel64.c` 确认仅 6 行差异；`grep -n netif kernel64.c` 应无命中 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 139: 网络接口与链路状态`；`make check` grep 覆盖此串 |
| 输入 `l132test` 提示 unknown command | 旧 README 写错命令名；`l132test` 属于下一课（Lesson 140） | 本课正确的 checkpoint 命令是 `l131test` 与 `l139test` |

---

## 7. 与 Linux 源码对照

1. **网络接口抽象**：Linux `include/linux/netdevice.h` 的 `struct net_device`——
   名字（`nd_name`/`ifname`）、ifindex、`netdev_ops`（`ndo_open`/`ndo_stop`/
   `ndo_start_xmit`）、`netdev_queue`。TinyOS 本课未实现；后续教学模型会仿照
   `{name_hash, ifindex, flags, ...}` 做接口描述符元数据。
2. **接口开/关**：Linux `net/core/dev.c` 的 `dev_open`/`dev_close`——置 `IFF_UP`/
   `IFF_RUNNING` 标志、调用 `ndo_open`、发送 `NETDEV_UP`/`NETDEV_DOWN` 通知。
   管理员视角是 `ip link set dev eth0 up`。
3. **链路状态（carrier）**：Linux `net/core/link_watch.c` + `netif_carrier_on/off`
   ——驱动报告物理链路通断，`netif_carrier_ok()` 查询；链路变化经 `linkwatch`
   异步上报 `NETDEV_CHANGE`。这是「物理通断」与「管理 up」两个正交维度的经典示例。
4. **接口统计/记账（预告）**：Linux `struct net_device_stats`（`rx_packets`,
   `tx_packets`, `rx_bytes`, `tx_bytes`）由 `net/core/dev.c` 的 `dev_get_stats`
   汇总——这是 Lesson 140「包记账」主题的对照点。
5. **诚实性对照**：Linux 文档区分「接口声明（net_device 存在）」与「实现（驱动
   实际收发）」；本课同理——checkpoint 消息是主题声明，实际网络机制尚未出现。
   教学模型不假装网络栈已存在。

**权威来源**：Linux `include/linux/netdevice.h`、`net/core/dev.c`、
`net/core/link_watch.c`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。概念模型
中，接口管理状态（`IFF_UP`）与链路状态（carrier）在 Linux 中是两个独立标志位，
本课仅在文档层建立这一心智模型。

---

## 8. 思考题与练习

1. **概念理解**：接口的「管理状态」（IFF_UP）与「链路状态」（carrier）有什么区别？
   举一个「管理 up 但 carrier down」的真实场景。
2. **源码定位**：在 `kernel64.c` 中找出 `l139test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l139test` 使用的模型结构体名与 `a` 的起始值。
3. **动手实验**：修改 `l139test` 的赋值把 `b` 改成 `132U`（即 `b==a`），重跑观察
   输出翻转为 `l139test: Lesson 132 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-138-stable/kernel64.c kernel64.c | grep -c '^[<>]'`
   确认本课相对上一课的改动行数；用 `grep -n netif kernel64.c` 确认无网络符号。
5. **Linux 对照**：阅读 `net/core/link_watch.c` 的 `linkwatch_fire_event`，说明为什么
   链路变化要异步上报而不是在中断上下文直接处理，并推测 TinyOS 教学模型会如何简化。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是阶段 6 网络主题 checkpoint 课的第三课，`kernel64.c` 相对 Lesson 138 只有
   3 处小增量（改名 + 新增模型 + 文案）。
2. 「网络接口与链路状态」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   没有网络实现符号——这是必须如实声明的边界。
3. 新增 `lesson_132_model`/`l139test`，`a` 从 `132U` 起头回锚到 Lesson 132（课号−7）；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l138test` 改名 `l131test`，模型 `lesson_131_model` 与赋值不变，证明改名不动模型。
5. 概念模型上，本课建立了「网络接口（net_device）vs 链路状态（carrier）」的 Linux
   心智模型，为后续「收发队列/包记账/loopback/IPv4 地址/UDP socket/端口分配」主题
   逐课展开做准备。
6. 旧 README 的 `Commands: l132test` 已勘误：本课源码实际的 checkpoint 命令是
   `l131test` 与 `l139test`（`l132test` 属于 Lesson 140）。

**下一课**：[`lesson-140-stable/README.md`](../lesson-140-stable/README.md) 主题为
「收发队列与包记账」——网络主题 checkpoint 课的第四课。它将在本课宣告的接口/链路
概念之上，把「收发队列」与「包记账（rx/tx 统计）」作为新的主题标签与概念模型，
衔接点是本课预告的接口统计概念（`net_device_stats`、`netdev_queue`）。

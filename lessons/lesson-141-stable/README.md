# Lesson 141: loopback 接口 — 精讲文档

> **课号**：Lesson 141（可执行课，checkpoint 快照）
> **主题**：loopback 接口——阶段 6（网络主题）checkpoint 课的第五课。banner/about
> 宣告「loopback 接口」，checkpoint 消息继续沿用网络/命名空间/cgroup/安全覆盖标签，
> 并追加 checkpoint 模型 `lesson_134_model`/`l141test`。
> **课程主线位置**：网络主题 checkpoint 序列。位于 Lesson 140（收发队列与包记账）
> 之后、Lesson 142（IPv4 地址元数据）之前。
> **前置课程**：[`lesson-140-stable/README.md`](../lesson-140-stable/README.md)
> **后续课程**：[`lesson-142-stable/README.md`](../lesson-142-stable/README.md)
> **一句话目标**：学完本课你能准确区分「主题宣告课」与「机制实现课」——本课源码里
> 「loopback 接口」只是 about/banner 文案与 checkpoint 覆盖标签（源码中 `loopback`
> 仅出现在横幅/`about` 文本），**没有** `loopback_setup`/`dev_loopback_xmit` 等任何
> 网络实现符号；本课把 Linux 的 loopback 机制作为**概念模型**精讲，并掌握 `l141test`
> 的断言语义。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 140 只有
3 处小增量（`l140test`→`l133test` 改名、新增 `struct lesson_134_model`/`l141test`、
about/banner 文案换成「loopback 接口」），**没有新增任何网络代码**；理解本课
checkpoint 模型 `l141test` 的断言与消息文本语义，并把「loopback 接口」作为 Linux
网络栈中最简单也最重要的虚拟接口来建立概念模型。

- **在课程主线中的位置**：阶段 6 网络主题 checkpoint 课序列的第五课（L138 立主题、
  L139 接口/链路、L140 队列/记账、L141 loopback、L142 IPv4 地址、L143 UDP socket、
  L144 端口分配）。banner/about 文案「loopback 接口」+ checkpoint 成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 标识
  本课主题；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 139：网络接口（`net_device`）与链路状态（carrier）概念模型；
  2. Lesson 140：收发队列（`netdev_queue`/backlog）与包记账（`net_device_stats`）
     概念模型——loopback 的发送/接收路径复用这两块概念；
  3. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定；
  4. 「模型编号 = 课号 − 7」回锚链与 `lNNNtest`/`lesson_NNN` 命名约定。
- **本课交付**：`l133test`（改名）、`l141test`（新增）两个 checkpoint 命令；`about`
  文案「loopback 接口」；对 Linux loopback 机制的完整概念模型。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课与「滚动标签链」的延续

**直觉**：阶段 6 的每一课（L138–L144）都是 checkpoint 快照：`kernel64.c` 只换
banner/about 文案 + checkpoint 模型，机制代码不动。Lesson 141 的主题标签是
「loopback 接口」，是滚动标签链的第五环。

**如何确认（自查清单）**：
1. `diff ../lesson-140-stable/kernel64.c kernel64.c` 只有 6 行实质变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `loopback_setup`/`dev_loopback_xmit`/`netif_carrier_on` 等网络符号
   ——**不存在**；`grep loopback kernel64.c` 只命中横幅与 `about` 文本两处；
3. 唯一带 "networking" 字样的是 checkpoint 成功串（覆盖标签）。

**为什么需要这样的课**：loopback 是每个系统都有的第一个接口（ifindex 1、名字 `lo`），
理解它等于理解「接口 + 队列 + 链路」三块概念的首次合体。课程在此用主题宣告立标签、
用文档层建模型，为后续 IPv4 地址（L142）与 UDP socket（L143）铺路。

### 2.2 概念二：loopback 接口是什么

**定义**：loopback（回环）接口是内核的**虚拟网络接口**：名字固定为 `lo`、
ifindex 固定为 1、没有真实硬件。发给它的包不会离开本机，而是被内核「原路送回」
本机的接收路径。

**为什么需要**：本机进程之间通信（localhost）、测试网络栈（无需硬件）、
`127.0.0.1` 习惯用法都依赖它；它也是「接口抽象」最纯粹的例子——上层协议完全
不知道包其实没有物理发送。

**工作机制**：发送路径 `dev_queue_xmit(skb)` → qdisc → `loopback_xmit`（
`drivers/net/loopback.c`）→ 函数克隆/调整 `sk_buff`（交换源/目的、重置 `protocol`
与 `dev`）→ 直接调用 `netif_rx` 把包**重新注入接收路径** → 协议栈按正常收包处理。
整个过程中没有 DMA、没有中断、没有物理介质，但接口状态（`IFF_UP`、carrier）与
收发统计（`rx_packets`/`tx_packets`）照常更新。

### 2.3 概念三：loopback 的接口属性

- **名字与编号**：`lo`，ifindex = 1（`LOOPBACK_IFINDEX`），由 `loopback_setup`
  初始化 `netdev_ops`/`ethtool_ops`；
- **地址**：IPv4 `127.0.0.1/8`（loopback 网段，`ipv4_is_loopback` 判断
  `IN_LOOPBACKNET`），IPv6 `::1/128`；
- **MTU**：65536（远大于以太网 1500，因为不走物理介质）；
- **链路状态**：始终 `netif_carrier_on`（逻辑上永远在线）——loopback 的「物理链路」
  恒为 up，只有管理员 `ip link set lo down` 才关闭管理状态；
- **统计**：收发计数正常累积，`ip -s link show lo` 可观察。

### 2.4 概念四：本课 checkpoint 模型

`struct lesson_134_model` 的 `a` 从 `134U` 起头 = 课号 141 − 7，延续回锚链
（L138→131、L139→132、L140→133、L141→134）。`l141test` 五连断言输出成功串
`l141test: bounded networking, namespaces, cgroups, and security checkpoint
passed` 或失败串 `l141test: Lesson 134 fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 140） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（982 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l140test`→`l133test`；新增 `struct lesson_134_model`、`l141test`；exec64 增加 `l133test`/`l141test` 分支；about/banner 文案换为「loopback 接口」 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `loopback 接口`、`l141test`、`Lesson 141` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 140 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `loopback_setup`、`dev_loopback_xmit`、`loopback_ops` 等
网络实现符号。任何「loopback 接口已实现」的表述都是错误的——本课是主题宣告课。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_134_model 与 l141test

```c
struct lesson_134_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_134_model lesson_134_state;
static TEXT64 void l141test(u16*c){lesson_134_state=(struct lesson_134_model){134U,135U,136U,137U,1,1,1,1};int ok=lesson_134_state.valid&&lesson_134_state.active&&lesson_134_state.ready&&lesson_134_state.accounted&&lesson_134_state.b==lesson_134_state.a+1U;text64(c,"l141test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 134 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_134_model`：延续 `{a,b,c,d,valid,active,ready,accounted}` 元数据模型；
- `lesson_134_state`：本课新增模型实例，`a=134U` = 课号 141 − 7，回锚到检查点 L134；
- 断言 `ok`：四个状态位全真 **且** `b==a+1`（`135==134+1`）才为真；
- 输出：前缀 `"l141test: "` + 三目结果——成功串 `"bounded networking, namespaces,
  cgroups, and security checkpoint passed"`，失败串 `"Lesson 134 fallback
  reported"`；
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。断言只校验
  元数据自洽，不执行任何网络代码。

#### 3.2.2 改名：l140test → l133test（保留 lesson_133_model）

```c
static TEXT64 void l133test(u16*c){lesson_133_state=(struct lesson_133_model){133U,134U,135U,136U,1,1,1,1};int ok=lesson_133_state.valid&&lesson_133_state.active&&lesson_133_state.ready&&lesson_133_state.accounted&&lesson_133_state.b==lesson_133_state.a+1U;text64(c,"l133test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 133 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 本课把上一课的 `l140test` 函数**改名**为 `l133test`，模型结构与赋值完全不变
  （`lesson_133_state`、`a=133U`）——改名只动函数名与命令名，不动模型；
- 成功串仍是 `"bounded networking, namespaces, cgroups, and security checkpoint
  passed"`，与 `l141test` 相同（同一网络主题覆盖标签）；
- 失败串 `"Lesson 133 fallback reported"` 跟随模型编号 133，符合
  「`lNNNtest` 操作 `lesson_NNN`」命名约定。

#### 3.2.3 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l133test")){if(!noargs64(arg))usage64(c,"l133test");else l133test(c);}else if(eq64(word,"l141test")){if(!noargs64(arg))usage64(c,"l141test");else l141test(c);}
```

- 本课把上一课的 `l140test` 分支改为 `l133test`，并新增 `l141test` 分支。两条分支
  都先做 `noargs64` 参数检查（带参数则 `usage64`），再调用对应函数。
- **勘误**：旧 README 写的 `Commands: l134test` 与源码不符——源码中 `l134test`
  在本课**不存在**（它要到 Lesson 142 才出现）。本课可用的网络主题 checkpoint
  命令是 **`l133test` 与 `l141test`**（连同继承的 `l130test`–`l132test` 及更早
  l-test）。
- about：`else text64(c,"Lesson 141: loopback 接口\n");`；开机横幅：
  `text64(&c,"Lesson 141: loopback 接口\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致。

#### 3.2.4 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 未来实现网络机制时将复用的继承件值得点明（源码在
`kernel64.c` 既有部分，非本课新增）：
1. `pipe_model` 环形缓冲 + `reads/writes` 计数：loopback「发出去的包马上被收到」
   与管道读写语义同构——两侧同一条通道，只是角色交换；
2. `address_space_map`/`vm_mappings` 的「登记-释放」模式：未来 `net_device` 描述符
   表（`{name_hash, ifindex, flags, ops...}`）的登记骨架；
3. `softirq_model`：loopback 的 `netif_rx` 重新注入路径即走软中断收包——与既有
   `softirq_run_budget` 语义衔接。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`loopback 接口`、`l141test`、`Lesson 141`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 141: loopback 接口\n..." → shell 循环
  exec64 命令 → l133test/l141test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 141: loopback 接口\n")` → 屏幕打印
   `Lesson 141: loopback 接口`。
2. **`l141test`** → `l141test(c)` 对 `lesson_134_state` 赋值并五连断言 → 输出
   `l141test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l133test`** → 复用 `lesson_133_model`（L140 的模型改名而来）→ 输出
   `l133test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`——前缀不同（`l133test:`），成功串相同，证明改名不动模型与消息。
4. **`l132test`** → 输出 `l132test: bounded networking, namespaces, cgroups, and
   security checkpoint passed`；**`l130test`** → 输出 `l130test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`——后者的消息仍是并发
   主题标签，体现滚动链从并发切换到网络的边界。
5. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 140
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本、命令名与模型编号。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-141-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `loopback 接口`、`Lesson 141` 与 kernel64.c 中的 `l141test`，全部命中输出
  `Multiboot2 and Lesson 141 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 141: loopback 接口`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 141: loopback 接口`
  2. `l141test` → `l141test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l133test` → `l133test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `l132test` → `l132test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  5. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  6. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。网络主题命令（l141test/
  l133test/l132test）的 passed 串含 "networking, namespaces, cgroups, and
  security"，而 `l130test` 仍是 "concurrency, SMP, RCU"——这是主题滚动链的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l141test` 输出 `l141test: Lesson 134 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l141test` 赋值 `{134U,135U,136U,137U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l133test` 输出失败串 | `lesson_133_model` 被改坏（它是 L140 的模型，本课只改函数名） | 检查 `l133test` 是否仍引用 `lesson_133_state`；比对赋值 `{133U,...}` |
| 输入 `l140test` 提示 unknown command | 本课把 `l140test` 改名 `l133test`，旧命令名已不存在 | `grep -n l140test kernel64.c` 应无命中；改用 `l133test`/`l141test` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`loopback_setup`/`dev_loopback_xmit` 等符号不存在 | `diff ../lesson-140-stable/kernel64.c kernel64.c` 确认仅 6 行差异；`grep -n loopback kernel64.c` 只应命中横幅/about 文本 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 141: loopback 接口`；`make check` grep 覆盖此串 |
| 输入 `l134test` 提示 unknown command | 旧 README 写错命令名；`l134test` 属于下一课（Lesson 142） | 本课正确的 checkpoint 命令是 `l133test` 与 `l141test` |

---

## 7. 与 Linux 源码对照

1. **loopback 设备注册**：Linux `drivers/net/loopback.c` 的 `loopback_setup`——设置
   `ndo_start_xmit = loopback_xmit`、`ndo_get_stats64`、`netif_keep_dst`、`LOOPBACK`
   标志与 65536 MTU；`loopback_net_init` 在命名空间初始化时注册 `lo`（ifindex 1）。
   TinyOS 本课未实现；教学模型可仿照 `{name_hash="lo", ifindex=1, mtu, flags}` 做
   接口描述符。
2. **loopback 发送路径**：`drivers/net/loopback.c` 的 `loopback_xmit`——调整
   `skb->protocol`、交换 src/dst 语义后调用 `netif_rx` 把包**重新注入接收路径**。
   这是「发送即接收」的闭环：发往 127.0.0.1 的包永远不出本机。
3. **接口抽象的统一性**：Linux 中 `lo` 与 `eth0` 都满足同一 `net_device` 契约
   （`netdev_ops`、队列、统计、carrier）——loopback 证明「接口」是一种纯软件抽象，
   与硬件无关；这正是 `netif_carrier_on` 恒真的原因（物理链路恒在）。
4. **IPv4 回环网段**：Linux `include/linux/inetdevice.h` 的 `ipv4_is_loopback` 判断
   `IN_LOOPBACKNET`（`127.0.0.0/8`）；`lo` 的地址由 `devinet_ioctl`/`inetdev_init`
   （`net/ipv4/devinet.c`）配置——这与 Lesson 142「IPv4 地址元数据」主题衔接。
5. **诚实性对照**：Linux 的 loopback 是真实驱动（`drivers/net/loopback.c`），而本课
   消息里的 "networking, namespaces, cgroups, and security" 只是主题声明。教学模型
   不假装网络栈已存在。

**权威来源**：Linux `drivers/net/loopback.c`、`include/linux/netdevice.h`、
`include/linux/inetdevice.h`、`net/ipv4/devinet.c`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。概念模型中，
Linux loopback 涉及 `sk_buff` 克隆、dst 处理、软中断重注入，本课仅保留「发即收、
恒在线、有统计」的核心心智模型。

---

## 8. 思考题与练习

1. **概念理解**：为什么 loopback 接口的 MTU 是 65536 而不是以太网的 1500？它与
   carrier 恒真有什么关系？
2. **源码定位**：在 `kernel64.c` 中找出 `l141test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l141test` 使用的模型结构体名与 `a` 的起始值。
3. **动手实验**：修改 `l141test` 的赋值把 `b` 改成 `134U`（即 `b==a`），重跑观察
   输出翻转为 `l141test: Lesson 134 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-140-stable/kernel64.c kernel64.c | grep -c '^[<>]'`
   确认本课相对上一课的改动行数；用 `grep -n loopback kernel64.c` 确认只命中文案。
5. **Linux 对照**：阅读 `drivers/net/loopback.c` 的 `loopback_xmit`，说明它为什么要
   调整 `skb` 后调用 `netif_rx`（而不是直接发回上层），并推测 TinyOS 教学模型会如何
   简化这条「发送即接收」通路。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是阶段 6 网络主题 checkpoint 课的第五课，`kernel64.c` 相对 Lesson 140 只有
   3 处小增量（改名 + 新增模型 + 文案）。
2. 「loopback 接口」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   `loopback` 只出现在文案里，没有网络实现符号——这是必须如实声明的边界。
3. 新增 `lesson_134_model`/`l141test`，`a` 从 `134U` 起头回锚到 Lesson 134（课号−7）；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l140test` 改名 `l133test`，模型 `lesson_133_model` 与赋值不变，证明改名不动模型。
5. 概念模型上，本课建立了「loopback 接口（lo/ifindex 1/恒在线/发送即接收）」的
   Linux 心智模型——它是「接口 + 队列 + 链路」三块概念的首次合体。
6. 旧 README 的 `Commands: l134test` 已勘误：本课源码实际的 checkpoint 命令是
   `l133test` 与 `l141test`（`l134test` 属于 Lesson 142）。

**下一课**：[`lesson-142-stable/README.md`](../lesson-142-stable/README.md) 主题为
「IPv4 地址元数据」——网络主题 checkpoint 课的第六课。它将在本课 loopback 的
127.0.0.1/8 地址概念之上，把「IPv4 地址（in_device/in_ifaddr、掩码/广播）元数据」
作为新的主题标签与概念模型，衔接点是本课预告的 `ipv4_is_loopback`/`devinet`
（`net/ipv4/devinet.c`）。

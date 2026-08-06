# Lesson 143: UDP socket 状态 — 精讲文档

> **课号**：Lesson 143（可执行课，checkpoint 快照）
> **主题**：UDP socket 状态——阶段 6（网络主题）checkpoint 课的第七课。banner/about
> 宣告「UDP socket 状态」，checkpoint 消息继续沿用网络/命名空间/cgroup/安全覆盖标签，
> 并追加 checkpoint 模型 `lesson_136_model`/`l143test`。
> **课程主线位置**：网络主题 checkpoint 序列。位于 Lesson 142（IPv4 地址元数据）之后、
> Lesson 144（socket 端口分配）之前。
> **前置课程**：[`lesson-142-stable/README.md`](../lesson-142-stable/README.md)
> **后续课程**：[`lesson-144-stable/README.md`](../lesson-144-stable/README.md)
> **一句话目标**：学完本课你能准确区分「主题宣告课」与「机制实现课」——本课源码里
> 「UDP socket 状态」只是 about/banner 文案与 checkpoint 覆盖标签（源码中
> `sock`/`sk_state` 的 grep 命中全部是 `task_state` 子串与文案的误报），**没有**
> `struct sock`/`udp_sendmsg`/`udp_table` 等任何网络实现符号；本课把 Linux 的
> 「UDP socket 状态」机制作为**概念模型**精讲，并掌握 `l143test` 的断言语义。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 142 只有
3 处小增量（`l142test`→`l135test` 改名、新增 `struct lesson_136_model`/`l143test`、
about/banner 文案换成「UDP socket 状态」），**没有新增任何网络代码**；理解本课
checkpoint 模型 `l143test` 的断言与消息文本语义，并把「UDP socket 状态」作为 Linux
网络栈 L4 层的核心概念模型来学习。

- **在课程主线中的位置**：阶段 6 网络主题 checkpoint 课序列的第七课（L138 立主题、
  L139 接口/链路、L140 队列/记账、L141 loopback、L142 IPv4 地址、L143 UDP socket、
  L144 端口分配）。banner/about 文案「UDP socket 状态」+ checkpoint 成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 标识
  本课主题；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 142：IPv4 地址元数据（`in_ifaddr`：地址/掩码/广播/scope）——socket 的
     local/remote 地址部分就来自这里；
  2. UDP 无连接、面向数据报（datagram）的协议语义（一次 send/recv 一整个报文）；
  3. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定；
  4. 「模型编号 = 课号 − 7」回锚链与 `lNNNtest`/`lesson_NNN` 命名约定。
- **本课交付**：`l135test`（改名）、`l143test`（新增）两个 checkpoint 命令；`about`
  文案「UDP socket 状态」；对 Linux「UDP socket 状态」机制的完整概念模型。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课与「滚动标签链」的延续

**直觉**：阶段 6 的每一课（L138–L144）都是 checkpoint 快照：`kernel64.c` 只换
banner/about 文案 + checkpoint 模型，机制代码不动。Lesson 143 的主题标签是
「UDP socket 状态」，是滚动标签链的第七环。

**如何确认（自查清单）**：
1. `diff ../lesson-142-stable/kernel64.c kernel64.c` 只有 6 行实质变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `struct sock`/`udp_sendmsg`/`udp_table`/`recvmsg` 等网络符号——
   **不存在**；`grep sk_state kernel64.c` 的 6 处命中全部是 `task_state` 的子串误报；
3. 唯一带 "networking" 字样的是 checkpoint 成功串（覆盖标签）。

**为什么需要这样的课**：有了 L3 地址（L142），下一步自然是 L4 传输层——socket 是
「地址 + 端口 + 状态 + 缓冲」的容器。课程在此用主题宣告立标签、用文档层建模型，
为 L144（端口分配）铺路。

### 2.2 概念二：UDP socket 是什么

**定义**：socket 是进程访问网络资源的文件描述符式抽象。UDP socket 由
`struct socket`（文件层）与 `struct sock`（协议层，`include/net/sock.h`）两层组成；
UDP 专用子类 `struct udp_sock`（`include/net/udp.h`）扩展了端口哈希节点、长度等字段。

**为什么需要**：进程需要一个「端点」来收发数据报。UDP 是无连接的，端点只需绑定
本地四元组的一部分（本地 IP + 本地端口）；可选的 `connect()` 固定对端（远端 IP +
远端端口），让 `send` 不必每次写地址。

**工作机制**：`socket()` 创建 → `bind()` 绑定本地端口/地址 → `sendto`/`recvfrom`
或 `send`/`recv`（connect 后）→ `close()`。内核在 `net/ipv4/udp.c` 的
`udp_sendmsg`/`udp_recvmsg` 中处理数据报的发送与接收；收包时用本地端口在
`udp_table`（UDP 哈希表）里查 `struct udp_sock`。

### 2.3 概念三：UDP socket 的「状态」

**定义**：UDP 没有 TCP 那样的完整连接状态机（SYN/ESTABLISHED/FIN…），但它仍然有
两类状态字段：
- `struct socket.state`（`include/linux/net.h` 的 `enum socket_state`）：
  `SS_UNCONNECTED`（默认）、`SS_CONNECTED`（`connect()` 后）、
  `SS_CONNECTING`/`SS_DISCONNECTING` 等；
- `struct sock.sk_state`（`include/net/tcp_states.h` 的 TCP 状态值）：UDP 通常保持
  `TCP_CLOSE`，`connect()` 后置为 `TCP_ESTABLISHED`。

**为什么需要**：状态告诉内核与用户：这个 socket 是否已绑定、能否直接 `send`、
`recv` 是否应阻塞等。UDP 的「连接」只是固定对端，不产生握手报文——这是与 TCP 的
本质区别。

**工作机制**：`bind()` 成功 → `sk_state` 从 `TCP_CLOSE` 变为 `TCP_ESTABLISHED`
（表示「已就绪收发」），`socket.state` 保持 `SS_UNCONNECTED`；`connect()` →
`socket.state = SS_CONNECTED` 且 `sk_daddr/sk_dport` 填对端；`shutdown()` 置
`sk_shutdown` 标志；`close()` 释放 socket 与端口哈希条目。

### 2.4 概念四：本课 checkpoint 模型

`struct lesson_136_model` 的 `a` 从 `136U` 起头 = 课号 143 − 7，延续回锚链
（L138→131、…、L143→136）。`l143test` 五连断言输出成功串 `l143test: bounded
networking, namespaces, cgroups, and security checkpoint passed` 或失败串
`l143test: Lesson 136 fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 142） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（988 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l142test`→`l135test`；新增 `struct lesson_136_model`、`l143test`；exec64 增加 `l135test`/`l143test` 分支；about/banner 文案换为「UDP socket 状态」 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `UDP socket 状态`、`l143test`、`Lesson 143` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 142 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `struct sock`、`udp_sendmsg`、`udp_table` 等网络实现符号
（`grep sk_state` 命中均为 `task_state` 子串误报，`sock`/`socket` 命中仅在横幅/
`about` 文案）。任何「UDP socket 已实现」的表述都是错误的——本课是主题宣告课。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_136_model 与 l143test

```c
struct lesson_136_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_136_model lesson_136_state;
static TEXT64 void l143test(u16*c){lesson_136_state=(struct lesson_136_model){136U,137U,138U,139U,1,1,1,1};int ok=lesson_136_state.valid&&lesson_136_state.active&&lesson_136_state.ready&&lesson_136_state.accounted&&lesson_136_state.b==lesson_136_state.a+1U;text64(c,"l143test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 136 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_136_model`：延续 `{a,b,c,d,valid,active,ready,accounted}` 元数据模型；
- `lesson_136_state`：本课新增模型实例，`a=136U` = 课号 143 − 7，回锚到检查点 L136；
- 断言 `ok`：四个状态位全真 **且** `b==a+1`（`137==136+1`）才为真；
- 输出：前缀 `"l143test: "` + 三目结果——成功串 `"bounded networking, namespaces,
  cgroups, and security checkpoint passed"`，失败串 `"Lesson 136 fallback
  reported"`；
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。断言只校验
  元数据自洽，不执行任何网络代码。

#### 3.2.2 改名：l142test → l135test（保留 lesson_135_model）

```c
static TEXT64 void l135test(u16*c){lesson_135_state=(struct lesson_135_model){135U,136U,137U,138U,1,1,1,1};int ok=lesson_135_state.valid&&lesson_135_state.active&&lesson_135_state.ready&&lesson_135_state.accounted&&lesson_135_state.b==lesson_135_state.a+1U;text64(c,"l135test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 135 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 本课把上一课的 `l142test` 函数**改名**为 `l135test`，模型结构与赋值完全不变
  （`lesson_135_state`、`a=135U`）——改名只动函数名与命令名，不动模型；
- 成功串仍是 `"bounded networking, namespaces, cgroups, and security checkpoint
  passed"`，与 `l143test` 相同（同一网络主题覆盖标签）；
- 失败串 `"Lesson 135 fallback reported"` 跟随模型编号 135，符合
  「`lNNNtest` 操作 `lesson_NNN`」命名约定。

#### 3.2.3 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l135test")){if(!noargs64(arg))usage64(c,"l135test");else l135test(c);}else if(eq64(word,"l143test")){if(!noargs64(arg))usage64(c,"l143test");else l143test(c);}
```

- 本课把上一课的 `l142test` 分支改为 `l135test`，并新增 `l143test` 分支。两条分支
  都先做 `noargs64` 参数检查（带参数则 `usage64`），再调用对应函数。
- **勘误**：旧 README 写的 `Commands: l136test` 与源码不符——源码中 `l136test`
  在本课**不存在**（它要到 Lesson 144 才出现）。本课可用的网络主题 checkpoint
  命令是 **`l135test` 与 `l143test`**（连同继承的 `l130test`–`l134test` 及更早
  l-test）。
- about：`else text64(c,"Lesson 143: UDP socket 状态\n");`；开机横幅：
  `text64(&c,"Lesson 143: UDP socket 状态\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致。

#### 3.2.4 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 未来实现网络机制时将复用的继承件值得点明（源码在
`kernel64.c` 既有部分，非本课新增）：
1. `struct lesson_136_model` 的 `{a,b,c,d}` 四元连续编号：可作 UDP socket 的
   `{local_addr, local_port, remote_addr, remote_port}` 四元组（socket 五元组的
   最简原型）教学映射；
2. `fd_table`/`file_model`（`fd_opens`/`fd_closes` 计数）：socket 作为「类文件描述符」
   的既有骨架（Linux `struct socket` 挂到 `struct file` 上）；
3. `task_state_name`/`enum task_state` 的「状态枚举 + 名称映射」模式：UDP socket
   状态（`SS_UNCONNECTED`/`SS_CONNECTED`）教学映射的直接同构。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`UDP socket 状态`、`l143test`、`Lesson 143`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 143: UDP socket 状态\n..." → shell 循环
  exec64 命令 → l135test/l143test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 143: UDP socket 状态\n")` → 屏幕打印
   `Lesson 143: UDP socket 状态`。
2. **`l143test`** → `l143test(c)` 对 `lesson_136_state` 赋值并五连断言 → 输出
   `l143test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l135test`** → 复用 `lesson_135_model`（L142 的模型改名而来）→ 输出
   `l135test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`——前缀不同（`l135test:`），成功串相同，证明改名不动模型与消息。
4. **`l134test`** → 输出 `l134test: bounded networking, namespaces, cgroups, and
   security checkpoint passed`；**`l130test`** → 输出 `l130test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`——后者的消息仍是并发
   主题标签，体现滚动链从并发切换到网络的边界。
5. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 142
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本、命令名与模型编号。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-143-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `UDP socket 状态`、`Lesson 143` 与 kernel64.c 中的 `l143test`，全部命中输出
  `Multiboot2 and Lesson 143 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 143: UDP socket 状态`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 143: UDP socket 状态`
  2. `l143test` → `l143test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l135test` → `l135test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `l134test` → `l134test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  5. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  6. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。网络主题命令（l143test/
  l135test/l134test）的 passed 串含 "networking, namespaces, cgroups, and
  security"，而 `l130test` 仍是 "concurrency, SMP, RCU"——这是主题滚动链的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l143test` 输出 `l143test: Lesson 136 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l143test` 赋值 `{136U,137U,138U,139U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l135test` 输出失败串 | `lesson_135_model` 被改坏（它是 L142 的模型，本课只改函数名） | 检查 `l135test` 是否仍引用 `lesson_135_state`；比对赋值 `{135U,...}` |
| 输入 `l142test` 提示 unknown command | 本课把 `l142test` 改名 `l135test`，旧命令名已不存在 | `grep -n l142test kernel64.c` 应无命中；改用 `l135test`/`l143test` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`struct sock`/`udp_sendmsg` 等符号不存在 | `diff ../lesson-142-stable/kernel64.c kernel64.c` 确认仅 6 行差异；`grep -n udp_sendmsg kernel64.c` 应无命中 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 143: UDP socket 状态`；`make check` grep 覆盖此串 |
| 输入 `l136test` 提示 unknown command | 旧 README 写错命令名；`l136test` 属于下一课（Lesson 144） | 本课正确的 checkpoint 命令是 `l135test` 与 `l143test` |

---

## 7. 与 Linux 源码对照

1. **socket 双层结构**：Linux `include/linux/net.h` 的 `struct socket`（文件层，含
   `type`、`state`）与 `include/net/sock.h` 的 `struct sock`（协议层，含 `sk_state`、
   `sk_saddr`、`sk_daddr`、`sk_num`、`sk_dport`、`sk_protocol`、`sk_refcnt`）；
   UDP 专用 `struct udp_sock`（`include/net/udp.h`）扩展哈希节点与 `len` 字段。
2. **UDP 收发路径**：Linux `net/ipv4/udp.c` 的 `udp_sendmsg`（组 IP 头、查路由、
   `ip_append_data`）、`udp_recvmsg`（从 `sk_receive_queue` 取数据报）；收包查表
   `udp_v4_lookup_locked` 在 `udp_table` 哈希槽里按（端口，地址）找 `struct
   udp_sock`。TinyOS 本课未实现；教学模型可仿照 `{local_ip,local_port,remote_ip,
   remote_port,state,valid}` 做 socket 记录。
3. **UDP 状态语义**：Linux `include/linux/net.h` 的 `enum socket_state`
   （`SS_UNCONNECTED`/`SS_CONNECTING`/`SS_CONNECTED`/`SS_DISCONNECTING`）与
   `include/net/tcp_states.h` 的 `TCP_CLOSE`/`TCP_ESTABLISHED`——UDP 无握手，
   `connect()` 只固定对端并置 `SS_CONNECTED`/`sk_state=TCP_ESTABLISHED`，
   `bind()` 后即可收发。
4. **socket 作为文件**：Linux `net/socket.c` 中 socket 的 `file->f_op` 指向
   `socket_file_ops`，`fd` 层复用 VFS——TinyOS 既有 `fd_table`/`file_model`
   （`fd_opens`/`fd_closes` 计数）是这一点的教学同构。
5. **诚实性对照**：Linux 的 UDP socket 是真实实现（`net/ipv4/udp.c`、`net/socket.c`），
   而本课消息里的 "networking, namespaces, cgroups, and security" 只是主题声明。
   教学模型不假装网络栈已存在。

**权威来源**：Linux `include/linux/net.h`、`include/net/sock.h`、
`include/net/udp.h`、`net/ipv4/udp.c`、`net/socket.c`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。概念模型中，
Linux UDP socket 涉及哈希表并发、skb 队列、端口复用（SO_REUSEPORT），本课仅保留
「本地/对端四元组 + 状态 + 收发」的核心心智模型。

---

## 8. 思考题与练习

1. **概念理解**：UDP 的「状态」为什么比 TCP 简单得多？`connect()` 对 UDP 意味着什么
   （不产生任何报文）？
2. **源码定位**：在 `kernel64.c` 中找出 `l143test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l143test` 使用的模型结构体名与 `a` 的起始值。
3. **动手实验**：修改 `l143test` 的赋值把 `b` 改成 `136U`（即 `b==a`），重跑观察
   输出翻转为 `l143test: Lesson 136 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-142-stable/kernel64.c kernel64.c | grep -c '^[<>]'`
   确认本课相对上一课的改动行数；用 `grep -n udp kernel64.c` 确认只命中文案。
5. **Linux 对照**：阅读 `net/ipv4/udp.c` 的 `udp_v4_lookup_locked` 与 `udp_table`，
   说明为什么收包要用（端口，地址）查哈希表，并推测 TinyOS 教学模型会如何用固定
   数组模拟 UDP socket 表。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是阶段 6 网络主题 checkpoint 课的第七课，`kernel64.c` 相对 Lesson 142 只有
   3 处小增量（改名 + 新增模型 + 文案）。
2. 「UDP socket 状态」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   没有网络实现符号（`sk_state` 命中均为 `task_state` 子串误报）——这是必须如实
   声明的边界。
3. 新增 `lesson_136_model`/`l143test`，`a` 从 `136U` 起头回锚到 Lesson 136（课号−7）；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l142test` 改名 `l135test`，模型 `lesson_135_model` 与赋值不变，证明改名不动模型。
5. 概念模型上，本课建立了「UDP socket（socket/sock 双层、SS_UNCONNECTED/
   SS_CONNECTED 状态）」的 Linux 心智模型，并点明 `{a,b,c,d}` 可映射 socket 的
   本地/对端四元组。
6. 旧 README 的 `Commands: l136test` 已勘误：本课源码实际的 checkpoint 命令是
   `l135test` 与 `l143test`（`l136test` 属于 Lesson 144）。

**下一课**：[`lesson-144-stable/README.md`](../lesson-144-stable/README.md) 主题为
「socket 端口分配」——网络主题 checkpoint 课的第八课。它将在本课 socket 状态概念
之上，把「端口分配（bind 端口冲突、端口范围、bind bucket）」作为新的主题标签与
概念模型，衔接点是 socket 四元组里的「端口」分量（`sk_num`/`sk_dport`）。

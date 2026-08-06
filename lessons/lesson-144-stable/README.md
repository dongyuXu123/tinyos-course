# Lesson 144: socket 端口分配 — 精讲文档

> **课号**：Lesson 144（可执行课，checkpoint 快照）
> **主题**：socket 端口分配——阶段 6（网络主题）checkpoint 课的第八课（该 checkpoint
> 序列的收官课）。banner/about 宣告「socket 端口分配」，checkpoint 消息继续沿用网络/
> 命名空间/cgroup/安全覆盖标签，并追加 checkpoint 模型 `lesson_137_model`/`l144test`。
> **课程主线位置**：网络主题 checkpoint 序列的最后一课。位于 Lesson 143（UDP socket
> 状态）之后，后续衔接网络主题机制实现课。
> **前置课程**：[`lesson-143-stable/README.md`](../lesson-143-stable/README.md)
> **后续课程**：阶段 6 网络主题的机制实现课（checkpoint 序列之后的课程）
> **一句话目标**：学完本课你能准确区分「主题宣告课」与「机制实现课」——本课源码里
> 「socket 端口分配」只是 about/banner 文案与 checkpoint 覆盖标签，源码中**没有**
> `inet_bind_bucket`/`udp_v4_get_port`/`inet_csk_get_port` 等任何网络实现符号；
> 本课把 Linux 的「socket 端口分配」机制作为**概念模型**精讲，并掌握 `l144test`
> 的断言语义。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 143 只有
3 处小增量（`l143test`→`l136test` 改名、新增 `struct lesson_137_model`/`l144test`、
about/banner 文案换成「socket 端口分配」），**没有新增任何网络代码**；理解本课
checkpoint 模型 `l144test` 的断言与消息文本语义，并把「socket 端口分配」作为 Linux
网络栈 L4 层「端口资源管理」的核心概念模型来学习。

- **在课程主线中的位置**：阶段 6 网络主题 checkpoint 课序列的最后一课（L138 立主题、
  L139 接口/链路、L140 队列/记账、L141 loopback、L142 IPv4 地址、L143 UDP socket、
  L144 端口分配）。banner/about 文案「socket 端口分配」+ checkpoint 成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 标识
  本课主题；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 143：UDP socket 双层结构（`struct socket`/`struct sock`）与状态概念——
     「端口」是 socket 四元组里的关键分量；
  2. TCP/UDP 端口号范围（0–65535）、临时端口（ephemeral port）概念；
  3. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定；
  4. 「模型编号 = 课号 − 7」回锚链与 `lNNNtest`/`lesson_NNN` 命名约定。
- **本课交付**：`l136test`（改名）、`l144test`（新增）两个 checkpoint 命令；`about`
  文案「socket 端口分配」；对 Linux「socket 端口分配」机制的完整概念模型。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课与「滚动标签链」的收官

**直觉**：阶段 6 的每一课（L138–L144）都是 checkpoint 快照：`kernel64.c` 只换
banner/about 文案 + checkpoint 模型，机制代码不动。Lesson 144 的主题标签是
「socket 端口分配」，是滚动标签链的**收官环**——L138 宣告主题后，L139–L144 依次
覆盖「接口/链路 → 队列/记账 → loopback → IPv4 地址 → UDP socket → 端口分配」，
把网络栈自底向上扫了一遍。

**如何确认（自查清单）**：
1. `diff ../lesson-143-stable/kernel64.c kernel64.c` 只有 6 行实质变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `inet_bind_bucket`/`udp_v4_get_port`/`inet_csk_get_port`/`bind_conflict`
   等网络符号——**不存在**（`grep port kernel64.c` 的命中均为 support/report/import
   等英文单词的子串误报）；
3. 唯一带 "networking" 字样的是 checkpoint 成功串（覆盖标签）。

**为什么需要这样的课**：端口分配是 socket 生命周期中 `bind()` 的关键环节——本地
端口是四元组的一部分，分配错了（冲突）就收不到包。课程以此收官 checkpoint 序列，
让学习者带着「接口→L3→L4→端口」的完整心智地图进入网络机制实现课。

### 2.2 概念二：端口分配（port allocation）是什么

**定义**：socket 建立后需要一个本地端口号才能收发。端口分配就是 `bind()` 时为
socket 确定并登记本地端口：显式指定（如 `bind(…, port=8080)`）或由内核从临时端口
范围自动挑选（`bind(…, port=0)`）。

**为什么需要**：端口号（16 位，0–65535）是共享资源——同一 IP 上两个 socket 不能
占用同一本地端口（除非 SO_REUSEPORT 等显式复用），否则收包无法区分归属。分配机制
的核心职责是**查重**（冲突检测）与**挑选**（自动分配时找一个空闲端口）。

**工作机制**：`bind()` 进入 `inet_bind`（`net/ipv4/af_inet.c`）→ 按协议分派：
UDP 走 `udp_v4_get_port`（`net/ipv4/udp.c`），TCP 走 `inet_csk_get_port`
（`net/ipv4/inet_connection_sock.c`）→ 在端口表/哈希桶里查目标端口是否已被占用 →
空闲则登记（挂入 `inet_bind_bucket`/UDP 端口哈希节点），占用则报 `EADDRINUSE`
或（临时端口场景）继续扫描下一个。

### 2.3 概念三：bind bucket 与临时端口范围

**定义**：Linux 用 `struct inet_bind_bucket`（`include/net/inet_hashtables.h`，
`{port, owners, …}`）登记「一个本地端口被哪些 socket 占用」；TCP 的 `bind_bucket`
挂在 `bind_bucket_hashinfo` 哈希表上。临时端口范围由 `net.ipv4.ip_local_port_range`
控制（默认 32768–60999）。

**为什么需要**：把「端口占用」建成 bucket（一端口一桶、桶里挂 owners 链表），
冲突检测就是一次哈希查找 + 遍历桶内 owners——比全表扫描高效得多。临时端口范围
避免与知名端口（<1024）和常用服务端口重叠。

**工作机制**：`inet_csk_get_port` 里 `inet_csk_find_port` 从
`inet_sk(sk)->inet_num`（已指定端口）或 `sysctl_local_port_range`（自动分配）出发，
用 `inet_bind_bucket` 查重；`inet_csk_bind_conflict`（
`net/ipv4/inet_connection_sock.c`）检查复用冲突。UDP 侧 `udp_lib_get_port`
（`net/ipv4/udp.c`）走 `udp_table` 的端口哈希链做同样的事。

### 2.4 概念四：本课 checkpoint 模型

`struct lesson_137_model` 的 `a` 从 `137U` 起头 = 课号 144 − 7，延续回锚链
（L138→131、…、L144→137）。`l144test` 五连断言输出成功串 `l144test: bounded
networking, namespaces, cgroups, and security checkpoint passed` 或失败串
`l144test: Lesson 137 fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 143） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（991 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l143test`→`l136test`；新增 `struct lesson_137_model`、`l144test`；exec64 增加 `l136test`/`l144test` 分支；about/banner 文案换为「socket 端口分配」 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `socket 端口分配`、`l144test`、`Lesson 144` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 143 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `inet_bind_bucket`、`udp_v4_get_port`、`inet_csk_get_port`
等网络实现符号。任何「socket 端口分配已实现」的表述都是错误的——本课是主题宣告课。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_137_model 与 l144test

```c
struct lesson_137_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_137_model lesson_137_state;
static TEXT64 void l144test(u16*c){lesson_137_state=(struct lesson_137_model){137U,138U,139U,140U,1,1,1,1};int ok=lesson_137_state.valid&&lesson_137_state.active&&lesson_137_state.ready&&lesson_137_state.accounted&&lesson_137_state.b==lesson_137_state.a+1U;text64(c,"l144test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 137 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_137_model`：延续 `{a,b,c,d,valid,active,ready,accounted}` 元数据模型；
- `lesson_137_state`：本课新增模型实例，`a=137U` = 课号 144 − 7，回锚到检查点 L137；
- 断言 `ok`：四个状态位全真 **且** `b==a+1`（`138==137+1`）才为真；
- 输出：前缀 `"l144test: "` + 三目结果——成功串 `"bounded networking, namespaces,
  cgroups, and security checkpoint passed"`，失败串 `"Lesson 137 fallback
  reported"`；
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。断言只校验
  元数据自洽，不执行任何网络代码。

#### 3.2.2 改名：l143test → l136test（保留 lesson_136_model）

```c
static TEXT64 void l136test(u16*c){lesson_136_state=(struct lesson_136_model){136U,137U,138U,139U,1,1,1,1};int ok=lesson_136_state.valid&&lesson_136_state.active&&lesson_136_state.ready&&lesson_136_state.accounted&&lesson_136_state.b==lesson_136_state.a+1U;text64(c,"l136test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 136 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 本课把上一课的 `l143test` 函数**改名**为 `l136test`，模型结构与赋值完全不变
  （`lesson_136_state`、`a=136U`）——改名只动函数名与命令名，不动模型；
- 成功串仍是 `"bounded networking, namespaces, cgroups, and security checkpoint
  passed"`，与 `l144test` 相同（同一网络主题覆盖标签）；
- 失败串 `"Lesson 136 fallback reported"` 跟随模型编号 136，符合
  「`lNNNtest` 操作 `lesson_NNN`」命名约定。

#### 3.2.3 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l136test")){if(!noargs64(arg))usage64(c,"l136test");else l136test(c);}else if(eq64(word,"l144test")){if(!noargs64(arg))usage64(c,"l144test");else l144test(c);}
```

- 本课把上一课的 `l143test` 分支改为 `l136test`，并新增 `l144test` 分支。两条分支
  都先做 `noargs64` 参数检查（带参数则 `usage64`），再调用对应函数。
- **勘误**：旧 README 写的 `Commands: l137test` 与源码不符——源码中 `l137test`
  在本课**不存在**（该命令名未在任何课程出现过；本课的模型是 `lesson_137_model`
  ≠ 命令 `l137test`）。本课可用的网络主题 checkpoint 命令是 **`l136test` 与
  `l144test`**（连同继承的 `l130test`–`l135test` 及更早 l-test）。
- about：`else text64(c,"Lesson 144: socket 端口分配\n");`；开机横幅：
  `text64(&c,"Lesson 144: socket 端口分配\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致。

#### 3.2.4 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 未来实现网络机制时将复用的继承件值得点明（源码在
`kernel64.c` 既有部分，非本课新增）：
1. `struct lesson_137_model` 的 `{a,b,c,d}` 连续编号：可作端口分配教学模型的
   `{port, owner_sock, conflict, valid}` 最简原型；
2. `pmm_alloc`/`pmm_free_page` 的位图「占用/释放」：端口位图（16 位端口空间）的
   既有同构——`pmm_bitmap` 的 bit 置位/清除语义与「端口是否被占用」完全一致；
3. `module_model`/`symbol_model` 的「登记-查询-冲突」模式：`bind_bucket` 查重与
   端口登记的骨架。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`socket 端口分配`、`l144test`、`Lesson 144`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 144: socket 端口分配\n..." → shell 循环
  exec64 命令 → l136test/l144test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 144: socket 端口分配\n")` → 屏幕打印
   `Lesson 144: socket 端口分配`。
2. **`l144test`** → `l144test(c)` 对 `lesson_137_state` 赋值并五连断言 → 输出
   `l144test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l136test`** → 复用 `lesson_136_model`（L143 的模型改名而来）→ 输出
   `l136test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`——前缀不同（`l136test:`），成功串相同，证明改名不动模型与消息。
4. **`l135test`** → 输出 `l135test: bounded networking, namespaces, cgroups, and
   security checkpoint passed`；**`l130test`** → 输出 `l130test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`——后者的消息仍是并发
   主题标签，体现滚动链从并发切换到网络的边界。
5. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 143
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本、命令名与模型编号。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-144-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `socket 端口分配`、`Lesson 144` 与 kernel64.c 中的 `l144test`，全部命中输出
  `Multiboot2 and Lesson 144 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 144: socket 端口分配`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 144: socket 端口分配`
  2. `l144test` → `l144test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l136test` → `l136test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `l135test` → `l135test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  5. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  6. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。网络主题命令（l144test/
  l136test/l135test）的 passed 串含 "networking, namespaces, cgroups, and
  security"，而 `l130test` 仍是 "concurrency, SMP, RCU"——这是主题滚动链的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l144test` 输出 `l144test: Lesson 137 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l144test` 赋值 `{137U,138U,139U,140U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l136test` 输出失败串 | `lesson_136_model` 被改坏（它是 L143 的模型，本课只改函数名） | 检查 `l136test` 是否仍引用 `lesson_136_state`；比对赋值 `{136U,...}` |
| 输入 `l143test` 提示 unknown command | 本课把 `l143test` 改名 `l136test`，旧命令名已不存在 | `grep -n l143test kernel64.c` 应无命中；改用 `l136test`/`l144test` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`inet_bind_bucket`/`udp_v4_get_port` 等符号不存在 | `diff ../lesson-143-stable/kernel64.c kernel64.c` 确认仅 6 行差异；`grep -n bind_bucket kernel64.c` 应无命中 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 144: socket 端口分配`；`make check` grep 覆盖此串 |
| 输入 `l137test` 提示 unknown command | 旧 README 写错命令名；`l137test` 从未在任何课程源码中出现 | 本课正确的 checkpoint 命令是 `l136test` 与 `l144test`；模型名 `lesson_137_model` ≠ 命令名 |

---

## 7. 与 Linux 源码对照

1. **bind 入口**：Linux `net/ipv4/af_inet.c` 的 `inet_bind`——把用户给的
   `sockaddr_in` 里的端口/地址写入 socket，再按协议分派到
   `udp_v4_get_port`（UDP）或 `inet_csk_get_port`（TCP）。
2. **bind bucket**：Linux `include/net/inet_hashtables.h` 的
   `struct inet_bind_bucket`——`{port, owners}` 一端口一桶；`net/ipv4/inet_hashtables.c`
   的 `inet_bind_bucket_create`/`inet_bind_hash` 负责建桶与挂 socket；
   `net/ipv4/inet_connection_sock.c` 的 `inet_csk_find_port` 负责查桶挑端口。
3. **冲突检测**：Linux `net/ipv4/inet_connection_sock.c` 的 `inet_csk_bind_conflict`
   ——遍历桶内 owners，比较地址/端口/复用标志，冲突返回真 → `bind()` 报
   `EADDRINUSE`。UDP 侧 `net/ipv4/udp.c` 的 `udp_lib_get_port` 沿 `udp_table`
   端口哈希链做同样检查。
4. **临时端口范围**：Linux `sysctl_ip_local_port_range`（`net.ipv4.ip_local_port_range`，
   默认 32768–60999）——`bind(port=0)` 时内核在此范围内扫描空闲端口，避免与
   <1024 的知名端口冲突。
5. **诚实性对照**：Linux 的端口分配是真实实现（哈希桶 + 冲突遍历），而本课消息里
   的 "networking, namespaces, cgroups, and security" 只是主题声明。教学模型不假装
   网络栈已存在。

**权威来源**：Linux `net/ipv4/af_inet.c`、`net/ipv4/inet_hashtables.c`、
`net/ipv4/inet_connection_sock.c`、`net/ipv4/udp.c`、`include/net/inet_hashtables.h`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。概念模型中，
Linux 端口分配涉及哈希桶、l3mdev、SO_REUSEPORT 等并发细节，本课仅保留
「端口查重 + 自动挑选 + 冲突报错」的核心心智模型。

---

## 8. 思考题与练习

1. **概念理解**：`bind(port=0)` 时内核在什么范围挑端口？为什么临时端口默认从 32768
   开始而不是从 1024 开始？
2. **源码定位**：在 `kernel64.c` 中找出 `l144test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l144test` 使用的模型结构体名与 `a` 的起始值。
3. **动手实验**：修改 `l144test` 的赋值把 `b` 改成 `137U`（即 `b==a`），重跑观察
   输出翻转为 `l144test: Lesson 137 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-143-stable/kernel64.c kernel64.c | grep -c '^[<>]'`
   确认本课相对上一课的改动行数；用 `grep -n bind_bucket kernel64.c` 确认无网络符号。
5. **Linux 对照**：阅读 `net/ipv4/inet_connection_sock.c` 的 `inet_csk_find_port` 与
   `include/net/inet_hashtables.h` 的 `struct inet_bind_bucket`，说明「一端口一桶 +
   owners 链表」相对逐端口位图扫描的优势，并推测 TinyOS 教学模型会采用哪种简化。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是阶段 6 网络主题 checkpoint 课的最后一课，`kernel64.c` 相对 Lesson 143 只有
   3 处小增量（改名 + 新增模型 + 文案）。
2. 「socket 端口分配」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   没有网络实现符号——这是必须如实声明的边界。
3. 新增 `lesson_137_model`/`l144test`，`a` 从 `137U` 起头回锚到 Lesson 137（课号−7）；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l143test` 改名 `l136test`，模型 `lesson_136_model` 与赋值不变，证明改名不动模型。
5. 概念模型上，本课建立了「端口分配（bind_bucket 查重、临时端口范围、EADDRINUSE）」
   的 Linux 心智模型，补齐了「接口→队列→loopback→IPv4→UDP→端口」的完整网络栈
   心智地图，为网络机制实现课做好概念铺垫。
6. 旧 README 的 `Commands: l137test` 已勘误：本课源码实际的 checkpoint 命令是
   `l136test` 与 `l144test`（`l137test` 从未作为命令出现过，模型名 `lesson_137`
   与命令名是两回事）。

**下一课**：网络主题机制实现课。checkpoint 序列（L138–L144）宣告了网络栈六大主题
标签（接口/链路、队列/记账、loopback、IPv4 地址、UDP socket、端口分配），下一阶段
将把这些主题逐一落实为教学模型代码——衔接点是本课与 L139–L143 建立的六个概念
模型，以及 `pmm_alloc`/`pipe_model`/`softirq_model` 等继承骨架。

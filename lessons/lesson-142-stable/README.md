# Lesson 142: IPv4 地址元数据 — 精讲文档

> **课号**：Lesson 142（可执行课，checkpoint 快照）
> **主题**：IPv4 地址元数据——阶段 6（网络主题）checkpoint 课的第六课。banner/about
> 宣告「IPv4 地址元数据」，checkpoint 消息继续沿用网络/命名空间/cgroup/安全覆盖标签，
> 并追加 checkpoint 模型 `lesson_135_model`/`l142test`。
> **课程主线位置**：网络主题 checkpoint 序列。位于 Lesson 141（loopback 接口）之后、
> Lesson 143（UDP socket 状态）之前。
> **前置课程**：[`lesson-141-stable/README.md`](../lesson-141-stable/README.md)
> **后续课程**：[`lesson-143-stable/README.md`](../lesson-143-stable/README.md)
> **一句话目标**：学完本课你能准确区分「主题宣告课」与「机制实现课」——本课源码里
> 「IPv4 地址元数据」只是 about/banner 文案与 checkpoint 覆盖标签，源码中**没有**
> `in_ifaddr`/`ifa_local`/`devinet` 等任何网络实现符号；本课把 Linux 的「IPv4 地址
> 元数据」机制作为**概念模型**精讲，并掌握 `l142test` 的断言语义。

---

## 1. 课程定位（Mission）

**一句话目标**：以「如实讲解」的方式定位本课：`kernel64.c` 相对 Lesson 141 只有
3 处小增量（`l141test`→`l134test` 改名、新增 `struct lesson_135_model`/`l142test`、
about/banner 文案换成「IPv4 地址元数据」），**没有新增任何网络代码**；理解本课
checkpoint 模型 `l142test` 的断言与消息文本语义，并把「IPv4 地址元数据」作为 Linux
网络栈 L3 层的核心概念模型来学习。

- **在课程主线中的位置**：阶段 6 网络主题 checkpoint 课序列的第六课（L138 立主题、
  L139 接口/链路、L140 队列/记账、L141 loopback、L142 IPv4 地址、L143 UDP socket、
  L144 端口分配）。banner/about 文案「IPv4 地址元数据」+ checkpoint 成功串
  `bounded networking, namespaces, cgroups, and security checkpoint passed` 标识
  本课主题；机制仍全部继承自早期课程。
- **前置知识清单**：
  1. Lesson 141：loopback 接口与其 `127.0.0.1/8` 地址概念；
  2. 32 位 IPv4 地址、子网掩码、前缀长度（prefixlen）的基础网络知识；
  3. 检查点模型结构 `{a,b,c,d,valid,active,ready,accounted}` 与五连断言约定；
  4. 「模型编号 = 课号 − 7」回锚链与 `lNNNtest`/`lesson_NNN` 命名约定。
- **本课交付**：`l134test`（改名）、`l142test`（新增）两个 checkpoint 命令；`about`
  文案「IPv4 地址元数据」；对 Linux「IPv4 地址元数据」机制的完整概念模型。

---

## 2. 核心概念精讲

### 2.1 概念一：主题宣告课与「滚动标签链」的延续

**直觉**：阶段 6 的每一课（L138–L144）都是 checkpoint 快照：`kernel64.c` 只换
banner/about 文案 + checkpoint 模型，机制代码不动。Lesson 142 的主题标签是
「IPv4 地址元数据」，是滚动标签链的第六环。

**如何确认（自查清单）**：
1. `diff ../lesson-141-stable/kernel64.c kernel64.c` 只有 6 行实质变化（改名 + 新增
   模型 + 文案）；
2. 源码中搜索 `in_ifaddr`/`ifa_local`/`devinet`/`inetdev`/`ipv4` 等网络符号——
   **不存在**；
3. 唯一带 "networking" 字样的是 checkpoint 成功串（覆盖标签）。

**为什么需要这样的课**：有了接口与 loopback，下一步自然是「给接口配地址」——这是
L3 层（IP）工作的起点：地址决定了接口属于哪个子网、能否路由。课程在此用主题宣告
立标签、用文档层建模型，为 L143（UDP socket）与 L144（端口分配）铺路。

### 2.2 概念二：IPv4 地址元数据（in_ifaddr）

**定义**：Linux 用 `struct in_ifaddr`（`include/net/inetdevice.h`）描述一个接口上的
**一个** IPv4 地址及其全部元数据：`ifa_local`（本机地址）、`ifa_mask`（子网掩码）、
`ifa_broadcast`（广播地址）、`ifa_prefixlen`（前缀长度）、`ifa_scope`（作用域：
host/link/global）、`ifa_flags`、`ifa_list`（同一接口多地址链表）。每个接口还有一个
`struct in_device`（`idev`）持有该接口的 IPv4 配置与地址链表头。

**为什么需要**：仅有一个 32 位数（如 192.168.1.5）不足以回答三个问题：这个地址属于
哪个子网（需要 mask/prefixlen）？广播地址是多少（需要按掩码计算）？它能被外界看到吗
（需要 scope）？地址元数据就是这些「配套信息」的载体。

**工作机制**（配置路径）：用户执行 `ip addr add 192.168.1.5/24 dev eth0` →
netlink `RTM_NEWADDR` → `inet_rtm_newaddr`（`net/ipv4/devinet.c`）校验 → 构造
`struct in_ifaddr` → `inet_insert_ifa` 挂到 `in_device->ifa_list` → 发
`NETDEV_CHANGEADDR` 通知 → 路由/邻居/ARP 按新地址工作。

### 2.3 概念三：地址作用域（scope）与回环地址

**定义**：`ifa_scope` 描述地址的可见范围：`RT_SCOPE_HOST`（仅本机可见，如
127.0.0.1）、`RT_SCOPE_LINK`（仅本链路可见，如链路本地 169.254.x.x）、
`RT_SCOPE_UNIVERSE`（全局可路由）。

**为什么需要**：路由查找时，scope 决定「这个地址能否作为源地址对外通信」——
loopback 的 127.0.0.1 是 HOST scope，永远不能作为包发到物理网络的源地址。
`ipv4_is_loopback`（`include/linux/inetdevice.h`，判断 `IN_LOOPBACKNET` =
`127.0.0.0/8`）专门用于识别回环地址。

**工作机制**：地址配置时内核按规则自动填 scope——loopback 前缀（127/8）默认
HOST，链路本地（169.254/16）默认 LINK，其余通常 GLOBAL；路由缓存与 `fib_lookup`
在比较源/目的时使用该字段。

### 2.4 概念四：本课 checkpoint 模型

`struct lesson_135_model` 的 `a` 从 `135U` 起头 = 课号 142 − 7，延续回锚链
（L138→131、…、L142→135）。`l142test` 五连断言输出成功串 `l142test: bounded
networking, namespaces, cgroups, and security checkpoint passed` 或失败串
`l142test: Lesson 135 fallback reported`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 141） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（985 行）：四族机制 + 全部 test/info 命令 + checkpoint | `l141test`→`l134test`；新增 `struct lesson_135_model`、`l142test`；exec64 增加 `l134test`/`l142test` 分支；about/banner 文案换为「IPv4 地址元数据」 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `IPv4 地址元数据`、`l142test`、`Lesson 142` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

**重要声明**：本课所有文件相对 Lesson 141 的差异仅为 checkpoint 模型/命令/文案；
`kernel64.c` 中**没有** `in_ifaddr`、`in_device`、`devinet`、`ipv4` 等网络实现符号。
任何「IPv4 地址元数据已实现」的表述都是错误的——本课是主题宣告课。

### 3.2 kernel64.c：本课增量精讲

#### 3.2.1 本课新增 checkpoint：lesson_135_model 与 l142test

```c
struct lesson_135_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_135_model lesson_135_state;
static TEXT64 void l142test(u16*c){lesson_135_state=(struct lesson_135_model){135U,136U,137U,138U,1,1,1,1};int ok=lesson_135_state.valid&&lesson_135_state.active&&lesson_135_state.ready&&lesson_135_state.accounted&&lesson_135_state.b==lesson_135_state.a+1U;text64(c,"l142test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 135 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `struct lesson_135_model`：延续 `{a,b,c,d,valid,active,ready,accounted}` 元数据模型；
- `lesson_135_state`：本课新增模型实例，`a=135U` = 课号 142 − 7，回锚到检查点 L135；
- 断言 `ok`：四个状态位全真 **且** `b==a+1`（`136==135+1`）才为真；
- 输出：前缀 `"l142test: "` + 三目结果——成功串 `"bounded networking, namespaces,
  cgroups, and security checkpoint passed"`，失败串 `"Lesson 135 fallback
  reported"`；
- **消息语义（如实说明）**：成功串里的 "networking, namespaces, cgroups, and
  security" 是阶段 6 网络主题的**覆盖标签**，不是本函数实现的功能。断言只校验
  元数据自洽，不执行任何网络代码。

#### 3.2.2 改名：l141test → l134test（保留 lesson_134_model）

```c
static TEXT64 void l134test(u16*c){lesson_134_state=(struct lesson_134_model){134U,135U,136U,137U,1,1,1,1};int ok=lesson_134_state.valid&&lesson_134_state.active&&lesson_134_state.ready&&lesson_134_state.accounted&&lesson_134_state.b==lesson_134_state.a+1U;text64(c,"l134test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 134 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 本课把上一课的 `l141test` 函数**改名**为 `l134test`，模型结构与赋值完全不变
  （`lesson_134_state`、`a=134U`）——改名只动函数名与命令名，不动模型；
- 成功串仍是 `"bounded networking, namespaces, cgroups, and security checkpoint
  passed"`，与 `l142test` 相同（同一网络主题覆盖标签）；
- 失败串 `"Lesson 134 fallback reported"` 跟随模型编号 134，符合
  「`lNNNtest` 操作 `lesson_NNN`」命名约定。

#### 3.2.3 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l134test")){if(!noargs64(arg))usage64(c,"l134test");else l134test(c);}else if(eq64(word,"l142test")){if(!noargs64(arg))usage64(c,"l142test");else l142test(c);}
```

- 本课把上一课的 `l141test` 分支改为 `l134test`，并新增 `l142test` 分支。两条分支
  都先做 `noargs64` 参数检查（带参数则 `usage64`），再调用对应函数。
- **勘误**：旧 README 写的 `Commands: l135test` 与源码不符——源码中 `l135test`
  在本课**不存在**（它要到 Lesson 143 才出现）。本课可用的网络主题 checkpoint
  命令是 **`l134test` 与 `l142test`**（连同继承的 `l130test`–`l133test` 及更早
  l-test）。
- about：`else text64(c,"Lesson 142: IPv4 地址元数据\n");`；开机横幅：
  `text64(&c,"Lesson 142: IPv4 地址元数据\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 命令集：除 checkpoint 外，其余命令与前课完全一致。

#### 3.2.4 未来网络机制的「种子」：继承的元数据模式

本课虽然没有网络代码，但阶段 6 未来实现网络机制时将复用的继承件值得点明（源码在
`kernel64.c` 既有部分，非本课新增）：
1. `vma_model`/`page_cache_model` 的「结构体数组 + valid/refs 位 + 计数」模式：
   未来 `in_ifaddr` 地址表（`{addr, mask, prefixlen, scope, flags, valid}`）的
   直接同构；
2. `struct lesson_135_model` 本身的 `{a,b,c,d}` 四元连续编号：可作为「IP 地址 +
   掩码 + 广播 + scope」教学元数据的最简原型；
3. `module_model`/`symbol_model` 的「登记-查询」模式：`devinet` 按名字/索引查接口
   地址的骨架。

### 3.3 构建管线（Makefile / linker）

- 构建流程与前课完全一致（`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`；`kernel64.ld` 三块
  guard+stack + ASSERT）。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`IPv4 地址元数据`、`l142test`、`Lesson 142`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) → enter_long_mode (boot.S)
  → kernel_main64_binary (kernel64.c)
       cpu_locals → pmm_init → vma_init/reclaim_init → vfs_init
       → active_sched_class → IDT/PIT/PIC
       → 横幅 "Lesson 142: IPv4 地址元数据\n..." → shell 循环
  exec64 命令 → l134test/l142test:checkpoint 断言（消息覆盖网络主题标签）
             → 其余命令与前课一致（内存/调度/并发/原语各 test+info）
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径：

1. **`about`** → `text64(c,"Lesson 142: IPv4 地址元数据\n")` → 屏幕打印
   `Lesson 142: IPv4 地址元数据`。
2. **`l142test`** → `l142test(c)` 对 `lesson_135_state` 赋值并五连断言 → 输出
   `l142test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`。
3. **`l134test`** → 复用 `lesson_134_model`（L141 的模型改名而来）→ 输出
   `l134test: bounded networking, namespaces, cgroups, and security checkpoint
   passed`——前缀不同（`l134test:`），成功串相同，证明改名不动模型与消息。
4. **`l133test`** → 输出 `l133test: bounded networking, namespaces, cgroups, and
   security checkpoint passed`；**`l130test`** → 输出 `l130test: bounded
   concurrency, SMP, RCU, and diagnostics checkpoint passed`——后者的消息仍是并发
   主题标签，体现滚动链从并发切换到网络的边界。
5. 继承命令（如 `reclaimtest`、`pctest`+`pcgo`、`softirqtest`）行为与 Lesson 141
   一致。

数据流要点：本课没有新的数据流——所有机制路径与前课相同；唯一变化是 checkpoint
消息文本、命令名与模型编号。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-142-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `IPv4 地址元数据`、`Lesson 142` 与 kernel64.c 中的 `l142test`，全部命中输出
  `Multiboot2 and Lesson 142 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动
  横幅第一行为 `Lesson 142: IPv4 地址元数据`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 142: IPv4 地址元数据`
  2. `l142test` → `l142test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l134test` → `l134test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `l133test` → `l133test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  5. `l130test` → `l130test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  6. （回归抽查）`reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit
     model passed`；`softirqtest` → `softirqtest: tasklet coalescing, FIFO work, and
     budget carry-over passed`
- **如何判断成功**：上述命令逐一打印预期串即成功。网络主题命令（l142test/
  l134test/l133test）的 passed 串含 "networking, namespaces, cgroups, and
  security"，而 `l130test` 仍是 "concurrency, SMP, RCU"——这是主题滚动链的直接证据。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l142test` 输出 `l142test: Lesson 135 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l142test` 赋值 `{135U,136U,137U,138U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `l134test` 输出失败串 | `lesson_134_model` 被改坏（它是 L141 的模型，本课只改函数名） | 检查 `l134test` 是否仍引用 `lesson_134_state`；比对赋值 `{134U,...}` |
| 输入 `l141test` 提示 unknown command | 本课把 `l141test` 改名 `l134test`，旧命令名已不存在 | `grep -n l141test kernel64.c` 应无命中；改用 `l134test`/`l142test` |
| 误以为本课有网络代码而搜不到 | 本课是主题宣告课，`in_ifaddr`/`devinet` 等符号不存在 | `diff ../lesson-141-stable/kernel64.c kernel64.c` 确认仅 6 行差异；`grep -n inetdev kernel64.c` 应无命中 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 142: IPv4 地址元数据`；`make check` grep 覆盖此串 |
| 输入 `l135test` 提示 unknown command | 旧 README 写错命令名；`l135test` 属于下一课（Lesson 143） | 本课正确的 checkpoint 命令是 `l134test` 与 `l142test` |

---

## 7. 与 Linux 源码对照

1. **IPv4 地址对象**：Linux `include/net/inetdevice.h` 的 `struct in_ifaddr`——
   `ifa_local`（本机地址）、`ifa_mask`（掩码）、`ifa_broadcast`（广播）、
   `ifa_prefixlen`、`ifa_scope`、`ifa_flags`、`ifa_list`；接口侧由 `struct
   in_device`（`idev`）持有 `ifa_list` 头。TinyOS 本课未实现；教学模型可仿照
   `{addr, mask, prefixlen, scope, flags, valid}` 做地址记录。
2. **地址配置路径**：Linux `net/ipv4/devinet.c` 的 `inet_rtm_newaddr` →
   `inet_insert_ifa`——校验地址合法性、构造 `in_ifaddr`、挂入 `ifa_list`、广播
   `NETDEV_CHANGEADDR`。用户态等价命令是 `ip addr add`。
3. **地址作用域**：Linux `include/linux/rtnetlink.h` 的 `RT_SCOPE_HOST`/
   `RT_SCOPE_LINK`/`RT_SCOPE_UNIVERSE`——决定地址的对外可见性；loopback 的
   127.0.0.1 恒为 HOST scope，不能作为物理网卡源地址。
4. **回环识别**：Linux `include/linux/inetdevice.h` 的 `ipv4_is_loopback`——判断
   `127.0.0.0/8`（`IN_LOOPBACKNET`）；配合 `net/ipv4/devinet.c` 在 `inetdev_init`
   中为 `lo` 默认配置 127.0.0.1/8。这是 L141 loopback 与 L142 地址元数据的天然衔接。
5. **诚实性对照**：Linux 的地址元数据是真实数据结构（`in_ifaddr` 挂载在 `idev`），
   而本课消息里的 "networking, namespaces, cgroups, and security" 只是主题声明。
   教学模型不假装网络栈已存在。

**权威来源**：Linux `include/net/inetdevice.h`、`include/linux/inetdevice.h`、
`net/ipv4/devinet.c`、`include/linux/rtnetlink.h`。
**教学模型简化了什么**：本课没有任何网络实现；主题转向只发生在文本层。概念模型中，
Linux 地址管理涉及 netlink、路由表联动（`fib`）、地址冲突检测，本课仅保留
「地址 + 掩码 + 广播 + scope」四元元数据的心智模型。

---

## 8. 思考题与练习

1. **概念理解**：为什么 127.0.0.1 的 scope 必须是 HOST？如果把它配成 GLOBAL 会出现
   什么语义问题？
2. **源码定位**：在 `kernel64.c` 中找出 `l142test` 的成功串与失败串，说明它们分别
   在什么条件下输出；再指出 `l142test` 使用的模型结构体名与 `a` 的起始值。
3. **动手实验**：修改 `l142test` 的赋值把 `b` 改成 `135U`（即 `b==a`），重跑观察
   输出翻转为 `l142test: Lesson 135 fallback reported`；再改回。
4. **动手实验**：`diff ../lesson-141-stable/kernel64.c kernel64.c | grep -c '^[<>]'`
   确认本课相对上一课的改动行数；用 `grep -n inetdev kernel64.c` 确认无网络符号。
5. **Linux 对照**：阅读 `net/ipv4/devinet.c` 的 `inet_insert_ifa`，说明为什么地址
   要挂链表（一个接口可以配多个地址，如 127.0.0.1 与 ::1 同属 lo），并推测 TinyOS
   教学模型会如何用固定数组模拟 `ifa_list`。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是阶段 6 网络主题 checkpoint 课的第六课，`kernel64.c` 相对 Lesson 141 只有
   3 处小增量（改名 + 新增模型 + 文案）。
2. 「IPv4 地址元数据」是 about/banner 与 checkpoint 消息文本的主题宣告，源码中
   没有网络实现符号——这是必须如实声明的边界。
3. 新增 `lesson_135_model`/`l142test`，`a` 从 `135U` 起头回锚到 Lesson 135（课号−7）；
   成功串 `bounded networking, namespaces, cgroups, and security checkpoint
   passed` 是网络主题的覆盖标签。
4. `l141test` 改名 `l134test`，模型 `lesson_134_model` 与赋值不变，证明改名不动模型。
5. 概念模型上，本课建立了「IPv4 地址元数据（in_ifaddr：地址/掩码/广播/scope）」的
   Linux 心智模型，并衔接了 L141 的 `ipv4_is_loopback`/127.0.0.1/8 概念。
6. 旧 README 的 `Commands: l135test` 已勘误：本课源码实际的 checkpoint 命令是
   `l134test` 与 `l142test`（`l135test` 属于 Lesson 143）。

**下一课**：[`lesson-143-stable/README.md`](../lesson-143-stable/README.md) 主题为
「UDP socket 状态」——网络主题 checkpoint 课的第七课。它将在本课 L3 地址概念之上，
把「UDP socket（sock/udp_sock、状态机、五元组）」作为新的主题标签与概念模型，
衔接点是 L4 层需要以地址（local/remote IP）为 socket 元数据的一部分。

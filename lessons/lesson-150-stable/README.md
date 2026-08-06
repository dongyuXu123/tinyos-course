# Lesson 150: network namespace — 精讲文档

> **课号**：Lesson 150 ｜ **主题**：network namespace（网络命名空间）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课是「namespace 三连」的收官课（进程 148 → mount 149 → **network 150**），namespace 三连完成后进入 PID/user namespace 与 cgroup 系列（151–156）
> **前置课程**：[`../lesson-149-stable/README.md`](../lesson-149-stable/README.md)（mount namespace 隔离）
> **后续课程**：[`../lesson-151-stable/README.md`](../lesson-151-stable/README.md)（PID namespace）
> **一句话目标**：讲清「network namespace 为什么能让每个容器拥有独立的网络栈」——网卡、路由、socket 都按世界隔离，对照 Linux `net/core/net_namespace.c`、`include/net/net_namespace.h`、`kernel/nsproxy.h`，并把教学内核继承的 `pipe_poll` 就绪位与等待队列「连接/就绪」设施按这一主题系统化复述，运行 `l150test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（network namespace 对象）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l149test` 恢复为历史命名 `l142test`（挂 `lesson_142_state`），新增 `lesson_143_model`/`lesson_143_state` 与 `l150test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l150test`（旧 README 所写 `l143test` 按源码勘误，源码中不存在 `l143test` 命令）；另保留历史检查点 `l100test`–`l142test`，以及 `polltest`/`pipeinfo`/`pipetest`/`pctest`/`pcgo` 等与连接/就绪相关的回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「网络栈按世界隔离」的直觉解释 network namespace（容器内有自己的 lo、自己的 IP、自己的路由表，`ip netns` 就是管理它的工具）；说出 Linux 中 network namespace 由 `struct net` 承载、经 `nsproxy->net_ns` 挂到进程上、`copy_net_ns()` 在 `CLONE_NEWNET` 时复制（`net/core/net_namespace.c`、`include/net/net_namespace.h`）；在教学内核中沿 `pipe_poll` → `polltest` → `l150test` 观察连接/就绪元数据与检查点状态；运行 `make check`/`make run` 验证本课稳定快照。

**在课程主线中的位置**：Lesson 143–156 是 bounded networking / namespace / cgroup / security 收敛检查点阶段；本课是 **namespace 三连的收官课**（进程 148 → mount 149 → **network 150**）。上一课建立了「挂载树世界隔离」概念，本课把隔离推广到「整个网络栈」——网卡、IP 地址、路由、socket、netfilter 全部按 namespace 归属。作为检查点课，源码 diff 极小（只有模型号与主题串推进），任务是把继承机制中与「连接/就绪」相关的设施（`pipe_poll`、`pipe_try_read/pipe_try_write`、`wait_queue`、`event`）按 network namespace 主题系统化复述。下一课（Lesson 151）转向 PID namespace，之后进入 cgroup 系列。

**前置知识清单**（学本课前必须掌握）：
1. mount namespace 概念：nsproxy 挂载、`CLONE_NEWNS`、挂载树复制（Lesson 149）。
2. 就绪位：`pipe_poll(mask)` 的 `POLL_IN=1U`/`POLL_OUT=2U`、管道水位（Lesson 43s/76s/146）。
3. 等待队列与唤醒：`waitq_wake_one/wake_all`、`event_set/event_wait`（Lesson 30s/70s/146）。
4. 连接状态机概念：状态 × 事件、TCP 状态集合（Lesson 145）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–149）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 150: network namespace`；
- 新命令 `l150test` 输出 `l150test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `polltest`/`pipeinfo`/`pctest`/`pcgo` 继续展示连接/就绪与事件元数据。

---

## 2. 核心概念精讲

### 2.1 network namespace：每个容器拥有独立的网络栈

**直觉**：容器里 `ifconfig lo` 有自己的回环、`ip addr` 看到的是容器私有的 172.x 地址、`iptables` 配的是容器自己的规则——仿佛每个容器都插了一张独立的「虚拟网卡」。这种「网络栈按世界隔离」的机制就是 **network namespace**。

**准确定义**：network namespace 为进程提供独立的**网络协议栈视图**：独立的回环设备、网络设备列表、IP 地址表、路由表（`fib_table`）、socket 表、netfilter 规则、`/proc/net` 视图。Linux 中每个 network namespace 对应一个 `struct net`（`include/net/net_namespace.h`），进程通过 `nsproxy->net_ns` 指向它；`CLONE_NEWNET` fork 时 `copy_net_ns()` 新建一个 `struct net`（默认只含 lo）。

### 2.2 为什么需要 network namespace（动机）

1. **网络隔离**：IP 地址、端口、路由是全局共享资源——两个容器都想监听 `0.0.0.0:8080` 就必须各占一个网络世界。
2. **安全与可控**：容器的网络策略（防火墙、带宽）不应影响宿主与邻居；独立 netfilter/路由表是网络隔离的基础。
3. **可移植拓扑**：每个容器有自己的 lo/eth0/网桥，`docker` 用 `veth` 对 + 网桥把容器 namespace 与宿主机 namespace 连接起来——namespace 是容器网络模型的原子构件。

### 2.3 Linux 中 network namespace 的工作机制

- **数据结构**：`struct net`（`dev_base_head` 设备链表、`loopback_dev`、`net->ipv4` 路由表、`netns_*` 子系统指针）在 `include/net/net_namespace.h`；创建/销毁在 `net/core/net_namespace.c` 的 `copy_net_ns()`/`net_drop_ns()`。
- **挂载**：进程的 `task_struct.nsproxy->net_ns` 指向所属 `struct net`（`include/linux/nsproxy.h`）；socket 创建时 `sock_alloc` 绑定当前 `net`，收发走该 namespace 的设备与路由。
- **连接点**：`veth` 对一端在容器 namespace、一端在宿主 namespace（`net/core/rtnetlink.c` 的 `rtnl_newlink`），桥接 `lo`/`eth0` 实现跨 namespace 通信。
- **教学简化**：教学内核没有 `struct net`/设备链表，只有「管道 + 就绪位」承载的连接模型：`pipe_poll` 的 `POLL_IN/POLL_OUT` 对应 socket 的可读/可写就绪，`pipe_try_read/pipe_try_write` 对应收发方向。这相当于「单网络世界、无 namespace 副本」的特例。

### 2.4 教学内核中与「network namespace」有关的既有设施

本课主题机制（network namespace）**未在源码中实现**，但「连接/就绪」这个主题素材在内核里完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| `pipe_model` | `struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };` | 字节流连接的教学模型（对照 socket 发送/接收队列） |
| `pipe_poll(mask)` | 返回 `POLL_IN/POLL_OUT` 就绪位 | socket 可读/可写就绪（对照 `sock_poll`） |
| `pipe_try_read/pipe_try_write` | 空/满时返回 0 并递增 `blocked_*` | 收发方向与阻塞边界（对照 `tcp_recvmsg`/`tcp_sendmsg`） |
| `wait_queue` | `waitq_wake_one/wake_all` | 连接就绪时的等待者唤醒（对照 `sk_wait_event`） |
| `event`/`semaphore` | `event_set/event_wait`、`sem_down/sem_up` | 连接事件与槽位同步 |
| `polltest` | 就绪位迁移断言 | 连接方向就绪语义的可执行验证 |

### 2.5 检查点模型：lesson_143_model 与 l150test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `143→146` 标记 Origin 为 Lesson 143（`a=143,b=144,c=145,d=146`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「网络计数连续性」。本课同时把上一课新增的 `l149test` 恢复为历史命名 `l142test`（挂 `lesson_142_state`，计数 `142→145`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理（lesson-149 曾用 `l149test` 名字挂 142 号模型，本课将其名实重新对齐）。这也是「命令名收敛」序列的又一次推进，为下一课（151）把 `l150test` 恢复为 `l143test` 铺路。

### 2.6 机制继承 + 检查点增量

本课主题机制（network namespace 隔离）**不是本课新写的代码**：管道、就绪位、等待队列、事件全部来自早期课程（Lesson 30s–70s）。本课实际增量只有三处：`l149test`→`l142test` 更名、`lesson_143_model`+`l150test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「连接/就绪」主题重新组织，并如实说明：**network namespace 对象（`struct net` 等价物）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l149test`→`l142test` 恢复命名；新增 `lesson_143_model`/`lesson_143_state`/`l150test`；`about` 与开机横幅更新。network namespace 主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化（md5 与上一课一致） |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`network namespace`/`l150test`/`Lesson 150`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（连接/就绪机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
static TEXT64 void l150test(u16*c){lesson_143_state=(struct lesson_143_model){143U,144U,145U,146U,1,1,1,1};int ok=lesson_143_state.valid&&lesson_143_state.active&&lesson_143_state.ready&&lesson_143_state.accounted&&lesson_143_state.b==lesson_143_state.a+1U;text64(c,"l150test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 143 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `143→146`（Origin Lesson 143），四布尔位全置 1，`b==a+1U` 校验计数连续——「网络世界按顺序演进」的元数据隐喻。
2. **逻辑分析（≥3 行）**：赋值语句把整个结构体字面量写入 `lesson_143_state`，随后 `ok` 由五个条件合取而成：`valid/active/ready/accounted` 四个布尔位 + `b==a+1U` 连续性。由于字面量全为 1 且 `144==143+1`，`ok` 恒为真，输出必为成功串；fallback 分支（`Lesson 143 fallback reported`）在「计数被破坏或模型被错误初始化」时才可能命中，属于防御性兜底。
3. **输出串（逐字抄录）**：成功 `l150test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 143 fallback reported`。
4. **恢复的 `l142test`**：本课同时把上一课的 `l149test` 更名回 `l142test`（同为 `lesson_142_state`，计数 `142→145`），使检查点命令名与 Origin 对齐；`l100test`–`l141test` 历史检查点全部保留。

#### 3.2.2 字节流连接模型：pipe_model / pipe_try_read / pipe_try_write（socket 队列的教学原型）

```c
struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };
static TEXT64 int pipe_try_write(u8 value){u8 id;if(pipe_model.used>=PIPE_CAP){pipe_model.blocked_writers++;return 0;}pipe_model.data[pipe_model.head]=value;pipe_model.head=(u8)((pipe_model.head+1)%PIPE_CAP);pipe_model.used++;pipe_model.writes++;if(waitq_wake_one(&pipe_read_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_readers++;return 1;}
static TEXT64 int pipe_try_read(u8*out){u8 id;if(!pipe_model.used){pipe_model.blocked_readers++;return 0;}*out=pipe_model.data[pipe_model.tail];pipe_model.tail=(u8)((pipe_model.tail+1)%PIPE_CAP);pipe_model.used--;pipe_model.reads++;if(waitq_wake_one(&pipe_write_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_writers++;return 1;}
```

1. **环形缓冲（≥3 行）**：`data[PIPE_CAP]` + `head/tail/used` 构成环形字节流——`head` 写、`tail` 读、`used` 是水位；满（`used>=PIPE_CAP`）时写返回 0 且 `blocked_writers++`，空（`!used`）时读返回 0 且 `blocked_readers++`，边界闭合。
2. **收发方向（≥3 行）**：写端成功即 `writes++` 并 `waitq_wake_one` 唤醒读等待者（`wake_readers++`），读端成功即 `reads++` 并唤醒写等待者（`wake_writers++`）——「发方唤醒收方、收方唤醒发方」正是 socket 发送/接收队列的唤醒对称性，对照 `tcp_sendmsg` 唤醒 `sk_wait_event` 等待者。
3. **对照 network namespace**：`pipe_model` 是「单个连接」，而 network namespace 是「一组连接 + 网络栈」的容器；教学内核用这一个管道代表整个「网络世界」。

#### 3.2.3 就绪位：pipe_poll / polltest（sock_poll 与就绪语义的教学原型）

```c
#define POLL_IN 1U
#define POLL_OUT 2U
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,e=pipe_model.used==1;while(pipe_model.used<PIPE_CAP)pipe_try_write(8);int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),h=pipe_poll(POLL_OUT)==POLL_OUT;text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
```

1. **就绪位查询（≥3 行）**：`pipe_poll` 按水位算就绪位——`used>0` 可读（`POLL_IN`）、`used<PIPE_CAP` 可写（`POLL_OUT`）；调用方用 `mask` 声明关注方向，返回值可直接与 `mask` 做 `&` 判定。这对应 socket 的「接收队列非空可读、发送缓冲区有空可写」。
2. **就绪迁移断言**：`polltest` 用 8 个断言覆盖「空→写→读→满→读」全过程：空管道 `POLL_IN` 不就绪（`a`）、`POLL_OUT` 就绪（`b`）；写一字节后 `POLL_IN` 就绪（`d`）；灌满后 `POLL_OUT` 不就绪（`f`）；读空后 `POLL_OUT` 恢复（`h`）。
3. **观察命令**：`polltest` 输出 `polltest: POLLIN/POLLOUT readiness transitions passed`；`pipeinfo` 打印 `pipe used/capacity`、`reads/writes`、`blocked r/w`、`wake r/w`、`poll_registrations`。

#### 3.2.4 唤醒机制：waitq_wake_one / waitq_wake_all（连接就绪通知的教学原型）

```c
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){u8 id;if(!waitq_dequeue(q,&id))return 0;if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0;threads[id].state=THREAD_RUNNABLE;q->wake_one++;*out=id;return 1;}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){u8 id,n=0;while(waitq_dequeue(q,&id))if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}q->wake_all+=n;return n;}
```

1. **唤醒一个 vs 全部**：`wake_one` 队头唤醒一个（适合「一个数据被一个读者取走」），`wake_all` 全部唤醒（适合广播）——对照 socket 的 `sk_wake_async`/`wake_up_interruptible`。
2. **状态守卫（≥3 行）**：两者都校验 `id` 合法（非 0、`<THREAD_COUNT`）且状态与传入 `state` 匹配，才置 `THREAD_RUNNABLE`——避免唤醒已退出/已就绪线程，是「连接就绪通知必须匹配等待状态」的教学体现。
3. **与 network namespace 对照**：等待队列是「连接就绪事件」的投递通道；network namespace 决定「哪一组连接」共享这些等待队列。

#### 3.2.5 连接事件：event_set / event_wait（跨线程连接同步的教学原型）

```c
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(e->signaled){irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}
```

1. **置位 + 广播**：`event_set` 关中断下置 `signaled=1` 并 `waitq_wake_all` 唤醒所有 `THREAD_BLOCKED_EVENT` 等待者。
2. **先检查后睡眠（≥3 行）**：`event_wait` 先查 `signaled`（已就绪直接返回，避免错过唤醒），未就绪才入队置阻塞，`sti; hlt` 自旋等唤醒，回到循环顶部重查 `signaled`——「lost wakeup」被两步法挡住，这是连接事件同步的正确性根基。
3. **观察**：`pctest`+`pcgo` 用它做生产/消费起始闸门；`pctest` 输出 `pctest: producer and consumer blocked on start event; run pcgo`，`pcgo` 输出 `pcgo: event set; broadcast wake-all issued`。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 150: network namespace\n`；检查点分支：
```c
else if(eq64(word,"l142test")){if(!noargs64(arg))usage64(c,"l142test");else l142test(c);}else if(eq64(word,"l150test")){if(!noargs64(arg))usage64(c,"l150test");else l150test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 150: network namespace\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` 编译 `kernel64.c` → `ld -T kernel64.ld` → `objcopy` 出 raw bin → `boot.S` 以 `.incbin` 嵌入 → `grub-mkrescue` 出 ISO。`make check` 断言 README 含 `network namespace`、`Lesson 150`，kernel64.c 含 `l150test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init / vma_init / reclaim_init / vfs_init / address_space_init
 ├─ 横幅 "Lesson 150: network namespace"
 └─ 主循环：命令 → exec64
     ├─ l150test / l142test → 阶段检查点（lesson_143_state / lesson_142_state）
     ├─ polltest / pipeinfo → 连接就绪位（POLL_IN/POLL_OUT）
     ├─ pipetest → 字节流 FIFO 满/空阻塞迁移
     ├─ pctest / pcgo / pcinfo → 连接事件 + 槽位同步
     └─ about → "Lesson 150: network namespace"
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`kernel_main64_binary` 完成 PMM/VM/VFS 初始化，打印横幅 `Lesson 150: network namespace`。
2. **`l150test`** → `l150test(c)` → 初始化 `lesson_143_state` → 五条件断言 → `l150test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`l142test`**（恢复名） → `l142test(c)` → `lesson_142_state` 断言 → `l142test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
4. **`polltest`** → `pipe_init` → `pipe_poll`/`pipe_try_write`/`pipe_try_read` 就绪位迁移 → `polltest: POLLIN/POLLOUT readiness transitions passed`。
5. **`pctest`** → `start_threads(3)` → 生产者/消费者阻塞在 `pc_start_event` → `pctest: producer and consumer blocked on start event; run pcgo`。
6. **`pcgo`** → `event_set(&pc_start_event)` → 广播唤醒 → `pcgo: event set; broadcast wake-all issued`。
7. **`about`** → `Lesson 150: network namespace`。

---

## 5. 构建、运行与验证

**依赖**：同前几课（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 150 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 150: network namespace` 横幅 |
| `l150test` | `l150test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l142test` | `l142test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `polltest` | `polltest: POLLIN/POLLOUT readiness transitions passed` |
| `pctest` | `pctest: producer and consumer blocked on start event; run pcgo` |
| `pcgo` | `pcgo: event set; broadcast wake-all issued` |
| `about` | `Lesson 150: network namespace` |

判定成功：`l150test` 输出 passed、无 fallback，`polltest` 输出 passed、无 `BROKEN`，`pctest`/`pcgo` 序列无异常，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l150test` 输出 `Lesson 143 fallback reported` | `lesson_143_state` 初始化/断言失败（stale 镜像） | `grep -n "l150test" kernel64.c`；确认初始化串 `{143U,144U,145U,146U,1,1,1,1}` 与 `b==a+1U` |
| `polltest` 输出 `BROKEN` | 管道就绪位与水位不一致 | 检查 `pipe_poll` 的 `used`/`PIPE_CAP` 比较；`pipe_try_write` 满时返回 0、`pipe_try_read` 空时返回 0 |
| `pipeinfo` 的 `poll_registrations` 异常 | `pipe_poll` 调用次数统计与预期不符 | 确认每次 `pipe_poll` 都递增 `poll_registrations`；`polltest` 会调用多次 |
| `pctest`/`pcgo` 后生产者未推进 | 事件唤醒丢失或 `pcgo` 未执行 | 确认 `event_wait` 先查 `signaled` 再入队；先 `pctest` 再 `pcgo` |
| `pcinfo` 的 `P errors/ok` 显示 no | 生产者/消费者序列错乱或仍有残留 | 检查 `pc_sequence_errors`、`pc_used`、`pc_spaces.count`/`pc_items.count` 是否归位 |
| `l150test` 与 `l142test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l150test` 只操作 `lesson_143_state`、`l142test` 只操作 `lesson_142_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 150' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `network namespace` 与 `Lesson 150` |
| 输入 `l150test` 提示 `unknown command` | exec64 分支未命中（命令表陈旧） | `grep -o 'l150test' kernel64.c` 应同时命中函数定义与 `eq64` 分支 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `pipe_model` 环形字节流 | `net/core/sock.c` 的发送/接收队列；`net/ipv4/tcp.c` `tcp_sendmsg`/`tcp_recvmsg` | 模型只有 4 字节单管道，无 sk_buff/sk_receive_queue |
| `pipe_poll` 的 POLL_IN/POLL_OUT | `net/socket.c` 的 `sock_poll()`；`include/linux/poll.h` | 模型只查水位，无 `POLLERR/POLLHUP` 与缓冲区阈值 |
| `waitq_wake_one/wake_all` | `kernel/sched/wait.c` 的 `__wake_up`；`net/core/sock.c` `sk_wait_event` | 模型是定长 FIFO，无 `wait_queue_entry` 链表 |
| `event_set/event_wait` | `net/core/sock.c` 的异步唤醒/`wake_up_interruptible` | 模型无 `struct socket_wq`，只保留「置位 + 广播」 |
| network namespace（主题概念） | `net/core/net_namespace.c` 的 `copy_net_ns()`；`include/net/net_namespace.h` 的 `struct net`；`kernel/nsproxy.h` | 教学内核**没有** `struct net`/设备链表/路由表，只有单管道连接模型 |
| `l150test` 断言 | 无直接对应（`ip netns`/container 网络测试套件） | 模型把 network namespace 主题的可验证状态固化进内核 |

**权威来源**：Linux `net/core/net_namespace.c`、`include/net/net_namespace.h`、`include/linux/nsproxy.h`、`net/socket.c` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有** `struct net`/`copy_net_ns` 或网络设备链表的等价实现——network namespace 是「主题宣告」，教学内核停留在「单网络世界、单管道连接模型」上。

---

## 8. 思考题与练习

1. **概念理解**：为什么两个容器可以同时监听 `0.0.0.0:8080` 而互不冲突？network namespace 隔离了哪些「网络全局资源」？
2. **源码定位**：在 `kernel64.c` 中找出所有「就绪/唤醒」相关的代码位置（提示：`pipe_poll`、`pipe_try_write`/`pipe_try_read`、`waitq_wake_one`、`event_set`），并说明每个位置的唤醒方向。
3. **动手实验**：给 `pipe_poll` 增加一个 `POLL_ERR` 位（当管道「溢出」标记置位时报错），在 `polltest` 里验证并重新构建运行，观察就绪位集合如何扩展。
4. **动手实验**：把 `pipe_model` 改造成「双端连接」（`data_a`/`data_b` 两个方向缓冲），在 `polltest` 里验证双向就绪位，体会 network namespace 里「每条连接都有收发两个方向」。
5. **Linux 对照**：阅读 `net/core/net_namespace.c` 的 `copy_net_ns` 与 `struct net` 的字段，说明 `CLONE_NEWNET` 复制了什么、共享了什么；对照教学模型「单网络世界」的简化。

---

## 9. 本课小结与下一课预告

1. network namespace 让每个容器拥有独立网络栈：`CLONE_NEWNET` + `struct net`（`net/core/net_namespace.c`、`include/net/net_namespace.h`）。
2. 网卡、IP、路由、socket、netfilter 都按 namespace 归属，`veth` 对实现跨 namespace 连接。
3. 教学内核没有 network namespace 对象，但连接/就绪素材完整：`pipe_model` 环形字节流、`pipe_poll` 就绪位、`waitq_wake_one/wake_all` 唤醒、`event_set/event_wait` 连接事件。
4. `polltest` 的 8 个就绪迁移断言是「连接方向就绪语义」的教学对应。
5. 检查点增量：`l149test`→`l142test` 更名、新增 `lesson_143_model`+`l150test`、横幅与 `about` 更新为 `Lesson 150: network namespace`。
6. 下一课（Lesson 151）主题为 **PID namespace**（对照 `kernel/pid_namespace.c`、`kernel/pid.c`）：namespace 三连之后回到「进程号世界」，教学内核将以 `task_table`/`task_struct`/`FIXED_PID`/`SECOND_PID` 进程号设施承接该主题，并继续「命令名向 Origin 收敛」的命名整理。

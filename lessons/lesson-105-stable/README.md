# Lesson 105: epoll 边沿触发 — 精讲文档

> **课号**：Lesson 105（主线源课编号 Lesson 98 线）
> **本课主题**：epoll 边沿触发（Edge-Triggered, ET）——用 `EPOLLET` 标志把"就绪上报"从"状态一直满足就一直报"改成"状态从不可用翻到可用时只报一次"；理解"边沿"是相对时间的变化量，不是绝对状态
> **课程主线位置**：VFS / 设备 / epoll / 服务管理教学模型阶段（Lesson 81 起的「checkpoint 课」系列）。Lesson 104 建立了"epoll 实例 + 固定 watch 表"，本课回答"什么时候上报"的一半：边沿触发；Lesson 106 回答另一半：水平触发（LT，默认）。
> **前置课程**：[`../lesson-104-stable/README.md`](../lesson-104-stable/README.md)（epoll 实例与固定 watch 表：实例 = 对象 + watch 表 + 就绪列表 + wait 队列）
> **后续课程**：[`../lesson-106-stable/README.md`](../lesson-106-stable/README.md)（epoll 水平触发：默认语义，只要条件满足就反复上报）
> **本课一句话目标**：理解边沿触发的本质——"只在就绪状态**从假变真**的那一刻把事件放进就绪列表"，并掌握它的使用铁律"ET 下必须把数据一次读到 EAGAIN，否则会漏掉后续数据"。
> **保留的原始快照信息**：This checkpoint models bounded VFS, devices, epoll, and service management metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l97test` + `l105test`（**勘误**：旧 README 标注的 `l98test` 在源码命令表中并不存在——源码 `exec64` 分派的是 `l97test` 与 `l105test`），plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能讲清 `EPOLLET` 的判定逻辑——"就绪事件在状态变化沿上产生一次"，能对比"读了一半"与"全读完"两种消费方式对 ET 是否漏事件的差异，并知道本课 checkpoint 里就绪标志 `ready` 的"边沿语义"。
- **在课程主线中的位置**：checkpoint 课（Lesson 81 起）系列中的「epoll 边沿触发」检查点。本系列每课源码增量只有几行（上一课测试改名 + 新测试 + about/banner 文案），主题由课程标签承载；`kernel64.c` 里没有 `EPOLLET` 实现代码，唯一的 `epoll` 字样在 checkpoint 断言串里——因此本课把 ET 的**语义模型**讲透（用继承的 `pipe_poll`/`polltest` 就绪状态做例子），并在 Linux 对照里给出 `fs/eventpoll.c` 的 `ep_poll_callback`/`ep_send_events` 真身。
- **前置知识清单**（学本课之前必须掌握）：
  1. 就绪状态与兴趣掩码："就绪 = 掩码 ∩ 实时状态"（Lesson 103/104）；
  2. `pipe_poll` 的可读/可写判定与 `polltest` 的四态迁移（Lesson 103）；
  3. epoll 实例的三件套：interest 表 / 就绪列表 / wait 队列（Lesson 104）；
  4. 事件唤醒原语 `event_set`/`waitq_wake_all`（Lesson 78 起）。
- **本课交付**：新增固定容量记录 `struct lesson_98_model` + `lesson_98_state` + `l105test`；把 `l104test` 改名为 `l97test`；`about` 与 banner 更新为「Lesson 105: epoll 边沿触发」。

---

## 2. 核心概念精讲

### 2.1 边沿触发：事件在"翻沿"上产生

**直觉**：门铃只在"按下"那一刻响，不会因为手一直按着就响个不停。ET 就绪上报就是门铃：fd 从"不可读"变成"可读"的**那一瞬间**产生一个事件；只要这个"可读"状态持续着，就不再产生第二个事件。

**定义**（对照就绪状态量 `R`，0=不可用，1=可用）：
- **边沿（edge）**：`R` 从 0→1 的那次变化。
- **ET 规则**：当且仅当检测到 0→1 沿时，把该 fd 的事件放进就绪列表。之后哪怕 `R` 一直等于 1，也不重复上报，直到 `R` 回到 0（消费完毕或状态翻转）再翻到 1，才会产生下一个事件。

```
时间轴:   R: 0 → 1 → 1 → 1 → 0 → 1 → 1 → 0
ET 上报:      ↑(报一次)          ↑(再报一次)
```

管道例子（`used` 即 `R` 的来源）：
- `used: 0 → 1`（写入第一个字节）：翻沿，报一次"可读"；
- 继续写入到 `used: 4`（满）：可读状态没变过（一直非 0），**不再报**；
- 读空 `used: 0`：翻回不可读；
- 再次写入 `used: 1`：第二次翻沿，再报一次。

### 2.2 为什么需要 ET：事件循环的吞吐

**水平触发的代价**：LT 下只要 fd 可读，`epoll_wait` 每次都返回它。如果程序只处理一个字节就回到 `epoll_wait`，一个一直满数据的 fd 会让循环**忙转（busy loop）**——LT 语义本身没有错，是消费习惯造成的忙转。
**ET 的收益**：事件只在变化沿产生，强迫（或者说鼓励）应用**一次把数据全部取走**；正因为一次取干净，每个事件只处理一轮，吞吐和延迟都更好。这使 ET 成为高性能服务器（nginx 等）的标配。

### 2.3 ET 的经典坑：漏事件 = 数据没读干净

ET 的"只报一次"意味着**就绪状态的 0 必须靠应用自己制造**：必须读到 `EAGAIN`（管道空）为止。如果读到一半就停手，管道里还剩数据，`used` 仍是 1，不会再有新的 0→1 沿——**剩余数据再也没有事件通知**。这就是 ET 使用手册上的第一铁律：
> 使用 ET 模式时，必须在收到事件后**一直读/写到返回 `EAGAIN`**（`EAGAIN` = 教学模型里的"读空管道"）。

### 2.4 教学模型里的"沿"：`polltest` 的两次翻转

继承的 `polltest` 恰好演示了两次 0→1 沿：
1. 空管道（`used=0`）→ 写入 7（`used=1`）：**可读沿**，`pipe_poll(POLL_IN)` 从 0 变 `POLL_IN`；
2. 写满（`used=4`）→ 读出一个（`used=3`）：**可写沿**，`pipe_poll(POLL_OUT)` 从 0 变 `POLL_OUT`。

如果用 ET 视角重放：第一次沿之后即使 `used` 一直非 0，`pipe_poll(POLL_IN)` 的"沿事件"只发生一次。这就是本课主题与既有代码的接缝——**代码没变，但读它的语义变了**。

### 2.5 checkpoint 固定元数据

`struct lesson_98_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担系列 checkpoint：`l105test` 断言 `b==a+1`（98+1=99）与四标志。`ready` 标志再次呼应主题——本课讨论的正是"就绪标志的边沿语义"。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-104） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：就绪状态模型、wait 队列/事件、`exec64` 分派 | **微增量（仅 3 行）**：新增 `struct lesson_98_model` + `lesson_98_state` + `l105test()`；把 `l104test` 改名为 `l97test`；`about`/banner/`exec64` 文案更新（ET 语义模型本课讲解；`pipe_poll`/`polltest` 继承自更早课程） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成「epoll 边沿触发 / l105test / Lesson 105」关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的 checkpoint 元数据：`l105test`

```c
static TEXT64 void l105test(u16*c){lesson_98_state=(struct lesson_98_model){98U,99U,100U,101U,1,1,1,1};int ok=lesson_98_state.valid&&lesson_98_state.active&&lesson_98_state.ready&&lesson_98_state.accounted&&lesson_98_state.b==lesson_98_state.a+1U;text64(c,"l105test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 98 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_98_state=(struct lesson_98_model){98U,99U,100U,101U,1,1,1,1};`：`a=98,b=99,c=100,d=101`，四标志全 1。
- `int ok=...`：四标志与 `b==a+1U` 五条件与，`99==98+1` 必须成立。
- 成功串 `bounded VFS, devices, epoll, and service management checkpoint passed`、失败串 `Lesson 98 fallback reported` 逐字来自源码。
- **设计说明**：fallback 里的 98 是被校验状态号，**不是**命令名——旧 README 据此误写 `l98test`；本课真实命令是 `l97test` 与 `l105test`。`ready` 标志是"就绪状态量"：本课全部语义讨论围绕它展开（沿 = 它的 0→1）。

#### (b) 上一课测试改名为 `l97test`

```c
static TEXT64 void l97test(u16*c){lesson_97_state=(struct lesson_97_model){97U,98U,99U,100U,1,1,1,1};int ok=lesson_97_state.valid&&lesson_97_state.active&&lesson_97_state.ready&&lesson_97_state.accounted&&lesson_97_state.b==lesson_97_state.a+1U;text64(c,"l97test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 97 fallback reported");putc64(c,'\n');}
```

- lesson-104 里它叫 `l104test`；本课改名 `l97test`，测试名与被校验状态号对齐。
- `exec64` 分派把 `l104test` 分支换成 `l97test` 分支，并新增 `l105test` 分支：
```c
else if(eq64(word,"l97test")){if(!noargs64(arg))usage64(c,"l97test");else l97test(c);}
else if(eq64(word,"l105test")){if(!noargs64(arg))usage64(c,"l105test");else l105test(c);}
```

#### (c) 就绪状态量：ET 语义的"输入信号"

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

- `pipe_poll` 返回的 `ready` 是**当前时刻**的就绪状态量 `R`，不携带历史信息。ET 判定需要的是 `R` 的**变化**（`R(t-1)=0 && R(t)=1`），因此 ET 语义必须依赖"上一次上报时的状态"这类记忆。
- 本课教学要点：`pipe_poll` 这类"状态量查询"是 ET 与 LT 的公共底座，**区别在消费者怎么解释返回值**——LT 每次见到 `ready!=0` 都报；ET 只在该位从无到有时报一次。

#### (d) `polltest`：用既有代码重放"两次边沿"

```c
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,e=pipe_model.used==1;while(pipe_model.used<PIPE_CAP)pipe_try_write(8);int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),h=pipe_poll(POLL_OUT)==POLL_OUT;text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
```

用 ET 视角逐段重放（这段代码本课未变，语义是本课重点）：
- `a=pipe_poll(POLL_IN)==0`：`R_IN=0`——还没到沿；
- `w=pipe_try_write(7)`：`used: 0→1`，**`R_IN` 出现 0→1 沿**；
- `d=pipe_poll(POLL_IN)==POLL_IN`：此刻若按 ET，应只在**这次**上报，之后 `R_IN` 恒为 1 不再报；
- `f=pipe_poll(POLL_OUT)==0`：写满后 `R_OUT=0`；
- `g=pipe_try_read(&v)`：`used: 4→3`，**`R_OUT` 出现 0→1 沿**；
- `h=pipe_poll(POLL_OUT)==POLL_OUT`：ET 下是第二次"翻沿上报"。

**边界与设计**：注意 `polltest` 本身做的是 LT 式逐次查询（每次都重新查），不是 ET；把它当作"沿事件的发生源"，就能看清"事件产生（写/读）"与"事件上报（ET/LT）"两层职责的分离。

#### (e) `exec64` 增量、`about` 与 banner

`about` 分支逐字来自源码：
```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 105: epoll 边沿触发\n");}
```
banner 逐字来自 `kernel_main64_binary`：
```c
text64(&c,"Lesson 105: epoll 边沿触发\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'epoll 边沿触发' README.md
	@grep -q 'l105test' kernel64.c
	@grep -q 'Lesson 105' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 105 checks passed.'
```

- 与 lesson-104 的唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链与链接脚本均未变化（同前几课）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ 初始化（同 Lesson 103/104）
    ├─ banner: "Lesson 105: epoll 边沿触发\nGETTICKS, ...\n"
    └─ for(;;) 键盘循环
        ├─ "l105test" ──► lesson_98_state 校验（a=98,b=99）──► "l105test: bounded VFS, ... checkpoint passed"
        ├─ "l97test"  ──► lesson_97_state 校验（上一课回归）
        ├─ "polltest" ──► 重放两次 0→1 沿（可读沿、可写沿）——ET 语义的教学样本
        └─ "pipeinfo" ──► 查看 used/writes/reads 对应沿事件的产生
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打出「Lesson 105: epoll 边沿触发」；初始化链不变。
2. **checkpoint**：输入 `l105test` → `l105test(c)` → `lesson_98_state` 新建并校验（`b==a+1`）→ VGA 打印 `l105test: bounded VFS, devices, epoll, and service management checkpoint passed`；`l97test` 回归校验 `lesson_97_state`。
3. **边沿事件流（用 `polltest` 演示）**：
   - 写入 7：`used 0→1`，产生**可读沿**（`R_IN` 0→1）；
   - 写满：`used 1→4`，可读状态恒 1（无新沿），可写状态恒 0（无新沿）；
   - 读出一个：`used 4→3`，产生**可写沿**（`R_OUT` 0→1）；
   - 8 个断言全真 → `polltest: POLLIN/POLLOUT readiness transitions passed`。
4. **ET 视角**：若把这两次"沿"当作唯一的上报时机，那么"写入后不读、再写再写"（`used` 恒 >0）不会产生第二个可读事件——这正是 ET 下"不读干净就漏数据"的推演路径。
5. **统计窗口**：`pipeinfo` 的 `reads/writes` 反映沿的"制造次数"，`used/capacity` 反映当前状态量 `R`。

输出串与源码逐字一致：`l105test: bounded VFS, devices, epoll, and service management checkpoint passed`；`polltest: POLLIN/POLLOUT readiness transitions passed`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致（`gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`，详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

**构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

**运行**：

```bash
make run
```

> 成功画面在 QEMU 图形窗口，请勿加 `-display none`。

**验证步骤与预期输出**（输出串从源码逐字抄录）：

1. 开机第一屏应显示：
   ```
   Lesson 105: epoll 边沿触发
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l105test`，预期输出：
   ```
   l105test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l105test: Lesson 98 fallback reported`。）
3. 输入 `l97test`（上一课回归），预期输出：
   ```
   l97test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
4. 输入 `polltest`，预期输出：
   ```
   polltest: POLLIN/POLLOUT readiness transitions passed
   tinyos>
   ```
5. 输入 `pipeinfo`，观察 `used/capacity` 与 `reads/writes`（沿事件产生源的计数）。
6. 输入 `about`，预期输出：
   ```
   Lesson 105: epoll 边沿触发
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 105 checks passed.`；QEMU 中 `l105test`/`l97test` 均打印 `...checkpoint passed`、`polltest` 打印 `...transitions passed`，即代表本课 checkpoint 与边沿触发主题的验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l105test` 打印 fallback 串 | `lesson_98_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=98,b=99`） |
| 输入 `l98test` 提示 unknown command | 旧 README 把 fallback 编号当成命令名；源码没有该命令 | 用 `l97test`/`l105test`；`help` 里的命令表为准 |
| `polltest` 打印 `BROKEN` | 就绪断言与 `used` 不一致（事件产生源出错） | 逐段核对：写 7 后 `used==1`、写满后 `pipe_poll(POLL_OUT)==0` |
| 概念混淆：把 `polltest` 当成 ET 演示 | `polltest` 每次重新查询，是 LT 式快照；它只提供"沿事件的发生源" | 用"两次 0→1 沿"视角重放（见 §3.2(d)），区分事件产生与事件上报 |
| 以为 ET 会在"一直可读"时反复上报 | 对"沿 = 0→1 变化"的理解有误 | 用 2.1 的时序图：`R` 持续为 1 时不产生新沿 |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'epoll 边沿触发' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l105test` 符号 | `grep -q 'l105test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：`EPOLLET` 标志与"边沿"的准确定义**
- TinyOS 教学模型：沿 = 就绪状态量 `R` 的 0→1 变化；"只报一次"由消费习惯（读到 EAGAIN）保证。
- Linux：`EPOLLET` 定义在 `include/uapi/linux/eventpoll.h`（`#define EPOLLET (__force __poll_t)(1U << 31)`），是 `struct epoll_event.events` 的最高位。`ep_poll_callback()`（`fs/eventpoll.c`）是就绪回调：对**非 ET** 条目直接把它挂进 `rdllist` 并唤醒等待者；对 **ET** 条目用 `ep_is_linked()`/`list_add` 判断"是否已在就绪列表中"——已在此轮列表里的 ET 条目**不重复入列**，实现"一次只报一次"。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_poll_callback`、`ep_is_linked`、`EPOLLET` 相关 `revents` 处理）；`include/uapi/linux/eventpoll.h`。
- 教学简化：没有 `epitem->rdllink` 链与 `ep_is_linked` 去重，ET 语义以纯概念形式讲解；但"在就绪列表里就不重复入列"正是"沿只报一次"的代码实现。

**对照点 2：ET 的"读干净"铁律 = 状态量必须回 0**
- TinyOS：`used>0` 即可读；读空 `used==0` 才能制造下一个可读沿。
- Linux：`epoll` 的 man page 与 `fs/eventpoll.c` 都强调 ET 模式必须循环 `read`/`write` 直到返回 `EAGAIN`；否则 fd 仍就绪但不再产生新沿，剩余数据"沉睡"。`EAGAIN` 对应"此刻无法推进"，即状态量回到"不可用"。
- 权威来源：`epoll(7)` man page（"Level-triggered and edge-triggered" 一节）；Linux v6.x `fs/eventpoll.c`。
- 教学简化：教学模型里 `pipe_try_read` 空管道返回 0（等价 `EAGAIN`），没有真正的 `EAGAIN` errno。

**对照点 3：ET 与 LT 在回调层的分叉**
- TinyOS：`pipe_poll` 只提供状态量，不分 ET/LT；上报策略是"读者"的问题。
- Linux：分叉在 `ep_poll_callback`（事件产生路径）与 `ep_send_events`（消费路径）：`ep_send_events` 每次把就绪条目从 `rdllist` 移出，对 LT 条目若仍就绪会**重新入列**（`ep_poll_callback` 检查非 ET 后加回），对 ET 条目则**出列后不自动回列**——直到下一次真正的 0→1 回调。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_send_events`、`ep_scan_ready_list`）。
- 教学简化：无就绪列表的出队/回队代码；用 `polltest` 的"沿事件"重放来理解这一机制。

**对照点 4：为什么高性能服务器偏爱 ET**
- TinyOS：概念上讲"一次取干净 → 每事件一轮 → 无忙转"。
- Linux：nginx 默认使用 ET（`EPOLLET`），配合非阻塞 fd 把每个连接的事件处理压缩到最少轮数；LT 更宽容（漏处理会重报），但可能引发忙转。
- 权威来源：`epoll(7)` man page；nginx 源码 `src/event/modules/ngx_epoll_module.c`。
- 教学简化：无真实网络，仅以管道做事件源。

---

## 8. 思考题与练习

1. **概念理解**：用你自己的话说明"ET 只报一次"到底报的是"状态"还是"状态的变化"？为什么说"读到一半就停"在 ET 下会漏掉后续数据，而 LT 不会？
2. **源码定位**：在本课 `kernel64.c` 中找出表示"就绪状态量"的字段（提示：`pipe_model` 的哪个成员），以及制造"0→1 沿"的三个函数（写、读、还有哪个？）。
3. **动手实验**：给 `polltest` 在第 4 步之后插入一段"不读，再次 `pipe_try_write`"的代码，观察 `used` 从 1 涨到 4 的过程中 `pipe_poll(POLL_IN)` 始终返回 `POLL_IN`（LT 快照），说明"沿只有一个"。改完请**恢复原值**。
4. **动手实验**：写一个 `pipeinfo` 观察脚本：先 `polltest`，再 `polltest`，比较两次 `reads/writes` 增量，理解"每次 polltest 都制造同样的一对沿"。改完请**恢复原值**。
5. **Linux 对照**：阅读 `fs/eventpoll.c` 的 `ep_poll_callback`，找到 `ep_is_linked` 判断，说明它如何阻止 ET 条目在同一就绪窗口内重复入列；如果把这段逻辑删掉，行为会退化成哪种触发模式？

---

## 9. 本课小结与下一课预告

- 本课把"边沿触发"讲透了：沿 = 就绪状态量 `R` 的 0→1 变化，ET 只在沿上报一次。
- 你理解了 ET 的代价与铁律：事件只报一次，应用必须读到空（`EAGAIN`）否则漏数据——这是"状态量必须回 0 才有新沿"的直接推论。
- 你学会了用既有代码重放语义：`polltest` 的两次 0→1 沿（可读沿、可写沿）是 ET 的教学样本，事件"产生"与事件"上报"是两层职责。
- 你在 Linux 对照里见到了真身：`EPOLLET` 标志、`ep_poll_callback` 的 `ep_is_linked` 去重、`ep_send_events` 的出队/回队，以及 nginx 用 ET 的吞吐动机。
- 你完成了 checkpoint 验证：`l105test` 与回归 `l97test` 打印 `bounded VFS, devices, epoll, and service management checkpoint passed`，并把旧 README 的 `l98test` 命令勘误为源码真实的 `l97test`/`l105test`。

**下一课预告**：Lesson 106「epoll 水平触发」。ET 的反面是 LT（默认，不设 `EPOLLET`）：只要条件满足就反复上报，没读干净下次还会报。下一课把 LT 的"持续上报、绝不错过"语义与 ET 对比讲清，并解释"LT 简单安全、ET 高效但必须配合非阻塞读"这对 trade-off。见 [`../lesson-106-stable/README.md`](../lesson-106-stable/README.md)。

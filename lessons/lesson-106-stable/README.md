# Lesson 106: epoll 水平触发 — 精讲文档

> **课号**：Lesson 106（主线源课编号 Lesson 99 线）
> **本课主题**：epoll 水平触发（Level-Triggered, LT，默认模式）——只要 fd 的就绪条件持续满足（"电平"保持高），`epoll_wait` 就持续返回它；对比 Lesson 105 的边沿触发（ET），讲清"持续上报 / 只在变化沿上报"这对核心 trade-off
> **课程主线位置**：VFS / 设备 / epoll / 服务管理教学模型阶段（Lesson 81 起的「checkpoint 课」系列）。Lesson 105 讲了 ET（只在 0→1 沿报一次），本课讲默认的 LT：条件不消除就永远上报——这是 `poll()`/`select()` 语义在 epoll 里的继承，也是最安全、最容易用对的一种。
> **前置课程**：[`../lesson-105-stable/README.md`](../lesson-105-stable/README.md)（epoll 边沿触发：`EPOLLET` 与"沿只报一次"语义）
> **后续课程**：[`../lesson-107-stable/README.md`](../lesson-107-stable/README.md)（epoll wait/wake 集成：上报之后，`epoll_wait` 怎么睡、怎么被唤醒）
> **本课一句话目标**：理解水平触发的本质——"只要条件满足就上报，直到条件消失"；读懂 LT 与 ET 在"上报次数、忙转风险、漏事件风险"三个维度上的对比，并会用"就绪位当前是否置位"来判定 LT 是否应该上报。
> **保留的原始快照信息**：This checkpoint models bounded VFS, devices, epoll, and service management metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l98test` + `l106test`（**勘误**：旧 README 标注的 `l99test` 在源码命令表中并不存在——源码 `exec64` 分派的是 `l98test` 与 `l106test`），plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能讲清 LT 的判定规则——"就绪位当前为 1 就上报，为 0 就不报"（只看电平、不看变化），并完成 ET vs LT 的三维对比表：上报次数、忙转风险、漏事件风险。
- **在课程主线中的位置**：checkpoint 课（Lesson 81 起）系列中的「epoll 水平触发」检查点。本系列每课源码增量只有几行（上一课测试改名 + 新测试 + about/banner 文案），主题由课程标签承载；`kernel64.c` 里没有 `EPOLLET`/LT 的实现代码——因此本课把 LT 语义建立在**继承的 `pipe_poll` 就绪位**上：`pipe_poll` 每次返回的 `ready` 就是"当前电平"，LT = "电平为高就持续上报"。
- **前置知识清单**（学本课之前必须掌握）：
  1. 就绪状态量与 `pipe_poll(mask)` 的返回位（Lesson 103）；
  2. ET 的"沿 = 0→1 变化"语义与 `EAGAIN` 铁律（Lesson 105）；
  3. epoll 实例 = 实例对象 + watch 表 + 就绪列表（Lesson 104）；
  4. `exec64` 命令分派与 VGA 输出（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_99_model` + `lesson_99_state` + `l106test`；把 `l105test` 改名为 `l98test`；`about` 与 banner 更新为「Lesson 106: epoll 水平触发」。

---

## 2. 核心概念精讲

### 2.1 水平触发：电平为高就一直响

**直觉**：水开报警器（电平型）只要壶里沸腾就一直叫，直到你把火关掉。LT 就绪上报就是"电平型报警器"：fd 可读（电平为高）期间，`epoll_wait` **每次调用都会返回它**；只有等到状态回到不可用（电平为低），它才停止上报。

**定义**（对照就绪状态量 `R`）：
- **水平（level）**：`R` 的当前值，0 或 1。
- **LT 规则**：`epoll_wait` 返回时，所有 `R==1` 且被登记过的 fd 都在就绪列表里。判定**不看历史、不看变化**，只看"现在是不是 1"。

```
时间轴:   R: 0 → 1 → 1 → 1 → 0 → 1 → 0
LT 上报:      ↑    ↑    ↑         ↑      （电平为 1 的每个查询窗口都报）
ET 上报(对比): ↑                    ↑    （只在 0→1 沿报一次）
```

管道例子（`used` 即 `R` 的来源）：
- 写入一个字节后 `used=1`：可读电平为高，**每次** `epoll_wait` 都返回"可读"；
- 只读一个字节但没读空（`used` 仍 >0）：电平仍高，**继续返回**——这就是 LT 与 ET 最关键的差别；
- 读空 `used=0`：电平变低，停止上报。

### 2.2 LT 为什么是默认：简单、安全、不丢事件

**简单**：LT 的消费习惯没有要求——"收到事件后读到多少算多少"都行。没处理完的数据下次还会再报，**绝不会因为"这次没报完整"而永久丢失事件**。这让 LT 成为绝大多数应用的安全默认。
**代价**：如果消费太慢（每次只处理一个字节），一个持续满数据的 fd 会让事件循环忙转——每次 `epoll_wait` 都返回它，应用处理一点点又回来等，CPU 空转。这是 LT 唯一需要警惕的点。

### 2.3 LT 与 ET：三维对比

| 维度 | LT（水平触发） | ET（边沿触发） |
|---|---|---|
| 判定依据 | 当前电平 `R==1` | 变化沿 `R: 0→1` |
| 上报次数 | 条件持续期间每次 `epoll_wait` 都报 | 每次翻沿只报一次 |
| 忙转风险 | 高（消费慢时） | 低（事件数少） |
| 漏事件风险 | 无（没处理完会再报） | 高（没读到 EAGAIN 就漏） |
| 使用难度 | 简单 | 必须配合非阻塞 fd + 读到 EAGAIN |

### 2.4 教学模型里的"电平"：`pipe_poll` 的每次返回值

继承的 `pipe_poll(mask)` 是"电平传感器"：它每次调用都返回**当前** `ready` 位（`R` 此刻的值），不记忆历史。LT 语义 = "`ready` 非 0 就上报"；ET 语义 = "`ready` 从 0 变非 0 才上报"。同一个传感器，两种读法——这就是本课与上一课在代码层面共用的底座。

### 2.5 checkpoint 固定元数据

`struct lesson_99_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担系列 checkpoint：`l106test` 断言 `b==a+1`（99+1=100）与四标志。`ready` 标志第三次呼应就绪主题——水平触发讨论的正是这个标志的"电平"读数。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-105） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：就绪电平模型、wait 队列/事件、`exec64` 分派 | **微增量（仅 3 行）**：新增 `struct lesson_99_model` + `lesson_99_state` + `l106test()`；把 `l105test` 改名为 `l98test`；`about`/banner/`exec64` 文案更新（LT 语义模型本课讲解；`pipe_poll`/`polltest` 继承自更早课程） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成「epoll 水平触发 / l106test / Lesson 106」关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的 checkpoint 元数据：`l106test`

```c
static TEXT64 void l106test(u16*c){lesson_99_state=(struct lesson_99_model){99U,100U,101U,102U,1,1,1,1};int ok=lesson_99_state.valid&&lesson_99_state.active&&lesson_99_state.ready&&lesson_99_state.accounted&&lesson_99_state.b==lesson_99_state.a+1U;text64(c,"l106test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 99 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_99_state=(struct lesson_99_model){99U,100U,101U,102U,1,1,1,1};`：`a=99,b=100,c=101,d=102`，四标志全 1。
- `int ok=...`：四标志与 `b==a+1U` 五条件与，`100==99+1` 必须成立。
- 成功串 `bounded VFS, devices, epoll, and service management checkpoint passed`、失败串 `Lesson 99 fallback reported` 逐字来自源码。
- **设计说明**：fallback 里的 99 是被校验状态号，**不是**命令名——旧 README 据此误写 `l99test`；本课真实命令是 `l98test` 与 `l106test`。`ready` 标志本课读作"电平"：LT 只看它的现值。

#### (b) 上一课测试改名为 `l98test`

```c
static TEXT64 void l98test(u16*c){lesson_98_state=(struct lesson_98_model){98U,99U,100U,101U,1,1,1,1};int ok=lesson_98_state.valid&&lesson_98_state.active&&lesson_98_state.ready&&lesson_98_state.accounted&&lesson_98_state.b==lesson_98_state.a+1U;text64(c,"l98test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 98 fallback reported");putc64(c,'\n');}
```

- lesson-105 里它叫 `l105test`；本课改名 `l98test`，测试名与被校验状态号对齐。
- `exec64` 分派把 `l105test` 分支换成 `l98test` 分支，并新增 `l106test` 分支：
```c
else if(eq64(word,"l98test")){if(!noargs64(arg))usage64(c,"l98test");else l98test(c);}
else if(eq64(word,"l106test")){if(!noargs64(arg))usage64(c,"l106test");else l106test(c);}
```

#### (c) 就绪电平传感器：`pipe_poll`

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

- LT 读法：只要返回的 `ready` 含 `POLL_IN`，就"电平为高"，应上报。**同一时刻的查询序列**（`used` 不变）会返回完全相同的结果——这就是"电平"，与时间无关，只与状态有关。
- 与 ET 读法对比：ET 需要"上一次查询结果"做差才能得到沿；LT 不需要任何记忆，读一次电平即可判定。
- **边界与设计**：`pipe_poll` 无副作用（只 `poll_registrations++`），所以"反复查询电平"是廉价的——LT 的"每次 `epoll_wait` 都重新 poll"正是依赖这一点。

#### (d) `polltest`：LT 视角的同一段代码

```c
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,e=pipe_model.used==1;while(pipe_model.used<PIPE_CAP)pipe_try_write(8);int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),h=pipe_poll(POLL_OUT)==POLL_OUT;text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
```

LT 视角逐段重放（代码本课未变）：
- 第 2/3 步：空管道 `pipe_poll(POLL_IN)==0`（电平低）、`pipe_poll(POLL_OUT)==POLL_OUT`（电平高）——**同一个 `used` 下电平读数固定**；
- 写 7 后：`pipe_poll(POLL_IN)==POLL_IN`（电平翻高）；此时若模拟"LT 连续查询"，只要 `used` 不回 0，每次结果都是 `POLL_IN`；
- 写满后：`pipe_poll(POLL_OUT)==0`（电平低）；读一个后：`pipe_poll(POLL_OUT)==POLL_OUT`（电平再高）。

**要点**：`polltest` 的每次 `pipe_poll` 都是"读电平"；把第 5 步后改成"不读、再查一次 `pipe_poll(POLL_IN)`"，得到的仍是 `POLL_IN`——这就是 LT 的"持续上报"在查询层的表现。

#### (e) `exec64` 增量、`about` 与 banner

`about` 分支逐字来自源码：
```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 106: epoll 水平触发\n");}
```
banner 逐字来自 `kernel_main64_binary`：
```c
text64(&c,"Lesson 106: epoll 水平触发\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'epoll 水平触发' README.md
	@grep -q 'l106test' kernel64.c
	@grep -q 'Lesson 106' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 106 checks passed.'
```

- 与 lesson-105 的唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链与链接脚本均未变化（同前几课）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ 初始化（同 Lesson 103–105）
    ├─ banner: "Lesson 106: epoll 水平触发\nGETTICKS, ...\n"
    └─ for(;;) 键盘循环
        ├─ "l106test" ──► lesson_99_state 校验（a=99,b=100）──► "l106test: bounded VFS, ... checkpoint passed"
        ├─ "l98test"  ──► lesson_98_state 校验（上一课回归）
        ├─ "polltest" ──► 四个"读电平"阶段（空/有数据/满/再空）——LT 语义的查询样本
        └─ "pipeinfo" ──► 查看 used 电平与 reads/writes 计数
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打出「Lesson 106: epoll 水平触发」；初始化链不变。
2. **checkpoint**：输入 `l106test` → `lesson_99_state` 新建并校验（`b==a+1`）→ VGA 打印 `l106test: bounded VFS, devices, epoll, and service management checkpoint passed`；`l98test` 回归校验 `lesson_98_state`。
3. **电平流（LT 语义）**：输入 `polltest` → 依次读四个电平：空（读低、写高）→ 写 7（读翻高）→ 写满（写翻低）→ 读 1（写翻高）。其中"读翻高"之后的电平会一直保持到下次写满——**LT 会在这一整段里持续上报**。
4. **LT vs ET 对比**：同一个 `polltest`，LT 读法是"电平为 1 的每个查询都报"（`pipe_poll` 每次返回相同值）；ET 读法是"只报 0→1 两次沿"。数据流相同，语义解释不同。
5. **统计窗口**：`pipeinfo` 的 `used/capacity` 就是当前电平读数，`reads/writes` 是电平翻转的制造次数。

输出串与源码逐字一致：`l106test: bounded VFS, devices, epoll, and service management checkpoint passed`；`polltest: POLLIN/POLLOUT readiness transitions passed`。

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
   Lesson 106: epoll 水平触发
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l106test`，预期输出：
   ```
   l106test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l106test: Lesson 99 fallback reported`。）
3. 输入 `l98test`（上一课回归），预期输出：
   ```
   l98test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
4. 输入 `polltest`，预期输出：
   ```
   polltest: POLLIN/POLLOUT readiness transitions passed
   tinyos>
   ```
5. 输入 `pipeinfo`，确认 `used` 读数与 `polltest` 最后一步一致（读出一个后 `used=3`，可写电平为高）。
6. 输入 `about`，预期输出：
   ```
   Lesson 106: epoll 水平触发
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 106 checks passed.`；QEMU 中 `l106test`/`l98test` 均打印 `...checkpoint passed`、`polltest` 打印 `...transitions passed`，即代表本课 checkpoint 与水平触发主题的验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l106test` 打印 fallback 串 | `lesson_99_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=99,b=100`） |
| 输入 `l99test` 提示 unknown command | 旧 README 把 fallback 编号当成命令名；源码没有该命令 | 用 `l98test`/`l106test`；`help` 里的命令表为准 |
| `polltest` 打印 `BROKEN` | 电平判定与 `used` 不一致 | 逐段核对：空管道可写不可读、写 7 后可读、写满不可写、读 1 后恢复可写 |
| 混淆 LT/ET 上报次数 | 没分清"查电平"（`pipe_poll`）与"上报策略"（LT/ET）两层 | 对照 2.1 的时序图与 2.3 对比表 |
| 以为 LT 会漏事件 | LT 语义是"持续上报直到电平归零"，漏事件是 ET 的坑 | 用"写 7 后不读、再查 `pipe_poll(POLL_IN)`"实验验证电平仍为高 |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'epoll 水平触发' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l106test` 符号 | `grep -q 'l106test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：LT 是默认模式（不设 `EPOLLET`）**
- TinyOS 教学模型：LT = "就绪位为 1 就上报"；`pipe_poll` 每次返回当前电平。
- Linux：`epoll_ctl` 登记时 `event.events` 不设 `EPOLLET` 位即 LT；`EPOLLET` 定义在 `include/uapi/linux/eventpoll.h`。`ep_send_events()`（`fs/eventpoll.c`）从 `rdllist` 取事件并调用 `ep_item_poll` 重新评估；对 LT 条目，若仍就绪则**重新加入 `rdllist`**（`ep_poll_callback` 中 `if (!ep_is_linked(epi)) list_add_tail(...)`），保证下一次 `epoll_wait` 仍能看到它。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_send_events`、`ep_poll_callback`、`ep_scan_ready_list`）；`include/uapi/linux/eventpoll.h`。
- 教学简化：无 `rdllist` 链表与"重新入列"逻辑；用"同一电平反复查询结果相同"表达 LT 的持续上报。

**对照点 2：LT 与 poll/select 的语义继承**
- TinyOS：`pipe_poll` 即 poll 就绪模型，LT 就是"poll 结果直接上报"。
- Linux：`poll()`/`select()` 天然是 LT（每次调用重新扫描全部 fd）；epoll 的 LT 就是"为每个 fd 复刻 poll 行为 + 只扫描就绪列表"。所以 LT 条目的处理路径与 `do_poll` 高度同构。
- 权威来源：Linux v6.x `fs/select.c`（`do_poll`、`poll_schedule_timeout`）；`fs/eventpoll.c`。
- 教学简化：没有超时、没有 `select` 的位图 fd_set。

**对照点 3：忙转风险的正确姿势**
- TinyOS：概念上讲"消费慢时 LT 每次 `epoll_wait` 都返回同一个 fd → 忙转"。
- Linux：解决姿势通常是"LT + 非阻塞 fd + 内部缓冲累积"，或干脆改用 ET + 循环读；`epoll_wait` 的 `maxevents` 参数也会影响单轮处理量。
- 权威来源：`epoll(7)` man page；nginx `src/event/modules/ngx_epoll_module.c`（LT/ET 两种模式都支持，默认取决于配置）。
- 教学简化：无真实事件循环性能数据，仅作语义对比。

**对照点 4：LT 的"再上报"依赖重新入列**
- TinyOS：`polltest` 每次查询重新读电平，天然"再上报"。
- Linux：`ep_send_events` 用 `ep_scan_ready_list` 把 `rdllist` 搬到 `txlist`，处理完后**把仍就绪的 LT 条目放回 `rdllist`**；ET 条目则否（等下一次回调）。这就是"LT 绝不错过"的代码保证。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_scan_ready_list`、`ep_send_events`）。
- 教学简化：无搬移/回填逻辑，语义以查询层表现呈现。

---

## 8. 思考题与练习

1. **概念理解**：为什么说 LT 是"绝不错过"而 ET 是"高效但危险"？如果应用在 LT 下每次只处理一个字节，会发生什么？换成 ET 呢？
2. **源码定位**：在本课 `kernel64.c` 中，`pipe_poll` 返回的哪个位对应"可读电平"，哪个位对应"可写电平"？`pipe_model.used` 在哪两个函数里改变电平？
3. **动手实验**：在 `polltest` 的第 5 步（`e=pipe_model.used==1`）之后插入 `int z=pipe_poll(POLL_IN)==POLL_IN;`（不改变管道状态），断言 `z` 恒真，验证"电平在 `used` 不变时保持稳定"（LT 持续上报的查询层表现）。改完请**恢复原值**。
4. **动手实验**：把 `polltest` 的 while 循环改成只写两次（`used` 到 3 就停），观察 `f` 断言（`pipe_poll(POLL_OUT)==0`）变成假，理解"没写满就不触发可写电平翻转"。改完请**恢复原值**。
5. **Linux 对照**：阅读 `fs/eventpoll.c` 的 `ep_scan_ready_list`，说明"处理完把仍就绪的 LT 条目放回 rdllist"对应本课 2.3 对比表里的哪一行；删掉这一步会导致什么后果？

---

## 9. 本课小结与下一课预告

- 本课把"水平触发"讲透了：LT 只看当前电平（`R==1`），不记忆变化，条件满足就持续上报。
- 你完成了 LT vs ET 的三维对比：上报次数（持续 vs 沿）、忙转风险（高 vs 低）、漏事件风险（无 vs 高）。
- 你理解了 LT 的默认地位：与 `poll`/`select` 语义同构、使用简单、绝不丢事件，代价是消费慢时的忙转。
- 你用同一段 `polltest` 代码分别演示了"电平读数"（LT）与"两次翻沿"（ET）两种读法，看清了查询层与上报层的分离。
- 你完成了 checkpoint 验证：`l106test` 与回归 `l98test` 打印 `bounded VFS, devices, epoll, and service management checkpoint passed`，并把旧 README 的 `l99test` 命令勘误为源码真实的 `l98test`/`l106test`。

**下一课预告**：Lesson 107「epoll wait/wake 集成」。ET 和 LT 讲完了"报什么"，下一课讲"谁来报、怎么等"：`epoll_wait` 没有就绪事件时把调用者挂进实例的 wait 队列睡下，fd 就绪回调通过 `wake_up(&ep->wq)` 把它唤醒；教学模型里对应的正是继承的 `event_set`/`event_wait`/`waitq_wake_all` 和 `THREAD_BLOCKED_EVENT` 状态。见 [`../lesson-107-stable/README.md`](../lesson-107-stable/README.md)。

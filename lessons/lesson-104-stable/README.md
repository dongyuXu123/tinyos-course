# Lesson 104: epoll 实例与固定 watch 表 — 精讲文档

> **课号**：Lesson 104（主线源课编号 Lesson 97 线）
> **本课主题**：epoll 实例与固定 watch 表——一个 epoll 实例（eventpoll 对象）如何"登记"一批 fd 的 watch 条目，每个条目保存自己的兴趣掩码；`epoll_ctl` 的 ADD/MOD/DEL 如何维护这张固定容量的 watch 表
> **课程主线位置**：VFS / 设备 / epoll / 服务管理教学模型阶段（Lesson 81 起的「checkpoint 课」系列）。Lesson 103 讲"单 fd 就绪查询"（poll），本课把就绪模型升级成"一对多"：一个实例管多个 fd。后续 105–107 分别讲 epoll 的边沿触发（ET）、水平触发（LT）与 wait/wake 集成。
> **前置课程**：[`../lesson-103-stable/README.md`](../lesson-103-stable/README.md)（poll 就绪队列：`pipe_poll(mask)` 的就绪位语义、`polltest` 状态迁移）
> **后续课程**：[`../lesson-105-stable/README.md`](../lesson-105-stable/README.md)（epoll 边沿触发：就绪只在"状态翻转"时上报一次）
> **本课一句话目标**：理解"epoll 实例 = 一个对象 + 一张固定容量的 watch 表"，每个 watch 条目携带"fd + 兴趣掩码"，实例用一个就绪列表回答 `epoll_wait`——本课先建立这个结构与语义，再在 Linux 对照里找到它的真身（`struct eventpoll` + `struct epitem`）。
> **保留的原始快照信息**：This checkpoint models bounded VFS, devices, epoll, and service management metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l96test` + `l104test`（**勘误**：旧 README 标注的 `l97test` 在源码命令表中并不存在——源码 `exec64` 分派的是 `l96test` 与 `l104test`），plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能讲清 epoll 的**对象模型**——"一个实例（instance）持有 N 个 watch 条目，每个条目 = fd + 兴趣掩码 + 就绪状态"，并能把本课源码里的固定容量容器（wait 队列环、任务表等）对应成"教学版的实例/ watch 表 / 就绪列表"。
- **在课程主线中的位置**：checkpoint 课（Lesson 81 起）系列中的「epoll 实例」检查点。本系列每课源码增量只有几行（上一课测试改名 + 新测试 + about/banner 文案），主题由课程标签承载；本课标签是「epoll 实例与固定 watch 表」，源码中并没有出现 `epoll` 标识符（`kernel64.c` 里唯一的 `epoll` 字样来自 checkpoint 断言串 `bounded VFS, devices, epoll, and service management checkpoint passed`）——因此本课把 epoll 的**概念模型**讲透，用**继承自更早课程**的固定容量结构做教学映射，并在 Linux 对照里给出真身。
- **前置知识清单**（学本课之前必须掌握）：
  1. poll 就绪位语义：`POLL_IN`/`POLL_OUT` 与 `pipe_poll`（Lesson 103）；
  2. 固定容量容器：`struct wait_queue { u8 ids[WAIT_QUEUE_CAP]; ... }` 的环形表、`task_table[TASK_TABLE_CAP]`（Lesson 37 起）；
  3. `exec64` 命令分派与 VGA 输出（Lesson 34 起）；
  4. 事件与唤醒原语：`event_set`/`event_wait`/`waitq_wake_all`（Lesson 78 起的调度模型）。
- **本课交付**：新增固定容量记录 `struct lesson_97_model` + `lesson_97_state` + `l104test`；把 `l103test` 改名为 `l96test`；`about` 与 banner 更新为「Lesson 104: epoll 实例与固定 watch 表」。

---

## 2. 核心概念精讲

### 2.1 epoll 实例：从"查一个"到"管一堆"

**直觉**：Lesson 103 的 `pipe_poll` 每次只回答"一根管道就绪吗"。真实程序往往同时盯着十几个 fd（监听 socket、若干个连接、键盘……）。逐个 poll 太啰嗦，于是内核把"要盯的一堆 fd"**打包成一个对象**，这就是 epoll 实例（epoll instance）。`epoll_create()` 返回一个实例的句柄，之后所有"加进去、改兴趣、等事件"都对着这个句柄做。

**定义**：一个 epoll 实例包含：
1. **interest list（interest 表 / watch 表）**：所有被登记（`EPOLL_CTL_ADD`）进来的 fd 条目，每条记着"这个 fd、我对它感什么兴趣（读/写/……）"；
2. **ready list（就绪列表）**：当前确实就绪的条目集合，`epoll_wait` 直接从这里取事件。

```
epoll 实例（eventpoll）
  ├─ watch 表（interest list，固定容量）:
  │    watch[0] = { fd=7,  events=EPOLLIN }
  │    watch[1] = { fd=9,  events=EPOLLIN|EPOLLOUT }
  │    watch[2] = { fd=12, events=EPOLLOUT }
  ├─ 就绪列表（ready list）: fd=9（可读）、fd=12（可写）→ 一次 epoll_wait 返回 2 个事件
  └─ wait 队列: epoll_wait 没事件时睡在这里，被唤醒后重新扫描
```

### 2.2 固定 watch 表：容量有界的登记册

**为什么固定容量**：教学内核里所有容器都定长（`fd_table[FD_MAX]`、`task_table[TASK_TABLE_CAP]`、`wait_queue.ids[WAIT_QUEUE_CAP]`）。epoll 的 watch 表也走同一路线：**登记满就拒绝**，绝不动态分配。这与真实 Linux 不同（Linux 的 interest list 是红黑树，可容纳数万条目），是"bounded"教学模型的一贯取舍。

**语义**：每条 watch 记录三个要素——**目标 fd**（听谁的）、**兴趣掩码**（听什么）、**就绪状态**（现在响没响）。`epoll_ctl` 的三个操作正好对应登记册的三个动作：
- `EPOLL_CTL_ADD`：新增一条（fd 没登记过才成功）；
- `EPOLL_CTL_MOD`：改某条的兴趣掩码；
- `EPOLL_CTL_DEL`：删掉某条。

本课的 `lesson_97_model` 四个 u32 + 四个标志位就是这种"固定记录"的缩影：每课 checkpoint 都在反复演示"固定容量记录 + 确定性校验"这一教学模型。

### 2.3 就绪状态如何进入 watch 条目

与 Lesson 103 一致：就绪判定是"兴趣掩码 × 实时状态"的交集。对管道这个状态就是 `used`：
- 想听可读（掩码含 `POLL_IN`）且 `used>0` → 就绪；
- 想听可写（掩码含 `POLL_OUT`）且 `used<PIPE_CAP` → 就绪。

watch 表本身不存数据，它只存"兴趣"，实时状态每次查底层的 fd（`pipe_poll`）。这条"查表"链是 epoll 一切后续语义（ET/LT）的基础。

### 2.4 checkpoint 固定元数据

`struct lesson_97_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担系列 checkpoint：`l104test` 断言 `b==a+1`（97+1=98）与四标志。`ready` 标志字面就是"就绪"位——巧合的是它正好呼应本课主题：每条固定记录都带一个"是否就绪"的标志位。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-103） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：固定容量容器（watch 表类比）、wait 队列/事件、`exec64` 分派 | **微增量（仅 3 行）**：新增 `struct lesson_97_model` + `lesson_97_state` + `l104test()`；把 `l103test` 改名为 `l96test`；`about`/banner/`exec64` 文案更新（epoll 概念模型本课讲解；watch 表/就绪列表的机制容器继承自更早课程） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成「epoll 实例与固定 watch 表 / l104test / Lesson 104」关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的 checkpoint 元数据：`l104test`

```c
static TEXT64 void l104test(u16*c){lesson_97_state=(struct lesson_97_model){97U,98U,99U,100U,1,1,1,1};int ok=lesson_97_state.valid&&lesson_97_state.active&&lesson_97_state.ready&&lesson_97_state.accounted&&lesson_97_state.b==lesson_97_state.a+1U;text64(c,"l104test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 97 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_97_state=(struct lesson_97_model){97U,98U,99U,100U,1,1,1,1};`：`a=97,b=98,c=99,d=100`，四标志全 1——这是"固定记录 + 就绪标志"的教学样本。
- `int ok=...`：四标志与 `b==a+1U` 五条件与，`98==97+1` 必须成立。
- 成功串 `bounded VFS, devices, epoll, and service management checkpoint passed`、失败串 `Lesson 97 fallback reported` 逐字来自源码。
- **设计说明**：fallback 里的 97 是被校验状态号，**不是**命令名——旧 README 据此误写 `l97test`；本课真实命令是 `l96test` 与 `l104test`。这条链上每个 checkpoint 断言与 `polltest` 的就绪断言互相独立：前者验证元数据记录，后者验证就绪行为。

#### (b) 上一课测试改名为 `l96test`

```c
static TEXT64 void l96test(u16*c){lesson_96_state=(struct lesson_96_model){96U,97U,98U,99U,1,1,1,1};int ok=lesson_96_state.valid&&lesson_96_state.active&&lesson_96_state.ready&&lesson_96_state.accounted&&lesson_96_state.b==lesson_96_state.a+1U;text64(c,"l96test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 96 fallback reported");putc64(c,'\n');}
```

- lesson-103 里它叫 `l103test`（校验同一份 `lesson_96_state`）；本课改名 `l96test`，测试名与被校验状态号对齐。
- `exec64` 分派把 `l103test` 分支换成 `l96test` 分支，并新增 `l104test` 分支：
```c
else if(eq64(word,"l96test")){if(!noargs64(arg))usage64(c,"l96test");else l96test(c);}
else if(eq64(word,"l104test")){if(!noargs64(arg))usage64(c,"l104test");else l104test(c);}
```

#### (c) 固定容量容器：教学版的"watch 表"

epoll 的 watch 表在真实内核里是动态红黑树，教学模型则用固定容量容器表达"登记 → 查表 → 取就绪"三件事。以下结构全部来自本课源码（继承自更早课程），逐个讲清它们与 epoll 概念的对应：

```c
struct wait_queue { u8 ids[WAIT_QUEUE_CAP],head,tail,count; u64 enqueues,wake_one,wake_all; };
```

- `ids[WAIT_QUEUE_CAP]`：定长条目数组，`WAIT_QUEUE_CAP = THREAD_COUNT-1`。**对应 watch 表的"登记册"**——每条登记一个等待者 id；
- `head/tail/count`：环形使用记账。**对应就绪列表的"当前有几条"**；
- `enqueues/wake_one/wake_all`：统计字段，对应 epoll 每次 ADD/唤醒的计数语义。

```c
struct event { u8 signaled; volatile struct wait_queue waitq; u64 sets,resets,waits,wakes; };
```

- `signaled` 是"事件是否已置位"标志——**对应 epoll 实例的就绪状态位**；`waitq` 是"谁在等这个事件"的登记册，`sets/waits/wakes` 是计数。一个 `struct event` 就是最小号的"epoll 实例"：一个状态位 + 一个等待者表 + 四组统计。

```c
struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; };
#define TASK_TABLE_CAP 4U
static struct task_struct task_table[TASK_TABLE_CAP];
```

- `task_table` 是又一个定长表：**对应"固定容量的 watch 表"这一模式**——登记满（4 条）就不再接受，`task_table_validate()` 做全表一致性检查。模式可复用：epoll 实例的 watch 表在概念上就是 `task_table` 这种"定长数组 + 每元素 `valid` 标志 + 全表校验"。

#### (d) 就绪判定：watch 条目的"兴趣 × 状态"

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

- 这就是一条 watch 条目评估自己的过程：**`mask` = 条目的兴趣掩码**，`pipe_model.used` = 底层 fd 的实时状态，返回的 `ready` = 该 fd 是否进就绪列表。
- 一个 epoll 实例有 N 条 watch，就执行 N 次这样的评估；所有 `ready!=0` 的条目进入就绪列表，`epoll_wait` 一次性带走。
- 与 Lesson 103 相同：查询只读 `used`，不阻塞、不破坏状态——"就绪"永远是快照。

#### (e) `exec64` 增量、`about` 与 banner

`about` 分支逐字来自源码：
```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 104: epoll 实例与固定 watch 表\n");}
```
banner 逐字来自 `kernel_main64_binary`：
```c
text64(&c,"Lesson 104: epoll 实例与固定 watch 表\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'epoll 实例与固定 watch 表' README.md
	@grep -q 'l104test' kernel64.c
	@grep -q 'Lesson 104' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 104 checks passed.'
```

- 与 lesson-103 的唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链不变（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx ... -Werror` → `kernel64.bin` → 内嵌 → ELF32 → ISO）。`kernel64.ld` 的三块 guard/stack 页与 `ASSERT`、`linker.ld` 的 `KEEP(.multiboot)` 均未变化。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ 初始化（同 Lesson 103）
    ├─ banner: "Lesson 104: epoll 实例与固定 watch 表\nGETTICKS, ...\n"
    └─ for(;;) 键盘循环
        ├─ "l104test" ──► lesson_97_state 校验（a=97,b=98）──► "l104test: bounded VFS, ... checkpoint passed"
        ├─ "l96test"  ──► lesson_96_state 校验（上一课回归）
        ├─ "polltest" ──► pipe_poll 就绪迁移（复习：watch 条目的"兴趣×状态"）
        └─ "pipeinfo" ──► 管道就绪统计
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 完成全套初始化（含 `vfs_init`→`pipe_init`），banner 打出「Lesson 104: epoll 实例与固定 watch 表」。
2. **概念流（epoll 实例）**：输入 `l104test` → `exec64` 的 `l104test` 分支 → `l104test(c)` → 新建 `lesson_97_state` 固定记录 → 校验 `b==a+1` 与四标志 → VGA 打印 `l104test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **概念流（watch 表评估）**：输入 `polltest` → 8 个断言把"空/有数据/满/再空"四个阶段跑一遍，等价于 4 次"评估一条 watch 条目"（`mask` 固定，`used` 变化）——这就是"一张 watch 表在 fd 状态变化时的就绪变迁"的最小演示。
4. **回归**：`l96test` 校验上一课的 `lesson_96_state`，保证改名后的回归链不断。
5. **统计窗口**：`pipeinfo` 打印 `used/capacity`、`reads/writes`、`blocked r/w`、`wake r/w`——对应就绪列表之外"事件计数"的观察口。

输出串与源码逐字一致：`l104test: bounded VFS, devices, epoll, and service management checkpoint passed`；`polltest: POLLIN/POLLOUT readiness transitions passed`。

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
   Lesson 104: epoll 实例与固定 watch 表
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l104test`，预期输出：
   ```
   l104test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l104test: Lesson 97 fallback reported`。）
3. 输入 `l96test`（上一课回归），预期输出：
   ```
   l96test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
4. 输入 `polltest`（复习就绪迁移），预期输出：
   ```
   polltest: POLLIN/POLLOUT readiness transitions passed
   tinyos>
   ```
5. 输入 `about`，预期输出：
   ```
   Lesson 104: epoll 实例与固定 watch 表
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 104 checks passed.`；QEMU 中 `l104test`/`l96test` 均打印 `...checkpoint passed`，即代表本课 checkpoint 与 epoll 实例主题的验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l104test` 打印 fallback 串 | `lesson_97_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=97,b=98`） |
| 输入 `l97test` 提示 unknown command | 旧 README 把 fallback 编号当成命令名；源码没有该命令 | 用 `l96test`/`l104test`；`help` 里的命令表为准 |
| `polltest` 打印 `BROKEN` | 就绪位判定与 `used` 不一致 | 见 Lesson 103 调试地图：逐段核对空/写/满/读四态 |
| `l96test` 打印 fallback 串 | 改名后 `l96test` 校验的 `lesson_96_state` 被破坏 | 检查 `lesson_96_state` 初始化与 `ok` 表达式（`a=96,b=97`） |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'epoll 实例与固定 watch 表' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l104test` 符号 | `grep -q 'l104test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：epoll 实例 = `struct eventpoll`**
- TinyOS 教学模型：本课没有 `eventpoll` 结构体，用"固定容量容器 + 状态位"类比（`struct event` 的 `signaled` + `waitq`，`task_table` 的定长数组模式）。概念上：一个实例对象 + 一张 watch 表 + 一个就绪列表 + 一个 wait 队列。
- Linux 实现：`fs/eventpoll.c` 的 `struct eventpoll` 包含 `wq`（`wait_queue_head_t`，`epoll_wait` 睡在这）、`rdllist`（ready list，`struct list_head`）、`ovflist`、`rbr`（红黑树根，interest list 的组织形式）、`user`、`file` 等。`epoll_create1()` → `ep_alloc()` 分配并初始化这个对象。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`struct eventpoll`、`ep_alloc`、`ep_create`）；`include/linux/eventpoll.h`。
- 教学简化：固定容量（无红黑树、无 `epoll_create` 的 `EPOLL_CLOEXEC` 参数），实例就是"一个对象 + 一张定长表"。

**对照点 2：watch 表 = interest list（`struct epitem`）**
- TinyOS：固定容量登记册（`ids[WAIT_QUEUE_CAP]`、`task_table[TASK_TABLE_CAP]`），登记满即拒绝。
- Linux：每条 watch 是一个 `struct epitem`，包含 `ffd`（目标 `struct file` + fd）、`event`（`struct epoll_event`，用户可见的兴趣掩码与 data）、`nwait`、`rbn`（红黑树节点）、`rdllink`（就绪列表节点）、`pwqlist`（poll wait queue 列表）。interest list 由 `rbr` 红黑树组织，按 fd 排序，`ep_find()` 用 `ep_cmp_ffd` 在树里查找。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`struct epitem`、`ep_find`、`ep_cmp_ffd`）。
- 教学简化：没有红黑树（线性数组 + 线性扫描），每条记录也没有 `nwait/pwqlist` 等回调挂钩——但这正是"固定容量"模型的代价与教学收益：一眼看穿登记册的全部条目。

**对照点 3：ADD/MOD/DEL = `epoll_ctl`**
- TinyOS：无 `epoll_ctl` 实现；概念上"登记/改兴趣/删登记"三动作对应本课讲解的 watch 表维护。
- Linux：`epoll_ctl(epfd, EPOLL_CTL_ADD|MOD|DEL, fd, &ev)`：ADD → `ep_insert()`（`ep_find` 查重、`ep_item_poll` 调 fd 的 `.poll()` 建立回调、插入红黑树、若立即就绪则加入 rdllist）；MOD → `ep_modify()`；DEL → `ep_remove()`。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`epoll_ctl`、`ep_insert`、`ep_modify`、`ep_remove`）；`include/uapi/linux/eventpoll.h`（`EPOLL_CTL_ADD=1`、`EPOLL_CTL_MOD=2`、`EPOLL_CTL_DEL=3`、`struct epoll_event`）。
- 教学简化：ADD 时"立即就绪则入就绪列表"这一步在本课没有代码对应，但在 Lesson 105（ET）与 106（LT）的语义讲解中必须理解它——**注册时就绪的 fd 要不要立刻上报，正是 ET 与 LT 的第一个分叉点**。

**对照点 4：就绪位 = 兴趣掩码 × 实时状态**
- TinyOS：`pipe_poll(mask)` 返回"掩码 ∩ 状态"，就绪列表 = 所有 `ready!=0` 的条目。
- Linux：`ep_send_events()` 遍历 ready list，把 `epi->event` 拷贝到用户数组；`ep_item_poll` 每轮都会重新调 fd 的 `.poll()` 拿最新就绪位，防止过期。
- 权威来源：Linux v6.x `fs/eventpoll.c`（`ep_send_events`、`ep_item_poll`）。
- 教学简化：没有 `eventpoll` 内核结构体与就绪列表的双向链表，`pipe_poll` 的 `ready` 就是一条 watch 单次评估的结果。

---

## 8. 思考题与练习

1. **概念理解**：epoll 实例为什么要同时维护"interest 表"和"就绪列表"两份东西？如果 `epoll_wait` 每次都临时遍历 interest 表重新 poll 所有 fd，正确性上有什么问题、性能上呢？
2. **源码定位**：在本课 `kernel64.c` 里找出三个"固定容量容器"（结构体 + 容量宏），分别说出它们对应 epoll 概念的哪一部分（实例 / watch 表 / 就绪列表）。
3. **动手实验**：在 `pipe_poll` 里临时去掉 `if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)` 这一句，重新 `make run` 并跑 `polltest`，观察哪几个断言变成假、最后打印什么。改完请**恢复原值**。
4. **动手实验**：把 `TASK_TABLE_CAP` 从 `4U` 改成 `2U`，`make run` 后跑 `taskvalidate`，观察固定容量减小是否让既有任务登记失败/校验失败。改完请**恢复原值**。（提示：理解"固定容量"在超限时的表现正是本课主题的要点。）
5. **Linux 对照**：阅读 `fs/eventpoll.c` 的 `ep_insert`，找出"fd 登记后若立即就绪则加入 rdllist"的代码位置，并说明这一步对 ET/LT 两种语义分别意味着什么（预告：这直接影响 Lesson 105/106 的首次上报行为）。

---

## 9. 本课小结与下一课预告

- 本课建立了 epoll 的对象模型：**实例 = 对象 + watch 表 + 就绪列表 + wait 队列**，四个部分在本课源码的固定容量容器里都有教学对应。
- 你理解了 watch 条目的三要素（fd、兴趣掩码、就绪状态）与 `epoll_ctl` 的 ADD/MOD/DEL 三动作。
- 你复习并深化了"就绪 = 兴趣掩码 × 实时状态"（`pipe_poll`），知道它就绪列表的过滤条件。
- 你在 Linux 对照里见到了真身：`struct eventpoll`、`struct epitem`、`ep_insert`，以及"注册时立即就绪"这个 ET/LT 分叉点。
- 你完成了 checkpoint 验证：`l104test` 与回归 `l96test` 打印 `bounded VFS, devices, epoll, and service management checkpoint passed`，并把旧 README 的 `l97test` 命令勘误为源码真实的 `l96test`/`l104test`。

**下一课预告**：Lesson 105「epoll 边沿触发」。watch 表建好了，"什么时候上报"就成问题：一个 fd 可读的状态持续很久，是一直报（水平触发 LT）还是只在"从不可读到可读"的翻沿报一次（边沿触发 ET）？下一课以 ET 开头，讲 `EPOLLET` 标志与"就绪上报一次、复位后不再报"的语义，并解释"必须把数据一次性读干净，否则会丢事件"的经典坑。见 [`../lesson-105-stable/README.md`](../lesson-105-stable/README.md)。

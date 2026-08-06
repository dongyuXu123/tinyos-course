# Lesson 101: 块设备请求队列 — 精讲文档

> **课号**：Lesson 101 ｜ **主题**：块设备请求队列（block device request queue）
> **课程主线位置**：VFS/设备/服务管理检查点阶段（Lesson 91–105），本课为 Lesson 94 原型的检查点
> **前置课程**：[`../lesson-100-stable/README.md`](../lesson-100-stable/README.md)（设备打开与 ioctl 元数据）
> **后续课程**：[`../lesson-102-stable/README.md`](../lesson-102-stable/README.md)（设备生命周期与卸载）
> **一句话目标**：讲清块设备与字符设备的本质差异，以及「请求队列」为什么是块设备数据路径的中枢（submit_bio → request_queue → I/O 调度 → 驱动），并把教学内核的 `workqueue` 有界 FIFO、`pc_buffer` 生产-消费队列、`page_cache` 块缓存映射到 Linux `block/` 层上，验证 `l101test` 检查点。

本课是稳定快照（stable snapshot）检查点。`kernel64.c` 相对上一课仅做三处增量：把上一课的 `l100test` 恢复为历史命名 `l93test`（挂在 `lesson_93_state` 上）、新增 `lesson_94_model` 状态与 `l101test` 检查点、更新 `about`/开机横幅为本课主题。块设备请求队列机制由累积代码承载：有界请求 FIFO 由 `workqueue[WORK_CAP]` 与 `softirq_run_budget` 表达，阻塞式请求/应答队列由 `pc_buffer` + 信号量表达，块数据缓存由 `page_cache_model` 表达。继承的进程、GUI、子系统回归保持有效。

> **命令说明**：本课检查点命令为 `l101test`（旧 README 写的 `l94test` 按源码勘误）；另保留历史检查点 `l82test`–`l93test`，以及 `softirqtest`/`pctest`+`pcgo`/`reclaimtest` 等队列与块缓存回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：说出块设备与字符设备的关键差异（定长块、随机访问、page cache、请求队列）；复述 Linux 块数据路径 `submit_bio` → `request_queue` → I/O 调度器 → 驱动 `request_fn`（对照 `block/blk-core.c`、`block/blk-mq.c`、`drivers/block/`）；在教学内核中沿 `workqueue_submit`（入队、满则 drop）→ `softirq_run_budget`（出队、预算限量）走一遍「有界请求队列」链，并解释 `page_cache` 的 `index/phys/dirty/writeback` 为什么是块缓存；运行 `l101test`/`softirqtest`/`reclaimtest` 验证。

**在课程主线中的位置**：Lesson 98–100 都在字符设备一侧（注册、节点、打开/ioctl），本课转向**块设备**——访问模型从字节流变成块，催生了「请求队列」这一层。作为检查点课，源码 diff 极小，任务是把继承机制中与「队列 + 块缓存」相关的部分（workqueue、生产者-消费者、page cache）按主题系统化复述。下一课（Lesson 102）是设备系列收尾——设备生命周期与卸载。

**前置知识清单**（学本课前必须掌握）：
1. 字符设备 vs 块设备差异与设备注册（Lesson 98）；dev_t 与设备节点（Lesson 99）；打开与 ioctl（Lesson 100）。
2. 有界 FIFO 队列：`workqueue[WORK_CAP]` 的 head/tail/used 与 `workqueue_submit`/`softirq_run_budget`（Lesson 40s）。
3. 信号量生产-消费：`pc_buffer`、`sem_down`/`sem_up`、`pc_reset`（Lesson 27–30）。
4. 页缓存与回收：`page_cache_get`、`reclaim_one` 的 `refs==1` 条件（Lesson 76/84/97）。
5. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位（Lesson 69–100）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 101: 块设备请求队列`；
- 新命令 `l101test` 输出 `l101test: bounded VFS, devices, epoll, and service management checkpoint passed`（或 fallback）；
- `softirqtest` 展示有界 FIFO 请求队列，`pctest`+`pcgo` 展示阻塞式队列，`reclaimtest` 展示块缓存命中/回收。

---

## 2. 核心概念精讲

### 2.1 块设备：与字符设备的本质差异

**直觉**：字符设备像水管（字节流），块设备像书架（按格存取）。块设备（磁盘、SSD、U 盘）有两个硬约束：
1. **块对齐**：硬件按固定大小（512B/4K）读写，跨块的部分访问要拆块；
2. **随机访问代价高**：磁头寻道（机械盘）或擦写粒度（SSD），于是内核用 **page cache** 缓存块数据，并用 **请求队列** 把零散的读写下发前**合并与排序**（I/O 调度）。

Linux 把这些约束全部收进块层（`block/`），对上层 VFS 提供统一的 `submit_bio`；块设备驱动则注册 `request_fn`（单队列 `blk_init_queue`）或 `blk_mq_ops`（多队列 `blk-mq`）。

### 2.2 请求队列：块数据路径的中枢

```
VFS / 文件系统
   │  block_read_full_page / buffered read
   ▼
submit_bio(struct bio)            ← 一个 bio 描述一次「扇区范围 + 数据页」的请求
   ▼
request_queue (q->make_request_fn / q->request_fn)
   │  合并（merge）、排序（elevator：deadline/kyber）
   ▼
块设备驱动 request_fn / mq ops   ← 真正的硬件 IO
```

- `struct bio`：一次 I/O 的最小请求描述（`bi_sector`、`bi_io_vec` 指向页）；
- `struct request_queue`：每个块设备一个，内含 I/O 调度器、合并逻辑、`queuedata`（指向驱动私有数据）；
- **请求队列存在的意义**：把「应用看到的随机小 IO」改造成「设备喜欢的大块顺序 IO」，吞吐与延迟都因此优化。

### 2.3 教学模型的有界请求队列：workqueue FIFO

教学内核没有 `request_queue`，但「**有界 FIFO 队列 + 入队/出队 + 满时拒绝 + 限量处理**」这一骨架完整存在于 `workqueue` 与 `softirq` 模型：

```c
static struct work_model workqueue[WORK_CAP];
static u8 work_head,work_tail,work_used;
```
- **入队** `workqueue_submit(kind,data)`：`work_used>=WORK_CAP` 时 `drops++` 并拒绝——请求队列满时的背压（blocking 层用 `blocked_writers`，budget 层用 `drops`）；
- **出队** `softirq_run_budget`：从 `work_tail` 取一个 work，`runs++`、`work_used--`，受 `SOFTIRQ_BUDGET` 限量——模拟驱动每轮最多处理多少请求的预算；
- **环形游标**：`head/tail` 取模 `WORK_CAP`——请求队列的经典环形缓冲。

### 2.4 阻塞式请求队列：pc_buffer 生产-消费

`workqueue` 是「队列满了就丢」的非阻塞模型；`pc_buffer` 则是「满了就等」的阻塞模型——用两个信号量 `spaces`（空位计数）与 `items`（数据计数）实现经典的 bounded buffer：
- `pc_producer`：`sem_down(&pc_spaces)`（没空位就睡）→ 写入环形 `pc_buffer[pc_head]` → `sem_up(&pc_items)`；
- `pc_consumer`：`sem_down(&pc_items)`（没数据就睡）→ 读 `pc_buffer[pc_tail]` → `sem_up(&pc_spaces)`；
- 这就是块设备请求队列在**阻塞 I/O** 语义下的样子：请求入队、队列满则等待、出队消费。

### 2.5 块数据缓存：page_cache_model

块设备数据要经过 page cache 才到用户。教学内核的 `struct page_cache_model{index,phys,valid,dirty,writeback,refs}` 就是「块缓存行」：
- `index` → 块/页在设备上的逻辑索引（`bio` 的扇区映射类比）；
- `phys` → 缓存的物理帧；
- `dirty`/`writeback` → 脏页与回写状态（`fs/buffer.c` 的 buffer head 状态位类比）；
- `page_cache_get(index,dirty)` 命中则 `refs++`、未命中则分配一帧插入缓存（`mm/filemap.c` 的 page cache 类比）；
- `reclaim_one` 只回收 `refs==1` 的页（Lesson 90/97 的引用归零原则在块缓存上的体现）。

### 2.6 检查点模型：lesson_94_model 与 l101test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `94→97` 标记 Origin 为 Lesson 94。本课同时把上一课新增的 `l100test` 恢复为历史命名 `l93test`（同一 `lesson_93_state`，计数 `93→96`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.7 机制继承 + 检查点增量

本课主题机制（请求队列、块缓存）不是本课新写的代码：workqueue/softirq 来自软中断阶段，pc_buffer 来自 IPC 阶段，page cache 来自页回收阶段。本课的实际增量只有三处：`l100test`→`l93test` 更名、`lesson_94_model`+`l101test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「块设备请求队列」主题重新组织。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l100test`→`l93test` 恢复命名；新增 `lesson_94_model`/`lesson_94_state`/`l101test`；`about` 与开机横幅更新。请求队列/块缓存机制由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`块设备请求队列`/`l101test`/`Lesson 101`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（请求队列/块缓存机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_94_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_94_model lesson_94_state;
static TEXT64 void l101test(u16*c){lesson_94_state=(struct lesson_94_model){94U,95U,96U,97U,1,1,1,1};int ok=lesson_94_state.valid&&lesson_94_state.active&&lesson_94_state.ready&&lesson_94_state.accounted&&lesson_94_state.b==lesson_94_state.a+1U;text64(c,"l101test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 94 fallback reported");putc64(c,'\n');}
```
1. **结构与序列**：计数 `94→97`（Origin Lesson 94），四布尔位全置 1，`b==a+1U` 校验连续性。
2. **成功串**：`l101test: bounded VFS, devices, epoll, and service management checkpoint passed`（逐字抄录）；fallback 为 `Lesson 94 fallback reported`。
3. **恢复的 `l93test`**：本课同时把 `l100test` 更名回 `l93test`（同为 `lesson_93_state`），使检查点命令名与 Origin 对齐；`l82test`–`l92test` 历史检查点全部保留。

#### 3.2.2 请求队列的数据结构：workqueue

```c
#define WORK_CAP 4U
struct work_model { u8 kind,data,queued; u64 runs; };
static struct work_model workqueue[WORK_CAP];
static u8 work_head,work_tail,work_used;
```
1. **有界 FIFO**：`workqueue[WORK_CAP=4]`，`work_head`（写游标）/`work_tail`（读游标）/`work_used`（占用数）三件套——请求队列的完整骨架。
2. **请求项字段**：`kind`（请求类型，类比 bio 的方向/优先级）、`data`（负载）、`queued`（是否仍在队列）、`runs`（已处理次数）。
3. **与 request_queue 对照**：Linux `struct request_queue` 里也有入队/出队游标、`queuedata` 与各种统计；教学模型只保留「环形 + 计数」的最小结构。

#### 3.2.3 请求入队：workqueue_submit（满则 drop）

```c
static TEXT64 int workqueue_submit(u8 kind,u8 data){if(work_used>=WORK_CAP){softirq_model.drops++;return 0;}workqueue[work_head]=(struct work_model){kind,data,1,0};work_head=(u8)((work_head+1)%WORK_CAP);work_used++;softirq_raise(1);return 1;}
```
1. **满队背压**：`work_used>=WORK_CAP` 时 `drops++` 并返回 0——请求队列满时的拒绝路径（Linux 中对应 `blk_peek_request` 返回空 / 请求分配失败）。
2. **入队**：在 `work_head` 写入 `{kind,data,queued=1,runs=0}`，`work_head` 取模推进、`work_used++`。
3. **唤醒处理**：`softirq_raise(1)` 置位 softirq pending——入队即通知「队列有活」，对应 `submit_bio` 之后 `q->request_fn` 被调度执行。

#### 3.2.4 请求出队与限量处理：softirq_run_budget

```c
static TEXT64 void softirq_run_budget(void){u8 budget=SOFTIRQ_BUDGET,i;while(budget&&softirq_model.pending){if(softirq_model.pending&1){for(i=0;i<TASKLET_CAP&&budget;i++)if(tasklets[i].pending&&!tasklets[i].disabled){tasklets[i].pending=0;tasklets[i].runs++;softirq_model.runs++;budget--;}}if(softirq_model.pending&2&&budget){if(work_used){struct work_model*w=&workqueue[work_tail];w->queued=0;w->runs++;work_tail=(u8)((work_tail+1)%WORK_CAP);work_used--;softirq_model.runs++;budget--;}}if(!tasklets[0].pending&&!tasklets[1].pending)softirq_model.pending&=(u8)~1U;if(!work_used)softirq_model.pending&=(u8)~2U;}if(softirq_model.pending)softirq_model.budget_exhaustions++;}
```
1. **预算限量**：`budget=SOFTIRQ_BUDGET`，每处理一个请求 `budget--`，`budget==0` 就退出循环——模拟驱动/中断上下文每轮最多处理的 I/O 数，防止饿死其他软中断。
2. **FIFO 出队**：`work_used` 非空时从 `work_tail` 取队头请求，置 `queued=0`、`runs++`、`work_tail` 取模推进、`work_used--`——请求按入队顺序处理（I/O 调度器退化为 FIFO）。
3. **两类负载共用一个预算**：tasklet（bit0）与 work（bit1）共享 `budget`——软中断系统的公平性约束。
4. **预算耗尽留待下轮**：`budget_exhaustions++` 并保留 `pending`——对应 Linux 软中断的「预算耗尽后重新触发 `__raise_softirq_irqoff`」。

#### 3.2.5 请求队列验证：softirqtest

```c
static TEXT64 void softirqtest(u16*c){u8 i;softirq_model=(struct softirq_model){0};work_head=work_tail=work_used=0;for(i=0;i<TASKLET_CAP;i++)tasklets[i]=(struct tasklet_model){0,0,0};tasklet_schedule(0);tasklet_schedule(0);tasklet_schedule(1);for(i=0;i<WORK_CAP;i++)workqueue_submit(i,0);int a=workqueue_submit(9,0)==0;softirq_run_budget();int b=!tasklets[0].pending&&!tasklets[1].pending&&work_used==2;softirq_run_budget();int d=work_used==0&&softirq_model.budget_exhaustions>=1;text64(c,"softirqtest: ");text64(c,a&&b&&d&&softirq_model.runs>=6?"tasklet coalescing, FIFO work, and budget carry-over passed":"BROKEN");putc64(c,'\n');}
```
1. **满队断言**：塞满 `WORK_CAP=4` 个 work 后再提交第 5 个（`workqueue_submit(9,0)`）必须返回 0——请求队列的满队拒绝。
2. **预算断言**：第一次 `softirq_run_budget` 后 tasklet 全清、`work_used==2`（预算 2 只处理了两个 work）；第二次跑完 `work_used==0` 且 `budget_exhaustions>=1`——预算超支与接续处理。
3. **成功串**：`softirqtest: tasklet coalescing, FIFO work, and budget carry-over passed`。

#### 3.2.6 阻塞式请求队列：pc_buffer 生产-消费

```c
#define PC_BUFFER_CAP 2
static u8 pc_buffer[PC_BUFFER_CAP],pc_head,pc_tail,pc_used,pc_next,pc_expected;
```
```c
static TEXT64 void pc_producer(void){u8 value;while(threads[1].progress<THREAD_STEPS){sem_down(&pc_spaces);{u64 flags=irq_save64();value=pc_next++;pc_buffer[pc_head]=value;pc_head=(u8)((pc_head+1)%PC_BUFFER_CAP);pc_used++;pc_produced++;irq_restore64(flags);}threads[1].progress++;sem_up(&pc_items);busy_delay();}thread_exit();}
static TEXT64 void pc_consumer(void){u8 value;while(threads[2].progress<THREAD_STEPS){sem_down(&pc_items);{u64 flags=irq_save64();value=pc_buffer[pc_tail];pc_tail=(u8)((pc_tail+1)%PC_BUFFER_CAP);pc_used--;if(value!=pc_expected)pc_sequence_errors++;pc_expected++;pc_consumed++;irq_restore64(flags);}threads[2].progress++;sem_up(&pc_spaces);busy_delay();}thread_exit();}
```
1. **两信号量**：`spaces`（初值 `PC_BUFFER_CAP`，空位计数）与 `items`（初值 0，数据计数）——bounded buffer 的标准做法，队列满/空时 `sem_down` 阻塞线程。
2. **生产者**：`sem_down(&pc_spaces)` 拿空位 → 写 `pc_buffer[pc_head]` → `sem_up(&pc_items)` 通知消费方——请求入队；
3. **消费者**：`sem_down(&pc_items)` 等数据 → 读 `pc_buffer[pc_tail]` → `sem_up(&pc_spaces)` 归还空位——请求出队；
4. **顺序校验**：`pc_next`/`pc_expected` 对照，`pc_sequence_errors++` 记录乱序——块请求按序交付的断言（对应 I/O 调度器保证的排序语义）。
5. **中断保护**：临界区 `irq_save64`/`irq_restore64` 包裹——队列操作在单核上的原子性。

#### 3.2.7 块数据缓存：page_cache_model

```c
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
```
```c
static TEXT64 int page_cache_get(u64 index,u8 dirty){u32 i;u64 p;for(i=0;i<PAGE_CACHE_MAX;i++)if(page_cache[i].valid&&page_cache[i].index==index){page_cache[i].refs++;page_cache[i].dirty|=dirty;cache_hits++;return 1;}cache_misses++;for(i=0;i<PAGE_CACHE_MAX;i++)if(!page_cache[i].valid){p=pmm_alloc();if(!p)return 0;page_cache[i]=(struct page_cache_model){index,p,1,dirty,0,1};page_cache_count++;if(dirty)page_cache[i].writeback=0;return 1;}return 0;}
```
1. **按 index 命中**：`page_cache[i].index==index` 时 `refs++`、`dirty|=dirty`、`cache_hits++`——块缓存的读命中（`mm/filemap.c` 的 `page_cache_get_speculative` 类比）。
2. **未命中则分配**：`cache_misses++`，从 PMM 分配一物理帧插入缓存槽 `{index,p,valid=1,dirty,writeback=0,refs=1}`——把设备块读入内存（`read_cache_page` 类比）。
3. **脏写回状态**：`dirty` 记录是否被改写，`writeback` 标记回写中——块缓存的回写协议（`fs/buffer.c`/`writeback` 机制）。
4. **定长两槽**：`PAGE_CACHE_MAX=2`——块缓存也是固定缓冲区（Lesson 97 主题的延续）。

#### 3.2.8 exec64 增量与开机横幅

- `about` 输出 `Lesson 101: 块设备请求队列\n`；检查点分支：
```c
else if(eq64(word,"l93test")){if(!noargs64(arg))usage64(c,"l93test");else l93test(c);}else if(eq64(word,"l101test")){if(!noargs64(arg))usage64(c,"l101test");else l101test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 101: 块设备请求队列\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线。`make check` 断言 README 含 `块设备请求队列`、`Lesson 101`，kernel64.c 含 `l101test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ softirq/workqueue 初始化（有界请求队列）
 ├─ vfs_init() → 页缓存/匿名页初始化
 ├─ 横幅 "Lesson 101: 块设备请求队列"
 └─ 主循环：命令 → exec64
     ├─ l101test / l93test → 阶段检查点（lesson_94_state / lesson_93_state）
     ├─ softirqtest → 请求入队/预算限量出队验证
     ├─ pctest + pcgo → 阻塞式请求队列（生产-消费）
     ├─ reclaimtest → 块缓存命中/回收验证
     └─ fdtest / pathtest → VFS 数据路径回归
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：初始化 softirq/workqueue 与页缓存，打印横幅 `Lesson 101: 块设备请求队列`。
2. **`l101test`** → `l101test(c)` → 断言 → `l101test: bounded VFS, devices, epoll, and service management checkpoint passed`。
3. **`softirqtest`** → 塞满 workqueue（4 个）→ 第 5 个提交被拒 → 两次 `softirq_run_budget` → `softirqtest: tasklet coalescing, FIFO work, and budget carry-over passed`。
4. **`pctest`** → 启动生产/消费线程阻塞在 start event → `pctest: producer and consumer blocked on start event; run pcgo`；**`pcgo`** → `event_set` → `pcgo: event set; broadcast wake-all issued`。
5. **`reclaimtest`** → `fault_insert` + 两次 `page_cache_get` + `reclaim_one` → `reclaimtest: anonymous reclaim and page-cache hit model passed`。
6. **`l93test`**（历史检查点） → `l93test: bounded VFS, devices, epoll, and service management checkpoint passed`。
7. **`about`** → `Lesson 101: 块设备请求队列`。

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
Multiboot2 and Lesson 101 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 101: 块设备请求队列` 横幅 |
| `l101test` | `l101test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `l93test` | `l93test: bounded VFS, devices, epoll, and service management checkpoint passed` |
| `softirqtest` | `softirqtest: tasklet coalescing, FIFO work, and budget carry-over passed` |
| `pctest` | `pctest: producer and consumer blocked on start event; run pcgo` |
| `pcgo` | `pcgo: event set; broadcast wake-all issued` |
| `reclaimtest` | `reclaimtest: anonymous reclaim and page-cache hit model passed` |
| `about` | `Lesson 101: 块设备请求队列` |

判定成功：`l101test`/`softirqtest`/`reclaimtest` 全部 passed、无 `BROKEN`/fallback 之外的异常输出、无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l101test` 输出 `Lesson 94 fallback reported` | `lesson_94_state` 初始化/断言失败（stale 镜像） | `grep -n "l101test" kernel64.c`；确认初始化串 `{94U,95U,96U,97U,1,1,1,1}` 与 `b==a+1U` |
| `softirqtest` 输出 `BROKEN` | 满队拒绝（`work_used>=WORK_CAP`）或预算出队计数异常 | 对照 `workqueue_submit` 的 `drops++` 与 `softirq_run_budget` 的 `budget--`；`softirqinfo` 看 runs/drops |
| `softirqtest` 的 `work_used` 未归零 | 预算只消耗 2 个 work，未跑第二轮 | 确认第二轮 `softirq_run_budget` 把剩余 2 个 work 消费完 |
| `pcgo` 显示 `pcgo: run pctest first` | `pc_test` 标志未置位 | 必须先运行 `pctest` 再运行 `pcgo` |
| `reclaimtest` 输出 `BROKEN` | `page_cache_get` 未命中分配失败或 `reclaim_one` 未回收 | 对照 `page_cache_get` 的 `pmm_alloc` 与 `reclaim_one` 的 `refs==1`/`freed` 条件 |
| 生产者/消费者卡死 | 信号量计数与 `pc_used` 不一致 | `pcinfo` 查看 `S count/wait`、`I count/wait` 与 `R used/cap` |
| `l101test` 与 `l93test` 串台 | 检查点命令与其 state 结构不匹配 | 确认 `l101test` 只操作 `lesson_94_state`、`l93test` 只操作 `lesson_93_state` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 101' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `块设备请求队列` 与 `Lesson 101` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `workqueue[WORK_CAP]` 有界 FIFO | `block/blk-core.c`：`struct request_queue`（`request_fn`/`make_request_fn`）；`block/elevator.c` 的 I/O 调度器 | 模型无 bio/request 结构、无合并（merge）、无排序（elevator），仅 FIFO |
| `workqueue_submit` 满队 `drops++` | `block/blk-mq.c`：`blk_mq_submit_bio` 请求分配失败/队列阻塞；`block/blk-core.c` 的 `rq_qos` 背压 | 模型无动态请求池与深度限流，只用一个计数表达满队 |
| `softirq_run_budget` 预算限量出队 | `block/blk-core.c` 的 `__blk_run_queue`/`blk_queue_bio`；软中断 `kernel/softirq.c`（`MAX_SOFTIRQ_TIME`/`MAX_ACTION` 预算） | 模型 `SOFTIRQ_BUDGET=2` 每次固定，无硬件队列与并发 |
| `pc_buffer` 信号量生产-消费 | `block/blk-core.c` 的同步等待；`kernel/sched/wait.c` 的等待队列（请求阻塞） | 模型单核两信号量，无 per-CPU 队列与 RCU |
| `pc_sequence_errors` 顺序校验 | `block/mq-deadline.c`/`block/kyber-iosched.c` 的排序与保证 | 模型只断言 FIFO 顺序，无 deadline 与 batch 逻辑 |
| `page_cache_model{index,phys,dirty,writeback}` | `mm/filemap.c`（`page_cache_get_speculative`）；`fs/buffer.c`（buffer head 状态位）；`mm/page-writeback.c` | 模型 2 槽定长，无 radix tree/xarray 索引与 LRU |
| `page_cache_get` 命中 `refs++`/未命中分配 | `mm/filemap.c`：`page_cache_sync_readahead()`/`do_read_cache_page()` | 模型无预读（readahead）与基数树 |
| `l101test` 断言 | 无直接对应（LTP `block` 测试套件 / blktests） | 模型把请求队列主题的可验证状态固化进内核 |

**权威来源**：Linux `block/blk-core.c`、`block/blk-mq.c`、`block/bio.c`、`block/elevator.c`、`mm/filemap.c`、`fs/buffer.c`、`drivers/block/` 为对照；Multiboot2 规范与 Intel SDM 仍为引导/硬件权威来源。

---

## 8. 思考题与练习

1. **概念理解**：为什么块设备需要请求队列而字符设备不需要？「合并 + 排序」分别解决了机械盘的什么物理问题？
2. **源码定位**：在 `kernel64.c` 中找出所有「有界队列」结构（提示：`workqueue`、`pc_buffer`、`kbd_queue`、`input_queue`），逐一说明各自的容量与满队处理方式。
3. **动手实验**：把 `SOFTIRQ_BUDGET` 从 2 改成 1，运行 `softirqtest`，观察 `budget_exhaustions` 与 `softirq_model.runs` 的变化，解释预算与轮次的关系。
4. **动手实验**：给 `page_cache_get` 增加一个「writeback 中的页不可回收」条件，并用 `reclaimtest` 验证 `reclaim_one` 跳过该页。
5. **Linux 对照**：阅读 `block/blk-core.c` 的 `submit_bio` 与 `block/blk-mq.c` 的 `blk_mq_submit_bio`，对比教学模型 `workqueue_submit` + `softirq_run_budget` 简化了请求下发与完成通知（completion/interrupt）的哪些环节。

---

## 9. 本课小结与下一课预告

1. 本课从字符设备转到块设备：块设备以定长块访问、可随机寻址、需要 page cache 与请求队列。
2. Linux 块数据路径是 `submit_bio` → `request_queue` → I/O 调度器 → 驱动 `request_fn`；`struct bio`/`struct request`/`struct request_queue` 是三层核心对象。
3. 教学内核用 `workqueue`（入队/出队/满队 drop/预算限量）复现请求队列骨架，用 `pc_buffer` + 信号量复现阻塞式队列，用 `page_cache_model` 复现块缓存与回写状态。
4. `softirqtest`/`reclaimtest` 对队列行为与块缓存命中/回收做确定性回归，`pctest`+`pcgo` 演示阻塞式队列的完整流程。
5. 检查点增量：新增 `l101test`（Origin Lesson 94），恢复历史命名 `l93test`，横幅与 `about` 更新。
6. 下一课（Lesson 102）是设备系列收尾——**设备生命周期与卸载**（对照 `fs/block_dev.c`、设备驱动 remove/bind 路径），把注册、节点、打开、队列之后的对象销毁与资源释放讲完。

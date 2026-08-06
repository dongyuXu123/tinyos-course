# Lesson 43: Linux 风格页缓存、匿名页、脏页元数据与有界回收接口模型 — 精讲文档

> **课号**：Lesson 43（可执行课）
> **主题**：Linux 风格页缓存（page cache）、匿名页（anonymous page）、脏页/引用/
> 可回收元数据与有界回收接口模型
> **课程主线位置**：第 6 阶段「Linux 风格 I/O 与文件抽象」首课。前课（42）完成
> user-pointer 安全模型；本课把「页」从裸的 `va/phys` 对升级为携带
> `backing/dirty/accessed/refs/reclaimable` 的 `struct page_model` 元数据，
> 新增固定容量页缓存表 `page_cache`（命中/未命中记账）与一次只回收
> 「已校验 PMM 帧」的有界 reclaim 扫描。
> **前置课程**：[`lesson-42-stable/README.md`](../lesson-42-stable/README.md)
> **后续课程**：[`lesson-44-stable/README.md`](../lesson-44-stable/README.md)
> （fd 表与 file/inode/dentry 引用模型）
> **一句话目标**：能讲清 Linux 为什么把物理页分成「匿名页」与「页缓存页」、
> 为什么脏页要打标记、为什么回收前必须检查引用计数与 pin，并在 TinyOS 里复刻
> 全部**元数据与记账**——全程无磁盘 I/O、无 swap、无写回线程。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——Linux 内存子系统里一个物理页同时携带哪几类
元数据：`backing`（来自文件还是匿名）、`dirty`（写后未写回）、`accessed`（被访问
过）、`refs`（引用计数）、`reclaimable`（可否回收）；页缓存如何用「命中/未命中」
区分快慢路径；`reclaim` 为什么必须跳过 pin 的帧。TinyOS 用扩展的
`struct page_model`、`PAGE_CACHE_MAX=2` 的 `page_cache` 表与 `reclaim_one`
实现上述语义的**判定与记账**。

- **在课程主线中的位置**：本课开启第 6 阶段。42（user-pointer 安全）→ 43
  （页/页缓存/回收元数据）→ 44（fd 表与 VFS 对象）→ 45（ramfs/initramfs 与
  路径查找）→ 46（管道与 poll）→ 47（信号）→ 48（时钟/timer/sleep）。43 把
  「物理内存」从 lesson-41 的 VMA/缺页模型推进到 Linux `struct page` 级别的
  元数据视角，是 44 之后「文件数据从哪来、脏了怎么回写」的物理基础。
- **前置知识清单**：
  1. lesson-41 的 `vma_table`/`vma_lookup`/`fault_pages` 与 `fault_insert`
     插页流程（`VMA_FILE`/`VMA_ANON` 两种 backing 类型）；
  2. lesson-34 起的 PMM 位图：`pmm_alloc`/`pmm_free_page`/`page_state`，
     `pmm_free_page` 对已映射帧返回 `"mapped"`、成功返回 `"freed"`；
  3. lesson-37/38 的 task/thread 元数据与调度器对象
     （`fair_sched_class` 的 `pick_next/enqueue/dequeue` 三操作）；
  4. Linux `include/linux/mm_types.h` 的 `struct page` 字段直觉
     （`_refcount`、`mapping`、`private` 脏位）、`mm/filemap.c` 的
     `find_get_page`/`add_to_page_cache` 语义。
- **本课交付**：`anoninfo`/`reclaimtest` 两条新命令；扩展的
  `struct page_model`（+backing/dirty/accessed/reclaimable/refs 五字段）；
  新的 `struct page_cache_model` 与 `page_cache_get`/`reclaim_one`/
  `reclaim_init` 三个函数；一组页缓存/匿名页/回收计数器；`tasklist` 改为
  继承性占位命令。

---

## 2. 核心概念精讲

### 2.1 概念一：页缓存（page cache）——文件数据的“最近用过的副本”

**定义**：页缓存是内核为「以页为单位的文件内容」保留的物理页集合，用
文件索引（本课用 `index` 字段代替 file+offset）作为键。**为什么需要**：磁盘
读一次 4 KiB 很慢，如果第二次读同一块，命中内存副本可以省一次磁盘 I/O；
脏页再写回磁盘，也不需要每次读写都同步落盘。**工作机制**：
`page_cache_get(index, dirty)` 先在表里线性找 `index` 相同的条目——
找到就 `refs++`、按需置脏、记一次 `cache_hits`；找不到就 `cache_misses++`，
若还有空槽则 `pmm_alloc` 一个新帧填表。Linux 对应 `mm/filemap.c` 的
`find_get_page`（命中）与 `add_to_page_cache`（未命中才分配插入）；本课是
固定 2 槽线性表，没有任何块设备。

### 2.2 概念二：匿名页（anonymous page）

**定义**：不背靠任何文件、纯属进程私有数据的页（堆、栈、bss）。Linux 中
`struct page` 的 `mapping` 指向 `struct anon_vma` 而非 `address_space`。
**为什么需要区分**：匿名页没有「写回磁盘」的落点，回收时只能丢弃或换出
（swap）；而页缓存页回收前可能要先写回。TinyOS 用 `backing=VMA_ANON` 标记，
且只有匿名页参与 `anon_pages` 计数。本课 `fault_insert` 插的页固定是匿名页，
`reclaimtest` 先插一页再回收它。

### 2.3 概念三：脏页（dirty）与 writeback 状态

**定义**：`dirty=1` 表示页内容已被写而尚未同步到后备存储。Linux 通过
`mark_page_dirty` 置位并由 `balance_dirty_pages`/写回线程异步落盘。
**为什么需要**：回收一个脏页前必须先把内容写走，否则数据丢失；干净页可直接
丢弃。本课 `page_cache_model.writeback` 表示「正在写回」的占位状态（始终为 0，
教学模型不启动写回线程），全局计数器 `writeback_pages` 保留该词法以便后续课
挂接。`fault_insert` 里 `m->dirty=write`：按写缺页置脏，读缺页保持干净——
这是 Linux `fault` 写路径置脏的教学简化。

### 2.4 概念四：refs 引用计数与 reclaimable 可回收位

**定义**：`refs` 模拟 Linux `_refcount`（page 的引用数，`get_page`/`put_page`
维护）；`reclaimable` 表示该页是否允许被回收。**为什么需要**：回收算法绝对
不能回收仍被引用的页——那等于把正在用的内存换给系统。Linux `vmscan.c` 的
`shrink_page_list` 会先检查 `page_count(page)` 是否已降到 1 才动手。
本课 `reclaim_one` 的硬条件是 `refs==1`（仅模型自身持有）；任何
`!live`、`!reclaimable`、`refs!=1` 的槽都 `reclaim_skips++` 跳过。

### 2.5 概念五：有界回收（bounded reclaim）扫描

**定义**：`reclaim_one` 线性扫描 `fault_pages[0..VMA_MAX_PAGES)`，逐槽
`reclaim_scans++`；对满足 `live && reclaimable && refs==1` 的槽，先调用
`pmm_free_page` 并**验证返回串必须是 `"freed"`** 才真正释放——如果帧是
VM 映射/线程栈（返回 `"mapped"`/`"thread stack"`），则只记账跳过。
**为什么这样设计**：教学模型把「回收」做成可审计的接口，skips 区分
「被 pin 跳过」与「PMM 拒绝」两种失败，且扫描只动元数据、绝不触碰
任何用户内存。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-42） |
|---|---|---|
| `boot.S` | Multiboot2 引导、进入 long mode | 未变化 |
| `kernel.c` | 32 位入口、低内存页表、user image 装载 | 未变化 |
| `kernel64.c` | 64 位内核主体（累积） | **核心**：page/页缓存结构 + `fault_insert` 元数据化 + `page_cache_get`/`reclaim_one`/`reclaim_init`/`anoninfo`/`reclaimtest`；`tasklist` 改占位；调度器改用 `rr_*` 直调 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 新增 grep**：README 含 `mm/filemap.c`、`mm/vmscan.c`、`include/linux/mm_types.h`；kernel64.c 含 `anoninfo`、`reclaimtest` |
| `grub.cfg` | 装载 | **menuentry 标题更新**为 lesson-43 主题 |

### 3.2 结构 / 宏 / 全局变量精讲

```c
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
#define PAGE_CACHE_MAX 2U
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
static struct page_model fault_pages[VMA_MAX_PAGES];
static struct page_cache_model page_cache[PAGE_CACHE_MAX];
static u32 vma_count, fault_page_count, page_cache_count;
static u64 fault_not_present, fault_protection, fault_unmapped, fault_insertions;
static u64 anon_pages, anon_reclaims, cache_hits, cache_misses, reclaim_scans, reclaim_skips, writeback_pages;
```

逐行注释：
- `struct page_model` 相对 lesson-41 从 4 字段扩到 9 字段：新增 `backing`
  （`VMA_ANON`/`VMA_FILE`，本课只产生匿名页）、`dirty`、`accessed`、
  `reclaimable`（u8 位标志）与 `refs`（u16 引用计数）——即 Linux `struct page`
  的「状态位 + 引用计数」教学版；
- `PAGE_CACHE_MAX=2`：页缓存表固定 2 槽，是「有界」的量化体现；
- `struct page_cache_model`：`index`（文件内页号，代替 Linux 的
  file+offset 键）、`phys`、`valid`、`dirty`、`writeback`（写回占位位）、
  `refs`；
- 计数变量分两组：既有 VMA/缺页组（`fault_*`）与新增页缓存/回收组
  （`anon_pages`、`anon_reclaims`、`cache_hits`、`cache_misses`、
  `reclaim_scans`、`reclaim_skips`、`writeback_pages`）。`writeback_pages`
  只被 `reclaim_init` 清零、从不递增——是留给写回建模的词法占位。

### 3.3 函数精讲：fault_insert —— 匿名页的插页元数据

```c
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;
if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;
p=pmm_alloc();if(!p)return 0;
for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];
m->va=down(va);m->phys=p;m->writable=write;m->live=1;
m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;
fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
```

算法步骤与逐行分析：
1. **入口三守卫**（首行）：`fault_page_count>=VMA_MAX_PAGES` 表满拒绝；
   `vma_range_valid(down(va), down(va)+PAGE_SIZE, write?VMA_W:VMA_R)` 要求
   整页落在带相应权限的 VMA 内（复用 lesson-41 的 `vma_lookup`，含
   `end-start<=0x10000` 上限）；`page_present(va)` 拒绝重复插同一地址。
   三关全过才继续，这正是 Linux `handle_mm_fault` 先查 `find_vma` 再
   分配 `struct page` 的教学顺序；
2. **取帧**（第二行）：`pmm_alloc()` 从位图要一个空闲物理帧，失败返回 0
   ——对应 Linux 分配失败走 `VM_FAULT_OOM`；
3. **填槽**（第三、四行）：线性找第一个 `!live` 的槽，抄入 va/phys/writable
   后一次性置五类元数据：`backing=VMA_ANON`（本课插的都是匿名页）、
   `dirty=write`（写缺页置脏，读缺页干净）、`accessed=1`（刚被访问）、
   `reclaimable=1`（本课所有插页默认可回收）、`refs=1`（模型自身一个引用）；
4. **记账**（末行）：`fault_page_count++`、`anon_pages++`、`fault_insertions++`
   三个计数器同步更新——`anon_pages` 是新引入的「匿名页总数」视角。

### 3.4 函数精讲：page_cache_get —— 命中/未命中的快慢路径

```c
static TEXT64 int page_cache_get(u64 index,u8 dirty){u32 i;u64 p;
for(i=0;i<PAGE_CACHE_MAX;i++)if(page_cache[i].valid&&page_cache[i].index==index){
page_cache[i].refs++;page_cache[i].dirty|=dirty;cache_hits++;return 1;}
cache_misses++;
for(i=0;i<PAGE_CACHE_MAX;i++)if(!page_cache[i].valid){
p=pmm_alloc();if(!p)return 0;
page_cache[i]=(struct page_cache_model){index,p,1,dirty,0,1};
page_cache_count++;if(dirty)page_cache[i].writeback=0;return 1;}return 0;}
```

逐行分析：
- **命中路径**（第二、三行）：线性找到 `valid && index==index` 的槽即命中。
  `refs++` 模拟 Linux `get_page`；`dirty|=dirty` 让「命中一次写请求」把
  干净页升级为脏页（脏状态只升不降）；`cache_hits++` 后立即返回——
  快路径不碰位图、不分配；
- **未命中路径**（第四行起）：先 `cache_misses++` 统一记账，再找空槽；
  空槽分配 `pmm_alloc` 失败返回 0（表满或位图空都进 `reclaim_skips` 类
  的拒绝分支）；
- **建槽**（第六行）：用 C99 复合字面量 `{index,p,1,dirty,0,1}` 初始化
  （valid=1、writeback=0、refs=1），随后 `if(dirty)page_cache[i].writeback=0`
  是无操作——因为复合字面量已把 writeback 置 0，这句是「显式声明：
  置脏的新页还没有进入写回」的文档性代码；
- `page_cache_count++` 记录活槽数，供 `reclaimtest` 断言
  `page_cache_count==1`。

### 3.5 函数精讲：reclaim_one —— 有界回收扫描

```c
static TEXT64 int reclaim_one(void){u32 i;
for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];
reclaim_scans++;
if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}
if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}
m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}
return 0;}
```

逐行分析：
- **逐槽扫描 + 扫描计数**：每个被检查的槽 `reclaim_scans++`（对应 Linux
  `vmscan` 的 `nr_scanned`）；
- **第一道过滤**（第三行）：`!live`（槽本来就是空的）、`!reclaimable`
  （被 pin 不可回收）、`refs!=1`（引用数不为 1，说明还有人用）任一成立即
  `reclaim_skips++` 并跳过——**绝不回收被引用的页**是本函数的安全红线；
- **第二道过滤**（第四行）：`pmm_free_page(m->phys)` 必须返回
  `"freed"` 才算真正归还；若该帧其实是 VM 映射帧/线程栈帧，
  `pmm_free_page` 会返回 `"mapped"`/`"thread stack"`（page_state 校验），
  此处同样 `reclaim_skips++` 跳过——回收接口只释放「PMM 认证为 allocated
  且无其他所有者」的帧；
- **释放记账**（第五行）：`live=0`、`fault_page_count--`、`anon_pages--`、
  `anon_reclaims++`，成功一次立即 `return 1`（只回收一页，保持可预期）；
- 全表扫完无满足条件者返回 0——`reclaimtest` 用返回值做断言。

### 3.6 函数精讲：reclaim_init / anoninfo / reclaimtest / tasklist

```c
static TEXT64 void reclaim_init(void){page_cache_count=0;anon_pages=0;anon_reclaims=0;
cache_hits=0;cache_misses=0;reclaim_scans=0;reclaim_skips=0;writeback_pages=0;}
static TEXT64 void anoninfo(u16*c){text64(c,"anon pages/cache live/reclaims: ");
hex64(c,anon_pages);text64(c,"/");hex64(c,page_cache_count);text64(c,"/");
hex64(c,anon_reclaims);text64(c," cache hit/miss: ");hex64(c,cache_hits);
text64(c,"/");hex64(c,cache_misses);text64(c," reclaim scans/skips: ");
hex64(c,reclaim_scans);text64(c,"/");hex64(c,reclaim_skips);putc64(c,'\n');}
static TEXT64 void reclaimtest(u16*c){int a=fault_insert(VMA_DATA_START,1),
b=page_cache_get(1,1),d=page_cache_get(1,0),e=reclaim_one();
text64(c,"reclaimtest: ");text64(c,a&&b&&d&&e&&anon_pages==0&&page_cache_count==1?
"anonymous reclaim and page-cache hit model passed":"BROKEN");
text64(c,"\npage cache is metadata-only; no disk I/O or swap executed\n");}
```

- `reclaim_init`：把全部页缓存/回收计数器归零，在 `kernel_main64_binary`
  中于 `pmm_init`/`vma_init` 之后、`address_space_init` 之前调用，保证每次
  启动计数基线一致；
- `anoninfo`：一行输出三组比例——`anon pages/cache live/reclaims`、
  `cache hit/miss`、`reclaim scans/skips`，全部用 `hex64` 打印 16 位十六进制；
- `reclaimtest`：四步串联——① `fault_insert(VMA_DATA_START,1)` 插一页
  （写缺页、匿名、脏）；② `page_cache_get(1,1)` 未命中建槽（脏）；
  ③ `page_cache_get(1,0)` 命中（refs 增、脏保持）；④ `reclaim_one()` 回收
  匿名页。断言 `a&&b&&d&&e&&anon_pages==0&&page_cache_count==1`：
  四步全成功、匿名页已归零、页缓存仍留 1 槽。末行显式声明
  「页缓存只是元数据；没有执行磁盘 I/O 或 swap」；
- `tasklist` 在本课改为占位（源码逐字）：
  `static TEXT64 void tasklist(u16*c){text64(c,"tasks: bounded task metadata; use taskvalidate for state checks\n");}`
  ——task 明细检查让位于本课内存元数据，仅提示用 `taskvalidate` 复核。

### 3.7 exec64 分支、kernel_main、grub.cfg 与 Makefile

`exec64` 新增两个分支（源码逐字）：

```c
else if(eq64(word,"anoninfo")){if(!noargs64(arg))usage64(c,"anoninfo");else anoninfo(c);}
else if(eq64(word,"reclaimtest")){if(!noargs64(arg))usage64(c,"reclaimtest");else reclaimtest(c);}
```

本课注意点：
- **help 文案未更新**：`help` 命令的输出串与 lesson-42 完全一致，
  仍列 `... ptrinfo ptrtest copytest schedinfo ...`，**没有**追加
  `anoninfo`/`reclaimtest`——这是源码事实，新命令仍可直接键入使用，
  只是不出现在 help 列表里；
- `about` 输出：`TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model`
- 开机横幅（源码逐字）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

- `grub.cfg` 的 menuentry 标题（源码逐字）：
  `menuentry "TinyOS lesson 43: page cache, anonymous pages, and reclaim model" {`
- 调度器微调：`irq0_schedule` 内的入队/出队从
  `active_sched_class->enqueue/dequeue` 改为直接调用 `rr_enqueue`/`rr_dequeue`
  （`next_runnable` 直接调 `rr_pick_next`），并把 `runtime_sched_class()`
  定义为恒返回 `&fair_sched_class`，配合 `task_names_keep` 里
  `(void)` 掉未使用变量——这些是构建洁净性改动，调度语义不变；
- Makefile `check` 目标新增 grep（README 三路径 + kernel64.c 两符号，
  其中 `anoninfo`/`reclaimtest` 被重复 grep 两次——Makefile 源码如此）：

```make
@grep -q 'mm/filemap.c' README.md
@grep -q 'mm/vmscan.c' README.md
@grep -q 'include/linux/mm_types.h' README.md
@grep -q 'anoninfo' kernel64.c
@grep -q 'reclaimtest' kernel64.c
@grep -q 'anoninfo' kernel64.c
@grep -q 'reclaimtest' kernel64.c
@printf '%s\n' 'Multiboot2 and lesson 43 checks passed.'
```

### 3.8 主控制流

```text
kernel_main64_binary
  ├─ task_names_keep / active_sched_class=&fair_sched_class
  ├─ pmm_init(h) → vma_init() → reclaim_init() → address_space_init(...)
  ├─ 横幅（lesson-43 字符串，含 "bounded reclaim metadata"）
  └─ 键盘循环 → exec64：
        anoninfo / reclaimtest / pfmodel / ptrinfo / ...（旧命令回归）
```

---

## 4. 数据流与运行逻辑

```text
输入 "reclaimtest"
  ├─ fault_insert(0x600000, 1)   → VMA 数据段命中、pmm_alloc 取帧、
  │    槽0: backing=ANON dirty=1 accessed=1 reclaimable=1 refs=1
  │    anon_pages=1  fault_page_count=1
  ├─ page_cache_get(1, 1)        → 未命中：pmm_alloc 建 page_cache[0]
  │    index=1 dirty=1 writeback=0 refs=1  cache_misses=1
  ├─ page_cache_get(1, 0)        → 命中：refs=2、dirty 保持 1、cache_hits=1
  └─ reclaim_one()               → 扫描槽0：live&&reclaimable&&refs==1 全过
       pmm_free_page(phys)=="freed" → live=0 anon_pages=0 anon_reclaims=1
       → "reclaimtest: anonymous reclaim and page-cache hit model passed"
       → "page cache is metadata-only; no disk I/O or swap executed"

输入 "anoninfo"
  → "anon pages/cache live/reclaims: 0000000000000000/0000000000000001/0000000000000001
     cache hit/miss: 0000000000000001/0000000000000001
     reclaim scans/skips: 0000000000000001/0000000000000000"
```

（一次 `reclaimtest` 后：`anon_pages=0`、`page_cache_count=1`、
`anon_reclaims=1`、`cache_hits=1`、`cache_misses=1`、`reclaim_scans=1`、
`reclaim_skips=0`。若先跑 `pfmodel`/`vmtest` 再跑 `reclaimtest`，扫描/跳过
计数会因既有 fault 页变化，属于预期。）

---

## 5. 构建、运行与验证

### 5.1 依赖

同旧课：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and lesson 43 checks passed.`（要求 README 含
`mm/filemap.c`、`mm/vmscan.c`、`include/linux/mm_types.h`，kernel64.c 含
`anoninfo` 与 `reclaimtest`，缺一即失败；旧 README 里的
`mm/memory.c`/`include/linux/writeback.h` 引用在 §7 中保留）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：

```text
TinyOS lesson 43: Linux-style page cache, anonymous pages, and reclaim model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

验证步骤（输出串从源码逐字）：

```bash
reclaimtest
```

预期：

```text
reclaimtest: anonymous reclaim and page-cache hit model passed
page cache is metadata-only; no disk I/O or swap executed
```

```bash
anoninfo
```

预期（一次 `reclaimtest` 之后）：

```text
anon pages/cache live/reclaims: 0000000000000000/0000000000000001/0000000000000001 cache hit/miss: 0000000000000001/0000000000000001 reclaim scans/skips: 0000000000000001/0000000000000000
```

```bash
tasklist
```

预期：`tasks: bounded task metadata; use taskvalidate for state checks`（本课占位）

继承回归：`pfmodel`（插一页并输出 `pages: 1`）、`ptrinfo`/`ptrtest`/
`copytest`、`vmainfo`/`vmatest` 行为与 lesson-42 一致；真实 `#PF` 命令
`pftest`/`isttest`/`stackguardtest` 保持致命停机。

### 5.4 课程实测记录（2026-08，学习快照）

`make check` 输出 `Multiboot2 and lesson 43 checks passed.`；`reclaimtest`
四步断言通过；`anoninfo` 显示 anon=0、cache live=1、reclaims=1、
hit/miss=1/1、scans/skips=1/0；`tasklist` 输出占位串；`pfmodel`/`vmainfo`
与 lesson-42 一致。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `mm/filemap.c`/`mm/vmscan.c`/`include/linux/mm_types.h`，或 kernel64.c 缺 `anoninfo`/`reclaimtest` | 对照 Makefile check 的 grep 列表 |
| `reclaimtest` 输出 `BROKEN` | 四步中断言不成立 | 逐项核对：插页成功、两次 `page_cache_get`（miss 后 hit）、`reclaim_one` 返回 1、`anon_pages==0`、`page_cache_count==1` |
| `anoninfo` 的 scans/skips 不符合预期 | 先前 `pfmodel` 等命令已插入页，扫描会计入既有 live 页 | 计数是累计的；`reclaim_scans` 对每个被检查槽加 1 |
| `anoninfo` 的 reclaims 大于 1 | `reclaimtest` 被多次运行 | 每次成功回收 `anon_reclaims++`；这是记账语义，非 bug |
| 担心「真做了磁盘 I/O / swap」 | 设计保证元数据化 | `reclaimtest` 末行声明；`page_cache_get`/`reclaim_one` 只碰 `pmm_alloc`/`pmm_free_page` 与结构体字段 |
| `help` 列表里没有 `anoninfo`/`reclaimtest` | 本课 help 文案与 lesson-42 相同（源码事实） | 命令仍可正常键入；可对照 lesson-43 的 exec64 分支确认 |
| `tasklist` 不再列出 task 明细 | 本课改为占位（源码事实） | task 状态用 `taskvalidate` 复核 |
| QEMU 窗口菜单名与课号不符 | `grub.cfg` menuentry 未更新 | 对照 lesson-43 的 `grub.cfg` 标题 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `mm/filemap.c`、`mm/memory.c`、`mm/vmscan.c`、
`include/linux/mm_types.h`、`include/linux/writeback.h`**（延续 lesson-41 的
`mm/mmap.c`/`include/linux/mm.h` 对照线）：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `struct page_model` 的 backing/dirty/accessed/reclaimable/refs | `include/linux/mm_types.h` 的 `struct page`（`_refcount`、`mapping`、`private`、`flags` 中的 `PG_dirty`/`PG_referenced`/`PG_active`） | 教学模型用 9 个标量字段表达，Linux 把状态压进一个 `flags` 位图 + `_refcount` |
| `page_cache_get` 命中/未命中 | `mm/filemap.c` 的 `find_get_page`/`find_lock_page` + `add_to_page_cache` | 教学模型固定 2 槽线性表；Linux 走 radix tree/XArray 与 rcu 锁 |
| `cache_hits`/`cache_misses` | `mm/filemap.c` 的 `filemap_hit`/`filemap_miss` 统计（procfs） | 记账点一致，教学模型更简 |
| `fault_insert` 置 `dirty=write` | `mm/memory.c` 的 `do_anonymous_page`/`handle_mm_fault` 写路径 `SetPageDirty` | 教学模型跳过真实 PTE 与 COW，只做标记 |
| `backing=VMA_ANON` 匿名页 | `anon_vma` + `mapping` 指向 `anon_vma`（`include/linux/rmap.h`/`mm/rmap.c`） | 教学模型用枚举值区分，不建 `anon_vma` 链 |
| `reclaim_one` 的 `refs==1` + `reclaimable` 检查 | `mm/vmscan.c` 的 `shrink_page_list`（`trylock_page` + `page_count(page)==1` 才回收） | 教学模型无 LRU 链表、无 `isolate_lru_page`、无页换出 |
| `pmm_free_page` 返回 `"freed"` 才释放 | `mm/page_alloc.c` 的 `__free_pages`（伙伴系统） | 教学模型用位图 + 字符串校验，无 buddy 排序 |
| `writeback_pages`/`writeback` 占位 | `include/linux/writeback.h` 的 `balance_dirty_pages`/`PD_Writeback` | **教学模型不启动写回线程**，仅保留字段 |
| 固定 `PAGE_CACHE_MAX=2` | 内存总量决定页缓存大小 | 有界是教学刻意简化 |

**权威来源**：Intel SDM Vol.3A（页表与 `#PF`；本课未执行真实缺页指令）、
Multiboot2 规范（内存映射来源）。

**教学模型简化了什么**：
1. 无磁盘、无 swap、无块设备：`page_cache_get` 的「未命中」不会真的去读盘；
2. 无 LRU/`active`/`inactive` 链表：回收就是固定数组线性扫描；
3. 无写回线程：`dirty` 页从不被异步落盘，`writeback` 恒为 0；
4. 无并发/锁：所有计数在单 CPU 中断上下文中自洽；
5. 不建真实 `anon_vma`/`address_space` 反向映射：`refs` 只是教学计数。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `reclaim_one` 要求 `refs==1` 才回收？如果把条件改成
   `refs<=2` 会破坏什么语义？（提示：Linux `page_count(page)==1` 的直觉。）
2. **源码定位**：`fault_insert` 里 `m->dirty=write` 的语义是什么？读缺页
   与写缺页插入的页在 dirty 位上有何区别？对应 Linux 哪条路径？
3. **动手实验**：把 `PAGE_CACHE_MAX` 改成 1，重建后依次执行
   `reclaimtest`（第二次 `page_cache_get` 命中）再执行第二次
   `reclaimtest`（此时第二个槽不存在、`page_cache_get(1,1)` 会 miss 后
   找不到空槽返回 0），观察 `reclaimtest` 是否输出 `BROKEN`，然后改回。
4. **Linux 对照**：打开 `mm/vmscan.c` 的 `shrink_page_list`，找出它回收一页
   前会做的至少 4 项检查，与本课 `reclaim_one` 的三条件（live/reclaimable/
   refs==1）逐一对比。
5. **设计思考**：本课 `anoninfo` 里 `writeback_pages` 永远是 0。如果要让
   `page_cache_get(1,1)` 置脏后把 `writeback_pages` 加 1、`reclaim_one`
   拒绝回收 `writeback` 中的页缓存页，需要在哪些函数加什么代码？
   （提示：这是向真实写回语义迈进的最小一步。）

---

## 9. 本课小结与下一课预告

**小结**：本课把「页」从裸地址对升级为 Linux `struct page` 风格的教学元数据
（backing/dirty/accessed/reclaimable/refs），并引入固定 2 槽的页缓存表与
`page_cache_get` 的命中/未命中快慢路径；`reclaim_one` 用「live +
reclaimable + refs==1 + PMM 返回 freed」四重门槛演示了有界回收，
`reclaimtest` 用一条命令串起插页→缓存 miss→缓存 hit→回收整条流水线，
`anoninfo` 把七类计数器打到屏幕。本课全程无磁盘 I/O、无 swap、无写回线程，
`writeback` 仅作词法占位；`tasklist` 让位为继承性占位；Makefile `check`
强制 README 与源码包含对应符号；grub.cfg 菜单同步更新。第 6 阶段
「I/O 与文件抽象」由此展开——页元数据是「文件数据落在内存里长什么样」的答案。

**下一课预告**：lesson-44 将在这套「页」之上引入 Linux 风格的 fd 表与
`struct file`/`inode`/`dentry` 引用模型（对照 `fs/open.c`、
`include/linux/fs.h`）：文件描述符怎么引用 file 对象、file 怎么持有
inode、dentry 怎么提供名字到 inode 的映射。本课学的 `refs` 引用计数思想
会直接复用到 file/inode 的生命周期记账上。

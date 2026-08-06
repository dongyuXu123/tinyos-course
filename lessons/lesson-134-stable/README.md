# Lesson 134: 内存压力诊断 — 精讲文档

> **课号**：Lesson 134（可执行课，checkpoint 快照）
> **主题**：内存压力诊断——把 PMM 页分配/释放、`fault_insert` 匿名插页、page cache
> 元数据与 `reclaim_one` 页回收串成一条「内存压力 → 诊断 → 回收」链路，并追加
> checkpoint 模型 `lesson_127_model`。
> **课程主线位置**：诊断/网络主题的「检查点课」序列（Lesson 133–138），位于
> Lesson 133（异常路径与故障分类）之后、Lesson 135（调度与并发综合诊断）之前。
> **前置课程**：[`lesson-133-stable/README.md`](../lesson-133-stable/README.md)
> **后续课程**：[`lesson-135-stable/README.md`](../lesson-135-stable/README.md)
> **一句话目标**：学完本课你能说清 TinyOS 模拟的「内存压力」生命周期——页从哪来
> （PMM）、怎么进 VMA（fault_insert）、怎么进缓存（page_cache_get）、怎么被回收
> （reclaim_one），以及 checkpoint 模型 `l134test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂内存子系统四层模型——PMM 位图分配器、匿名页插入
（`fault_insert`）、page cache 元数据（`page_cache_get`）、单页回收
（`reclaim_one`），理解 `meminfo`/`anoninfo`/`reclaimtest` 三个诊断命令，并掌握
本课新增的 `lesson_127_model`/`l134test` 断言。

- **在课程主线中的位置**：与 Lesson 133–138 同属「诊断/网络主题的检查点课」。
  `kernel64.c` 相对上一课仅 3 处增量：`l133test`→`l126test` 改名、新增
  `struct lesson_127_model` 与 `l134test`、exec64/about/banner 文案换成
  「内存压力诊断」。内存回收机制（`reclaim_one`、`page_cache_get`）继承自早期课程，
  本课侧重讲解，不新增机制。
- **前置知识清单**：
  1. Lesson 133：`fault_insert` 如何登记匿名页（`backing=VMA_ANON; dirty;
     reclaimable=1; refs=1`）——本课回收的对象正是它插的页；
  2. PMM 位图：`pmm_alloc`/`pmm_free_page`/`page_state` 与 `fixed`/`reserved`
     帧语义；
  3. VMA 模型（`vma_table`/`vma_lookup`/`vma_range_valid`）；
  4. 文本 VGA 输出管线（`text64`/`hex64`/`putc64`）与 shell 命令分发（`exec64`）。
- **本课交付**：理解「内存压力诊断」四层模型与 `reclaimtest`/`anoninfo`/`meminfo`
  命令输出；`l126test`、`l134test` 两个 checkpoint 测试；`about` 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：内存压力与页回收（page reclaim）

**直觉**：物理内存是有限资源。当匿名页、page cache、线程栈、用户程序争抢 4 KiB 帧
时，分配器会耗尽——Linux 的做法是触发回收（reclaim）：把可回收页写回或清零后归还
给伙伴系统。TinyOS 用一个极小的模型模拟这个动作。

**准确定义**：`reclaim_one()` 扫描 `fault_pages[]` 表，挑出「活、可回收、引用数为
1」的匿名页，先调 `pmm_free_page` 把帧还给 PMM，再把模型记录清除。这就是
`mm/vmscan.c` 的 `shrink_page_list`→`free_unref_page` 链路的 3 行版。

**决策条件**（可回收判据）：
```text
m->live==1 且 m->reclaimable==1 且 m->refs==1
```
三者缺一即 `reclaim_skips++` 跳过——`reclaimable` 由 `fault_insert` 写入 1，`refs`
表示映射计数（有别的进程引用就不能回收）。这就是「内存压力诊断」的判断核心：先看
谁该被回收，再看回收后账目是否守恒（`anon_pages` 减 1、`fault_page_count` 减 1）。

### 2.2 概念二：page cache 命中与未命中

**直觉**：读文件/块设备时，内核先把数据缓存起来；第二次读同一 index 就直接命中。
TinyOS 用 `page_cache_get(index, dirty)` 模拟：按 `index` 查 2 槽缓存表，命中则
`cache_hits++` 并合并 dirty 位；未命中则 `pmm_alloc` 一页建新缓存项。

```c
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
#define PAGE_CACHE_MAX 2U
```

**为什么**：`page_cache_count`、`cache_hits`/`cache_misses`、`reclaim_scans`/
`reclaim_skips` 这些计数器全部暴露在 `anoninfo` 里，构成「内存压力诊断」的量化
指标——这正是 Linux `/proc/meminfo` 的 teaching 版。

### 2.3 概念三：确定性 checkpoint 模型

**直觉**：检查点课用「结构体 + 赋值 + 断言」把主题固化。Lesson 134 新增
`struct lesson_127_model`，`l134test` 把它赋为 `{127U,128U,129U,130U,1,1,1,1}`
后做五连断言（`valid && active && ready && accounted && b==a+1`）。

**工作机制**：赋值是字面量，断言恒真，输出恒为 `bounded concurrency, SMP, RCU, and
diagnostics checkpoint passed`；「fallback」分支只在模型被改坏时出现
（`Lesson 127 fallback reported`）。字段 `a` 从 `127U` 起头，等于课号 134 − 7，
是回锚到 Lesson 127 检查点的记号。**这是教学模型：不执行任何真实回收/并发代码，
只校验元数据自洽。**

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 133） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验用户镜像、构造 handoff | 未变化 |
| `kernel64.c` | 64 位内核本体（959 行）：PMM/VMA/page cache/reclaim/调度/进程/GUI/checkpoint | `l133test`→`l126test`；新增 `struct lesson_127_model`、`l134test`；exec64 增加 `l126test`/`l134test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局 | 未变化 |
| `Makefile` | 构建/检查；`check` grep `内存压力诊断`、`l134test`、`Lesson 134` | 仅 grep 文案 |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：内存压力诊断实现精讲

#### 3.2.1 数据结构与计数器

```c
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
#define VMA_MAX_PAGES 4U
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
#define PAGE_CACHE_MAX 2U
static struct page_model fault_pages[VMA_MAX_PAGES];
static struct page_cache_model page_cache[PAGE_CACHE_MAX];
static u64 anon_pages, anon_reclaims, cache_hits, cache_misses, reclaim_scans, reclaim_skips, writeback_pages;
```

逐字段注释：
- `fault_pages[4]` 是「插入的匿名页」表：`live` 表示记录在册，`backing=VMA_ANON`、
  `dirty`（写访问）、`accessed`、`reclaimable`（可回收）、`refs`（引用计数）。
  可回收判据所需的三个位（live/reclaimable/refs==1）全部在这里。
- `page_cache[2]` 是 page cache 元数据：`index`（缓存对象号）、`phys`（帧）、
  `dirty`/`writeback`（写回状态）、`refs`。
- 计数器组 `anon_pages/anon_reclaims/cache_hits/cache_misses/reclaim_scans/
  reclaim_skips/writeback_pages` 是 `anoninfo` 的输出源，也是「内存压力诊断」的量化
  指标集合。

#### 3.2.2 PMM：页从哪里来、怎么还

```c
static TEXT64 u64 pmm_alloc(void){u32 i;if(!pmm_ready)return 0;for(i=0;i<PMM_FRAMES;i++)if(!bit(i)){mark(i);pmm_free--;pmm_used++;return(u64)i*PAGE_SIZE;}return 0;}
```

- 职责：在 16 MiB 窗口（`PMM_MAX_PHYS`）里找第一个空闲位，置位并更新 `pmm_free`/
  `pmm_used` 账目，返回帧物理地址。
- 边界：`pmm_ready` 未就绪返回 0（`pmm_init` 在 banner 前已跑）；遍历到
  `PMM_FRAMES` 仍无空闲则返回 0（内存压力到达顶点的信号）。
- 为什么：`bit(i)`/`mark(i)` 直接操作 `volatile u8 pmm_bitmap[]`，是 Linux
  `mm/page_alloc.c` 伙伴系统分配器的教学版——只找「任意空闲帧」，不做阶数/迁移类型。

```c
static TEXT64 const char *pmm_free_page(u64 p){u32 i;const char*s=page_state(p);if(!eq64(s,"allocated"))return s;if(vm_frame_owned(p))return "mapped";if(thread_stack_owned(p))return "thread stack";i=(u32)(p/PAGE_SIZE);unmark(i);pmm_free++;pmm_used--;return "freed";}
```

- 算法步骤：①`page_state` 必须等于 `allocated`，否则原样返回状态串（例如
  `free`/`fixed/reserved`）；②若帧被 VM 映射或线程栈占用则拒绝（返回 `mapped`/
  `thread stack`）——所有权检查防止误释放；③`unmark` + 更新账目，返回 `freed`。
- 边界：调用方必须逐字比较返回值 `eq64(r,"freed")` 才能确认成功（`reclaim_one`、
  `pfree` 命令都这么做）。
- 为什么：把「帧是否被其他子系统持有」作为释放前置条件，是 Linux 帧引用计数
  （`page_ref`）的简化——TinyOS 用 `vm_frame_owned`/`thread_stack_owned` 两个查询
  代替了引用计数。

```c
static TEXT64 void meminfo(u16*c){text64(c,"PMM: 4 KiB physical frames in 16 MiB mapped window\nstatus:  ");text64(c,pmm_error);if(!pmm_ready){putc64(c,'\n');return;}text64(c,"\ntracked: ");hex64(c,pmm_total);text64(c,"\nfree:    ");hex64(c,pmm_free);text64(c,"\nused:    ");hex64(c,pmm_used);text64(c,"\ninvariant tracked = free + used: ");text64(c,pmm_total==pmm_free+pmm_used?"yes":"BROKEN");text64(c,"\nbitmap:  ");hex64(c,(u64)(unsigned long)pmm_bitmap);text64(c," +");hex64(c,PMM_BITMAP_BYTES);text64(c,"\nfixed:   ");hex64(c,(u64)(unsigned long)pmm_fixed);putc64(c,'\n');}
```

- 职责：打印 PMM 账目与不变量校验。`invariant tracked = free + used` 输出
  `yes`/`BROKEN` 是**内存账目守恒**的运行时断言——任何泄漏或双重释放都会让它翻
  转，这是「内存压力诊断」的第一道探针。
- 输入输出：读全局 `pmm_total/pmm_free/pmm_used/pmm_error/pmm_ready`；输出到 VGA。

#### 3.2.3 fault_insert：匿名页从哪里进（承接 Lesson 133）

```c
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;p=pmm_alloc();if(!p)return 0;for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];m->va=down(va);m->phys=p;m->writable=write;m->live=1;m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
```

（本课重点重看三个字段）`m->backing=VMA_ANON` 标记匿名页；`m->dirty=write` 把
「是否写访问」直接翻译成脏位；`m->reclaimable=1; m->refs=1` 让新页**立即满足可回收
判据**——回收器不会刚插就收，但账目允许。`pmm_alloc` 失败时返回 0，这正是内存压力
的第一个表象：**想插页却拿不到帧**。

#### 3.2.4 page_cache_get：缓存命中/未命中

```c
static TEXT64 int page_cache_get(u64 index,u8 dirty){u32 i;u64 p;for(i=0;i<PAGE_CACHE_MAX;i++)if(page_cache[i].valid&&page_cache[i].index==index){page_cache[i].refs++;page_cache[i].dirty|=dirty;cache_hits++;return 1;}cache_misses++;for(i=0;i<PAGE_CACHE_MAX;i++)if(!page_cache[i].valid){p=pmm_alloc();if(!p)return 0;page_cache[i]=(struct page_cache_model){index,p,1,dirty,0,1};page_cache_count++;if(dirty)page_cache[i].writeback=0;return 1;}return 0;}
```

- 算法步骤：①线性扫 2 槽找同 `index` → 命中：`refs++`、`dirty|=dirty`（写访问
  会累积脏位）、`cache_hits++`；②未命中：`cache_misses++`，找空槽 `pmm_alloc` 建
  新项；③无空槽或分配失败返回 0。
- 边界：缓存容量固定 2（`PAGE_CACHE_MAX`），满了且都是有效项就「换不进来」——与
  Linux 的 `page_cache_lookup`→`add_to_page_cache_lru` 链路的容量上限约束对应。
- 为什么：`dirty` 是「写入过」标记，`writeback` 表示正在写回；教学模型明确声明
  **元数据 only，无磁盘 I/O**（`reclaimtest` 输出里写死
  `page cache is metadata-only; no disk I/O or swap executed`）。

#### 3.2.5 reclaim_one：单页回收（本课主题核心）

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```

- 签名与职责：扫描 `fault_pages[]`，回收第一个满足条件的匿名页，成功返回 1，否则 0。
- 算法步骤：①对每个槽 `reclaim_scans++` 记录扫描次数；②判据 `live &&
  reclaimable && refs==1` 任一不满足 → `reclaim_skips++` 跳过；③调
  `pmm_free_page` 把帧还给 PMM，只有逐字命中 `"freed"` 才继续（被 `vm_frame_owned`
  等拦截的帧在此被跳过）；④清除记录：`live=0`、`fault_page_count--`、
  `anon_pages--`、`anon_reclaims++`。
- 边界与错误处理：回收失败（未 freed）也计入 `reclaim_skips`；一次只收一页
  （与 `reclaimtest` 的断言 `anon_pages==0 && page_cache_count==1` 配套）。
- 为什么：`refs==1` 才回收是 Linux `page_count(page)==1` 的引用计数判据的直接
  简化；跳过被映射帧则是 `vm_normal_page`+`try_to_unmap` 语义的替代品。

```c
static TEXT64 void reclaim_init(void){page_cache_count=0;anon_pages=0;anon_reclaims=0;cache_hits=0;cache_misses=0;reclaim_scans=0;reclaim_skips=0;writeback_pages=0;}
static TEXT64 void anoninfo(u16*c){text64(c,"anon pages/cache live/reclaims: ");hex64(c,anon_pages);text64(c,"/");hex64(c,page_cache_count);text64(c,"/");hex64(c,anon_reclaims);text64(c," cache hit/miss: ");hex64(c,cache_hits);text64(c,"/");hex64(c,cache_misses);text64(c," reclaim scans/skips: ");hex64(c,reclaim_scans);text64(c,"/");hex64(c,reclaim_skips);putc64(c,'\n');}
```

- `reclaim_init`：在 `kernel_main64_binary` 里随 `vma_init()` 一起调用，把七个内存
  计数器归零，保证诊断基线一致。
- `anoninfo`：把「匿名页数 / 缓存活页数 / 回收次数 / 命中-未命中 / 扫描-跳过」一次
  性打全——这是本课主题「内存压力诊断」的主仪表盘。

#### 3.2.6 reclaimtest：一条命令走完「插页→缓存→回收」

```c
static TEXT64 void reclaimtest(u16*c){int a=fault_insert(VMA_DATA_START,1),b=page_cache_get(1,1),d=page_cache_get(1,0),e=reclaim_one();text64(c,"reclaimtest: ");text64(c,a&&b&&d&&e&&anon_pages==0&&page_cache_count==1?"anonymous reclaim and page-cache hit model passed":"BROKEN");text64(c,"\npage cache is metadata-only; no disk I/O or swap executed\n");}
```

- 算法步骤：①`fault_insert(VMA_DATA_START,1)` 插一页（`a`）；②
  `page_cache_get(1,1)` 写脏命中/建项（`b`）；③`page_cache_get(1,0)` 同 index 读
  → 命中（`d`）；④`reclaim_one()` 把匿名页收走（`e`）。
- 断言：`a&&b&&d&&e && anon_pages==0 && page_cache_count==1`——插的页被回收干净
  （匿名归零），缓存项保留 1 个。这就是「内存压力→插页→缓存→回收」完整闭环的
  教学验收。
- 诚实声明：随后打印 `page cache is metadata-only; no disk I/O or swap executed`，
  明确该模型不碰真实块设备。

#### 3.2.7 本课新增 checkpoint：lesson_127_model 与 l134test

```c
struct lesson_127_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_127_model lesson_127_state;
static TEXT64 void l134test(u16*c){lesson_127_state=(struct lesson_127_model){127U,128U,129U,130U,1,1,1,1};int ok=lesson_127_state.valid&&lesson_127_state.active&&lesson_127_state.ready&&lesson_127_state.accounted&&lesson_127_state.b==lesson_127_state.a+1U;text64(c,"l134test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 127 fallback reported");putc64(c,'\n');}
```

- 字段语义：4 个 u32 连续编号（a=127、b=128、c=129、d=130）+ 4 个状态位
  （valid/active/ready/accounted）。`a` 从 `127U` 起头 = 课号 134 − 7，回锚到
  Lesson 127 检查点模型。
- 断言逻辑：`ok` 要求五个条件全真（四个状态位 + `b==a+1`），据此输出成功串
  `bounded concurrency, SMP, RCU, and diagnostics checkpoint passed` 或失败串
  `Lesson 127 fallback reported`。
- 为什么：作为回归探针固化「继承机制自洽」；不执行任何回收/并发代码，消息描述的是
  整个内核机制的覆盖面。它与本课主题（内存压力诊断）的关系是：**checkpoint 保证
  上一课的状态机没被回收逻辑改坏**。

#### 3.2.8 exec64 命令接线与文案（本课增量）

```c
}else if(eq64(word,"l126test")){if(!noargs64(arg))usage64(c,"l126test");else l126test(c);}else if(eq64(word,"l134test")){if(!noargs64(arg))usage64(c,"l134test");else l134test(c);}
```

- 本课把上一课的 `l133test` 分支改名 `l126test`（复用 `lesson_126_model`），并新增
  `l134test` 分支。**勘误**：旧 README 写的 `Commands: l127test` 与源码不符，源码中
  可用的 checkpoint 命令是 `l126test` 与 `l134test`。
- about：`else text64(c,"Lesson 134: 内存压力诊断\n");`；开机横幅：
  `text64(&c,"Lesson 134: 内存压力诊断\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT;
  unknown=-ENOSYS; bounded reclaim metadata\n");`。
- 与内存主题直接相关的命令还有 `meminfo`、`anoninfo`、`reclaimtest`、`pfmodel`、
  `palloc`、`pfree <hex>`、`pageinfo <hex>`（都在 exec64 里有分支）。

### 3.3 构建管线（Makefile / linker）

- 构建流程与 Lesson 133 完全一致：`gcc $(CFLAGS64)` 编 kernel64.o →
  `ld -m elf_x86_64 -T kernel64.ld` + `objcopy -O binary` 出裸二进制 →
  32 位 `boot.o`（`.incbin` 嵌入）→ `ld -m elf_i386 -T linker.ld` 出外层 ELF32 →
  `grub-mkrescue` 出 ISO。`CFLAGS64` 含 `-m64 -ffreestanding -fpie -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx -Wall -Wextra -Werror`。
- `kernel64.ld` 用 `. = ALIGN(0x1000)` 划出 idle/rsp0/ist1 三块 guard+stack，三条
  `ASSERT(...==0x1000)` 锁尺寸。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`内存压力诊断`、`l134test`、`Lesson 134`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**，仅 `check` grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) → kernel_main64_binary (kernel64.c)
       module/init/wait/resource 模型 → pmm_init(h)（PMM 位图就绪）
       → vma_init() + reclaim_init()（内存计数器归零）
       → vfs_init()/address_space_init → IDT/PIT/PIC
       → 横幅 "Lesson 134: 内存压力诊断\n..." → 键盘 shell 循环
  exec64 分支 → meminfo:PMM 账目与 invariant 断言
             → pfmodel/reclaimtest:插页→缓存→回收闭环
             → anoninfo:匿名/缓存/回收计数器
             → l126test/l134test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以内存主题相关命令为例：

1. **`meminfo`** → exec64 命中 `meminfo` 分支 → `meminfo(c)` 打印
   `PMM: 4 KiB physical frames in 16 MiB mapped window`，随后 `tracked/free/used`
   三行账目与 `invariant tracked = free + used: yes`。
2. **`reclaimtest`** → `reclaimtest(c)`：`fault_insert`（PMM 出 1 帧）→
   `page_cache_get(1,1)` 建缓存项 → `page_cache_get(1,0)` 命中 →
   `reclaim_one` 回收匿名页 → 输出 `reclaimtest: anonymous reclaim and page-cache
   hit model passed`，下一行 `page cache is metadata-only; no disk I/O or swap
   executed`。
3. **`l134test`** → `l134test(c)` 对 `lesson_127_state` 赋值并五连断言 → 输出
   `l134test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。

数据流要点：PMM 位图是唯一「物理真实」的分配来源（`pmm_alloc` 动位图、记账），
VMA/page cache/回收都是元数据登记；所有账目变化都会反映到 `anoninfo`/`meminfo` 的
计数器与 invariant 断言上。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-134-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `内存压力诊断`、`l134test`、`Lesson 134` 与 kernel64.c 中的 `l134test`，全部命中
  输出 `Multiboot2 and Lesson 134 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅
  第一行为 `Lesson 134: 内存压力诊断`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 134: 内存压力诊断`
  2. `l134test` → `l134test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  3. `l126test` → `l126test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  4. `reclaimtest` → `reclaimtest: anonymous reclaim and page-cache hit model
     passed`，下一行 `page cache is metadata-only; no disk I/O or swap executed`
  5. `anoninfo` → `anon pages/cache live/reclaims: 0000000000000000/0000000000000001/0000000000000001
     cache hit/miss: 0000000000000001/0000000000000001 reclaim scans/skips:
     0000000000000001/0000000000000000`（在 `reclaimtest` 之后立即执行可得此值）
  6. `meminfo` → 首行 `PMM: 4 KiB physical frames in 16 MiB mapped window`，末行
     `invariant tracked = free + used: yes`
  7. `pfmodel` → `pfmodel: not-present/protection/unmapped classified; bounded page
     inserted`
- **如何判断成功**：上述命令逐一打印预期串即成功；`meminfo` 的 invariant 必须为
  `yes`（账目守恒）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l134test` 输出 `Lesson 127 fallback reported` | checkpoint 模型被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l134test` 赋值 `{127U,128U,129U,130U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `reclaimtest` 输出 `BROKEN` | `reclaim_one` 判据不满足或 `pmm_free_page` 未返回 `freed` | `anoninfo` 看 `reclaim scans/skips`；确认被插页 `reclaimable=1` 且 `refs==1`；确认帧未被 `vm_frame_owned` 占用 |
| `meminfo` 输出 `invariant tracked = free + used: BROKEN` | PMM 账目泄漏或双重释放 | 逐命令检查 `palloc`/`pfree`/`reclaimtest` 后 `pmm_free+pmm_used` 是否等于 `pmm_total` |
| `anoninfo` 的 cache hit/miss 不符合预期 | `page_cache_get` 顺序或 index 用错 | 按 `reclaimtest` 的调用顺序（同 index 两次）复现；确认第二个调用命中（`cache_hits` 增 1） |
| `palloc` 后 `pfree <hex>` 报 `cannot free: fixed/reserved` | 释放了保留/固定帧 | 用 `pageinfo <hex>` 看状态；只释放 `allocated` 状态的帧 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 134: 内存压力诊断`；`make check` grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **物理页分配器**：TinyOS `pmm_alloc` 线性扫位图找空闲帧；Linux
   `mm/page_alloc.c` 的伙伴系统按阶数/迁移类型维护 freelist。TinyOS 无合并、
   无防碎片，只保留「分配/释放记账」语义。
2. **页回收**：TinyOS `reclaim_one` 判据 `live && reclaimable && refs==1` 对应
   Linux `mm/vmscan.c` 的 `shrink_page_list`：`PageDirty`→回写、
   `try_to_unmap`→解除映射、`page_count(page)==1`→`free_unref_page`。
   TinyOS 砍掉了 LRU 链表、swap、kswapd 后台线程。
3. **page cache**：TinyOS `page_cache_get` 按 index 命中/未命中并计数，对应 Linux
   `mm/filemap.c` 的 `page_cache_lookup`/`add_to_page_cache_lru`；`dirty/writeback`
   标志对应 `PG_dirty`/`PG_writeback`。TinyOS 明确不执行磁盘 I/O。
4. **内存压力诊断接口**：TinyOS `anoninfo`/`meminfo` 输出匿名页数、命中率、
   扫描/跳过次数、`free+used` 不变量；对应 Linux `fs/proc/meminfo.c` 与
   `mm/vmstat.c` 的 `pgscan_*`/`pgskip_*` 计数（`/proc/vmstat`）。TinyOS 是单命令
   文本输出版。
5. **OOM 语义**：TinyOS `pmm_alloc` 耗尽返回 0、`fault_insert` 失败返回 0，但
   不做 `mm/oom_kill.c` 的杀进程决策——教学模型只暴露「拿不到帧」这个压力信号。
6. **回收不变量**：`pmm_total==pmm_free+pmm_used` 是 TinyOS 自有的账目守恒断言；
   Linux 用 `pgdat->node_page_state` 与 debug 版 `page_owner` 做类似校验。

**权威来源**：Intel SDM Vol.3A（分页）、Linux `mm/vmscan.c`/`mm/page_alloc.c`/
`mm/filemap.c`、`fs/proc/meminfo.c`。
**教学模型简化了什么**：无真实页表写入（`fault_insert` 只登记元数据）、无 LRU 排序、
无写回/swap、无 OOM killer，一次只收一页。

---

## 8. 思考题与练习

1. **概念理解**：`reclaim_one` 为什么要求 `refs==1` 才回收？如果把 `refs` 改成 2，
   预测 `reclaimtest` 的哪个断言会失败？
2. **源码定位**：在 `kernel64.c` 中找出 `reclaim_init` 的调用位置，说明它和
   `vma_init` 的顺序为什么决定 `reclaimtest` 的基线。
3. **动手实验**：修改 `reclaim_one`，把 `reclaimable` 判据去掉（只留
   `m->live && m->refs==1`），重跑 `reclaimtest`，观察是否仍输出 passed，并用
   `anoninfo` 解释行为差异。
4. **动手实验**：先执行 `meminfo` 记录 `free` 值，再执行 `palloc`、`reclaimtest`、
   `meminfo`，验证 `invariant tracked = free + used: yes` 始终成立；然后故意
   两次 `pfree` 同一帧（第二次应报 `cannot free: free`），观察账目是否被破坏。
5. **Linux 对照**：阅读 `mm/vmscan.c` 的 `shrink_page_list` 注释与
   `fs/proc/meminfo.c` 的字段定义，对比它们与 `reclaim_one`/`anoninfo` 的分工，
   指出 TinyOS 砍掉的三个处理阶段。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是检查点课，`kernel64.c` 相对 Lesson 133 只有 3 处小增量，内存机制全部继承，
   主题由 banner/about 文案「内存压力诊断」标识。
2. 内存压力链路四层：PMM 位图分配（`pmm_alloc`）→ 匿名插页（`fault_insert`）→
   page cache（`page_cache_get`）→ 单页回收（`reclaim_one`）。
3. 可回收判据是 `live && reclaimable && refs==1`，被映射帧由
   `pmm_free_page` 的所有权检查拦截。
4. `meminfo` 的 `invariant tracked = free + used` 是账目守恒探针；`anoninfo` 是
   匿名页/缓存/回收计数器仪表盘。
5. `reclaimtest` 一条命令走完「插页→缓存命中→回收」闭环，并诚实声明无磁盘 I/O。
6. `pmm_alloc` 耗尽返回 0 是内存压力的第一个表象，教学模型不做 OOM killer。
7. 旧 README 的 `Commands: l127test` 已勘误为源码实际的 `l126test` 与 `l134test`。

**下一课**：[`lesson-135-stable/README.md`](../lesson-135-stable/README.md) 主题为
「调度与并发综合诊断」，将从内存切到 CPU：站在 `irq0_schedule`、`rr_pick_next`、
信号量/事件/等待队列与生产者-消费者模型之上，讲解 `threadinfo`/`pcinfo`/`ps64`
诊断命令，并追加新的 checkpoint 模型 `lesson_128_model`（命令 `l135test`）。两课的
衔接点：内存压力诊断依赖 IRQ0 的 PIT 调度驱动，调度诊断则复用本课继承的
`pmm_alloc` 为线程栈分配帧。
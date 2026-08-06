# Lesson 155: cgroup 内存限制 — 精讲文档

> **课号**：Lesson 155 ｜ **主题**：cgroup 内存限制（cgroup memory limits）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课为 **cgroup 系列四连**（层级 153 → CPU 统计 154 → **内存限制 155** → 设备策略 156）的第三课
> **前置课程**：[`../lesson-154-stable/README.md`](../lesson-154-stable/README.md)（cgroup CPU 统计）
> **后续课程**：[`../lesson-156-stable/README.md`](../lesson-156-stable/README.md)（cgroup 设备策略）
> **一句话目标**：讲清 cgroup 的 memory 控制器如何记账与限流——`memory.max`（硬上限）、`memory.current`（当前用量）、`memory.swap.max`、OOM 触发与回收（reclaim），对照 Linux `mm/memcontrol.c`、`mm/vmscan.c`、`mm/oom_kill.c`，并把教学内核中继承的**内存账本设施**（PMM 的 `pmm_total/pmm_free/pmm_used` 不变量、`page_model` 的 refs/reclaimable 位、`page_cache_model` 的 dirty/writeback 位、`reclaim_one` 回收路径）按 cgroup 内存限制主题系统化复述，运行 `l155test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（cgroup memory 控制器）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l154test` 恢复为历史命名 `l147test`（挂 `lesson_147_state`），新增 `lesson_148_model`/`lesson_148_state` 与 `l155test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l155test`（旧 README 所写 `l148test` 按源码勘误）；另保留历史检查点 `l100test`–`l147test`，以及 `meminfo`/`palloc`/`pfree`/`pageinfo`/`anoninfo`/`reclaimtest`/`pfmodel`/`reclaimtest` 等内存账本回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「一个永远记着的账本」解释 cgroup 内存限制（`memory.current` 记录该组所有页，`memory.max` 是天花板，超限先回收后 OOM）；说出 Linux memory 控制器的核心对象与文件（`mm/memcontrol.c` 的 `struct mem_cgroup`/`memory.max`/`memory.current`/`memory.stat`）；在教学内核中沿 `meminfo`（`tracked = free + used` 不变量）→ `palloc`/`pfree` → `anoninfo` → `reclaimtest` 观察内存记账与回收；运行 `l155test`/`reclaimtest` 验证。

**在课程主线中的位置**：Lesson 154 讲完 CPU 计量，本课把 cgroup 的第二个控制器——**内存限制**——讲清。CPU 是「时间」资源，内存是「空间」资源，两者是容器资源限制的主轴。作为检查点课，源码 diff 极小，任务是把继承机制中与「内存记账与回收」相关的设施（PMM 位图与不变量、页模型 refs、页缓存 dirty/writeback、回收路径）按 cgroup 内存限制主题系统化复述。下一课（Lesson 156）讲 cgroup 设备策略，收束整个 cgroup 系列。

**前置知识清单**（学本课前必须掌握）：
1. PMM：`PMM_MAX_PHYS 0x01000000ULL`（16 MiB）、`pmm_bitmap`/`pmm_fixed` 位图、`pmm_total/pmm_free/pmm_used`（Lesson 22s/34s）。
2. 页模型：`struct page_model{va,phys; writable,live,backing,dirty,accessed,reclaimable; refs}`（Lesson 64s）。
3. 页缓存：`struct page_cache_model{index,phys; valid,dirty,writeback; refs}`、`PAGE_CACHE_MAX`（Lesson 64s）。
4. 回收路径：`reclaim_init`/`reclaim_one`、`anon_pages`/`anon_reclaims`/`reclaim_scans`/`reclaim_skips`（Lesson 64s/66s）。
5. 缺页分类：`pf_classify`（PF_NOT_PRESENT/PF_PROTECTION/PF_UNMAPPED）与 `fault_insert`（Lesson 64s）。
6. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–154）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 155: cgroup 内存限制`；
- 新命令 `l155test` 输出 `l155test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `meminfo`/`anoninfo`/`reclaimtest` 继续展示内存账本与回收。

---

## 2. 核心概念精讲

### 2.1 cgroup 内存限制：一个永远记着的账本

**直觉**：给容器设 `memory.max=4G` 后，内核从此替它记一本账：`memory.current` 实时记录该组已占用的所有内存（匿名页 + 页缓存 + 内核对象）。账本超过上限时，先尝试**回收**（写回脏页、释放缓存），回收不够才**OOM**（杀进程）。

```
memory.current（该组所有页的用量）
   ├── 达到 memory.max → 回收：memory.reclaim / 后台 vmscan
   │       └── 仍超 → OOM killer（mm/oom_kill.c）
   ├── memory.swap.max → 换出到 swap 的上限
   └── memory.stat → user/anon/file/kernel 细分用量
```

**准确定义**：cgroup memory 控制器按**层级树**记账：父组的 `memory.current` = 自身用量 + 全部子组用量之和；`memory.max` 的硬上限在超过时触发回收/OOM。记账对象不只匿名页，还包括 page cache（文件页）与内核内存（`memory.kmem`）。

### 2.2 为什么需要 cgroup 内存限制（动机）

1. **防内存挤占**：一个容器无上限申请内存会挤垮整机；`memory.max` 把风险关进组里。
2. **限额语义**：`memory.max` 硬上限 + `memory.high` 软上限（超了先回收不立即 OOM）+ `memory.swap.max`（换出上限）——三层缓冲让限制可调。
3. **统计与计费**：`memory.stat` 细分 `anon/file/kernel_stack/sock` 等用量，是监控与调优的数据基础。

### 2.3 Linux 中 cgroup 内存限制的工作机制

- **核心结构**：`mm/memcontrol.c` 的 `struct mem_cgroup`（`memory.max`、`memory.current`、`memory.swap.max`、`memory.stat`、`memory.events`）。每个页通过 `page->mem_cgroup` 归属某个组。
- **记账**：`__count_memcg_events`/`page_counter_charge` 在页分配/映射时把用量加进组账本；LRU 按组独立维护（`struct lruvec`），实现**按组回收**。
- **回收**：`mm/vmscan.c` 的 `shrink_node`/`shrink_lruvec` 沿组回收 LRU 页；`memory.reclaim` 是 v2 的同步回收接口。
- **OOM**：超限且回收无效 → `mm/oom_kill.c` 的 `out_of_memory` 在该组内选 victim。
- **教学简化**：教学内核没有 `mem_cgroup`，但「页的账本」完整存在：PMM 位图记录每 4 KiB 帧的 free/allocated/fixed 三态与 `tracked=free+used` 不变量，`page_model`/`page_cache_model` 带 refs/dirty/reclaimable 位，`reclaim_one` 实现一次「扫描→选页→释放」的回收——这是「单组、无上限」的内存记账与回收模型。

### 2.4 教学内核中与「内存记账」有关的既有设施

本课主题机制（cgroup memory 控制器）**未在源码中实现**，但「内存账本与回收」素材完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| 物理帧账本 | `#define PMM_MAX_PHYS 0x01000000ULL`、`pmm_bitmap`/`pmm_fixed`、`pmm_total/pmm_free/pmm_used` | 「已用/空闲/固定」三态账本（对照 memory.current 的组分账） |
| 账本不变量 | `meminfo` 断言 `tracked = free + used` | 账本守恒——总量恒等于已用加空闲（对照 page_counter 的一致性） |
| 页模型 | `struct page_model{...dirty,accessed,reclaimable; refs}` | Linux `struct page` 的 flags/refcount 浓缩 |
| 页缓存 | `struct page_cache_model{...dirty,writeback; refs}` | page cache 的脏/回写位（对照 memory.stat 的 file 用量） |
| 回收路径 | `reclaim_one`：`reclaim_scans++` → 选 `reclaimable&&refs==1` → `pmm_free_page` → `anon_reclaims++` | vmscan 按组回收 LRU 页的教学缩影 |
| 回收统计 | `anon_pages`/`anon_reclaims`/`reclaim_scans`/`reclaim_skips`/`writeback_pages` | memory.stat 式统计 |
| 缺页分类 | `pf_classify` 与 `fault_insert` | 页「存在性」判定（对照 page fault 与缺页装载） |

### 2.5 检查点模型：lesson_148_model 与 l155test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `148→151` 标记 Origin 为 Lesson 148（`a=148,b=149,c=150,d=151`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「账本连续性」。本课同时把上一课新增的 `l154test` 恢复为历史命名 `l147test`（挂 `lesson_147_state`，计数 `147→150`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.6 机制继承 + 检查点增量

本课主题机制（cgroup memory 控制器）**不是本课新写的代码**：PMM 位图与不变量来自物理内存阶段，页模型/页缓存/回收来自缺页与 reclaim 阶段。本课实际增量只有三处：`l154test`→`l147test` 更名、`lesson_148_model`+`l155test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「内存账本」主题重新组织，并如实说明：**cgroup memory 控制器（`mem_cgroup`/`memory.max` 式对象）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l154test`→`l147test` 恢复命名；新增 `lesson_148_model`/`lesson_148_state`/`l155test`；`about` 与开机横幅更新。cgroup 内存限制主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`cgroup 内存限制`/`l155test`/`Lesson 155`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（内存账本机制 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_148_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_148_model lesson_148_state;
static TEXT64 void l155test(u16*c){lesson_148_state=(struct lesson_148_model){148U,149U,150U,151U,1,1,1,1};int ok=lesson_148_state.valid&&lesson_148_state.active&&lesson_148_state.ready&&lesson_148_state.accounted&&lesson_148_state.b==lesson_148_state.a+1U;text64(c,"l155test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 148 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `148→151`（Origin Lesson 148），四布尔位全置 1，`b==a+1U` 校验计数连续。
2. **逻辑分析（≥3 行）**：结构体字面量一次性写入 `lesson_148_state`，`ok` 由四布尔位 + `b==a+1U` 合取；字面量全 1 使断言恒真，成功串必输出；`Lesson 148 fallback reported` 是防御性兜底，仅在模型计数被破坏时命中。
3. **输出串（逐字抄录）**：成功 `l155test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 148 fallback reported`。
4. **恢复的 `l147test`**：本课把上一课 `l154test` 更名回 `l147test`（同为 `lesson_147_state`，计数 `147→150`）；`l100test`–`l146test` 历史检查点全部保留。

#### 3.2.2 账本基础：PMM 位图与不变量

```c
#define PMM_MAX_PHYS 0x01000000ULL
static volatile u8 pmm_bitmap[PMM_BITMAP_BYTES];
static volatile u8 pmm_fixed[PMM_BITMAP_BYTES];
static u64 pmm_total,pmm_free,pmm_used;
```

1. **位图账本**：`PMM_MAX_PHYS`（16 MiB）/`PAGE_SIZE`（4 KiB）= 4096 个帧；`pmm_bitmap` 每帧 1 位记 free/allocated，`pmm_fixed` 另记固定保留——两本账对照 cgroup 的「用量账」与「保留账」。
2. **三计数**：`pmm_total`（可分配帧数）、`pmm_free`（空闲）、`pmm_used`（已用）——`meminfo` 断言 `tracked = free + used`，即账本守恒不变量。
3. **volatile**：位图在中断/多线程上下文被改，`volatile` 防止读缓存——账本必须实时。

```c
static TEXT64 const char *page_state(u64 p){u32 i;if(!pmm_ready)return "PMM unavailable";if((p&(PAGE_SIZE-1))||p>=PMM_MAX_PHYS)return "invalid";i=(u32)(p/PAGE_SIZE);if(!bit(i))return "free";if(fixed(i))return "fixed/reserved";return "allocated";}
```

1. **三态判定**：`free`（位为 0）→ `fixed/reserved`（固定位）→ `allocated`——先查空闲再查固定，账本状态的完整分类（`pageinfo` 命令打印）。
2. **边界检查**：非页对齐或越 `PMM_MAX_PHYS` 判 `invalid`——查询非法地址不越账。
3. **前置**：`pmm_ready` 未就绪返回 `PMM unavailable`——账本初始化前的防御。

#### 3.2.3 页级账本：page_model 与 page_cache_model

```c
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
```

1. **page_model 即 Linux `struct page`**：`refs`（引用计数，`reclaim_one` 要求 `refs==1`）、`reclaimable`（可回收位）、`dirty`/`accessed`——回收候选页的全部判定字段。
2. **page_cache_model 即 page cache 页**：`dirty`（脏）、`writeback`（回写中）——对照 memory 控制器对文件页的记账（`memory.stat` 的 file 行）。
3. **两类账本**：匿名页（`fault_pages`）与缓存页（`page_cache`）分开记账，`anoninfo` 分别打印 `anon pages/cache live/reclaims`。

#### 3.2.4 回收路径：reclaim_one（vmscan 的教学缩影）

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```

1. **扫描**：遍历 `fault_pages` 找回收候选，每次访问 `reclaim_scans++`——对照 `shrink_lruvec` 按 LRU 扫描。
2. **候选条件（≥3 行分析）**：`live&&reclaimable&&refs==1` 才可能回收——live（页存在）、reclaimable（允许回收）、refs==1（无其他引用，引用计数为 1 才可释放），任一不满足则 `reclaim_skips++` 跳过；这与 Linux `try_to_free_pages` 检查 `page_count` 与 `PageReclaim` 同构。
3. **物理释放**：`pmm_free_page(m->phys)` 返回 `"freed"` 才继续——物理位图同步更新（`pmm_free++`/`pmm_used--`），账本在页模型与 PMM 两层一致。
4. **记账更新**：`live=0`、`fault_page_count--`、`anon_pages--`、`anon_reclaims++`——回收量全部入账（对照 memory.stat 的回收事件）。

#### 3.2.5 账本观察与验证：meminfo / anoninfo / reclaimtest

```c
static TEXT64 void meminfo(u16*c){text64(c,"PMM: 4 KiB physical frames in 16 MiB mapped window\nstatus:  ");text64(c,pmm_error);if(!pmm_ready){putc64(c,'\n');return;}text64(c,"\ntracked: ");hex64(c,pmm_total);text64(c,"\nfree:    ");hex64(c,pmm_free);text64(c,"\nused:    ");hex64(c,pmm_used);text64(c,"\ninvariant tracked = free + used: ");text64(c,pmm_total==pmm_free+pmm_used?"yes":"BROKEN");text64(c,"\nbitmap:  ");hex64(c,(u64)(unsigned long)pmm_bitmap);text64(c," +");hex64(c,PMM_BITMAP_BYTES);text64(c,"\nfixed:   ");hex64(c,(u64)(unsigned long)pmm_fixed);putc64(c,'\n');}
```

1. **账本一览**：`tracked/free/used` 三行十六进制 + 位图/固定位图地址——整个物理内存账本对用户可见。
2. **不变量断言**：`pmm_total==pmm_free+pmm_used` 输出 `yes`/`BROKEN`——账本守恒是每次查看都验证的不变量（对照 page_counter 的 `page_counter_cancel` 一致性）。
3. **reclaimtest 全流程**：`fault_insert(VMA_DATA_START,1)`（插匿名页）→ `page_cache_get(1,1)`/`page_cache_get(1,0)`（缓存命中）→ `reclaim_one()`（回收匿名页）→ 成功串 `reclaimtest: anonymous reclaim and page-cache hit model passed` + `page cache is metadata-only; no disk I/O or swap executed`。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 155: cgroup 内存限制\n`；检查点分支：
```c
else if(eq64(word,"l147test")){if(!noargs64(arg))usage64(c,"l147test");else l147test(c);}else if(eq64(word,"l155test")){if(!noargs64(arg))usage64(c,"l155test");else l155test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 155: cgroup 内存限制\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```
`bounded reclaim metadata` 正是指 `reclaim_scans/skips/writeback_pages` 这套回收统计——内存限制主题的元数据宣言。

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` → `kernel64.ld` → `objcopy` → `boot.S` 嵌入 → `grub-mkrescue`）。`make check` 断言 README 含 `cgroup 内存限制`、`Lesson 155`，kernel64.c 含 `l155test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ pmm_init()（位图账本）/ reclaim_init()（回收统计清零）
 ├─ 横幅 "Lesson 155: cgroup 内存限制"
 └─ 主循环：命令 → exec64
     ├─ l155test / l147test → 阶段检查点（lesson_148_state / lesson_147_state）
     ├─ meminfo → tracked/free/used + 不变量断言
     ├─ palloc / pfree / pageinfo → 帧分配/释放/状态查询
     ├─ anoninfo → 匿名页/缓存/回收统计
     ├─ reclaimtest → 插入→命中→回收全流程
     └─ pfmodel → 缺页三分类（not-present/protection/unmapped）
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`pmm_init` 从 Multiboot2 mmap 建位图账本，`reclaim_init` 清零回收统计，打印横幅 `Lesson 155: cgroup 内存限制`。
2. **`l155test`** → `l155test(c)` → 初始化 `lesson_148_state` → 五条件断言 → `l155test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`meminfo`** → 打印 `tracked: <total>`、`free: <free>`、`used: <used>` 与 `invariant tracked = free + used: yes`。
4. **`palloc`** → `pmm_alloc()` 置位并 `pmm_free--`/`pmm_used++` → `allocated: <phys>`；`pfree <hex>` → `pmm_free_page` → `freed` 或拒绝原因。
5. **`reclaimtest`** → `fault_insert` + `page_cache_get`（命中） + `reclaim_one`（回收） → `reclaimtest: anonymous reclaim and page-cache hit model passed` + `page cache is metadata-only; no disk I/O or swap executed`。
6. **`about`** → `Lesson 155: cgroup 内存限制`。

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
Multiboot2 and Lesson 155 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 155: cgroup 内存限制` 横幅 |
| `l155test` | `l155test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l147test` | `l147test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `meminfo` | `PMM: 4 KiB physical frames in 16 MiB mapped window`、`tracked:/free:/used:` 行与 `invariant tracked = free + used: yes` |
| `reclaimtest` | `reclaimtest: anonymous reclaim and page-cache hit model passed` 及 `page cache is metadata-only; no disk I/O or swap executed` |
| `pfmodel` | `pfmodel: not-present/protection/unmapped classified; bounded page inserted` |
| `about` | `Lesson 155: cgroup 内存限制` |

判定成功：`l155test`/`reclaimtest` 输出 passed、无 fallback/`BROKEN`，`meminfo` 不变量为 `yes`，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l155test` 输出 `Lesson 148 fallback reported` | `lesson_148_state` 初始化/断言失败（stale 镜像） | `grep -n "l155test" kernel64.c`；确认初始化串 `{148U,149U,150U,151U,1,1,1,1}` 与 `b==a+1U` |
| `meminfo` 显示 `invariant ... BROKEN` | `pmm_free`/`pmm_used` 与位图不一致（释放路径漏更新） | 对照 `pmm_alloc`/`pmm_free_page` 的 `pmm_free++`/`pmm_used--`；`palloc` 后立即 `meminfo` 验证 |
| `reclaimtest` 输出 `BROKEN` | `reclaim_one` 的候选条件不满足（`refs!=1`/`!reclaimable`）或 `pmm_free_page` 拒绝 | 对照 `fault_insert` 是否置 `reclaimable`；`anoninfo` 看 `anon_pages`/`reclaim_scans` |
| `pfree <hex>` 提示 `cannot free: ...` | 页非 `allocated` 态、被映射或属线程栈 | `pageinfo <hex>` 看状态；`page_state` 返回 `free`/`fixed/reserved`/`invalid` |
| `pfmodel` 分类异常 | VMA 权限位与 `pf_classify` 不匹配 | `pf_classify` 按 `v->prot&VMA_W/VMA_R` 分 PF_PROTECTION，`vma_lookup` 失败分 PF_UNMAPPED |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 155' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `cgroup 内存限制` 与 `Lesson 155` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `pmm_bitmap`/`pmm_fixed` 位图 | `mm/page_alloc.c` 的 `struct page` 与 `memmap`；`mm/memcontrol.c` 的 `struct mem_cgroup` 分账 | 模型只有全局位图，无 per-cgroup `page_counter` |
| `pmm_total==pmm_free+pmm_used` 不变量 | `mm/memcontrol.c` 的 `memory.current`（page_counter 累加）；`page_counter_charge`/`uncharge` | 模型是单个全局账本，无层级汇总 |
| `page_model{refs,reclaimable,dirty}` | `include/linux/mm_types.h` 的 `struct page`（`_refcount`/`flags`）；`mm/vmscan.c` 的 `PageReclaim` | 模型无 LRU 链表与 `lruvec` 分组 |
| `page_cache_model{dirty,writeback}` | `mm/filemap.c` 的 page cache；`memory.stat` 的 file 行 | 模型无真实 I/O，写回只是元数据位 |
| `reclaim_one`（scan/skip/refs==1/free） | `mm/vmscan.c` 的 `shrink_node`/`try_to_free_pages`；`mm/memcontrol.c` 的按组回收 | 模型线性扫 4 页定长表，无 LRU 序与压力分级 |
| `anoninfo` 的 anon/cache/reclaim 统计 | `memory.stat`（anon/file/kernel_stack 细分）与 `memory.events`（oom/reclaim 事件） | 模型只打印计数，无事件计数器与水位 |
| `pf_classify`/`fault_insert` | `mm/memory.c` 的 `handle_mm_fault` 缺页处理 | 模型无真实页表 walk 与 `do_anonymous_page` |
| `memory.max`/OOM | `mm/memcontrol.c`（`memory.max`）、`mm/oom_kill.c`（`out_of_memory`） | 模型无上限与 OOM，只有「回收一页」的单步 |

**权威来源**：Linux `mm/memcontrol.c`、`mm/vmscan.c`、`mm/page_alloc.c`、`mm/oom_kill.c`、`include/linux/mm_types.h` 为对照；Intel SDM 与 Multiboot2 规范仍为引导/硬件权威来源。

**如实说明**：本课**没有** `struct mem_cgroup`、`memory.max` 或 `memory.current` 的等价实现——cgroup 内存限制是「主题宣告」，教学内核停留在「单组、无上限、位图 + 页模型 + 回收路径」的模型上。

---

## 8. 思考题与练习

1. **概念理解**：为什么 cgroup 要把匿名页与 page cache 分开记账（`memory.stat` 的 anon/file）？回收时哪个优先、为什么？
2. **源码定位**：在 `kernel64.c` 中找出 `pmm_used++`/`pmm_used--` 的全部位置（提示：`pmm_alloc`、`pmm_free_page`），说明账本守恒如何保证。
3. **动手实验**：把 `reclaim_one` 的候选条件 `refs!=1` 去掉，运行 `reclaimtest` 观察输出变化，说明引用计数保护了什么。
4. **动手实验**：仿照 `memory.max` 语义，给 `pmm_alloc` 增加「`pmm_used>=上限` 时先调用 `reclaim_one()` 再尝试分配」的逻辑，并用 `meminfo` 验证。
5. **Linux 对照**：阅读 `mm/vmscan.c` 的 `shrink_lruvec`，说明回收为什么按 LRU 序而不是任意序；对比教学模型「线性扫定长表」的简化。

---

## 9. 本课小结与下一课预告

1. cgroup 内存限制 = 记账 + 限流：`memory.current` 实时记账，`memory.max` 超限先回收后 OOM。
2. Linux 用 `struct mem_cgroup` + `page_counter`（`mm/memcontrol.c`）按层级记账，`mm/vmscan.c` 按组回收，`mm/oom_kill.c` 兜底。
3. 教学内核的账本素材完整：PMM 位图与 `tracked=free+used` 不变量、`page_model`/`page_cache_model` 的 refs/dirty/reclaimable 位、`reclaim_one` 的单步回收。
4. `reclaim_one` 的 `refs==1&&reclaimable` 候选条件与 `reclaim_scans/skips` 统计是 vmscan 的教学缩影。
5. 检查点增量：`l154test`→`l147test` 更名、新增 `lesson_148_model`+`l155test`、横幅与 `about` 更新为 `Lesson 155: cgroup 内存限制`。
6. 下一课（Lesson 156）主题为 **cgroup 设备策略**（对照 `kernel/cgroup/devices/` 与 BPF cgroup device 控制器）：cgroup 系列收官，从「用量限制」转到「访问许可」，教学内核将以 VGA/键盘/用户态访问设施承接该主题。

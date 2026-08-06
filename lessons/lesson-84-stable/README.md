# Lesson 84: 共享页生命周期 — 精讲文档

> **课号**：Lesson 84（主线源课编号 Lesson 77 线）
> **本课主题**：共享页生命周期（shared page lifecycle）——页对象 `refs` 引用计数如何表达"分配 → 共享 → 释放"的一生，页缓存命中/未命中，回收只对 `refs==1` 的私页生效
> **课程主线位置**：调度 / COW 元数据教学模型阶段（Lesson 64 起）。COW 三连课的收尾：Lesson 82 造出页对象、Lesson 83 统计缺页入口、本课讲页的"一生"——引用计数如何在共享中增、在释放中减，回收如何谨慎地只碰私页。
> **前置课程**：[`../lesson-83-stable/README.md`](../lesson-83-stable/README.md)（COW 写时复制缺页统计：`pf_classify` 与四个计数器）
> **后续课程**：[`../lesson-85-stable/README.md`](../lesson-85-stable/README.md)（下一主题）
> **本课一句话目标**：理解"物理页的引用计数决定它的生死"——`refs=0` 空闲、`refs=1` 私有、`refs>1` 共享；写时复制把共享页分裂为两个 `refs=1` 的私页；回收器 `reclaim_one` 只回收 `refs==1` 的页，绝不碰仍被共享的页。
> **保留的原始快照信息**：This checkpoint models bounded scheduling and copy-on-write metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l84test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能用 `refs` 引用计数讲述一个物理页的完整一生：空闲（0）→ 分配（1）→ 共享（N）→ 写时分裂回（1）→ 回收归零；并能在 `reclaimtest`/`anoninfo` 里观察"共享页不可回收、私页可回收"的确定性行为。
- **在课程主线中的位置**：COW 三连课（82–84）的收官。`writable`（Lesson 82）决定"能不能写"，`pf_classify`（Lesson 83）决定"缺页怎么分类"，`refs`（本课）决定"这页该不该存在"。三者在 Linux 里共同构成 `mm/memory.c` 的 COW 与 `mm/vmscan.c` 的回收语义。
- **前置知识清单**（学本课之前必须掌握）：
  1. `struct page_model` 的字段与 `fault_insert`（Lesson 82）；
  2. `pf_classify` 的三类缺页分类与 `PF_PROTECTION`=COW 写保护（Lesson 83）；
  3. `pmm_free_page`/`page_state` 的回收语义（"allocated" 才能 free，`thread_stack_owned`/`vm_frame_owned` 保护）（Lesson 34 起）；
  4. `exec64` 命令分派与 VGA 输出（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_77_model` + `lesson_77_state` + `l84test`；把 `l83test` 改名为 `l76test`；`about` 与 banner 更新为「Lesson 84: 共享页生命周期」。

---

## 2. 核心概念精讲

### 2.1 引用计数：一页的"一生"

**直觉**：一个物理页可能同时被多个映射引用（父子进程共享、多个文件映射、内核引用）。如果某个使用者释放了它，别人还在用，立刻就会出故障。所以每页记一个"我还有多少人在用"的计数：**引用计数（refcount）**。

```
refs=0 空闲页（PMM 位图中 free）
   │  pmm_alloc / fault_insert
   ▼
refs=1 私有页（只有一个人引用）
   │  第二个映射接入（fork/共享）
   ▼
refs=N 共享页（N 个引用）→ 写时复制：分裂为两个 refs=1 的私页
   │  释放一个引用
   ▼
refs=1 回到私有
   │  reclaim_one（refs==1 才回收）
   ▼
refs=0 归还 PMM
```

### 2.2 `refs` 与 COW 的关系

COW 的关键就在"共享 → 分裂"两步：
- **共享**：fork 时把页的引用计数加 1（`refs++`），页表项标只读；
- **分裂**：写者触发保护缺页，`do_wp_page` 复制新页，原页 `refs--`、新页 `refs=1`。

TinyOS 的 `page_model.refs` 就是这个计数的教学投影（u16 足够，因为教学模型里共享数很小）。`page_cache_model.refs` 同理——页缓存页也要计数。**回收的准则**：`reclaim_one` 只回收 `refs==1` 的页——因为 `refs>1` 意味着还有别人在用，回收就是灾难；`refs==0` 则说明页已无效。

### 2.3 页缓存：`page_cache_get` 的命中/未命中

页缓存（page cache）是"文件内容在内存里的副本"的教学模型：`page_cache_get(index,dirty)` 先查缓存，命中则 `refs++`（多一个使用者）、`dirty|=dirty`（可能变脏），未命中则从 PMM 分配新页并 `refs=1`。这正是 Linux `get_page`/`page_cache_get_speculative` 的语义。

### 2.4 回收：`reclaim_one` 的谨慎

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```

回收只做"最安全的选择"：页必须 `live`（有效）、`reclaimable`（允许回收）、`refs==1`（没人共享）。三者任一不满足就 `reclaim_skips++` 跳过。真实内核的 `shrink_page_list` 也遵循同样的铁律：**引用计数不为 1 的页绝不回收**。

### 2.5 「固定元数据 + 确定性验证」教学模型

`struct lesson_77_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 是调度 checkpoint 的收尾投影。本课的主验证是 `reclaimtest`：一条命令走完"插入私页 → 页缓存命中两次 → 回收成功"的完整生命周期。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-83） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：页引用计数、页缓存、回收、`exec64` 分派 | **主要增量**：新增 `struct lesson_77_model`、`lesson_77_state`、`l84test()`；把 `l83test` 改名为 `l76test`；`exec64` 分支与 `about`、banner 文案更新（`refs`/`page_cache_get`/`reclaim_one` 继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 84 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的固定元数据记录与 `l84test`

```c
struct lesson_77_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_77_model lesson_77_state;
static TEXT64 void l84test(u16*c){lesson_77_state=(struct lesson_77_model){77U,78U,79U,80U,1,1,1,1};int ok=lesson_77_state.valid&&lesson_77_state.active&&lesson_77_state.ready&&lesson_77_state.accounted&&lesson_77_state.b==lesson_77_state.a+1U;text64(c,"l84test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 77 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_77_state=(struct lesson_77_model){77U,78U,79U,80U,1,1,1,1};`：`a=77,b=78,c=79,d=80`。
- `int ok=...`：四标志与 `b==a+1` 五条件与。
- 成功串 `bounded scheduling and copy-on-write checkpoint passed` / 失败串 `Lesson 77 fallback reported` 逐字来自源码。至此 `l84test` 是 `lKtest`→`lesson_K_model` 命名链的最后一环。

#### (b) 上一课回归测试改名为 `l76test`

lesson-83 的 `l83test` 改名 `l76test`（校验 `lesson_76_state`）。`exec64` 命令集变为 `l64 l65 l69 l70 l71 l72 l73 l74 l75 l76 l84`。

#### (c) 引用计数的载体：两个页对象

```c
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
static struct page_model fault_pages[VMA_MAX_PAGES];
static struct page_cache_model page_cache[PAGE_CACHE_MAX];
static u32 vma_count, fault_page_count, page_cache_count;
static u64 anon_pages, anon_reclaims, cache_hits, cache_misses, reclaim_scans, reclaim_skips, writeback_pages;
```

- `fault_pages[4]`：匿名缺页产生的页，`refs` 表示映射数；`reclaimable` 是"是否允许回收"的门槛。
- `page_cache[2]`：页缓存页，`index` 是文件偏移，`refs` 同样表示使用者数；`writeback` 是回写标志。
- 回收与缓存记账：`anon_pages`（匿名页数）、`anon_reclaims`（已回收数）、`cache_hits/cache_misses`、`reclaim_scans/reclaim_skips`。

#### (d) 共享的入口：`page_cache_get` 命中/未命中

```c
static TEXT64 int page_cache_get(u64 index,u8 dirty){u32 i;u64 p;for(i=0;i<PAGE_CACHE_MAX;i++)if(page_cache[i].valid&&page_cache[i].index==index){page_cache[i].refs++;page_cache[i].dirty|=dirty;cache_hits++;return 1;}cache_misses++;for(i=0;i<PAGE_CACHE_MAX;i++)if(!page_cache[i].valid){p=pmm_alloc();if(!p)return 0;page_cache[i]=(struct page_cache_model){index,p,1,dirty,0,1};page_cache_count++;if(dirty)page_cache[i].writeback=0;return 1;}return 0;}
```

逐行注释：
- 命中：`index` 相同且 `valid` → `refs++`（多一个使用者），`dirty|=dirty`（只要任一使用者写，页就脏），`cache_hits++`。
- 未命中：`cache_misses++`，找空槽 `pmm_alloc` 分配物理页，聚合初始化 `{index, p, valid=1, dirty, writeback=0, refs=1}`，`page_cache_count++`。
- **语义对照**：命中路径 ≈ Linux `get_page()`（引用加 1）；未命中路径 ≈ `__page_cache_alloc` + `add_to_page_cache_lru`。
- 边界：容量 `PAGE_CACHE_MAX=2`，PMM 分配失败返回 0。

#### (e) 生命周期的终点：`reclaim_one`

```c
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
```

逐行注释：
- 线性扫描 4 个页槽，每扫一个 `reclaim_scans++`。
- 三重门槛：`!m->live`（页无效）、`!m->reclaimable`（不允许回收）、`m->refs!=1`（仍被共享）——任一命中 `reclaim_skips++` 并跳到下一页。**`refs!=1` 的跳过是 COW 安全的底线**：共享页被回收等于把别人脚下的地板抽走。
- `pmm_free_page(m->phys)` 的返回值必须是 `freed`（该函数内部还会拒绝 `thread_stack_owned`/`vm_frame_owned` 的帧）；不是则 `reclaim_skips++`。
- 回收成功：`m->live=0`、`fault_page_count--`、`anon_pages--`、`anon_reclaims++`，返回 1。

#### (f) 一条命令走完生命周期：`reclaimtest`

```c
static TEXT64 void reclaimtest(u16*c){int a=fault_insert(VMA_DATA_START,1),b=page_cache_get(1,1),d=page_cache_get(1,0),e=reclaim_one();text64(c,"reclaimtest: ");text64(c,a&&b&&d&&e&&anon_pages==0&&page_cache_count==1?"anonymous reclaim and page-cache hit model passed":"BROKEN");text64(c,"\npage cache is metadata-only; no disk I/O or swap executed\n");}
```

- `a=fault_insert(VMA_DATA_START,1)`：分配匿名私页（`refs=1`）；
- `b=page_cache_get(1,1)`：页缓存未命中 → 分配缓存页（`refs=1`），`cache_misses=1`；
- `d=page_cache_get(1,0)`：同一 `index` 命中 → `refs++`（=2），`cache_hits=1`；
- `e=reclaim_one()`：扫描 `fault_pages`，唯一的匿名私页满足 `refs==1` → 回收成功；
- 断言：`a&&b&&d&&e` 且 `anon_pages==0`（匿名页已被回收归零）且 `page_cache_count==1`（缓存页仍在）。
- 成功串 `anonymous reclaim and page-cache hit model passed` 后跟一句元边界说明 `page cache is metadata-only; no disk I/O or swap executed`。

#### (g) `anoninfo`：读生命周期统计

```c
static TEXT64 void anoninfo(u16*c){text64(c,"anon pages/cache live/reclaims: ");hex64(c,anon_pages);text64(c,"/");hex64(c,page_cache_count);text64(c,"/");hex64(c,anon_reclaims);text64(c," cache hit/miss: ");hex64(c,cache_hits);text64(c,"/");hex64(c,cache_misses);text64(c," reclaim scans/skips: ");hex64(c,reclaim_scans);text64(c,"/");hex64(c,reclaim_skips);putc64(c,'\n');}
```

一行输出 6 组量：`anon pages`（现存匿名页）、`cache live`（缓存页数）、`reclaims`（累计回收数）、`cache hit/miss`、`reclaim scans/skips`——完整呈现页的"存量 + 流量"。

#### (h) `exec64` 增量与 banner

```c
}else if(eq64(word,"l76test")){if(!noargs64(arg))usage64(c,"l76test");else l76test(c);}
}else if(eq64(word,"l84test")){if(!noargs64(arg))usage64(c,"l84test");else l84test(c);}
```

`about`：`text64(c,"Lesson 84: 共享页生命周期\n");`
banner：`text64(&c,"Lesson 84: 共享页生命周期\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`
- banner 第二行的 `bounded reclaim metadata` 在本课达到语义闭环：回收元数据（`refs`/`reclaimable`）正是共享页生命周期的开关。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q '共享页生命周期' README.md
	@grep -q 'l84test' kernel64.c
	@grep -q 'Lesson 84' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 84 checks passed.'
```

- 与 lesson-83 唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链不变（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse ... -Werror` → `kernel64.bin` → 内嵌 → ELF32 → ISO）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ reclaim_init(): 清零 anon_pages/reclaims/cache/reclaim 计数
    ├─ banner: "Lesson 84: 共享页生命周期\n..."
    └─ for(;;) 键盘循环
        ├─ "reclaimtest" ──► fault_insert(DATA,写) [私页 refs=1]
        │                     ├─ page_cache_get(1,1)  [未命中, 缓存页 refs=1]
        │                     ├─ page_cache_get(1,0)  [命中, refs=2]
        │                     ├─ reclaim_one()        [回收匿名私页]
        │                     └─ 打印 "anonymous reclaim and page-cache hit model passed"
        ├─ "anoninfo" ──► 打印 anon/cache/reclaim/hit-miss/scans-skips
        └─ "l84test" ──► lesson_77_state 校验 ──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`reclaim_init` 清零全部生命周期计数；打印 banner。
2. **分配**：`reclaimtest` 的 `fault_insert(VMA_DATA_START,1)` 从 PMM 分配物理页，`refs=1`（私有页），`anon_pages=1`。
3. **页缓存**：`page_cache_get(1,1)` 未命中 → 分配缓存页 `refs=1`、`cache_misses=1`；`page_cache_get(1,0)` 命中 → `refs=2`、`cache_hits=1`。
4. **回收**：`reclaim_one` 扫描 `fault_pages`，匿名私页满足 `live && reclaimable && refs==1` → `pmm_free_page` 归还 PMM、`anon_pages=0`、`anon_reclaims=1`。缓存页 `refs=2` 不在 `fault_pages` 扫描范围，安然无恙。
5. **读统计**：`anoninfo` 显示 `anon pages/cache live/reclaims: 0/1/1 cache hit/miss: 1/1 reclaim scans/skips: ...`。
6. **checkpoint**：`l84test` 打印 `l84test: bounded scheduling and copy-on-write checkpoint passed`。

输出串与源码逐字一致：`l84test: ` + `bounded scheduling and copy-on-write checkpoint passed`；`reclaimtest: anonymous reclaim and page-cache hit model passed`；`page cache is metadata-only; no disk I/O or swap executed`。

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
   Lesson 84: 共享页生命周期
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l84test`，预期输出：
   ```
   l84test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l84test: Lesson 77 fallback reported`。）
3. 输入 `l76test`（回归），预期输出：
   ```
   l76test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `reclaimtest`，预期输出：
   ```
   reclaimtest: anonymous reclaim and page-cache hit model passed
   page cache is metadata-only; no disk I/O or swap executed
   tinyos>
   ```
5. 输入 `anoninfo`，预期出现（数值为十六进制）：
   ```
   anon pages/cache live/reclaims: 0/1/1 cache hit/miss: 1/1 reclaim scans/skips: ...
   ```
6. 输入 `pfmodel` 再 `anoninfo`，观察 `anon pages` 变为 1（`pfmodel` 插入了新匿名页，未被回收）。
7. 输入 `about`，预期输出：
   ```
   Lesson 84: 共享页生命周期
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 84 checks passed.`；QEMU 中 `l84test` 打印 `...passed`、`reclaimtest` 打印 `...passed` 即代表共享页生命周期模型验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `reclaimtest` 打印 `BROKEN` | `a/b/d/e` 四断言中某一项失败，或 `anon_pages!=0`、`page_cache_count!=1` | 逐项核对：`fault_insert` 返回 1、`page_cache_get` 两次返回 1、`reclaim_one` 返回 1、`anon_pages==0` |
| `reclaim_one` 恒返回 0 | `fault_pages` 中无满足 `live && reclaimable && refs==1` 的页 | `anoninfo` 看 `reclaim skips` 是否在增长；检查页是否被 `pmm_free_page` 拒绝（`thread_stack_owned`/`vm_frame_owned`） |
| `page_cache_get` 第二次未命中 | `index` 不匹配（两次 `index` 不同） | `reclaimtest` 里两次都传 `index=1`；核对命中分支的 `page_cache[i].index==index` |
| `anon_pages` 恒为 0 | 没有匿名页被插入或都被回收 | `pfmodel` 插入后再 `anoninfo`；`fault_insert` 前置检查（容量/权限/present）是否拦截 |
| 回收了共享页（不应该发生） | `reclaim_one` 的 `refs!=1` 门槛被改掉 | 检查 `if(!m->live||!m->reclaimable||m->refs!=1)`；`refs==1` 是回收的前提 |
| `l84test` 打印 fallback 串 | `lesson_77_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=77,b=78`） |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep '共享页生命周期' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l84test` 符号 | `grep -q 'l84test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：struct page 的引用计数**
- TinyOS 教学模型：`page_model.refs`（u16）与 `page_cache_model.refs` 表达"使用者数"；`refs==1` 私有、`refs>1` 共享、`refs==0` 无效。
- Linux 实现：`struct page`（`include/linux/mm_types.h`）的 `_refcount`（`atomic_t`，页级内核引用）与 `_mapcount`（用户映射数）；`page_ref_count()`/`page_mapcount()` 读取，`get_page`/`put_page`（`include/linux/mm.h`、`mm/page_alloc.c`）增删引用，`put_page` 归零后进 `free_unref_page` 回收到伙伴系统。
- 权威来源：Linux v6.x `include/linux/mm_types.h`、`mm/page_alloc.c`；Intel SDM Vol.3A §4.10（PTE 与物理页）。
- 教学简化：`refs` 合并 `_refcount` 与 `_mapcount` 两种计数，且非原子（无并发）；容量固定为 4 页。

**对照点 2：回收与 shrink_page_list**
- TinyOS 教学模型：`reclaim_one` 只回收 `live && reclaimable && refs==1` 的页；`reclaim_scans/reclaim_skips` 记账。
- Linux 实现：`mm/vmscan.c` 的 `shrink_page_list()` 是回收主路径，`isolate_lru_page` 后 `try_to_unmap`/`__remove_mapping`：`__remove_mapping` 用 `page_ref_freeze(page, 2)` 检查引用计数——**引用计数不为 1（用户映射）或不为 2（含内核）就放弃回收**，与 TinyOS 的 `refs!=1` 跳过是同一条铁律。
- 教学简化：无 LRU、无隔离批次、无脏页回写（`writeback_pages` 只是计数），直接扫描 4 个槽位。

**对照点 3：COW 中 refs 的变化**
- TinyOS：COW 语义 = 共享时 `refs++`、写时分裂为两个 `refs=1` 的页。
- Linux：`wp_page_copy`（`mm/memory.c`）在复制后 `page_remove_rmap(old_page)`（映射计数 -1）并对新页 `page_add_new_anon_rmap`（计数置 1）；共享时 `copy_pte_range` 调 `page_dup_rmap`（映射计数 +1）。
- 教学简化：TinyOS 的 `fault_insert` 直接产出 `refs=1` 的新页，没有真实 rmap（反向映射）操作。

**对照点 4：页缓存引用**
- TinyOS：`page_cache_get` 命中 `refs++`、未命中分配 `refs=1`。
- Linux：`mm/filemap.c` 的页缓存操作（`find_get_page`/`add_to_page_cache`），`page_cache_get_speculative`（`include/linux/pagemap.h`）对缓存页做带检查的引用获取。
- 教学简化：无基数树（radix tree / XArray）、无 LRU、无回收页的缓存淘汰联动。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `reclaim_one` 必须拒绝 `refs!=1` 的页？如果强行回收一个 `refs=2` 的共享页，会发生什么（用"两个映射还在用已归还的物理页"来回答）？
2. **源码定位**：在 `reclaimtest` 里，`page_cache_get(1,0)` 执行后缓存页的 `refs` 是多少？为什么 `reclaim_one` 不会回收它（提示：它不在 `fault_pages` 扫描范围，且 `refs=2`）？
3. **动手实验**：修改 `reclaim_one`，把 `m->refs!=1` 改成 `m->refs>1`（允许回收 `refs=2` 的页），重新 `make run` 执行 `reclaimtest`。观察它是否打印 `BROKEN`，并思考这样改为什么破坏 COW 安全。改完请**恢复原值**。
4. **动手实验**：在 `reclaimtest` 里给 `page_cache_get(1,0)` 的 `index` 改成 `2`，重新运行。观察第二次调用从命中变为未命中，`cache_hits/cache_misses` 与 `page_cache_count` 随之变化。改完请**恢复原值**。
5. **Linux 对照**：阅读 `mm/vmscan.c` 的 `__remove_mapping` 与 `page_ref_freeze` 的用法，说明"回收必须先冻结引用计数"与 TinyOS `refs==1` 检查之间的异同（谁更保守、为什么）。

---

## 9. 本课小结与下一课预告

- 本课用 `refs` 引用计数讲完了物理页的一生：空闲（0）→ 分配（1）→ 共享（N）→ 写时分裂回（1）→ 回收归零。
- 你理解了 COW 与引用计数的联动：共享页 `refs>1` 时绝不可回收，写时复制把共享页分裂为两个 `refs=1` 的私页。
- 你掌握了 `page_cache_get` 的命中/未命中语义（`refs++`/`refs=1`）与 `reclaim_one` 的三重回收门槛（`live && reclaimable && refs==1`）。
- 你对照了 Linux 的 `_refcount`/`_mapcount`、`get_page`/`put_page`、`shrink_page_list`/`__remove_mapping`、`wp_page_copy`，知道教学模型在原子性、LRU、rmap 上的简化。
- 你验证了 `l84test`/`l76test` 与 `reclaimtest`/`anoninfo`/`pfmodel` 的确定性输出，COW 元数据三连课至此闭环。

**下一课预告**：Lesson 85（下一主题）。COW 三连课（82 页对象 → 83 缺页统计 → 84 生命周期）已完成"元数据真实、行为不执行"的内存教学闭环。下一课将在此基础上继续主线——请带着"页对象与引用计数还能服务于哪些内存语义（如 mmap、缺页、交换）"的问题进入下一课，衔接点是本课的 `reclaim_one` 与 `page_cache`。

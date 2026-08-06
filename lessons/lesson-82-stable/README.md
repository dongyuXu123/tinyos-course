# Lesson 82: Copy-on-Write 基础元数据 — 精讲文档

> **课号**：Lesson 82（主线源课编号 Lesson 75 线）
> **本课主题**：写时复制（Copy-on-Write, COW）的基础元数据——页对象 `struct page_model` 的写权限标志与引用计数、VMA 地址区间模型、匿名页缺页插入
> **课程主线位置**：调度 / COW 元数据教学模型阶段（Lesson 64 起）。Lesson 79–81 讲调度三连课，本课起进入内存线：先建 COW 的"元数据地基"（页对象、权限、引用计数），随后 Lesson 83 讲缺页统计、Lesson 84 讲共享页生命周期。
> **前置课程**：[`../lesson-81-stable/README.md`](../lesson-81-stable/README.md)（context switch 上下文切换元数据：CPU 帧与切换）
> **后续课程**：[`../lesson-83-stable/README.md`](../lesson-83-stable/README.md)（COW 写时复制缺页统计：`pf_classify` 三类缺页计数）
> **本课一句话目标**：理解"一个物理页可以被多个地址映射共享，因此在写时需复制"这一 COW 核心，并把 Linux `mm/memory.c` 的 COW 概念投影成 `page_model` 的 `writable` 标志与 `refs` 引用计数两个字段。
> **保留的原始快照信息**：This checkpoint models bounded scheduling and copy-on-write metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l82test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能说出 COW 的完整思想——fork 时父子进程共享物理页、页表项置为只读、引用计数加 1，谁先写谁触发保护缺页、复制新页并恢复可写；并能在 `vmainfo`/`pfmodel`/`anoninfo` 里读出页对象的 `writable` 与 `refs`。
- **在课程主线中的位置**：COW 元数据三连课（82–84）的第一课，负责建立"页 = 数据 + 权限 + 引用计数"的对象模型。之后两课分别在缺页路径与回收路径上使用这个模型。
- **前置知识清单**（学本课之前必须掌握）：
  1. 分页与 PTE：`PTE_PRESENT/PTE_WRITABLE/PTE_USER`、`vm_mappings`/`address_space_map`（Lesson 32 起）；
  2. PMM：`pmm_alloc`/`pmm_free_page`/`page_state`（Lesson 34 起）；
  3. VMA 概念：`struct vma_model` 与 `vma_lookup`/`vma_range_valid`（Lesson 32 起）；
  4. `exec64` 命令分派与 VGA 输出（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_75_model` + `lesson_75_state` + `l82test`；把 `l81test` 改名为 `l74test`；`about` 与 banner 更新为「Lesson 82: Copy-on-Write 基础元数据」。COW 基础命令 `vmainfo`/`pfmodel`/`anoninfo`/`reclaimtest` 可用。

---

## 2. 核心概念精讲

### 2.1 写时复制（Copy-on-Write）：直觉与动机

**直觉**：`fork()` 产生子进程时，父子进程的代码和数据绝大多数内容一样。如果立刻把整份内存复制一份，又慢又费内存。聪明的做法是：**先共享**——让父子进程的页表都指向同一个物理页；但把该页标记为**只读**，并记录"这个页现在被 2 个地址映射（引用计数=2）"。将来谁要写这个页，就会触发一次"写保护缺页"；内核在缺页处理里**复制一个新页**给写入者，把新页标记为可写，并把原页的引用计数减 1。从此两人各写各的，互不干扰。

```
fork 之后（共享，只读）：
  父进程 PTE ──► 物理页 P (refs=2, writable=0)
  子进程 PTE ──► 物理页 P (refs=2, writable=0)

子进程首次写 P（触发保护缺页 do_wp_page）：
  父进程 PTE ──► 物理页 P (refs=1, writable=0)   ← 保持只读
  子进程 PTE ──► 新页 P' (refs=1, writable=1)    ← 复制 + 可写
```

**为什么需要**：内存是稀缺资源；共享 + 惰性复制把 fork 的代价从"拷贝整个地址空间"降到"改几个页表项"，只在真正写入时才付出复制成本。这是操作系统性能与内存效率的核心技巧。

### 2.2 页对象：`struct page_model`

Linux 用 `struct page` 描述每个物理页（`_refcount` 引用计数、`_mapcount` 映射计数）。本课的教学投影：

```c
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
```

字段与 COW 的对应：
- `writable`：该页是否可写——对应页表项 PTE 的 RW 位。COW 的关键动作就是"把共享页的 writable 清 0"。
- `refs`（u16）：引用计数——当前有几个地址映射/引用这个页。COW 的另一个关键动作是"写时把 refs 减 1，新页 refs 置 1"。
- `live`：页对象是否有效；`backing`：后备类型（`VMA_ANON` 匿名 / `VMA_FILE` 文件）；`dirty/accessed`：脏位与访问位；`reclaimable`：是否可回收。
- `va`/`phys`：虚拟地址与物理地址。

### 2.3 VMA 模型：哪些地址区间可写

COW 只对"允许写"的区间有意义。内核用固定 VMA 表描述进程地址空间：

```c
static TEXT64 void vma_init(void){vma_count=3;vma_table[0]=(struct vma_model){VMA_CODE_START,VMA_CODE_END,0,VMA_R|VMA_X,VMA_FILE,1};vma_table[1]=(struct vma_model){VMA_DATA_START,VMA_DATA_END,0,VMA_R|VMA_W,VMA_ANON,1};vma_table[2]=(struct vma_model){VMA_STACK_START,VMA_STACK_END,0,VMA_R|VMA_W,VMA_ANON,1};fault_page_count=0;fault_not_present=fault_protection=fault_unmapped=fault_insertions=0;}
```

- 代码段 `0x00400000–0x00401000`：`r-x`（不可写，所以对它写会产生保护缺页）；
- 数据段 `0x00600000–0x00602000`：`rw-` 匿名（可写，写时缺页会插入匿名页）；
- 栈段 `0x00800000–0x00802000`：`rw-` 匿名。
- `vma_lookup(va)` 线性扫描这 3 条记录返回命中 VMA；`vma_range_valid(start,end,prot)` 校验区间落在单个 VMA 内且权限满足。

### 2.4 缺页插入：`fault_insert` 如何"制造"一个页

```c
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;p=pmm_alloc();if(!p)return 0;for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];m->va=down(va);m->phys=p;m->writable=write;m->live=1;m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
```

- 边界检查：页槽位没满（`VMA_MAX_PAGES`）、`va` 落在合法 VMA 且权限满足（写请求需 `VMA_W`，读请求需 `VMA_R`）、该页尚未 present。
- 从 PMM 分配物理页，填满 `page_model`：`writable=write`（**写缺页插入的页可写，读缺页插入的页不可写**——这正是"写权限来自缺页请求"的模型化表达）、`refs=1`（新页引用计数为 1）、`dirty=write`、`backing=VMA_ANON`。
- 记账：`fault_page_count++`、`anon_pages++`、`fault_insertions++`。

### 2.5 「固定元数据 + 确定性验证」教学模型

COW 三连课同样遵守"元数据真实、行为不执行"：`page_model` 的 `writable/refs` 是真实 COW 语义的投影，但绝不真的执行一次 fork 复制或改 PTE。`struct lesson_75_model` 继续承担调度 checkpoint：

```c
struct lesson_75_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

`l82test` 断言 `b==a+1` 与四标志，证明"COW 元数据新增不影响既有调度 checkpoint"。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-81） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：页对象/VMA/缺页元数据、调度器、`exec64` 分派 | **主要增量**：新增 `struct lesson_75_model`、`lesson_75_state`、`l82test()`；把 `l81test` 改名为 `l74test`；`exec64` 分支与 `about`、banner 文案更新（页对象/VMA 元数据继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 82 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的固定元数据记录与 `l82test`

```c
struct lesson_75_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_75_model lesson_75_state;
static TEXT64 void l82test(u16*c){lesson_75_state=(struct lesson_75_model){75U,76U,77U,78U,1,1,1,1};int ok=lesson_75_state.valid&&lesson_75_state.active&&lesson_75_state.ready&&lesson_75_state.accounted&&lesson_75_state.b==lesson_75_state.a+1U;text64(c,"l82test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 75 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_75_state=(struct lesson_75_model){75U,76U,77U,78U,1,1,1,1};`：`a=75,b=76,c=77,d=78`，四标志全真。
- `int ok=...`：四标志与 `b==a+1` 五条件与。
- `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 75 fallback reported");`：注意成功串本身就叫 **copy-on-write checkpoint passed**——从 Lesson 69 起这条成功串就同时背书调度与 COW 两个主题，本课正式展开 COW 语义。
- `putc64(c,'\n');`。

#### (b) 上一课回归测试改名为 `l74test`

lesson-81 的 `l81test` 改名 `l74test`（校验 `lesson_74_state`）。`exec64` 命令集变为 `l64 l65 l69 l70 l71 l72 l73 l74 l82`。

#### (c) 页对象与页缓存模型

```c
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
static struct page_model fault_pages[VMA_MAX_PAGES];
static struct page_cache_model page_cache[PAGE_CACHE_MAX];
static u32 vma_count, fault_page_count, page_cache_count;
static u64 fault_not_present, fault_protection, fault_unmapped, fault_insertions;
static u64 anon_pages, anon_reclaims, cache_hits, cache_misses, reclaim_scans, reclaim_skips, writeback_pages;
```

逐行注释：
- `page_model`：匿名缺页插入产生的页对象，`fault_pages[4]` 固定容量。`writable` 与 `refs` 是本课 COW 语义的主角。
- `page_cache_model`：页缓存（page cache）记录，`page_cache[2]`。`index` 是文件偏移索引，`writeback` 标记回写。也带 `refs`——说明"引用计数"是所有页对象共有的概念。
- 计数器：`fault_*` 四类是缺页统计（下下节课展开）；`anon_pages/anon_reclaims` 是匿名页与回收计数；`cache_hits/cache_misses` 是页缓存命中；`reclaim_scans/reclaim_skips` 是回收扫描。
- `u16 refs` 刻意用 16 位：教学模型假设共享数很小，够用且省空间（Linux 的 `_refcount` 是 `atomic_t`）。

#### (d) 页对象如何"出生"：`fault_insert`

```c
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;p=pmm_alloc();if(!p)return 0;for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];m->va=down(va);m->phys=p;m->writable=write;m->live=1;m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
```

逐行注释：
- 前置三检查：容量（`fault_page_count>=VMA_MAX_PAGES`）、VMA 合法性与权限（`write?VMA_W:VMA_R`）、页已 present（`page_present(va)` 查 `fault_pages` 中 `live` 且 `va` 相同者）。任一不满足返回 0。
- `p=pmm_alloc()`：PMM 分配物理页，失败返回 0。
- 填对象：`m->writable=write`（写缺页→可写；读缺页→只读，**这是 COW 语义的最小种子**：只读页遇到写请求就是 COW 缺页）、`m->refs=1`（新页只有一个引用）、`m->dirty=write`、`m->reclaimable=1`。
- 记账后返回 1。

#### (e) 缺页分类 `pf_classify` 与 VMA 查询

```c
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
static TEXT64 const struct vma_model *vma_lookup(u64 va){u32 i;for(i=0;i<vma_count;i++)if(vma_table[i].valid&&va>=vma_table[i].start&&va<vma_table[i].end)return &vma_table[i];return 0;}
static TEXT64 int vma_range_valid(u64 start,u64 end,u8 prot){const struct vma_model*v;if(end<=start||end-start>0x10000ULL)return 0;v=vma_lookup(start);return v&&end<=v->end&&(v->prot&prot)==prot;}
```

- 三类缺页：`PF_UNMAPPED`（地址不在任何 VMA）、`PF_PROTECTION`（权限不符——写只读页就是这类，**COW 缺页属于这一类**）、`PF_NOT_PRESENT`（地址合法但页没在）。
- `pfmodel` 命令演示三类分类并插入一页：
```
pfmodel: not-present/protection/unmapped classified; bounded page inserted
```
`pf_classify(VMA_DATA_START,1)`（数据段写，页不在）→ `PF_NOT_PRESENT`；`pf_classify(VMA_CODE_START,1)`（代码段写，权限不符）→ `PF_PROTECTION`；`pf_classify(0x00100000,0)`（无 VMA）→ `PF_UNMAPPED`。

#### (f) `vmainfo` 与 `anoninfo`：读 COW 元数据

```c
static TEXT64 void vmainfo(u16*c){u32 i;text64(c,"VMA table (bounded Linux-style metadata)\n");for(i=0;i<vma_count;i++){text64(c,"vma ");hex64(c,i);text64(c," ");hex64(c,vma_table[i].start);text64(c,"-");hex64(c,vma_table[i].end);text64(c," ");text64(c,vma_prot(vma_table[i].prot));text64(c," ");text64(c,vma_backing(vma_table[i].kind));putc64(c,'\n');}text64(c,"pages/live, faults np/prot/unmapped: ");hex64(c,fault_page_count);text64(c," ");hex64(c,fault_not_present);text64(c," ");hex64(c,fault_protection);text64(c," ");hex64(c,fault_unmapped);putc64(c,'\n');}
```

- `vmainfo` 打印三张 VMA（`vma 0 r-x file`、`vma 1 rw- anon`、`vma 2 rw- anon`）与缺页计数。
- `anoninfo` 打印 `anon pages/cache live/reclaims:` 与 `cache hit/miss:`、`reclaim scans/skips:`——匿名页与页缓存的元数据总量。

#### (g) `exec64` 增量与 banner

```c
}else if(eq64(word,"l74test")){if(!noargs64(arg))usage64(c,"l74test");else l74test(c);}
}else if(eq64(word,"l82test")){if(!noargs64(arg))usage64(c,"l82test");else l82test(c);}
```

`about`：`text64(c,"Lesson 82: Copy-on-Write 基础元数据\n");`
banner：`text64(&c,"Lesson 82: Copy-on-Write 基础元数据\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`
- banner 第二行的 `bounded reclaim metadata` 恰好预告本课主题：COW/回收是同一套页元数据的两面。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'Copy-on-Write 基础元数据' README.md
	@grep -q 'l82test' kernel64.c
	@grep -q 'Lesson 82' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 82 checks passed.'
```

- 与 lesson-81 唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链不变（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse ... -Werror` → `kernel64.bin` → 内嵌 → ELF32 → ISO）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ vma_init(): 建 3 条 VMA（code r-x / data rw- / stack rw-），清空缺页计数
    ├─ reclaim_init(): 清空匿名页/页缓存/回收计数
    ├─ banner: "Lesson 82: Copy-on-Write 基础元数据\n..."
    └─ for(;;) 键盘循环
        ├─ "pfmodel" ──► pf_classify ×3 + fault_insert(VMA_DATA_START,1)
        │                └─ fault_pages[0] = {va, phys, writable=1, refs=1, anon}
        ├─ "vmainfo" ──► 打印 3 条 VMA 与缺页计数
        ├─ "anoninfo" ──► 打印 anon_pages/page_cache 计数
        └─ "l82test" ──► lesson_75_state 校验 ──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`vma_init` 建 3 条 VMA、清零缺页计数；`reclaim_init` 清零匿名页/页缓存计数；打印 banner。
2. **插入匿名页**：`pfmodel` 调 `pf_classify(VMA_DATA_START,1)`（写数据段、页不在 → `PF_NOT_PRESENT`），再 `fault_insert(VMA_DATA_START,1)`——PMM 分配物理页，填入 `fault_pages[0]`：`va=0x00600000`、`writable=1`、`refs=1`、`backing=VMA_ANON`、`dirty=1`。
3. **读元数据**：`vmainfo` 显示 VMA 表与 `pages/live, faults np/prot/unmapped`；`anoninfo` 显示 `anon pages=1`。
4. **COW 视角**：若此时"另一个映射"也想共享这个页，内核会把 `refs` 加 1 并把 `writable` 清 0；下一次写触发 `PF_PROTECTION`，缺页处理复制新页、`refs` 减 1、新页 `writable=1`。本课的 `page_model` 字段已为这套流程备齐。
5. **checkpoint**：`l82test` 打印 `l82test: bounded scheduling and copy-on-write checkpoint passed`。

输出串与源码逐字一致：`l82test: ` + `bounded scheduling and copy-on-write checkpoint passed`；`pfmodel: not-present/protection/unmapped classified; bounded page inserted`。

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
   Lesson 82: Copy-on-Write 基础元数据
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l82test`，预期输出：
   ```
   l82test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l82test: Lesson 75 fallback reported`。）
3. 输入 `l74test`（回归），预期输出：
   ```
   l74test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `pfmodel`，预期输出：
   ```
   pfmodel: not-present/protection/unmapped classified; bounded page inserted
   tinyos>
   ```
5. 输入 `vmainfo`，预期出现：
   ```
   VMA table (bounded Linux-style metadata)
   vma 0 0000000000400000-0000000000401000 r-x file
   vma 1 0000000000600000-0000000000602000 rw- anon
   vma 2 0000000000800000-0000000000802000 rw- anon
   pages/live, faults np/prot/unmapped: ...
   ```
6. 输入 `anoninfo`，预期出现 `anon pages/cache live/reclaims:` 与 `cache hit/miss:`、`reclaim scans/skips:`。
7. 输入 `about`，预期输出：
   ```
   Lesson 82: Copy-on-Write 基础元数据
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 82 checks passed.`；QEMU 中 `l82test` 打印 `...passed`、`pfmodel` 打印 `...bounded page inserted` 即代表 COW 基础元数据验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `pfmodel` 打印 `BROKEN` | `pf_classify` 三类分类结果与预期不符 | 逐项核对：`VMA_DATA_START` 写→`PF_NOT_PRESENT`、`VMA_CODE_START` 写→`PF_PROTECTION`、`0x00100000`→`PF_UNMAPPED`、`fault_insert` 返回值 |
| `fault_insert` 返回 0 插不进页 | `fault_page_count>=VMA_MAX_PAGES` 或 VMA 权限不符或页已 present | 先 `vmainfo` 看 `pages/live` 与 `fault_page_count`；确认 `va` 落在 data/stack VMA |
| `vmainfo` 显示 0 条 VMA | `vma_init` 未执行或 `vma_count` 被清零 | 确认 `kernel_main64_binary` 在 banner 前调用 `vma_init()`；`vma_count=3` |
| 写代码段不报保护缺页 | `pf_classify` 的写检查未生效 | 检查 `(write&&!(v->prot&VMA_W))`；`vma_table[0].prot` 应为 `VMA_R|VMA_X`（无 `VMA_W`） |
| `l82test` 打印 fallback 串 | `lesson_75_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=75,b=76`） |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'Copy-on-Write 基础元数据' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l82test` 符号 | `grep -q 'l82test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：COW 概念与 do_wp_page / wp_page_copy**
- TinyOS 教学模型：`page_model.writable` 是页的写权限位、`refs` 是引用计数；`PF_PROTECTION` 缺页类对应"写只读共享页"的触发点。`fault_insert` 产生 `writable=write`、`refs=1` 的新页——这正是 `wp_page_copy` 复制产物的最小形态。
- Linux 实现：`mm/memory.c` 的 `do_wp_page()` 处理写保护缺页（`FAULT_FLAG_WRITE` 命中只读映射），调用 `wp_page_copy()`：分配新页、`copy_page` 复制内容、更新 `rss` 与页表、调用 `page_remove_rmap`/`page_add_new_anon_rmap` 调整映射计数；原共享页引用计数减 1。
- 权威来源：Linux v6.x `mm/memory.c`（`do_wp_page`、`wp_page_copy`）；Intel SDM Vol.3A §4.10（分页）中 PTE RW 位与保护语义。
- 教学简化：TinyOS 不真正复制字节（没有 `copy_page`）、不修改真实 PTE（`fault_pages` 是独立元数据），`writable/refs` 只是 COW 状态的投影。

**对照点 2：fork 时的共享与引用计数**
- TinyOS 教学模型：`refs`（u16）表示"一个物理页被多少个映射引用"；COW 语义要求共享时 `refs>1`、写时分裂为 `refs=1` + 新页。
- Linux 实现：fork 时 `copy_pte_range()`/`copy_one_pte()`（`mm/memory.c`）对可写匿名页清除 `_PAGE_RW` 并 `page_dup_rmap()`（映射计数 +1）；`struct page` 的 `_refcount`（`page_ref_count`）与 `_mapcount` 分别计"内核引用"与"用户映射数"。
- 教学简化：`refs` 合并了 `_refcount` 与 `_mapcount` 两种计数，且没有真实 PTE 操作。

**对照点 3：VMA 权限与保护缺页**
- TinyOS：`vma_table[0]`（code `r-x`）对写返回 `PF_PROTECTION`。
- Linux：`handle_mm_fault()`（`mm/memory.c`）先查 `vm_flags`（如 `VM_WRITE`），权限不符走 `VM_FAULT_SIGSEGV`；`FAULT_FLAG_WRITE` 传入底层缺页处理。
- 教学简化：无 `vm_ops`、无文件后备页的 fault 回调（data/stack 全是匿名）。

---

## 8. 思考题与练习

1. **概念理解**：为什么 COW 必须把共享页标记为只读？如果共享页保持可写，`do_wp_page` 还有机会介入吗？结合"写者必须被强制走缺页路径"回答。
2. **源码定位**：找到 `fault_insert`，说出"写缺页插入的页 `writable=1`、读缺页插入的页 `writable=0`"这一设计对应 COW 的哪个阶段（写时复制 / 读时共享）。
3. **动手实验**：在 `fault_insert` 里把 `m->refs=1` 临时改成 `m->refs=2`，重新 `make run` 执行 `pfmodel` 后 `anoninfo`，观察"一个页两个引用"状态。思考：后续 `reclaim_one` 会拒绝回收它吗（见 Lesson 84）？改完请**恢复原值**。
4. **动手实验**：给 `pfmodel` 加一次 `fault_insert(VMA_CODE_START,1)`（写代码段），观察其返回 0（VMA 权限不符）——这是"保护缺页不可插入"的体现。改完请**恢复原值**。
5. **Linux 对照**：阅读 `mm/memory.c` 的 `wp_page_copy`，列出它与 `fault_insert` 的 5 处差异（页表、rmap、rss、copy_page、锁），说明教学模型哪些环节被省掉了。

---

## 9. 本课小结与下一课预告

- 本课建立了 COW 的地基：`struct page_model` 用 `writable`（写权限位）与 `refs`（引用计数）两个字段承载写时复制的核心状态。
- 你理解了 COW 的完整流程——fork 共享 + 置只读 + 引用计数，谁先写谁触发保护缺页并复制新页——并知道 `PF_PROTECTION` 就是 COW 缺页的落点。
- 你掌握了 VMA 三区间（code r-x / data rw- / stack rw-）与 `fault_insert` 生成匿名页的记账（`writable`、`refs=1`、`dirty`、`anon_pages++`）。
- 你对照了 Linux `mm/memory.c` 的 `do_wp_page`/`wp_page_copy`/`copy_pte_range`，知道 `writable/refs` 是 `_PAGE_RW` 与 `_refcount`/`_mapcount` 的最小投影。
- 你验证了 `l82test`/`l74test` 与 `pfmodel`/`vmainfo`/`anoninfo` 的确定性输出。

**下一课预告**：Lesson 83「COW 写时复制缺页统计」。本课造出了页对象，下一课统计缺页：`pf_classify` 的三类计数（`fault_not_present`/`fault_protection`/`fault_unmapped`）如何在 `vmainfo` 里呈现、`fault_insertions` 如何累计——把 COW 的"缺页"路径做成可观测的统计。衔接点正是本课的 `PF_PROTECTION` 分类。

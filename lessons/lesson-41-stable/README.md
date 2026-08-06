# Lesson 41: Linux 风格固定 VMA、范围校验与有界 demand page-fault 分类 — 精讲文档

> **课号**：Lesson 41（可执行课）
> **主题**：Linux 风格固定 VMA、范围校验与有界 demand page-fault 分类
> **课程主线位置**：第 5 阶段「Linux 风格内核抽象」第五课。前课完成 exec 镜像校验
> （40）；本课把「进程地址空间里有什么」变成显式元数据——固定 4 槽 VMA 表
> （code/data/stack 三段，带 R/W/X 权限与 file/anon 属性），并按
> `pf_classify` 把模拟访问分类为 not-present / protection / unmapped。
> **前置课程**：[`lesson-40-stable/README.md`](../lesson-40-stable/README.md)
> **后续课程**：[`lesson-42-stable/README.md`](../lesson-42-stable/README.md)
> **一句话目标**：能讲清楚 Linux 的 VMA（虚拟内存区）是什么、`find_vma` 怎么查、
> 缺页怎么分类（not-present / protection / unmapped）、缺页时怎么插页，并在
> TinyOS 里用固定表 + 有界 PMM 记账复刻这个流程——**教学测试绝不触发真实缺页**。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课你能解释——Linux 为什么把地址空间切成一段段 VMA、
每段带 `vm_start/vm_end/vm_flags` 与 `vm_file`；为什么 `#PF` 之后内核先
`find_vma` 再按「有 VMA 但页不在 / 有 VMA 但权限不够 / 根本不在任何 VMA」三种
情况处理；TinyOS 用 `vma_table[4]`、`vma_lookup`、`pf_classify`、
`fault_insert` 给出同样流程的元数据版。

- **在课程主线中的位置**：衔接 lesson-40 的「地址范围内有 code/data/stack 三段」
  （ELF 校验已约束 `[USER_CODE_VA, USER_STACK_VA]`），为 lesson-42 的
  user-pointer 校验提供「VMA 权限」判据。本课**不修改真实 #PF 处理**：
  `pftest`/`isttest` 与真实异常处理仍保持致命行为。
- **前置知识清单**：
  1. lesson-34 的真实 #PF：`exception_pf` 走 IST1、`cr2` 保存故障地址、处理后
     停机（`exception_report_ist`）；
  2. lesson-40 的地址常量：`USER_CODE_VA=0x400000`、`USER_STACK_VA=0x800000`；
  3. 分页知识：`down()` 页对齐、`PTE_PRESENT_WRITABLE`、`invlpg`；
  4. Linux 常识：`struct vm_area_struct` 的 `vm_start/vm_end/vm_flags/vm_file`，
     `find_vma()` 二叉树/红黑树查找。
- **本课交付**：`vmainfo`/`vmatest`/`pfmodel` 三条命令；`vma_table[4]` 固定表
  （3 段真实 + 1 槽余量）；`vma_lookup`/`vma_range_valid`/`pf_classify`/
  `fault_insert`；三个缺页分类计数器与一个页插入计数器。

---

## 2. 核心概念精讲

### 2.1 概念一：VMA —— 地址空间是一段段「有属性的区间」

Linux 的 `mm_struct` 里没有「整个地址空间一个权限」的概念，而是把它切成很多段
VMA（`struct vm_area_struct`），每段记录：

| 字段（Linux） | 含义 | 本课字段 |
|---|---|---|
| `vm_start` / `vm_end` | 区间 [start, end) | `start` / `end` |
| `vm_flags`（VM_READ/WRITE/EXEC） | 权限 | `prot`（`VMA_R/W/X`） |
| `vm_file` / 匿名 | 文件映射还是匿名 | `kind`（`VMA_FILE`/`VMA_ANON`） |

TinyOS 固定三张 VMA（与 lesson-40 的 ELF 段一一对应）：

```text
VMA 0: 0x00400000-0x00401000  r-x  file   （代码段）
VMA 1: 0x00600000-0x00602000  rw-  anon   （数据段）
VMA 2: 0x00800000-0x00802000  rw-  anon   （栈段）
```

### 2.2 概念二：`vma_lookup` —— 一次线性查找

Linux 用红黑树（老内核）/区间树找「包含某地址的 VMA」；教学模型只有 3 段，
直接线性扫描 `vma_table`，命中条件 `va>=start && va<end`。查询结果决定缺页分类。

### 2.3 概念三：缺页三种分类（`pf_classify`）

真实 `#PF` 到达后，内核先 `find_vma`，再区分：

| 情况 | 含义 | 本课 `pf_classify` 返回 |
|---|---|---|
| 地址不在任何 VMA | 非法访问（segfault） | `PF_UNMAPPED` |
| 在 VMA 内但权限不够（写只读页 / 读无读权页） | 保护违规 | `PF_PROTECTION` |
| 在 VMA 内、权限够、但页表项不存在 | 正常缺页，应插页 | `PF_NOT_PRESENT` |

注意本课 `pf_classify` 是**纯计算**（不执行任何会触发 `#PF` 的指令），
分类结果由 `pfmodel` 汇总显示。

### 2.4 概念四：有界插页（`fault_insert`）

缺页处理后 Linux 会 `do_anonymous_page`/`alloc_page_vma` 分配一页并建页表项。
教学模型用 `fault_pages[]`（最多 `VMA_MAX_PAGES=4` 条）记录「哪一页被插入」，
并真正调用 `pmm_alloc()` 占一页物理内存做记账。边界：最多插 4 页、
每页必须通过 `vma_range_valid` 且页对齐、重复地址不重复插。

### 2.5 概念五：与真实 #PF 的关系

本课教学模型**不触碰**真实异常路径：`pftest`/`isttest`/`stackguardtest` 依然
触发真实 `#PF` 并走 IST1 停机——那是 lesson-27/28 的既有行为。`pfmodel` 是
「在数据层面模拟一次缺页」，注释明确：`no real fault instruction executed`。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-40） |
|---|---|---|
| `boot.S` / `kernel.c` | 引导 | 未变化 |
| `kernel64.c` | 64 位内核主体 | **核心**：VMA 表 + pf 分类 + 插页 + 3 命令 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | **check 目标新增 grep**：README 必须含 `mm/mmap.c`、`mm/memory.c`、`include/linux/mm.h` |
| `grub.cfg` | 装载 | 未变化 |

### 3.2 常量 / 结构 / 全局变量精讲

```c
/* Bounded analogue of Linux mm_struct->mmap/VMA lookup. Records describe
 * ranges only; the teaching model never authorizes a CPU memory access. */
#define VMA_MAX 4U
#define VMA_R 1U
#define VMA_W 2U
#define VMA_X 4U
#define VMA_CODE_START 0x00400000ULL
#define VMA_CODE_END 0x00401000ULL
#define VMA_DATA_START 0x00600000ULL
#define VMA_DATA_END 0x00602000ULL
#define VMA_STACK_START 0x00800000ULL
#define VMA_STACK_END 0x00802000ULL
#define VMA_FILE 1U
#define VMA_ANON 2U
#define VMA_MAX_PAGES 4U
struct vma_model { u64 start,end,backing; u8 prot,kind,valid; };
struct page_model { u64 va,phys; u8 writable,live; };
static struct vma_model vma_table[VMA_MAX];
static struct page_model fault_pages[VMA_MAX_PAGES];
static u32 vma_count, fault_page_count;
static u64 fault_not_present, fault_protection, fault_unmapped, fault_insertions;
enum pf_class { PF_NOT_PRESENT=1, PF_PROTECTION=2, PF_UNMAPPED=3 };
```

逐行注释：
- 头注释是纪律：VMA 记录**只描述范围**，教学模型从不授权一次真实 CPU 访存；
- `VMA_R/W/X` 用 1/2/4 位值，`vma_prot` 打印 `r-x`/`rw-`/`---`；
- 三段地址与 lesson-40 的 ELF 段/用户栈一致（code 0x400000、stack 0x800000），
  数据段 0x600000 是新增的中间区间；
- `vma_model.backing` 字段在本课未用（保留给将来），`kind` 才是 file/anon；
- `page_model` 记录「已插入页」的 `va`（页对齐）、`phys`、`writable`、`live`；
- `fault_*` 四个只增计数器；`pf_class` 枚举给三分类命名。

### 3.3 函数精讲：vma 查询与范围校验

**`vma_lookup` / `vma_range_valid` / `vma_prot` / `vma_backing`**

```c
static TEXT64 const char *vma_prot(u8 p){return p==(VMA_R|VMA_X)?"r-x":p==(VMA_R|VMA_W)?"rw-":"---";}
static TEXT64 const char *vma_backing(u8 k){return k==VMA_FILE?"file":"anon";}
static TEXT64 const struct vma_model *vma_lookup(u64 va){u32 i;
    for(i=0;i<vma_count;i++)if(vma_table[i].valid&&va>=vma_table[i].start&&va<vma_table[i].end)
        return &vma_table[i];return 0;}
static TEXT64 int vma_range_valid(u64 start,u64 end,u8 prot){const struct vma_model*v;
    if(end<=start||end-start>0x10000ULL)return 0;
    v=vma_lookup(start);return v&&end<=v->end&&(v->prot&prot)==prot;}
```

- `vma_lookup`：线性扫 `vma_count` 个有效槽，找包含 `va` 的段，找不到返回 NULL
  （对应 Linux `find_vma` 未命中 → `PF_UNMAPPED`）；
- `vma_range_valid(start,end,prot)`：① 区间非法（`end<=start`）或超过 64 KiB
  （`0x10000`，有界）；② 起点必须在某 VMA 内；③ 终点不越过该 VMA；④ 请求的
  权限位 `prot` 必须被 VMA 的 `prot` 覆盖——这是 lesson-42 `uaccess_validate`
  的直接判据；
- `vma_prot`/`vma_backing`：打印辅助，未知值显示 `---`/`anon`。

### 3.4 函数精讲：缺页分类与插页

**`page_present` / `pf_classify` / `fault_insert`**

```c
static TEXT64 int page_present(u64 va){u32 i;va=down(va);
    for(i=0;i<fault_page_count;i++)if(fault_pages[i].live&&fault_pages[i].va==va)return 1;return 0;}
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);
    if(!v){fault_unmapped++;return PF_UNMAPPED;}
    if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}
    if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}
    return PF_NOT_PRESENT;}
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;
    if(fault_page_count>=VMA_MAX_PAGES||
       !vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;
    p=pmm_alloc();if(!p)return 0;
    for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){
        fault_pages[i].va=down(va);fault_pages[i].phys=p;
        fault_pages[i].writable=write;fault_pages[i].live=1;
        fault_page_count++;fault_insertions++;return 1;}
    return 0;}
```

分析（每个 ≥3 行）：
- `page_present`：把 `va` 页对齐（`down`）后查已插入页表——对应「页表项是否
  present」的判定；
- `pf_classify`：决策顺序与 Linux `handle_mm_fault` 一致——先查 VMA（无 → unmapped），
  再查权限（读要求 R、写要求 W；违反 → protection），最后查页是否存在
  （缺 → not-present）。**三个计数器分别对应三种失败路径**，`pfmodel` 验证
  `pf_classify(VMA_DATA_START,1)==PF_NOT_PRESENT`（数据段写一页，未插）等三个断言；
- `fault_insert`：前置三条件——页数未满 4、整页区间通过
  `vma_range_valid`（页对齐、在 VMA 内、权限覆盖）、该页尚未插入；然后真正
  `pmm_alloc()` 占一页物理内存（记账 `pmm_free--`），写入 `fault_pages` 并计数。
  它不写页表、不改 PTE——`fault_pages` 只是**元数据记账**。

### 3.5 函数精讲：vmainfo / pfmodel / vmatest 与 vma_init

```c
static TEXT64 void vma_init(void){vma_count=3;
    vma_table[0]=(struct vma_model){VMA_CODE_START,VMA_CODE_END,0,VMA_R|VMA_X,VMA_FILE,1};
    vma_table[1]=(struct vma_model){VMA_DATA_START,VMA_DATA_END,0,VMA_R|VMA_W,VMA_ANON,1};
    vma_table[2]=(struct vma_model){VMA_STACK_START,VMA_STACK_END,0,VMA_R|VMA_W,VMA_ANON,1};
    fault_page_count=0;fault_not_present=fault_protection=fault_unmapped=fault_insertions=0;}
static TEXT64 void vmainfo(u16*c){u32 i;
    text64(c,"VMA table (bounded Linux-style metadata)\n");
    for(i=0;i<vma_count;i++){
        text64(c,"vma ");hex64(c,i);text64(c," ");hex64(c,vma_table[i].start);
        text64(c,"-");hex64(c,vma_table[i].end);text64(c," ");
        text64(c,vma_prot(vma_table[i].prot));text64(c," ");
        text64(c,vma_backing(vma_table[i].kind));putc64(c,'\n');}
    text64(c,"pages/live, faults np/prot/unmapped: ");hex64(c,fault_page_count);
    text64(c," ");hex64(c,fault_not_present);text64(c," ");
    hex64(c,fault_protection);text64(c," ");hex64(c,fault_unmapped);putc64(c,'\n');}
```

- `vma_init`：开机把三段 VMA 填好（结构体字面量 + `valid=1`），清空四个计数器；
- `vmainfo`：逐段打印 `start-end prot backing`，最后一行打印插页数/未映射数/
  保护违规数/未存在数；
- `pfmodel` 分支（源码逐字）：先对三个地址各做一次分类——`VMA_DATA_START` 写
  （期望 not-present）、`VMA_CODE_START` 写（期望 protection，代码段只读）、
  `0x00100000` 读（期望 unmapped，不在任何 VMA）；再 `fault_insert(VMA_DATA_START,1)`
  插一页；输出
  `pfmodel: not-present/protection/unmapped classified; bounded page inserted` +
  `\nno real fault instruction executed; pages: <n>`；
- `vmatest` 分支：验证 `vma_range_valid(VMA_DATA_START, +PAGE, W)` 为真、
  `vma_range_valid(VMA_CODE_START, +PAGE, W)` 为假、`vma_lookup(0x00100000)` 为空，
  输出 `vmatest: lookup/protection/range validation passed` 或 `BROKEN`。

### 3.6 kernel_main、横幅与 Makefile 变化

`kernel_main64_binary` 在 `address_space_init` 前新增 `vma_init();`；
横幅（源码逐字）：

```text
TinyOS lesson 41: Linux-style bounded VMA/page-fault model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

`about`：`TinyOS lesson 41: bounded Linux-style VMA lookup and demand page-fault model`

Makefile `check` 目标新增三条 grep（README.md 必须包含对照路径，缺一即失败）：

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'mm/mmap.c' README.md
	@grep -q 'mm/memory.c' README.md
	@grep -q 'include/linux/mm.h' README.md
	@printf '%s\n' 'Multiboot2 and lesson 41 checks passed.'
```

### 3.7 主控制流

```text
kernel_main64_binary
  ├─ task_model_init / active_sched_class / pmm_init
  ├─ vma_init()            ← 本课新增：填 3 段 VMA
  ├─ address_space_init / GDT/TSS/IDT/PIT/PIC / exec_validate()
  ├─ 横幅（lesson-41 字符串）
  └─ 键盘循环 → exec64：
        vmainfo / vmatest / pfmodel / execinfo / forkinfo / tasklist / ...
```

---

## 4. 数据流与运行逻辑

```text
输入 "pfmodel"
  → pf_classify(0x00600000, 1)  → 命中 VMA 1（rw-），页未插 → PF_NOT_PRESENT（计数+1）
  → pf_classify(0x00400000, 1)  → 命中 VMA 0（r-x），写无 W 权 → PF_PROTECTION（计数+1）
  → pf_classify(0x00100000, 0)  → 不在任何 VMA            → PF_UNMAPPED（计数+1）
  → fault_insert(0x00600000, 1) → 页对齐、VMA 校验通过、pmm_alloc 成功 → 插 1 页
  → 输出："pfmodel: not-present/protection/unmapped classified; bounded page inserted"
            "no real fault instruction executed; pages: 0000000000000001"
输入 "vmatest" → 三断言全真 → "vmatest: lookup/protection/range validation passed"
输入 "vmainfo"
  → "VMA table (bounded Linux-style metadata)" + 三行 vma 记录
  → "pages/live, faults np/prot/unmapped: ..."
```

再跑一次 `pfmodel`：数据段那页已插入，`fault_insert` 会因 `page_present(va)` 拒绝
再插，但分类计数继续累加——可观察到「not-present 只对未插页产生」。

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

`make check` 输出：`Multiboot2 and lesson 41 checks passed.`（此串与 lesson-40
不同——Makefile 新增了三条 README 对照路径 grep，且 README 必须包含
`mm/mmap.c`、`mm/memory.c`、`include/linux/mm.h`）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：

```text
TinyOS lesson 41: Linux-style bounded VMA/page-fault model
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved
```

验证步骤（输出串从源码逐字）：

```bash
vmatest
```

预期：`vmatest: lookup/protection/range validation passed`

```bash
pfmodel
```

预期：

```text
pfmodel: not-present/protection/unmapped classified; bounded page inserted
no real fault instruction executed; pages: 0000000000000001
```

```bash
vmainfo
```

预期：

```text
VMA table (bounded Linux-style metadata)
vma 0 0000000000400000-0000000000401000 r-x file
vma 1 0000000000600000-0000000000602000 rw- anon
vma 2 0000000000800000-0000000000802000 rw- anon
pages/live, faults np/prot/unmapped: 0000000000000001 0000000000000001 0000000000000001 0000000000000001
```

（页数/三分类计数按运行顺序显示；第二次 `pfmodel` 后 `pages` 仍为 1。）

继承回归：`exectest`（passed; no execution）、`forklifecycle`（passed）、
`taskvalidate`（passed）、`processtest`、`vmtest` 行为不变；**真实** `#PF`
命令 `pftest`/`isttest`/`stackguardtest` 依旧致命停机（本课不改变它们）。

### 5.4 课程实测记录（2026-08，稳定快照）

`make check` 输出 `Multiboot2 and lesson 41 checks passed.`；`vmatest` 通过；
`pfmodel` 三分类各计数 1 并插入 1 页；`vmainfo` 显示 3 段 VMA 与计数；
重复 `pfmodel` 不再插新页；`pftest` 仍触发真实致命 #PF。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make check` 报错 | README 缺 `mm/mmap.c`/`mm/memory.c`/`include/linux/mm.h` 任一字符串 | 对照 Makefile check 目标的 `grep -q` 三行 |
| `vmatest` 输出 `BROKEN` | 三段 VMA 初始化或断言被改 | 核对 `vma_init` 字面量与 `vmatest` 三个断言 |
| `pfmodel` 分类计数全 0 | `pf_classify` 未执行（分支未挂上） | 检查 `exec64` 的 `pfmodel` 分支 |
| `pfmodel` 第二次不插页 | 数据段页已存在（`page_present` 拒绝） | 这是设计行为；`pages` 保持 1 |
| 想用真实 `#PF` 验证分类 | 本课教学模型不触发真实缺页 | `pfmodel` 只是数据模拟；`pftest` 仍是致命真实异常 |
| `vmainfo` 的 backing 显示 anon | `kind` 字段未设 file | 对照 `vma_init`：VMA 0 是 `VMA_FILE`，其余 `VMA_ANON` |
| `fault_insert` 分配失败 | `pmm_alloc` 返回 0（无空闲帧） | `meminfo` 查 `free`；VMA_MAX_PAGES=4 上限 |

---

## 7. 与 Linux 源码对照

本课对照 **Linux v6.12 `mm/mmap.c`、`mm/memory.c` 与 `include/linux/mm.h`**：

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `struct vma_model { start,end,prot,kind }` | `include/linux/mm.h` 的 `struct vm_area_struct`（`vm_start`/`vm_end`/`vm_flags`/`vm_file`/`vm_ops`…） | 教学模型每段 5 个字段；Linux 还有 `vm_prev/next`、`vm_mm`、`vm_page_prot` 等 |
| `vma_table[4]` 固定数组 | `mm_struct->mmap` 双向链表 + `mm_rb` 红黑树（`include/linux/mm_types.h`） | 教学模型线性扫描；Linux 用红黑树 `find_vma`（`mm/mmap.c`）保证 O(log n) |
| `vma_range_valid(start,end,prot)` | `mm/mmap.c` 的 `find_vma_intersection` / `check_vma_flags`、`may_expand_vm` | 教学模型加 64 KiB 上限并校验权限位 |
| `pf_classify` 三分支 | `mm/memory.c` 的 `handle_mm_fault` → `do_anonymous_page`/`do_fault`/`vm_fault_t`；`arch/x86/mm/fault.c` 先 `find_vma` | 顺序一致：无 VMA → `SIGSEGV`；权限 → `SIGSEGV`；缺页 → 插页 |
| `fault_insert` + `fault_pages[]` | `do_anonymous_page`（`mm/memory.c`）→ `alloc_zeroed_user_highpage` + `pte_mkdirty` | 教学模型用 `pmm_alloc` 记账，**不写页表、不改 PTE** |
| `fault_not_present/protection/unmapped` 计数 | `arch/x86/mm/fault.c` 的 `perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS)` 与 error-code 分类 | 教学模型把「页表 error code 判断」简化为纯函数 |
| `vma_init` 三段硬编码 | exec 装载时 `mm/mmap.c` 的 `mmap_region` 逐段建 VMA | 教学模型直接写字面量，无 `mmap` 系统调用 |

**权威来源**：Intel SDM（`#PF` 的 error code 位：P/W/U/R 位决定 not-present 与
保护违规）、ELF 装载约定（段 → VMA 的映射）。

**教学模型简化了什么**：
1. 无红黑树：3 段线性查找；2. 无 `mm_struct`：VMA 表是全局静态，不绑定进程；
3. 无页表操作：`fault_insert` 只记账，不 `pte` 写入、不 `invlpg`；
4. 无 COW/交换/文件映射回读：`VMA_FILE` 只有标记没有 `vm_ops`；
5. 不触发真实 `#PF`：教学测试全部是数据层模拟，真实异常路径保持致命；
6. 无 `mmap`/`munmap`/`mprotect` 系统调用：VMA 数量固定为 3。

---

## 8. 思考题与练习

1. **概念理解**：`pf_classify(VMA_CODE_START, 1)` 为什么返回 `PF_PROTECTION`？
   把代码段写成只读在真实系统里对应什么错误码位（Intel SDM 的 W 位）？
2. **源码定位**：指出 `fault_insert` 的三个前置拒绝条件分别对应源码中的哪个
   `if`，并说明为什么需要「页对齐 + 范围 + 未插入」三重检查。
3. **动手实验**：在 `vma_init` 里把 VMA 1 的 `VMA_R|VMA_W` 改成 `VMA_R`，重建后
   运行 `vmatest` 与 `pfmodel`，观察哪个输出变化（应出现 protection 分类），
   然后改回（勿提交）。
4. **Linux 对照**：在 `arch/x86/mm/fault.c` 的 `do_user_addr_fault` 中找出
   `find_vma` 调用点与 `SIGSEGV` 路径，对比 `pf_classify` 的三个返回。
5. **设计思考**：`vma_range_valid` 为什么限制 `end-start>0x10000` 即拒绝？如果要
   支持 2 MiB 的 VMA，需要改哪些常量/检查？

---

## 9. 本课小结与下一课预告

**小结**：本课把「进程地址空间」显式化为固定 4 槽 VMA 表：3 段真实区间
（code r-x / data rw- / stack rw-）带 `vma_lookup`/`vma_range_valid` 查询与
权限校验；`pf_classify` 按「无 VMA → 权限不足 → 页不在」的顺序分类缺页并计数；
`fault_insert` 用 `pmm_alloc` 有界记账（上限 4 页、页对齐、去重）。
`vmainfo`/`vmatest`/`pfmodel` 三命令可观察全部状态；真实 `#PF` 路径未改动。
Makefile `check` 新增了对 README 中 Linux 对照路径的强制 grep。

**下一课预告**：进入 [`lesson-42-stable/README.md`](../lesson-42-stable/README.md)，
在本课 VMA 表之上做 user-pointer 校验：`uaccess_validate` 依次检查 canonical、
range、VMA 命中、VMA 权限，`uaccess_copy` 模拟有界 `copy_to_user`/
`copy_from_user` 的成功/失败记账——**绝不解引用任意用户指针**，对照
`include/linux/uaccess.h`、`mm/usercopy.c` 与
`arch/x86/include/asm/uaccess.h`。

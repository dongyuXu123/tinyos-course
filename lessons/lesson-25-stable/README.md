# Lesson 25: 高别名静态栈 guard page 与双别名运行时映射窗口 — 精讲文档

> **课号**：25　**主题**：guarded static kernel stacks and dual-alias VM window
> **课程主线位置**：阶段四（内核运行时基础设施）——在 Lesson 24 的 TSS/`rsp0`/IST1 预备之后、
> Lesson 26（16 MiB PMM 扩展）之前，属于「用户态入场前的最后两块地基」。
> **前置课程**：[../lesson-24-stable/README.md](../lesson-24-stable/README.md)
> **后续课程**：[../lesson-26-stable/README.md](../lesson-26-stable/README.md)
> **一句话目标**：把三个静态内核栈（idle、未来 `rsp0`、IST1）变成带高别名 guard page 的显式
> 契约，并把原先「低地址单向映射槽」升级为一个低/高双别名同步的受控运行时映射窗口。

> **Course status: stable snapshot (validated; verified build artifacts included).**
> 本目录为已校验稳定快照：`build/` 内含已验证构建产物，README 为精讲文档。

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能够在 64 位内核里用链接器符号声明「栈的 guard/payload/end 三段
  区间」，让三个静态栈的高别名访问被 guard page 保护；同时拥有一个低地址
  `0x003ff000` 与高地址 `0xffffffff803ff000` 同步映射/解映射的运行时窗口，并会证明
  这两个别名指向同一物理页。

- **在课程主线中的位置**：本课处于「内核运行时基础设施」阶段。上一课（Lesson 24）建立了
  TSS、`rsp0`、IST1 与 #PF 异常栈，但三个静态栈还没有任何越界保护；本课给它们加
  guard page，并把上一课遗留的「仅低地址动态映射槽」升级成双别名窗口。下一课
  （Lesson 26）在这个窗口机制上把 4 MiB 映射地平线扩到 16 MiB。

- **前置知识清单**（学本课之前必须掌握）：
  1. x86-64 四级页表：PML4/PDPT/PD/PT 与 PTE 位（Present、Writable、PFN）——
     参见课程 11–13；
  2. 高低双别名映射（`ffffffff80000000` 高半内核别名）——Lesson 16–17；
  3. TSS `rsp0` / IST1 与 IDT 中断门——Lesson 24；
  4. 物理页管理器 PMM 的 bitmap 账本（`pmm_bitmap` / `pmm_fixed`）——Lesson 19–21；
  5. `TEXT64` 段、`kernel64.bin` 二进制续体、`invlpg`——Lesson 17–18。

- **本课交付**（可见结果）：
  - 新命令 `stackinfo`（打印三段栈的 guard/payload/end 区间）与
    `stackguardtest idle|rsp0|ist1`（确定性地触发 guard page #PF，致命并停在报告屏）；
  - `vminfo` 升级为同时显示低/高两个别名的 PTE；`vmtest` 升级为证明
    「低写高读 + 高写低读」双向一致；
  - 链接脚本 `kernel64.ld` 中可见三块「guard 页 + 栈 payload 页」的布局与三条
    `ASSERT` 尺寸断言。

## 2. 核心概念精讲

### 2.1 guard page（哨兵页）

- **定义**：guard page 是紧贴在向下生长栈的下边界（低地址侧）之前、PTE 被清为「非 Present」
  的一整页虚拟地址。任何对它的访问都会触发 #PF，而不是静默写坏相邻内存。
- **为什么需要（动机）**：栈溢出是内核最经典的破坏源。向下生长的栈一旦越界，会先写坏
  紧邻低地址的其它数据结构；传统做法是事后用金丝雀（stack canary）检测，而 guard page
  是**结构性**防线——让越界访问直接变成异常，从而被 #PF 处理程序拦截并报告。
- **工作机制**：页表 PTE 的 Present 位（bit 0）为 0 时，TLB 与页表遍历都把该地址视为不可映射，
  产生 `#PF` 并写入 CR2。本课在**高别名**页表（`high_pt0`）里把 guard 页的 PTE 清零，因为
  C 代码与 `rsp0`/IST1 都在高别名下运行，越界只可能发生在高别名访问路径上；低别名仍保持
  Present，用于引导期兼容（教学模型的明确取舍）。
- **示意图**：

```
高地址（栈顶）                 低地址（栈底）                      再往下
┌──────────────────┐
│  payload 页       │  ← __idle_stack_start .. __idle_stack_end
│  (1 页, 4 KiB)    │     栈向下生长进这一页
└──────────────────┘
┌──────────────────┐
│  guard 页         │  ← __idle_guard_start .. __idle_guard_start+0x1000
│  高别名 PTE = 0   │     向下越界 → #PF（IST1），CR2 指向该地址
└──────────────────┘
```

- 关键认识：guard page 保护的是**向下越过高别名边界**的访问，它**不**保护 payload 页内部的
  深栈使用，也不保护 worker 栈或引导栈（它们不在本课的 guard 契约内）。

### 2.2 链接器符号而非声明顺序定义区间

- 上一课（Lesson 24）用 `static u8 tss_rsp0_stack[PAGE_SIZE]` 数组地址计算栈顶，数组之间
  的 padding 不可控。本课改为在 `kernel64.ld` 中**逐段布局**：先用 `. = ALIGN(0x1000)` 对齐，
  再 `__idle_guard_start = .; . += 0x1000;` 预留 guard 页，然后放置 `.data.stack.idle` 段并在
  段尾定义 `__idle_stack_end`。
- 为什么这样设计：guard 页是「纯空白、没有对应 C 对象」的保留区，只有链接器才能在不分配
  数组的前提下把它夹在栈 payload 之间；同时 `ASSERT(... == 0x1000)` 把「每个栈恰好一页」的
  契约变成**构建期失败**而不是运行时 bug。`rsp0` 与 IST1 因此可以放心地指向
  `__rsp0_stack_end` / `__ist1_stack_end`（payload 顶部）。

### 2.3 低/高双别名运行时映射窗口

- **定义**：一对同步的 4 KiB 映射槽：低地址 `DYNAMIC_TEST_VA = 0x003ff000`（物理低窗，
  pt1 的 511 号 PTE）与高地址 `DYNAMIC_TEST_HIGH_VA = KERNEL_VMA_BASE + 0x003ff000`
  （`ffffffff803ff000`，high_pt1 的 511 号 PTE）。两者初始都非 Present。
- **为什么需要（动机）**：内核要在任意物理帧上做临时读写（例如为硬件 DMA 或调试），但
  物理帧本身没有固定虚拟地址。一个「单槽窗口」把任意已分配帧临时映射进来，用完即走。
  上一课窗口只存在于低地址别名；本课把它同步到高别名，使 C 代码（全部运行在高半区）
  也能直接访问窗口内容，而低别名仍保留给 32 位引导兼容路径。
- **工作机制**：`map_page` 在保存-IF 临界区里把两个 PTE 写成 `phys|PTE_PRESENT_WRITABLE`，
  然后对**两个**虚拟地址各发一次 `invlpg`；`unmap_page` 对称地把两个 PTE 清零。
  `vm_window_phys` 全局变量记录窗口当前归属的物理帧（0 = 未映射），同时充当「已占用」互斥。
- **示意图**：

```
       物理页 P (pmm_alloc 得到)
              │
      ┌───────┴───────┐
      ▼               ▼
 低 PTE 0x003ff000   高 PTE 0xffffffff803ff000
 pt1[511]           high_pt1[511]
```

- 双 PTE 一致性检查：`map_page`/`unmap_page` 都会在临界区外先读回两个 PTE，若 `vm_window_phys`
  或任一 PTE 非零则拒绝映射（返回 `"slot already mapped"`），保证「一次只有一帧可出现在窗口」。

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|------|------|------------------------|
| `boot.S` | Multiboot2 header、32 位引导、long mode 进入 | **未变化** |
| `kernel.c` | 32 位引导期：构建低/高页表、long_mode_handoff | **微小变化**：高别名窗口槽 `hpt1[511]` 也清零 |
| `kernel64.c` | 64 位续体：PMM、调度、异常、shell | **主要增量**：guard 页初始化、双别名窗口、`stackinfo`/`stackguardtest`/`vmtest` 升级 |
| `kernel64.ld` | 64 位续体链接脚本 | **主要增量**：三块 guard+payload 布局与 `ASSERT` |
| `linker.ld` | 32 位内核 ELF 布局 | **未变化** |
| `Makefile` | 构建管线 | **未变化** |
| `grub.cfg` | GRUB 菜单项 | **微小变化**：menuentry 文案改为本课标题 |

### 3.2 kernel64.ld：guard/payload/end 三段布局

```ld
.data : {
    . = ALIGN(0x1000);                 /* 页对齐起点 */
    __idle_guard_start = .; . += 0x1000;   /* 预留 1 页 guard（无 C 对象） */
    __idle_stack_start = .; *(.data.stack.idle) __idle_stack_end = .;
                                         /* idle payload 恰好 1 页，段尾记 end */
    . = ALIGN(0x1000);
    __rsp0_guard_start = .; . += 0x1000;
    __rsp0_stack_start = .; *(.data.stack.rsp0) __rsp0_stack_end = .;
    . = ALIGN(0x1000);
    __ist1_guard_start = .; . += 0x1000;
    __ist1_stack_start = .; *(.data.stack.ist1) __ist1_stack_end = .;
    . = ALIGN(0x1000);
    *(.data .data.*)                    /* 其余可写数据 */
    *(.bss .bss.* COMMON)
    BYTE(0)                             /* objcopy 需要 PROGBITS 结尾哨兵 */
}
ASSERT(__idle_stack_end - __idle_stack_start == 0x1000, "idle stack size")
ASSERT(__rsp0_stack_end - __rsp0_stack_start == 0x1000, "rsp0 stack size")
ASSERT(__ist1_stack_end - __ist1_stack_start == 0x1000, "ist1 stack size")
```

逐行要点：

1. `. = ALIGN(0x1000)`：强制下一个输出位置 4 KiB 对齐。C 侧的
   `__attribute__((section(".data.stack.idle"),aligned(PAGE_SIZE),used))` 只是「请求」页对齐，
   真正保证对齐的是这里——因为 `kernel64.bin` 是 raw 二进制，若不对齐，嵌入 ELF 后栈页
   会与 guard 页错位。
2. `__idle_guard_start = .; . += 0x1000;`：guard 页没有任何字节被填充（不产生输出内容），
   仅靠位置计数器前移 1 页。因此 raw 镜像里它的字节是下一段输出的数据（这正是 guard 页
   要靠**运行时清 PTE** 才能变「非 Present」的原因——文件内容无所谓，页表才决定可见性）。
3. `*(.data.stack.idle)`：把 C 里的 `idle_stack[PAGE_SIZE]` 收集进来；`__idle_stack_end = .`
   在段尾贴标签，它就是 payload 顶部 = idle 帧的初始 RSP 基准。
4. 三条 `ASSERT`：若任何 `static u8 x[PAGE_SIZE]` 被编译器塞进错误对齐/尺寸的位置，
   **链接期直接报错**，防止「guard 与 payload 重叠」这类静默布局漂移。

### 3.3 kernel.c：双别名窗口槽的引导期准备

```c
/* The controlled runtime window is absent through both aliases until map_page(). */
pt1[PAGE_ENTRIES-1]=0;
hpt1[PAGE_ENTRIES-1]=0;
```

- 上一课只把低别名 `pt1[511]`（VA `0x003ff000`）清为 0；本课补上高别名 `hpt1[511]`
  （VA `0xffffffff803ff000`）。两行都在 `setup_long_mode_tables()` 末尾、把 8 个页表按
  4 MiB identity 填满之后执行，确保窗口槽「初始非 Present」的契约在 32 位引导期就成立，
  与 64 位续体的 `map_page` 双 PTE 写法一一对应。

### 3.4 kernel64.c：本课新增符号与常量

| 符号 | 含义 |
|------|------|
| `DYNAMIC_TEST_HIGH_VA` | `KERNEL_VMA_BASE + DYNAMIC_TEST_VA`，即 `0xffffffff803ff000` |
| `__idle_guard_start` / `__idle_stack_start` / `__idle_stack_end` | idle 栈三段区间（链接器导出） |
| `__rsp0_guard_start` / `__rsp0_stack_start` / `__rsp0_stack_end` | `rsp0` 栈三段区间 |
| `__ist1_guard_start` / `__ist1_stack_start` / `__ist1_stack_end` | IST1 栈三段区间 |
| `stack_guards_init()` | 把三个 guard 页的高别名 PTE 清零并 `invlpg` |
| `vm_pte_low()` / `vm_pte_high()` | 分别取低/高窗口 PTE 指针 |
| `stackinfo` / `stackguardtest` | 新 shell 命令（打印区间 / 触发 guard #PF） |

C 侧三个栈数组现在显式分节放置：

```c
static u8 idle_stack[PAGE_SIZE] __attribute__((section(".data.stack.idle"),aligned(PAGE_SIZE),used));
static u8 tss_rsp0_stack[PAGE_SIZE] __attribute__((section(".data.stack.rsp0"),aligned(PAGE_SIZE),used));
static u8 exception_ist_stack[PAGE_SIZE] __attribute__((section(".data.stack.ist1"),aligned(PAGE_SIZE),used));
```

- 逐行说明：`section(...)` 让链接脚本能精确夹放 guard 页；`aligned(PAGE_SIZE)` 请求页对齐
  （最终由链接器确认）；`used` 阻止编译器把「看似只用符号、不用数组」的数组裁掉——因为
  代码现在只引用 `__xxx_stack_end` 链接符号，不再直接引用数组名。

#### 函数：`stack_guards_init`

```c
static TEXT64 void stack_guards_init(struct long_mode_handoff*h){
    volatile u64 *pt=(volatile u64 *)(unsigned long)h->high_pt0;   /* 高别名 PT0 */
    u64 g[3]={(u64)(unsigned long)__idle_guard_start,
              (u64)(unsigned long)__rsp0_guard_start,
              (u64)(unsigned long)__ist1_guard_start};             /* 三个 guard 高地址 */
    u32 i;
    for(i=0;i<3;i++){
        u64 off=g[i]-KERNEL_VMA_BASE;    /* 高地址转物理偏移（identity 映射） */
        pt[off/PAGE_SIZE]=0;             /* 清 PTE → 非 Present */
        invlpg64(g[i]);                  /* 使 TLB 立即失效，不等上下文切换 */
    }
}
```

- 签名与职责：接收 `long_mode_handoff`，把三个静态栈 guard 页在高别名下置为不可访问。
- 输入输出：输入页表基址 `high_pt0` 的物理地址与三个链接符号；无返回值；副作用是改写
  三个 PTE 并刷新 TLB。
- 算法步骤：(1) 取高别名 PT0；(2) 对每个 guard 页把 VMA 减去 `KERNEL_VMA_BASE` 得到
  identity 偏移（因为 `ffffffff80000000 + phys`）；(3) `off/PAGE_SIZE` 即 PTE 索引；
  (4) 写 0 并 `invlpg`。
- 边界与错误处理：guard 页全部位于 4 MiB 身份映射的第一 MiB 内（`.data` 在 64 位续体头部），
  因此 `off/PAGE_SIZE < 512` 恒成立，无需越界检查；若链接布局漂移（guard 高地址落在
  `high_pt1` 区间），PTE 会写错页表——这正是 `kernel64.ld` 三条 `ASSERT` 要防的。
- 设计动机：只清**高别名** PTE，低别名保持 Present 以兼容引导期 32 位访问；guard 保护范围
  与本课契约（只保护高运行时别名访问）严格一致。

#### 函数：`map_page` / `unmap_page`（双别名版本）

```c
static TEXT64 const char *map_page(struct long_mode_handoff*h,u64 p){
    volatile u64*l,*q;u64 flags;const char*s=page_state(p);
    if(!eq64(s,"allocated"))return s;                 /* 只允许映射 PMM 已分配页 */
    flags=irq_save64();                               /* 保存 IF 并 cli */
    l=vm_pte_low(h);q=vm_pte_high(h);
    if(vm_window_phys||*l||*q){irq_restore64(flags);return "slot already mapped";}
    *l=*q=p|PTE_PRESENT_WRITABLE;                     /* 两个 PTE 写同一帧+标志 */
    invlpg64(DYNAMIC_TEST_VA);invlpg64(DYNAMIC_TEST_HIGH_VA);  /* 双 invlpg */
    vm_window_phys=p;                                 /* 记录窗口归属 */
    irq_restore64(flags);
    return "mapped";
}
static TEXT64 const char *unmap_page(struct long_mode_handoff*h){
    volatile u64*l,*q;u64 flags;
    if(!vm_window_phys)return "slot already unmapped";
    flags=irq_save64();l=vm_pte_low(h);q=vm_pte_high(h);
    *l=*q=0;                                          /* 两个 PTE 一起清零 */
    invlpg64(DYNAMIC_TEST_VA);invlpg64(DYNAMIC_TEST_HIGH_VA);
    vm_window_phys=0;
    irq_restore64(flags);
    return "unmapped";
}
```

- 签名与职责：`map_page(h,p)` 把已分配物理帧 `p` 映射进双别名窗口；`unmap_page(h)` 撤销。
- 输入输出：返回结果字符串（`"mapped"`/`"unmapped"`/错误原因），供 shell 直接打印；
  `vm_window_phys` 是唯一窗口所有者。
- 算法步骤（map）：校验页状态 → 关中断 → 校验窗口空闲（`vm_window_phys` 或任一 PTE 非零则拒）
  → 同时写两个 PTE → 双 `invlpg` → 记录 owner → 恢复 IF。
- 边界与错误处理：窗口非空闲、页非「allocated」（free / fixed / invalid）都拒绝并返回
  中文可读原因串；中断关断保证「读 PTE 校验 + 写 PTE」原子，避免 IRQ 期间窗口被并发改。
- 设计动机：低别名供引导/外部工具使用，高别名供 C 代码直接解引用；`invlpg` 对两个 VA 各发
  一次是因为同一物理页的两个不同虚拟地址需要分别刷新 TLB 项。

#### 函数：`vminfo` 与 `vmtest`

`vminfo` 现在打印两组地址和两组 PTE：

```c
static TEXT64 void vminfo(u16*c,struct long_mode_handoff*h){
    text64(c,"VM low/high: ");hex64(c,DYNAMIC_TEST_VA);text64(c," ");
    hex64(c,DYNAMIC_TEST_HIGH_VA);
    text64(c,"\nstate/owner: ");text64(c,vm_window_phys?"mapped ":"unmapped ");hex64(c,vm_window_phys);
    text64(c,"\nPTE low/high: ");hex64(c,*vm_pte_low(h));text64(c," ");hex64(c,*vm_pte_high(h));
    putc64(c,'\n');
}
```

`vmtest` 本课升级为**双向别名验证**：

```c
static TEXT64 void vmtest(u16*c,struct long_mode_handoff*h){
    volatile u64 *v=(volatile u64 *)DYNAMIC_TEST_VA,*q=(volatile u64 *)DYNAMIC_TEST_HIGH_VA;
    u64 p,before,after;const char*r;
    if(vm_window_phys){text64(c,"vmtest requires an unmapped slot\n");return;}
    before=pmm_free;p=pmm_alloc();if(!p){text64(c,"vmtest allocation failed\n");return;}
    r=map_page(h,p);if(!eq64(r,"mapped")){text64(c,"vmtest map failed: ");text64(c,r);putc64(c,'\n');pmm_free_page(p);return;}
    *v=0x564d544553543135ULL;                 /* 低别名写入 "VMTEST15" */
    if(*q!=0x564d544553543135ULL){text64(c,"vmtest low/high mismatch\n");return;}   /* 高别名读到同值 */
    *q=0x48494748564d3235ULL;                 /* 高别名写入 "HIGHVM25" */
    if(*v!=0x48494748564d3235ULL){text64(c,"vmtest high/low mismatch\n");return;}   /* 低别名读到同值 */
    r=unmap_page(h);if(!eq64(r,"unmapped")){text64(c,"vmtest unmap failed\n");return;}
    r=pmm_free_page(p);after=pmm_free;
    if(!eq64(r,"freed")||after!=before){text64(c,"vmtest PMM accounting failed\n");return;}
    text64(c,"vmtest: low/high map/write/read/unmap/free passed\n");
}
```

- 职责：把 PMM 记账与双别名同步一起回归：`before/after` 检查 `pmm_free` 前后不变，
  证明「map 不丢页、free 不重复记账」。
- 算法步骤：要求窗口空 → 记录 `pmm_free` → `pmm_alloc` → `map_page` → 低写高读 →
  高写低读 → `unmap_page` → `pmm_free_page` → 校验 free 计数复原 → 打印通过。
- 边界与错误处理：每一步失败都有对应输出串并提前返回；映射失败时先把刚分配的页 free 掉，
  避免内存泄漏后 `pmm_free` 计数对不上。

#### 函数：`exec64` 新增分支（`stackinfo` / `stackguardtest`）

```c
else if(eq64(word,"stackinfo")){
    text64(c,"idle guard/payload/end: ");hex64(c,(u64)(unsigned long)__idle_guard_start);
    text64(c," ");hex64(c,(u64)(unsigned long)__idle_stack_start);text64(c," ");
    hex64(c,(u64)(unsigned long)__idle_stack_end);
    text64(c,"\nrsp0 guard/payload/end: ");hex64(c,(u64)(unsigned long)__rsp0_guard_start);
    text64(c," ");hex64(c,(u64)(unsigned long)__rsp0_stack_start);text64(c," ");
    hex64(c,(u64)(unsigned long)__rsp0_stack_end);
    text64(c,"\nIST1 guard/payload/end: ");hex64(c,(u64)(unsigned long)__ist1_guard_start);
    text64(c," ");hex64(c,(u64)(unsigned long)__ist1_stack_start);text64(c," ");
    hex64(c,(u64)(unsigned long)__ist1_stack_end);putc64(c,'\n');
}
else if(eq64(word,"stackguardtest")){
    if(eq64(arg,"idle")||eq64(arg,"rsp0")||eq64(arg,"ist1")){
        volatile u64 *bad=(volatile u64 *)(unsigned long)
            (eq64(arg,"idle")?(u64)(unsigned long)__idle_guard_start:
             eq64(arg,"rsp0")?(u64)(unsigned long)__rsp0_guard_start:
             (u64)(unsigned long)__ist1_guard_start);
        text64(c,"stackguardtest: fatal #PF expected\n");
        p=*bad;(void)p;                     /* 确定性读 guard 页 → #PF */
    }else usage64(c,"stackguardtest idle|rsp0|ist1");
}
```

- `stackguardtest` 是一个**确定性的 volatile 读**（`p=*bad`），不是编译器依赖的真实栈溢出，
  因此结果可复现；`#PF` 走 IST1，`exception_report_ist` 打印 CR2、被中断 RSP、handler RSP
  与 IST1 区间后 `cli; hlt` 停住。

#### 函数：`exception_report_ist`（改为打印链接符号区间）

```c
TEXT64 void exception_report_ist(struct exception_frame_ist*f){
    ...
    text64(&c,"TinyOS lesson 25 IST exception\nexception: #PF\nvector: ");
    hex64(&c,f->vector);text64(&c,"\nerror:  ");hex64(&c,f->error);
    text64(&c,"\nrip:    ");hex64(&c,f->rip);text64(&c,"\nsaved rsp: ");hex64(&c,f->rsp);
    text64(&c,"\nhandler rsp: ");hex64(&c,rsp);
    text64(&c,"\nIST1 range: ");hex64(&c,(u64)(unsigned long)__ist1_stack_start);
    text64(&c," ");hex64(&c,runtime_tss.ist1);
    __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
    text64(&c,"\ncr2:    ");hex64(&c,cr2);
    text64(&c,"\nCPU halted intentionally.\n");
    for(;;)__asm__ volatile("cli; hlt");
}
```

- 相比上一课，`IST1 range` 起点从数组名改为链接符号 `__ist1_stack_start`，与
  `stackinfo`/`runtime_tss.ist1`（`__ist1_stack_end`）组成同一套符号来源，三个显示点一致。
- 帧结构 `exception_frame_ist`（56 字节）沿用上一课：CPU 通过 IST 进入时把被中断的
  CPL0 栈指针作为 `rsp` 压入，所以「saved rsp」与「handler rsp」不同，正是证明
  #PF 确实跑在 IST1 上的依据。

### 3.5 主控制流

`kernel_main64_binary` 的初始化序列（本课新增 `stack_guards_init` 在最前）：

```
kernel_main64_binary(h)
  ├─ pmm_init(h)             ← 页账本就绪
  ├─ stack_guards_init(h)    ← ★ 本课新增：清三个 guard 页高别名 PTE + invlpg
  ├─ runtime_gdt_tss_init()  ← rsp0=__rsp0_stack_end, ist1=__ist1_stack_end
  ├─ idle_init()             ← idle 帧放 __idle_stack_end-sizeof(*f)
  ├─ install_idt(h)          ← #PF 门带 IST1
  ├─ pit_init(); pic_init()
  └─ 打印 banner → shell 循环
```

shell 循环里 `stackinfo`/`stackguardtest`/`vminfo`/`vmtest` 直接走 `exec64` 分支。

## 4. 数据流与运行逻辑

- 命令 `stackinfo`：`exec64` 识别 `word=="stackinfo"` → 打印三组
  `guard/payload/end` 十六进制区间 → 屏幕依次显示
  `idle guard/payload/end: ffffffff80xxxxxx ffffffff80xxxxxx ffffffff80xxxxxx` 等三行。
- 命令 `stackguardtest ist1`：`exec64` 校验参数 → 打印
  `stackguardtest: fatal #PF expected` → `p=*bad` 读非 Present 页 → CPU 走 IST1 →
  `exception_report_ist` 全屏报告（`TinyOS lesson 25 IST exception` … `CPU halted intentionally.`）→ 停住。
- 命令 `palloc` 得到一个物理帧 → `vmap <hex>` 调 `map_page` → 低/高两个 PTE 同步写该帧 →
  `vminfo` 显示 `mapped` 与两组 PTE → `vmtest` 双向读写一致 → `vunmap` 清两个 PTE →
  `pfree <hex>` 归还帧。

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （与 Makefile 变量 `CC/LD/OBJCOPY` 一致）。
- **构建命令**（与 Makefile 目标一致）：

```bash
cd /home/dongyu/.zcode/workspace/default/lessons/lesson-25-stable
make clean && make -j"$(nproc)"
make check        # grub-file --is-x86-multiboot2 build/kernel.elf；预期打印 Multiboot2 header check passed.
```

- **运行命令**：`make run`。成功画面在 QEMU 图形窗口（VGA 教学 shell），**勿加 `-display none`**。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. 启动后第一屏：
     ```
     TinyOS lesson 25: guarded kernel stacks and dual VM window
     Kernel-only preparation; IRQ0 return-frame switching retained
     tinyos>
     ```
  2. 输入 `stackinfo`：应出现三行 `idle guard/payload/end: …`、`rsp0 guard/payload/end: …`、
     `IST1 guard/payload/end: …`，每个区间的 `payload` 与 `end` 相差 `0x1000`，`guard` 紧邻
     payload 低地址侧。
  3. 输入 `vminfo`：出现 `VM low/high: 00000000003ff000 ffffffff803ff000`、
     `state/owner: unmapped 0000000000000000`、`PTE low/high: 0000000000000000 0000000000000000`。
  4. 输入 `palloc` 记下返回帧，输入 `vmap <该帧>`：预期 `mapped: <帧> at 00000000003ff000`；
     再次 `vminfo` 时 `PTE low/high` 两个 PTE 应相同（`<帧>|003`）。再 `vunmap` 恢复。
  5. 输入 `vmtest`（窗口为空时）：预期
     `vmtest: low/high map/write/read/unmap/free passed`。
  6. 回归 `bptest`（#BP 返回）、`idletest`、`preempttest`、`pctest`/`pcgo`、键盘与 PMM 命令。
  7. 致命测试须各自**全新启动**：`stackguardtest idle|rsp0|ist1`、`isttest`、`vmfaulttest`、
     `udtest`、`pftest` 都应在报告后停住（`CPU halted intentionally.`）。
- **判断成功**：干净构建 + `make check` 通过；`readelf -l build/kernel.elf` 无 RWX 外
  `LOAD` 段；`nm` 可见 `__idle_guard_start` 等符号；VGA 上上述命令输出与预期一致。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `stackguardtest` 不死机 | guard PTE 未清成 0（`stack_guards_init` 没被调用，或 `invlpg` 缺失） | `vminfo`/`gdb` 检查 `high_pt0` 中对应 PTE；确认 `kernel_main64_binary` 在 `sti` 之前调用 `stack_guards_init` |
| guard 与 payload 重叠 | C 数组未进 `.data.stack.*` 段或链接对齐漂移 | 看 `kernel64.ld` 的 `ASSERT` 是否报错；`nm` 打印三个 `__xxx_stack_start/end` 差应为 `0x1000` |
| `vminfo` 两个 PTE 不一致 | 只改了低别名 PTE（旧 `vm_pte` 残留） | 确认用的是 `vm_pte_low`+`vm_pte_high` 双写，`map_page`/`unmap_page` 各两次 `invlpg` |
| `vmtest` 报 `low/high mismatch` | 高别名 PTE 未写或 TLB 未刷新，高地址读到旧值 | `invlpg64(DYNAMIC_TEST_HIGH_VA)` 是否执行；QEMU `-d mmu` 观察两别名翻译 |
| `vmtest` 报 `PMM accounting failed` | `pmm_free` 前后计数不符（map/free 重复记账） | 检查 `pmm_free_page` 的 `vm_window_phys` 拒绝分支是否命中 |
| `stackinfo` 地址异常（未落在高半区） | 链接脚本符号名与 C `extern` 名不一致 | 对拍 `nm build/kernel64.elf` 与 `kernel64.c` 的 `extern u8 __xxx[];` |
| 栈越界直接静默破坏数据 | guard 页被 PTE Present 化（如低别名误写、或引导期映射残留） | 启动后 `vminfo` 风格地 dump `high_pt0` 相关 PTE；确认 `stack_guards_init` 每次 boot 都执行 |

## 7. 与 Linux 源码对照

- **TinyOS**：每个静态内核栈 = 1 页 payload + 紧邻低地址 1 页 guard（高别名 PTE 清零），
  栈 `rsp0`/IST1 指向 payload 顶部，越界走 IST1 #PF 并停机。
- **Linux 对照**（`arch/x86/kernel/vmlinux.lds.S`）：`STACK_ALIGN`、`init_thread_union` 与
  每 CPU 栈在链接脚本中布局；`vmap_stack`/`VM_STACK` 分配页时用 `PAGE_KERNEL` 并在栈顶之下
  放置 guard page（`_PAGE_NX` 区段隔离）。`arch/x86/kernel/process_64.c` 的 `load_sp0`
  维护 TSS `rsp0`。
- **权威来源**：Intel SDM Vol.3 §4.10（PTE 位与页面错误）、§6.14（中断门与 IST）、§5.8.5
  （TSS）；GNU ld 手册（`ASSERT`、位置计数器、`KEEP`）。
- **教学模型简化**：guard page 只有一页、只保护高别名路径、不做 guard 页回收；栈越界
  直接 `hlt` 停机而非恢复；`rsp0` 在 CPL3 到来之前只是预备值，硬件尚未消费。

## 8. 思考题与练习

1. **概念理解**：为什么 guard 页只能保护「向下越界」，不能保护「向高地址越界写」？
   若想让 IST1 栈溢出同样被检测，guard 页该放哪一侧？
2. **源码定位**：在 `kernel64.c` 中找出 `map_page` 的三个失败返回串，并说明各自在什么
   条件下触发；为什么失败路径在 `irq_restore64` 之后才返回？
3. **动手实验**：把 `stack_guards_init` 里的 `invlpg64(g[i])` 删掉重编译运行
   `stackguardtest idle`，观察是否仍按预期 #PF（提示：首次访问不受旧 TLB 影响，但换用
   QEMU `-d tlb` 观察差异），再恢复原代码。
4. **动手实验**：修改 `kernel64.ld`，把 `__idle_stack_start` 前移到 guard 页内（制造重叠），
   重新 `make`，观察 `ASSERT` 是否拦截；还原后再跑 `stackinfo` 确认区间。
5. **Linux 对照**：查阅 Linux `arch/x86/kernel/vmlinux.lds.S` 中 `init_thread_union` 的
   布局，对比 TinyOS 用 `ASSERT` 保护栈尺寸的做法，说明哪一层（链接期 vs 运行期）更早
   发现问题。

## 9. 本课小结与下一课预告

- 本课把三个静态内核栈（idle、`rsp0`、IST1）从「裸数组」升级为「链接器符号定义的三段
  区间（guard/payload/end）」，并为每个栈准备了高别名非 Present guard 页；`stack_guards_init`
  在开中断前完成 PTE 清零与 TLB 刷新。
- `rsp0` 与 IST1 现在指向 `__rsp0_stack_end`/`__ist1_stack_end`，所有显示点统一使用链接符号，
  消除了上一课「数组名 vs 实际地址」的不一致。
- 运行时映射窗口从「单低地址槽」升级为「低 `0x003ff000` + 高 `0xffffffff803ff000` 双别名
  同步槽」：`map_page`/`unmap_page` 在保存-IF 临界区内双写 PTE 并双 `invlpg`。
- `vmtest` 证明低写高读、高写低读双向一致，并校验 PMM 记账不丢页、不重复释放。
- 新增 `stackinfo` 与 `stackguardtest` 两条命令；`stackguardtest` 用确定性读触发 #PF，
  #PF 沿 IST1 报告后停机。
- 已知边界（延续旧 README 记录）：guard 只拦高别名向下越界，不拦高地址端破坏；
  `rsp0` 仍是预备值，需等 CPL3 进入后才会被硬件消费。
- **下一课**（[../lesson-26-stable/README.md](../lesson-26-stable/README.md)）：把 4 MiB
  身份映射与 PMM 账本同时扩展到 16 MiB——低/高各 8 张页表（`pt[8]`/`high_pt[8]` 数组化
  `long_mode_handoff`），双别名窗口移到 `0x00fff000`；本课的「双 PTE + 双 invlpg」契约原样
  继承，作为下一课 16 MiB 地平线之上的唯一动态窗口。

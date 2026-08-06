# Lesson 27: final-PT 16 槽双别名 map/unmap 注册表与 PMM 所有权 — 精讲文档

> **课号**：27　**主题**：bounded final-PT dual-alias mapping registry and ownership
> **课程主线位置**：阶段四（内核运行时基础设施）——在 Lesson 26（16 MiB PMM 地平线）之后、
> Lesson 28（首次 CPL3 进入）之前，把 Lesson 26 的「单一动态窗口」升级为固定的
> **16 槽双别名映射注册表**，并给 PMM 引入「映射帧所有权」账本。
> **前置课程**：[../lesson-26-stable/README.md](../lesson-26-stable/README.md)
> **后续课程**：[../lesson-28-stable/README.md](../lesson-28-stable/README.md)
> **一句话目标**：在 16 MiB 映射地平线内、沿用预分配的页表拓扑，实现一个 16 槽、可寻址、
> 低/高双别名同步、且能拒绝释放「被映射帧」的映射注册表。

> **Course status: stable snapshot (validated; canonical learning implementation promoted).**
> 本目录为已校验稳定快照（被提升为规范学习实现），`build/` 内含已验证构建产物。

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能把「单槽窗口」推广为「带所有权的 16 槽注册表」：
  理解 `vm_slot` 的地址→槽号换算、`vm_pair_ok` 的 PTE 对一致性校验、`vm_frame_owned`
  的帧所有权检查，以及为什么 `pfree` 必须拒绝释放仍在映射中的帧。

- **在课程主线中的位置**：上一课把窗口做成 16 MiB 地平线上唯一的动态槽；本课在
  `pt[7]`/`high_pt[7]`（final-PT）里预留 16 个初始非 Present 槽位
  （`0x00ff0000`–`0x00ffffff` 与高别名 `0xffffffff80ff0000`–`0xffffffff80ffffff`），
  用 `struct vm_mapping[16]` 记录每槽 owner，从而「一帧只能出现在一个槽、一个槽只能
  拥有一帧」。下一课（Lesson 28）将复用槽 0/1 的思想为 CPL3 用户代码/栈准备映射。

- **前置知识清单**：
  1. Lesson 25 的双 PTE / 双 `invlpg` 同步窗口契约；
  2. Lesson 26 的 `pt[8]`/`high_pt[8]` 数组页表与 `PAGE_TABLES_PER_ALIAS`；
  3. PMM bitmap 账本、`pmm_alloc`/`pmm_free_page`/`page_state`；
  4. `irq_save64`/`irq_restore64` 保存-IF 临界区惯例；
  5. `invlpg` 的 TLB 失效语义。

- **本课交付**（可见结果）：
  - `vminfo`（无参）显示区域 `slots live/total: <n> 0000000000000010`；
  - `vmap <low-va> <phys>` / `vunmap <low-va>` / `vminfo <low-va>` 可寻址操作；
  - `pfree <phys>` 对活映射帧输出 `cannot free: mapped`；
  - `vmtest` 在两槽上完成分配→映射→帧所有权拒绝→双向读写→解映射→释放→记账复原。

## 2. 核心概念精讲

### 2.1 映射注册表（registry）与槽（slot）

- **定义**：`struct vm_mapping { u64 phys; u8 live; }` 构成的 16 项数组 `vm_mappings[16]`。
  每项对应一个固定低虚拟地址槽：槽 `i` 的低 VA = `VM_REGION_START + i*PAGE_SIZE`。
- **为什么需要（动机）**：单槽窗口一次只能映射一帧，且调用方必须记住窗口地址。注册表让
  「哪个 VA 映射哪帧」成为可查询、可校验、可拒绝重复映射的状态，是地址空间管理器的雏形。
- **工作机制**：`vm_slot(va,&slot)` 把页对齐且落在 `[VM_REGION_START, VM_REGION_END)` 的
  低 VA 换算成槽号；`map_page` 在临界区内先做三项拒绝（槽已占、帧已占、PTE 对不一致），
  再双写 PTE 并更新 `vm_mappings[slot]`，最后双 `invlpg`。
- **示意图**：

```
VM_REGION_START 0x00ff0000                    VM_REGION_END 0x01000000
   │ 槽0   槽1   │ ...  槽15  │
   ├───────┬──────┼───────────┤      （低别名；高别名=+KERNEL_VMA_BASE）
   PTE:  pt[7][496] .. pt[7][511]
   registry: vm_mappings[0..15] = {phys, live}
```

### 2.2 PTE 对一致性校验（`vm_pair_ok`）

- 双别名同步窗口的稳健性依赖「两个 PTE 永远相等」；但运行期可能因 bug 或残留状态而
  破坏。`vm_pair_ok(h,slot)` 读取低/高两个 PTE：
  - 若两值不等 → 返回 0（拒绝任何操作，返回 `"inconsistent PTE pair"`）；
  - 若槽 `live` → 要求低 PTE 的帧号等于 `vm_mappings[slot].phys` 且 Present+Writable 位匹配；
  - 若槽空 → 要求两个 PTE 都为 0。
- 作用：任何 map/unmap 操作前先验证硬件页表与软件账本自洽，把「账本与页表漂移」变成
  可诊断的错误而不是静默踩踏。

### 2.3 PMM 页所有权账本

- 上一课 `pmm_free_page` 只拒绝 `vm_window_phys`（唯一窗口 owner）；本课推广为
  `vm_frame_owned(p)`：扫描 16 槽 registry，任一 `live` 槽的 `phys==p` 即拒绝
  （返回 `"mapped"`）。同时保留 worker 栈页拒绝（`"thread stack"`）。
- 意义：物理帧一旦被映射，其「分配状态」（bitmap 置位）由 PMM 记账；但「释放权」归属
  映射所有者。必须先 `vunmap` 解除映射，`pfree` 才允许归还帧——这正是
  「PMM 管分配、VM 管映射」的所有权分离。
- 本课未动：`page_state` 只按 bitmap/fixed 报 `free`/`fixed/reserved`/`allocated`，
  所有权细节在 `pmm_free_page` 层拒绝。

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|------|------|------------------------|
| `boot.S` | 32 位引导 | **未变化** |
| `kernel.c` | 32 位引导期页表 | **微小变化**：final-PT 预留 16 槽（`VM_REGION_FIRST_PTE` 起）双清 |
| `kernel64.c` | 64 位续体 | **主要增量**：`vm_mappings[16]` 注册表、`vm_slot`/`vm_pair_ok`/`vm_frame_owned`、多槽 map/unmap、`vminfo [va]`、两槽 `vmtest` |
| `kernel64.ld` | 64 位链接脚本 | **未变化** |
| `linker.ld` | 32 位 ELF | **未变化** |
| `Makefile` | 构建 | **未变化** |
| `grub.cfg` | GRUB 菜单 | **微小变化**：menuentry 文案 |

### 3.2 kernel.c：final-PT 16 槽双清

```c
#define VM_REGION_START 0x00ff0000ULL
#define VM_REGION_SLOTS 16U
#define VM_REGION_FIRST_PTE (PAGE_ENTRIES-VM_REGION_SLOTS)   /* = 496 */
...
for(j=VM_REGION_FIRST_PTE;j<PAGE_ENTRIES;j++){
    ((volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[PAGE_TABLES_PER_ALIAS-1])[j]=0;
    ((volatile u64 *)(unsigned long)(u32)long_mode_handoff.high_pt[PAGE_TABLES_PER_ALIAS-1])[j]=0;
}
```

- 上一课只清 final-PT 的 511 项；本课从 496 到 511 共 16 项全部清 0（低、高各一遍）。
  这 16 个 PTE 对应 VA `0x00ff0000`–`0x00ffffff`（高别名 `0xffffffff80ff0000`–`0xffffffff80ffffff`）。
- 为什么从 496 开始：`512 - 16 = 496`，`VM_REGION_FIRST_PTE` 常量与 64 位侧 `vm_pte_low/high`
  的 `VM_REGION_FIRST_PTE+slot` 取址完全一致，避免「引导期预留 16 槽、运行期却算错偏移」。

### 3.3 kernel64.c：注册表数据结构与常量

```c
#define VM_REGION_START 0x00ff0000ULL
#define VM_REGION_SLOTS 16U
#define VM_REGION_END (VM_REGION_START+VM_REGION_SLOTS*PAGE_SIZE)      /* 0x01000000 */
#define VM_REGION_HIGH_START (KERNEL_VMA_BASE+VM_REGION_START)
#define VM_REGION_FIRST_PTE (PAGE_ENTRIES-VM_REGION_SLOTS)
#define VM_REGION_PT_INDEX (PAGE_TABLES_PER_ALIAS-1)
#define PTE_FRAME_MASK 0x000ffffffffff000ULL
struct vm_mapping { u64 phys; u8 live; };
static struct vm_mapping vm_mappings[VM_REGION_SLOTS];
```

- `PTE_FRAME_MASK` 取出 PTE 的物理帧号位（低 12 位是标志位）；`vm_pair_ok` 用它比对
  页表帧号与注册表 owner。
- `vm_mappings` 是 64 位续体里的全局账本（raw binary 落在 `.data`），初始零 = 全空。

#### 函数：`vm_slot`

```c
static TEXT64 int vm_slot(u64 va,u32 *slot){
    if((va&(PAGE_SIZE-1))||va<VM_REGION_START||va>=VM_REGION_END)return 0;
    *slot=(u32)((va-VM_REGION_START)/PAGE_SIZE);
    return 1;
}
```

- 签名与职责：把低 VA 换算为槽号 0–15；失败返回 0。
- 算法步骤：(1) 非页对齐 → 拒绝；(2) 落在区域外 → 拒绝；(3) `(va-START)/PAGE_SIZE` 即槽号。
- 边界与错误处理：区域是半开区间 `[START, END)`，`END` 恰好是 16 MiB 边界；页对齐检查
  保证不会把「槽内偏移」误当新槽。
- 设计动机：VA→槽号是纯算术映射，无需哈希或查找表；且与 `VM_REGION_FIRST_PTE+slot`
  的 PTE 索引一一对应。

#### 函数：`vm_pair_ok`

```c
static TEXT64 int vm_pair_ok(struct long_mode_handoff*h,u32 slot){
    volatile u64*l=vm_pte_low(h,slot),*q=vm_pte_high(h,slot);
    u64 a=*l,b=*q;
    if(a!=b)return 0;
    if(vm_mappings[slot].live)
        return (a&PTE_FRAME_MASK)==vm_mappings[slot].phys&&(a&PTE_PRESENT_WRITABLE)==PTE_PRESENT_WRITABLE;
    return !a;
}
```

- 签名与职责：校验槽的「页表对 == 注册表」三元组自洽；返回 1 才允许 map/unmap。
- 算法步骤：(1) 低/高 PTE 不等 → 不通过；(2) live 槽要求帧号与标志位精确匹配注册表；
  (3) 空槽要求两 PTE 均为 0。
- 边界与错误处理：`a!=b` 在最前，短路后续判断；live 槽的 Present/Writable 位被逐位检查，
  防止「live 但 PTE 被意外清掉」的半死状态继续被误操作。
- 设计动机：把「软件账本」与「硬件页表」的漂移检测前置到每次操作入口，本课所有失败
  返回串（`inconsistent PTE pair`）都源自此函数。

#### 函数：`vm_frame_owned`

```c
static TEXT64 int vm_frame_owned(u64 p){
    u32 i;
    for(i=0;i<VM_REGION_SLOTS;i++)
        if(vm_mappings[i].live&&vm_mappings[i].phys==p)return 1;
    return 0;
}
```

- 签名与职责：判断物理帧 `p` 是否被任一活映射槽占有。
- 算法步骤：线性扫描 16 槽，`live && phys==p` 即命中。
- 边界与错误处理：本课仅 16 槽，线性扫描代价可忽略；后续课程（Lesson 28+）会追加
  `user_code_phys`/`user_stack_phys` 两个「非注册表」保留帧判断。
- 设计动机：`pmm_free_page` 用它返回 `"mapped"`，实现「映射帧不可释放」的所有权规则。

#### 函数：`map_page` / `unmap_page`（多槽版本）

```c
static TEXT64 const char *map_page(struct long_mode_handoff*h,u64 va,u64 p){
    volatile u64*l,*q;u64 flags;u32 slot;const char*s;
    if(!vm_slot(va,&slot))return "VA outside mapping region";   /* 地址合法性 */
    s=page_state(p);if(!eq64(s,"allocated"))return s;           /* 只映射已分配帧 */
    flags=irq_save64();l=vm_pte_low(h,slot);q=vm_pte_high(h,slot);
    if(!vm_pair_ok(h,slot)){irq_restore64(flags);return "inconsistent PTE pair";}
    if(vm_mappings[slot].live||*l){irq_restore64(flags);return "slot already mapped";}
    if(vm_frame_owned(p)){irq_restore64(flags);return "frame already mapped";}
    *l=*q=p|PTE_PRESENT_WRITABLE;      /* 双写 PTE */
    vm_mappings[slot].phys=p;vm_mappings[slot].live=1;  /* 登记注册表 */
    invlpg64(va);invlpg64(KERNEL_VMA_BASE+va);          /* 双 invlpg */
    irq_restore64(flags);
    return "mapped";
}
static TEXT64 const char *unmap_page(struct long_mode_handoff*h,u64 va){
    volatile u64*l,*q;u64 flags;u32 slot;
    if(!vm_slot(va,&slot))return "VA outside mapping region";
    flags=irq_save64();l=vm_pte_low(h,slot);q=vm_pte_high(h,slot);
    if(!vm_pair_ok(h,slot)){irq_restore64(flags);return "inconsistent PTE pair";}
    if(!vm_mappings[slot].live){irq_restore64(flags);return "slot already unmapped";}
    *l=*q=0;vm_mappings[slot].phys=0;vm_mappings[slot].live=0;
    invlpg64(va);invlpg64(KERNEL_VMA_BASE+va);
    irq_restore64(flags);
    return "unmapped";
}
```

- 签名与职责：`map_page(h,va,p)` 把已分配帧 `p` 映射进低 VA `va` 的槽；`unmap_page(h,va)`
  撤销该槽。返回结果串供 shell 打印。
- 算法步骤（map）：`vm_slot` 校验地址 → `page_state` 校验帧 → 关中断 → `vm_pair_ok`
  → 槽空检查 → 帧唯一检查 → 双写 PTE → 更新注册表 → 双 `invlpg` → 恢复 IF。
- 算法步骤（unmap）：`vm_slot` → 关中断 → `vm_pair_ok` → 槽 live 检查 → 双清 PTE →
  清注册表 → 双 `invlpg` → 恢复 IF。
- 边界与错误处理：五个错误返回串（区域外 / 非 allocated / 不一致对 / 槽已占 / 帧已占 /
  槽已空）区分了所有失败语义；临界区保证「检查 + 写 PTE + 写注册表」对中断原子。
- 设计动机：与 Lesson 25/26 的单槽版相比，新增「槽地址参数化」与「帧唯一性」两件事；
  双 PTE 双 `invlpg` 契约不变。

#### 函数：`vminfo`（带可选 VA）与 `vmtest`（两槽）

```c
static TEXT64 void vminfo(u16*c,struct long_mode_handoff*h,u64 va,int one){
    u32 i,n=0,slot;
    if(one&&!vm_slot(va,&slot)){text64(c,"VM VA outside mapping region\n");return;}
    for(i=0;i<VM_REGION_SLOTS;i++)if(vm_mappings[i].live)n++;
    text64(c,"VM region low/high: ");hex64(c,VM_REGION_START);text64(c," ");hex64(c,VM_REGION_HIGH_START);
    text64(c,"\nslots live/total: ");hex64(c,n);text64(c," ");hex64(c,VM_REGION_SLOTS);
    if(one){
        text64(c,"\nVA low/high: ");hex64(c,va);text64(c," ");hex64(c,KERNEL_VMA_BASE+va);
        text64(c,"\nstate/owner: ");text64(c,vm_mappings[slot].live?"mapped ":"unmapped ");hex64(c,vm_mappings[slot].phys);
        text64(c,"\nPTE low/high: ");hex64(c,*vm_pte_low(h,slot));text64(c," ");hex64(c,*vm_pte_high(h,slot));
    }
    putc64(c,'\n');
}
```

- 无参模式统计 `slots live/total`；带参模式打印该槽 owner 与两个 PTE。`vmtest` 用两个不同
  槽（`va[2]={VM_REGION_START, VM_REGION_START+PAGE_SIZE}`），在两槽上分别做
  「分配→映射→`pmm_free_page` 必须返回 `"mapped"`→低写高读→高写低读→解映射→`pmm_free_page`
  返回 `"freed"`」，最后校验 `pmm_free` 复原，输出

```
vmtest: two-slot dual-alias map/ownership/unmap/free passed
```

- 其测试魔数 `0x564d544553543237ULL+i`（"VMTEST27"+i）与 `0x48494748564d3237ULL+i`
  （"HIGHVM27"+i）沿用了 Lesson 25 的可读标记风格。

### 3.4 shell 分支与 banner

- `exec64` 本课把 `vmap`/`vunmap`/`vminfo` 改成带参数形式：

```c
else if(eq64(word,"vmap")){...usage64(c,"vmap <low-va> <phys>");...}
else if(eq64(word,"vunmap")){...usage64(c,"vunmap <low-va>");...}
else if(eq64(word,"vminfo")){if(noargs64(arg))vminfo(c,h,0,0);else if(hexarg64(arg,&p))vminfo(c,h,p,1);else usage64(c,"vminfo [low-va]");}
```

- `isttest` 与 `vmfaulttest` 的触发地址改为 `VM_REGION_START`（首个非 Present 槽）。
- banner（源码逐字）：

```
TinyOS lesson 27: bounded dual-alias mapping registry
Kernel-only preparation; IRQ0 return-frame switching retained
```

### 3.5 构建管线与主控制流

- `Makefile`/`kernel64.ld`/`linker.ld` 未变化；构建命令与上两课相同。
- 主控制流不变：`pmm_init → stack_guards_init → runtime_gdt_tss_init → idle_init →
  install_idt → pit_init → pic_init → banner → shell`；新命令全部经 `exec64` 进入。

## 4. 数据流与运行逻辑

- 命令 `vmap 0xff0000 <phys>`：`exec64` 解析两个十六进制参数 → `map_page(h,0xff0000,p)`
  → `vm_slot` 得槽 0 → 三项拒绝检查 → 写 `pt[7][496]`/`high_pt[7][496]` →
  `vm_mappings[0]={p,1}` → 打印 `mapped: <p> at 0000000000ff0000`。
- `vminfo 0xff0000`：`vminfo(c,h,p,1)` → 打印 `state/owner: mapped <p>` 与两个 PTE。
- `pfree <p>`：`pmm_free_page` → `page_state` 报 `allocated` → `vm_frame_owned(p)` 命中
  → 打印 `cannot free: mapped`。
- `vunmap 0xff0000` → `unmap_page` 双清 PTE 与注册表 → `pfree <p>` 现在返回 `freed`。
- `vmtest`：两槽全流程回归，`pmm_free` 前后相等才打印通过。

## 5. 构建、运行与验证

- **构建命令**（与 Makefile 一致）：

```bash
cd /home/dongyu/.zcode/workspace/default/lessons/lesson-27-stable
make clean && make -j"$(nproc)"
make check
```

- **运行命令**：`make run`（QEMU VGA 图形窗口，勿加 `-display none`）。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. 启动后 `vminfo`：`VM region low/high: 0000000000ff0000 ffffffff80ff0000`、
     `slots live/total: 0000000000000000 0000000000000010`。
  2. `palloc` 记下帧，`vmap 0xff0000 <帧>`：预期 `mapped: <帧> at 0000000000ff0000`；
     `vminfo 0xff0000` 显示 `state/owner: mapped <帧>` 与两组相同 PTE。
  3. `pfree <帧>`：预期 `cannot free: mapped`（所有权拒绝）。
  4. `vunmap 0xff0000`：预期 `unmapped`；再 `pfree <帧>`：预期 `freed`。
  5. `vmtest`：预期 `vmtest: two-slot dual-alias map/ownership/unmap/free passed`。
  6. `vmfaulttest` 访问 `VM_REGION_START` 非 Present 槽，**仅在全新启动**下执行，预期
     #PF 报告（IST1）后停机。
  7. 回归 `lminfo`、`hhinfo`、`meminfo`、`tssinfo`、`stackinfo`、`preempttest`、`idletest`、
     `pctest`/`pcgo`、键盘、`bptest`；`isttest`/`udtest`/`pftest`/guard 测试各自新启动。
- **判断成功**：干净构建 + `make check`；`vminfo` 计数正确、`pfree` 拒绝 `mapped`、
  `vmtest` 通过、`vminfo`/`hhinfo`/`meminfo` 边界一致。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `vmap` 报 `slot already mapped` | 该槽已 live（registry 或 PTE 残留） | `vminfo <va>` 看 state/owner 与两个 PTE；确认上一轮 `vunmap` 成功 |
| `vmap` 报 `frame already mapped` | 目标帧已属另一槽 | `vminfo` 无参看 live 数；逐槽 `vminfo <va>` 比对 `phys` |
| `vmap`/`vunmap` 报 `inconsistent PTE pair` | 低/高 PTE 不等，或与注册表不符 | 直接读 `vm_pte_low/high` 两组值；检查是否有绕过 `map_page` 的写 PTE 路径 |
| `pfree` 应 `freed` 却 `cannot free: mapped` | `vunmap` 未执行或未清注册表 | 确认 `vunmap` 返回 `unmapped`；`vminfo <va>` 显示 `unmapped 0000000000000000` |
| `vmtest` 报 `mapped-frame ownership failed` | `pmm_free_page` 的 `vm_frame_owned` 未命中 | 检查 `vm_mappings[slot].live` 是否已置 1；`vm_frame_owned` 是否被调用 |
| `vminfo` 显示的 PTE 槽位偏移错误 | 引导期预留与运行期 `VM_REGION_FIRST_PTE` 不一致 | 对拍 `kernel.c` 与 `kernel64.c` 两处 `VM_REGION_FIRST_PTE=496` |
| 访问 `0x00ff0000` 未 #PF | 引导期双清没执行（残留 identity PTE） | 检查 `kernel.c` 的 496–511 清 0 循环；启动后 `vminfo` 看 PTE 是否 0 |

## 7. 与 Linux 源码对照

- **TinyOS**：16 槽注册表 + 每槽 owner + `vm_pair_ok` 一致性校验 + `pfree` 所有权拒绝；
  映射与分配分离，映射不释放帧、释放先解映射。
- **Linux 对照**：`mm/vmalloc.c` 的 vmalloc 区域用 `vmap_area` 红黑树 + `vm_struct` 记录
  每段映射的 owner（`addr`/`size`/`pages`）；`unmap_vmap_area` 先解除映射再允许释放；
  `mm/memory.c` 的 `follow_page`/`pte_offset_map` 做页表一致遍历。TinyOS 的
  `vm_pair_ok` 与 `vm_frame_owned` 对应 Linux 的「PTE 与 rmap/vm_area 同步」思想，
  只是用固定 16 槽代替树结构。
- **权威来源**：Intel SDM Vol.3 §4.10.4（页面错误条件）、§4.10.1（Present 位与 TLB）；
  GNU ld 手册（位置计数器与 `ASSERT`）。
- **教学模型简化**：槽数固定 16、不动态分配页表、不引入地址空间对象；映射一律 RW，
  无 NX/COW 等标志位。

## 8. 思考题与练习

1. **概念理解**：为什么 `vunmap` 只解除映射、绝不调用 `pmm_free_page`？如果它顺手释放了
  物理帧，`pfree` 的所有权语义会怎样被破坏？
2. **源码定位**：列出 `map_page` 的全部失败返回串，并说明 `vm_pair_ok` 在哪些情形下
  能捕捉到「注册表说 live 但 PTE 被清零」的漂移。
3. **动手实验**：把 `VM_REGION_SLOTS` 改成 8（同时改 `kernel.c` 的清零循环），重新构建
  运行 `vminfo`，观察 `slots live/total` 分母变化，并说明高槽地址的 PTE 取址是否会越界。
4. **动手实验**：在 `unmap_page` 中故意跳过 `vm_mappings[slot].live=0`，运行
   `vunmap`→`vmap`，观察 `vm_pair_ok` 或 `slot already mapped` 如何暴露账本残留。
5. **Linux 对照**：阅读 `mm/vmalloc.c` 的 `vmap_area`/`vm_struct` 关系，对比 TinyOS
  的 `vm_mappings` 注册表，总结两者在「释放前必须解除映射」上的共同不变量。

## 9. 本课小结与下一课预告

- 本课把单槽窗口升级为 final-PT 预留的 **16 槽双别名映射注册表**：低
  `0x00ff0000`–`0x00ffffff`，高 `0xffffffff80ff0000`–`0xffffffff80ffffff`。
- `vm_slot` 做 VA→槽号算术换算；`vm_pair_ok` 校验低/高 PTE 与注册表三元组自洽；
  `vm_frame_owned` 实现「映射帧不可释放」的 PMM 所有权规则。
- `vmap <low-va> <phys>` / `vunmap <low-va>` / `vminfo [low-va]` 提供可寻址操作；
  所有 map/unmap 在保存-IF 临界区内双写 PTE、更新注册表、双 `invlpg`。
- `vmtest` 在两槽上证明 分配→映射→所有权拒绝→双向读写→解映射→释放→记账复原 全链路。
- 本课不分配页表、不映射区域外 RAM、不改三 TCB 调度、TSS/`rsp0`/IST1、guard 栈与 IRQ。
- 已知边界（延续旧 README 记录）：注册表槽 0/1 本课仍可映射，但已被设计为留给下一课
  的 CPL3 用户代码/栈页；PMM 只跟踪 16 MiB 地平线。
- **下一课**（[../lesson-28-stable/README.md](../lesson-28-stable/README.md)）：首次进入
  **CPL3**——用 GDT 用户段 `USER_DS=0x2b`/`USER_CS=0x33`、用户代码/栈页的 PTE_USER 位与
  `iretq` 构造用户帧，用户 `ud2` 异常经 TSS `rsp0` 回到内核，证明「用户 CS + 内核 rsp0
  异常栈」这一 CPL3 进入路径。

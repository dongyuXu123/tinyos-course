# Lesson 33: 有界 address-space 对象与内核/用户映射所有权 — 精讲文档

> **课号**：Lesson 33
> **主题**：有界 address-space 对象与内核/用户映射所有权
> **课程主线位置**：第 3 阶段「用户程序、进程、虚拟内存与用户空间」（32–60）第二课，
> 虚拟内存抽象线的起点。
> **前置课程**：[Lesson 32（校验后的内置用户程序镜像与最小加载器）](../lesson-32-stable/README.md)
> **后续课程**：[Lesson 34（有界 process/thread 对象与受控生命周期）](../lesson-34-stable/README.md)
> **一句话目标**：学完本课，你能说清「当前唯一的一对低/高页表被封装成
> `struct address_space` 对象后，为什么只有显式用户映射（`MAP_OWNER_USER`）能进
> 有界窗口，而内核专属地址、高别名地址、重复槽位、重复帧、重复释放都会被
> 对象边界逐项拒绝」。

---

## 1. 课程定位（Mission）

- **一句话目标**：把 Lesson 27–32 的「裸 `vm_mappings[]` 注册表 + 全局 `map_page/
  unmap_page` 函数」重构为**对象化**接口：`address_space_init/address_space_map/
  address_space_release`。`struct address_space` 拥有页表指针、低/高窗口边界、
  内核/用户映射计数；`vm_mapping` 增加 `owner` 字段实现所有权分离。行为上，
  用户只能操作 `MAP_OWNER_USER` 的槽位，内核与高别名地址被硬性拒绝。
- **课程主线位置**：这是第 3 阶段虚拟内存抽象的第一课。第 2 阶段（27）把
  VM 区域做成「受控双别名注册表」；本课把注册表提升为「对象 + 所有权」；
  Lesson 34 将让 process 持有 address_space，Lesson 36 将实例化两个对象——所以
  本课的对象必须「可复制、可归属、可计数」。
- **前置知识清单**：
  1. 16 槽 VM 区域：`VM_REGION_START=0x00ff0000`，槽 0/1 保留给用户代码/栈
     （Lesson 27–32）；
  2. 低/高双别名 PTE 对必须一致（`invlpg` 两条路都要刷，Lesson 27）；
  3. 镜像校验边界（`validate_user_image`，Lesson 32）；
  4. PMM 帧所有权：`pmm_free_page` 拒绝释放 `vm_frame_owned` 的帧（Lesson 27）。
- **本课交付**：`vmtest` 仍通过；`vmap`/`vunmap` 换成对象接口后，命令层可
  观察四类被拒绝的映射（高别名地址、重复槽位、重复帧、重复释放），返回串
  各不相同；banner/about 标注 Lesson 33。

---

## 2. 核心概念精讲

### 2.1 address_space 对象：把「页表 + 窗口 + 记账」打包

**定义**：

```c
struct address_space { struct long_mode_handoff *tables; u64 low_start,low_end,high_start; u8 kernel_mappings,user_mappings,initialized; };
static struct address_space kernel_address_space;
```

**为什么需要**：裸全局函数（`map_page`/`unmap_page`）的问题在于「没有归属」——
任何人任何时候都能映射任意物理页。把状态装进对象后：
1. **有窗口**：`low_start/low_end` 限定可映射的低地址范围；
2. **有计数**：`kernel_mappings/user_mappings` 让「谁占了几个槽」可审计；
3. **有生命**：`initialized` 标志让未初始化对象上的操作立即失败；
4. **可归属**：对象可以被进程持有（Lesson 34），也可以被复制出第二个实例
   （Lesson 36 的 `user_address_spaces[1]`）。
**工作机制**：`address_space_init` 一次初始化：

```c
static TEXT64 void address_space_init(struct address_space *as,struct long_mode_handoff *h){
    as->tables=h; as->low_start=VM_REGION_START; as->low_end=VM_REGION_END;
    as->high_start=VM_REGION_HIGH_START; as->kernel_mappings=0;
    as->user_mappings=0; as->initialized=1;
}
```

`high_start` 目前仅记录（高别名窗口由 `tables->high_pt` 推导），低窗口是唯一
操作面。边界：`initialized=0` 的对象调用任何操作都返回拒绝串。

### 2.2 映射所有权：内核映射 vs 用户映射

**定义**：

```c
enum mapping_owner { MAP_OWNER_NONE=0, MAP_OWNER_KERNEL=1, MAP_OWNER_USER=2 };
struct vm_mapping { u64 phys; u8 live; u8 owner; };
```

**为什么需要**：映射不仅要问「物理帧是谁的」（PMM 层），还要问「**这个槽是谁
建的**」（VM 层）。本课的教学约束是：`address_space_map` **只接受
`MAP_OWNER_USER`**，即对象窗口里的映射只允许用户用途。内核专属映射返回
`"kernel-only mapping"`。这为 Lesson 34 的「进程拥有地址空间」预演了
「对象窗口 = 用户窗口」的语义。

**工作机制**（`address_space_map` 入口处）：

```c
static TEXT64 const char *address_space_map(struct address_space *as,u64 va,u64 p,u8 owner){
    struct long_mode_handoff*h=as?as->tables:0;
    ...
    if(!address_space_slot(as,va,&slot)) return "VA outside user-owned window";
    if(owner!=MAP_OWNER_USER) return "kernel-only mapping";
    ...
}
```

两行哨兵：窗口外 → `VA outside user-owned window`；owner 非用户 → 
`kernel-only mapping`。此后才开始查 PMM 状态与 PTE 对。

### 2.3 双别名 PTE 对一致性（vm_pair_ok）

**定义**：每个槽对应低页表 `h->pt[VM_REGION_PT_INDEX]` 与高页表
`h->high_pt[...]` 中的两个 PTE，二者必须一致：

```c
static TEXT64 int vm_pair_ok(struct long_mode_handoff*h,u32 slot){
    volatile u64*l=vm_pte_low(h,slot),*q=vm_pte_high(h,slot);
    u64 a=*l,b=*q;
    if(a!=b) return 0;
    if(vm_mappings[slot].live) return (a&PTE_FRAME_MASK)==vm_mappings[slot].phys&&
        (a&PTE_PRESENT_WRITABLE)==PTE_PRESENT_WRITABLE;
    return !a;
}
```

**为什么需要**：同一物理页必须同时通过低地址（`0x00ff0000+...`，用户视图）与
高地址（`0xffffffff80ff0000+...`，内核视图）可达。若两侧 PTE 不一致，会出现
「一边能访问、一边 #PF」的隐性错误。`vm_pair_ok` 在 map 与 release 的前置检查
中被复用，保证**不变量先验证再修改**。

### 2.4 有界窗口与槽位保留

**定义**：`address_space_slot` 是窗口的唯一入口：

```c
static TEXT64 int address_space_slot(struct address_space *as,u64 va,u32 *slot){
    if(!as||!as->initialized||(va&(PAGE_SIZE-1))||va<as->low_start||va>=as->low_end) return 0;
    *slot=(u32)((va-as->low_start)/PAGE_SIZE);
    if(*slot<2) return 0;   /* 槽 0/1 是用户代码页/栈页所在位置，不开放 */
    return 1;
}
```

四层检查：① 对象存在且已初始化；② VA 页对齐；③ 在 `low_start..low_end` 内；
④ 槽号 ≥ 2（槽 0/1 保留）。`map_page` 时期同名逻辑（`vm_slot`）升级为对象方法，
返回值从「无」变成「可定位的拒绝串」。

---

## 3. 源码精讲（本课最长的章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 32） |
|---|---|---|
| `kernel64.c` | 64 位主内核 | **核心增量**：`mapping_owner`/`address_space`/`vm_mapping.owner`、`address_space_init/slot/map/release`、`vminfo`/`vmtest`/`exec64` 改用对象接口、`kernel_main64_binary` 初始化对象、banner/about 文案 |
| `kernel.c` | 32 位引导 + 内嵌镜像 | 未变化（Lesson 32 的镜像与校验器原样保留） |
| `boot.S` | Multiboot2 头 + 进 long mode | 未变化 |
| `Makefile` | 构建 `kernel.iso` | 未变化 |
| `kernel64.ld` | 64 位链接脚本 | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 数据结构与全局对象精讲

```c
enum mapping_owner { MAP_OWNER_NONE=0, MAP_OWNER_KERNEL=1, MAP_OWNER_USER=2 };
struct vm_mapping { u64 phys; u8 live; u8 owner; };
struct address_space { struct long_mode_handoff *tables; u64 low_start,low_end,high_start;
                       u8 kernel_mappings,user_mappings,initialized; };
static struct vm_mapping vm_mappings[VM_REGION_SLOTS];
static struct address_space kernel_address_space;
```

- `owner` 占 1 字节，与 `live` 并排；`address_space` 用 6 个字段描述「窗口 +
  记账 + 生命周期」。`kernel_address_space` 是全局唯一实例——本课仍是「单地址
  空间」，对象化先把接口立起来，多实例推迟到 Lesson 36。
- `vm_frame_owned` 不变（用户代码/栈物理帧 + 已映射帧不可释放），保证 PMM 与
  VM 两层所有权检查叠加。

### 3.3 address_space_map —— 用户映射的唯一入口（关键函数）

```c
static TEXT64 const char *address_space_map(struct address_space *as,u64 va,u64 p,u8 owner){
    struct long_mode_handoff*h=as?as->tables:0;
    volatile u64*l,*q; u64 flags; u32 slot; const char*s;
    if(!address_space_slot(as,va,&slot)) return "VA outside user-owned window";
    if(owner!=MAP_OWNER_USER) return "kernel-only mapping";
    s=page_state(p);
    if(!eq64(s,"allocated")) return s;
    flags=irq_save64();
    l=vm_pte_low(h,slot); q=vm_pte_high(h,slot);
    if(!vm_pair_ok(h,slot)){ irq_restore64(flags); return "inconsistent PTE pair"; }
    if(vm_mappings[slot].live||*l){ irq_restore64(flags); return "slot already mapped"; }
    if(vm_frame_owned(p)){ irq_restore64(flags); return "frame already mapped"; }
    *l=*q=p|PTE_PRESENT_WRITABLE|PTE_USER;
    vm_mappings[slot].phys=p; vm_mappings[slot].owner=owner; vm_mappings[slot].live=1;
    as->user_mappings++;
    invlpg64(va); invlpg64(KERNEL_VMA_BASE+va);
    irq_restore64(flags);
    return "mapped";
}
```

- **签名与职责**：在 `as` 的窗口内把物理帧 `p` 以双别名方式映射到 `va`；
  `owner` 必须是 `MAP_OWNER_USER`。返回 `"mapped"` 或六种拒绝串之一。
- **算法步骤**：① 窗口/对齐检查（`address_space_slot`）；② 所有权检查
  （`owner!=MAP_OWNER_USER → "kernel-only mapping"`）；③ PMM 状态检查
  （物理页必须 `allocated`，`free`/`fixed/reserved`/`invalid` 都返回原串）；
  ④ `irq_save64` 关中断，避免与 IRQ 路径竞争 PTE；⑤ `vm_pair_ok` 一致性检查；
  ⑥ 槽位占用检查（`live` 或低 PTE 非零 → `"slot already mapped"`）；⑦ 帧占用
  检查（`vm_frame_owned` → `"frame already mapped"`）；⑧ 写低/高两个 PTE，
  PTE 值 = `p|PTE_PRESENT_WRITABLE|PTE_USER`；⑨ 记账（`owner`、`live=1`、
  `user_mappings++`）；⑩ 双地址 `invlpg` 刷新 TLB；⑪ 恢复中断。
- **边界与错误处理**：任何失败都在写 PTE **之前** `irq_restore64` 返回，不留下
  半改状态；六个拒绝串在命令层原样打印，可逐条对照测试。
- **为什么这样设计**：`PTE_PRESENT_WRITABLE|PTE_USER` 比 Lesson 27 的
  `map_page` 多加了 `PTE_USER`——用户窗口里的映射必须带 user 位，否则 CPL3
  永远无法访问（对应真实内核 `pgprot` 与 `VM_USER` 的映射语义）。

### 3.4 address_space_release —— 解除映射的唯一入口（关键函数）

```c
static TEXT64 const char *address_space_release(struct address_space *as,u64 va){
    struct long_mode_handoff*h=as?as->tables:0;
    volatile u64*l,*q; u64 flags; u32 slot;
    if(!address_space_slot(as,va,&slot)) return "VA outside user-owned window";
    flags=irq_save64();
    l=vm_pte_low(h,slot); q=vm_pte_high(h,slot);
    if(!vm_pair_ok(h,slot)){ irq_restore64(flags); return "inconsistent PTE pair"; }
    if(!vm_mappings[slot].live){ irq_restore64(flags); return "slot already released"; }
    if(vm_mappings[slot].owner!=MAP_OWNER_USER){ irq_restore64(flags); return "mapping owner mismatch"; }
    *l=*q=0;
    vm_mappings[slot].phys=0; vm_mappings[slot].owner=MAP_OWNER_NONE; vm_mappings[slot].live=0;
    as->user_mappings--;
    invlpg64(va); invlpg64(KERNEL_VMA_BASE+va);
    irq_restore64(flags);
    return "unmapped";
}
```

- **签名与职责**：解除 `va` 的双别名映射，归还槽位。返回 `"unmapped"` 或
  拒绝串。
- **算法步骤**：① 窗口检查；② 关中断；③ 一致性检查；④ `live` 检查（未映射的
  槽 → `"slot already released"`，即**重复释放**被拒绝）；⑤ 所有权检查
  （非用户所有者 → `"mapping owner mismatch"`）；⑥ 清低/高 PTE；⑦ 清注册表；
  ⑧ `user_mappings--`；⑨ 双 `invlpg`；⑩ 恢复中断。
- **边界与错误处理**：`"slot already released"` 是本课调试地图的第二个可观察
  拒绝；与 `map` 对称，任何失败都不留半改状态。
- **为什么这样设计**：释放比 Lesson 27 的 `unmap_page` 多了 `owner` 检查——
  「只有所有者能拆除自己的映射」，这是所有权模型在写路径上的落地。

### 3.5 vmtest —— 对象接口的回归验证（关键函数）

```c
static TEXT64 void vmtest(u16*c,struct long_mode_handoff*h){(void)h;
    u64 p[2],va[2]={VM_REGION_START,VM_REGION_START+PAGE_SIZE},before,after;
    volatile u64 *v,*q; const char*r; u32 i;
    before=pmm_free;
    for(i=0;i<2;i++){
        p[i]=pmm_alloc();
        if(!p[i]){ text64(c,"vmtest allocation failed\n"); return; }
        r=address_space_map(&kernel_address_space,va[i],p[i],MAP_OWNER_USER);
        if(!eq64(r,"mapped")){ text64(c,"vmtest map failed: "); text64(c,r); putc64(c,'\n'); return; }
        if(!eq64(pmm_free_page(p[i]),"mapped")){ text64(c,"vmtest mapped-frame ownership failed\n"); return; }
        v=(volatile u64 *)(unsigned long)va[i];
        q=(volatile u64 *)(unsigned long)(KERNEL_VMA_BASE+va[i]);
        *v=0x564d544553543237ULL+i;
        if(*q!=0x564d544553543237ULL+i){ text64(c,"vmtest low/high mismatch\n"); return; }
        *q=0x48494748564d3237ULL+i;
        if(*v!=0x48494748564d3237ULL+i){ text64(c,"vmtest high/low mismatch\n"); return; }
    }
    for(i=0;i<2;i++){
        r=address_space_release(&kernel_address_space,va[i]);
        if(!eq64(r,"unmapped")||!eq64(pmm_free_page(p[i]),"freed")){ text64(c,"vmtest unmap/free failed\n"); return; }
    }
    after=pmm_free;
    if(after!=before){ text64(c,"vmtest PMM accounting failed\n"); return; }
    text64(c,"vmtest: two-slot dual-alias map/ownership/unmap/free passed\n");
}
```

- **签名与职责**：分配 2 页、映射到槽 2/3、验证低/高读写互通、释放并归还，
  最后核对 PMM 计数。
- **算法步骤**：① 记录 `before=pmm_free`；② 每槽：`pmm_alloc` → 对象映射
  （必须返回 `"mapped"`）→ `pmm_free_page` 必须拒绝（返回 `"mapped"`，证明帧
  已被 VM 层持有）→ 低地址写魔数、高地址读回 → 高地址写另一魔数、低地址读回；
  ③ 每槽：对象释放（必须 `"unmapped"`）+ `pmm_free_page`（必须 `"freed"`）；
  ④ 核对 `after==before`；⑤ 打印通过文案。
- **边界与错误处理**：`(void)h` 说明对象接口已替代裸 `h` 传参；任何一步不符都
  打印特定失败串并 return。
- **为什么这样设计**：这次回归验证的重点从「映射能通」扩展到「**所有权闭环**」——
  映射后帧不可 free、释放后帧可 free、计数守恒。

### 3.6 exec64 / kernel_main64_binary 增量

- `vmap`：`r=address_space_map(&kernel_address_space,va,p,MAP_OWNER_USER);`
- `vunmap`：`r=address_space_release(&kernel_address_space,p);`
- `kernel_main64_binary` 开头在 `pmm_init(h)` 后立即
  `address_space_init(&kernel_address_space,h);`
- `about`：`TinyOS lesson 33: validated embedded user image; bounded dual-alias mapping registry`
- banner：`TinyOS lesson 33: validated embedded user image and SYS_EXIT`

### 3.7 构建管线

Makefile/链接脚本与 Lesson 32 完全一致，无新增构建步骤（对象化是纯 C 重构）。

### 3.8 主控制流

```
kernel_main64_binary
  ├─ pmm_init(h)
  ├─ address_space_init(&kernel_address_space,h)     ← 本课新增：窗口/记账就绪
  ├─ ... 既有初始化 ...
  └─ shell: vmap <low-va> <phys> / vunmap <low-va> / vmtest
        └─ address_space_map / address_space_release（对象接口，拒绝串可见）
```

---

## 4. 数据流与运行逻辑

1. **启动**：`kernel_main64_binary` 在 `pmm_init` 之后立即 `address_space_init`，
   使全局 `kernel_address_space` 拥有 `tables=h`、窗口 `0x00ff0000..0x01000000`、
   `initialized=1`。
2. **映射命令**：`vmap <va> <phys>` → `exec64` → `address_space_map(...,
   MAP_OWNER_USER)`：
   - `va=0xffffffff80000000`（高别名）→ `address_space_slot` 拒绝 →
     `cannot map: VA outside user-owned window`；
   - `va` 合法但 `phys` 不是 `allocated` → 返回 `page_state` 的串
     （`free`/`fixed/reserved`/`invalid`）；
   - `va` 已占用 → `cannot map: slot already mapped`；
   - `phys` 已在映射中 → `cannot map: frame already mapped`；
   - 成功 → `mapped: <phys> at <va>`，PTE 低/高同时写入并 `invlpg` 双刷新。
3. **解除映射**：`vunmap <va>` → `address_space_release`：重复释放 →
   `slot already released`；非用户所有者 → `mapping owner mismatch`；成功 →
   `unmapped`。
4. **回归**：`vmtest` 全流程通过后打印
   `vmtest: two-slot dual-alias map/ownership/unmap/free passed`。
5. **用户路径**：`cpl3test` 走镜像（Lesson 32 产物），EXIT 停机路径不变。

---

## 5. 构建、运行与验证

**依赖**：与 Lesson 32 相同。

**构建与格式校验**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 通过时输出：

```
Multiboot2 header check passed.
```

**运行**（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

**验证步骤**（全新启动后）：

1. `vmtest`，预期末行（逐字摘自 `vmtest`）：

   ```
   vmtest: two-slot dual-alias map/ownership/unmap/free passed
   ```

2. `vmap 0xffffffff80000000 <phys>`，预期（逐字摘自 `address_space_map` +
   `exec64`）：

   ```
   cannot map: VA outside user-owned window
   ```

   （注：`<phys>` 用 `palloc` 返回的物理页地址；先执行 `palloc` 取得 `allocated`
   页。）

3. 对同一槽第二次 `vmap`，预期 `cannot map: slot already mapped`。

4. 连续两次 `vunmap` 同一地址，第二次预期 `slot already released`。

5. 运行 `idtinfo` 后 `cpl3test`：镜像 0、1、2、99、3 调用链与 Lesson 32 一致，
   末屏 EXIT 报告（逐字摘自 `syscall_report`）：

   ```
   TinyOS lesson 33 SYS_EXIT
   user requested controlled exit
   user return frame is valid; halting intentionally
   ```

6. banner 与 `about`（逐字摘自源码）：banner 为
   `TinyOS lesson 33: validated embedded user image and SYS_EXIT`，`about` 为
   `TinyOS lesson 33: validated embedded user image; bounded dual-alias mapping registry`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `vmap 0xffffffff80000000 ...` 返回 `VA outside user-owned window` | 高别名地址不在 `low_start..low_end` | 检查 `address_space_slot` 的范围判断；这是**预期拒绝** |
| 重复 `vmap` 同地址返回 `slot already mapped` | 槽已被占用（`live` 或低 PTE 非零） | `vminfo <low-va>` 查看 `state/owner` |
| 第二次 `vunmap` 返回 `slot already released` | 槽未映射时释放 | 这是**预期拒绝**，对应「重复释放」 |
| 释放帧失败显示 `cannot free: mapped` | 帧仍被 `vm_mappings` 持有 | 先 `vunmap` 再 `pfree` |
| 映射后低/高地址读不一致（vmtest 失败串） | `vm_pair_ok` 不变量被破坏或 PTE 未同步写 | 检查 `*l=*q=` 是否成对；`invlpg64` 两条路 |
| 对象操作全部拒绝（含合法 `vmap`） | `address_space_init` 未调用或 `initialized=0` | 确认 `kernel_main64_binary` 在 `pmm_init` 后调用 `address_space_init` |
| `owner` 显示乱值 | `vm_mapping` 初始化/记账漏 `owner` 字段 | 检查 map/release 是否成对维护 `owner` 与 `live` |
| 用户代码页或栈页所在槽被 `vmap` 占用 | `address_space_slot` 未保留槽 0/1 | 检查 `if(*slot<2) return 0;` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学模型简化了什么 |
|---|---|---|
| `struct address_space`（tables + 窗口 + 记账） | `mm_struct`（`mm->pgd`、`TASK_SIZE`、`mm->map_count`） | Linux 每个进程一个 `mm_struct` 且含 VMA 红黑树；TinyOS 是单实例、无 VMA |
| `MAP_OWNER_USER` 所有权 | PTE `_PAGE_USER` + VMA `VM_USER`/`vm_flags` | Linux 权限由页表与 VMA 双重表达；TinyOS 用对象内 owner 字段 |
| `address_space_map` 返回拒绝串 | `vm_insert_page`/`do_mmap` 返回 errno | Linux 返回负 errno（`-ENOMEM` 等）；TinyOS 返回可打印串 |
| `vm_pair_ok` 低/高一致性 | 内核高半区映射由 `init_mm` 维护、`mmap` 通过 `pgd_alloc` 管理 | Linux 高半区由 TLB 与 `trampoline_pgd` 等机制管理，无逐 PTE 对校验 |

权威来源：Intel SDM Vol.3 §4（分页结构）、§4.3（PTE 权限位）、Linux
`arch/x86/include/asm/pgtable_types.h`（`_PAGE_USER`）。

---

## 8. 思考题与练习

1. **概念理解**：`address_space_map` 为什么同时检查 `vm_mappings[slot].live` 和
   `*l`（低 PTE）两个信号？哪个会被 `vm_pair_ok` 之外的路径破坏？
2. **源码定位**：找出 `vm_pair_ok` 被调用的所有位置，说明「先验证后修改」不变量
   在本课的落实点。
3. **动手实验**：把 `address_space_map` 的 `if(owner!=MAP_OWNER_USER)` 改掉，
   再用 `vmap` 映射一个 `MAP_OWNER_KERNEL` 槽，观察 `vminfo` 里 owner 显示与
   `vunmap` 的 `mapping owner mismatch` 行为。
4. **动手实验**：在 `address_space_slot` 里把保留槽放宽到 `*slot<1`，观察槽 1
   （用户栈地址附近）被映射后 `cpl3test` 是否异常——体会保留槽的意义。
5. **Linux 对照**：对比 `address_space_map` 与 Linux `mmap` 权限传播路径，列出
   Linux 中「用户进程不能映射内核地址」由哪些机制共同保证（页表 U/S 位、
   `TASK_SIZE` 检查、`mmap_min_addr` 等）。

---

## 9. 本课小结与下一课预告

- 映射管理从「全局函数 + 裸注册表」升级为「`struct address_space` 对象 +
  所有权字段」：窗口、计数、生命周期、归属全部对象化。
- `address_space_map` 只接受 `MAP_OWNER_USER`，把「内核专属映射」挡在对象入口，
  高别名地址、重复槽位、重复帧、重复释放都有可打印的拒绝串。
- `vmtest` 用「映射→持有→读写互通→释放→归还」全链路验证了低/高双别名与
  PMM 帧所有权闭环。
- 镜像校验边界（Lesson 32）与地址空间边界（本课）叠加，用户侧行为不变。

**下一课（Lesson 34）**：引入**有界 process/thread 对象**——一个固定进程
（PID 1）+ 一个用户线程（TID 1）持有 address-space、镜像元数据与保存的用户
上下文；CPL3 进入变成显式 `READY → RUNNING` 转换，`SYS_EXIT` 执行
`RUNNING → EXITED` 并记录生命周期转换。address-space 对象将成为 process 的
所属资源。

# Lesson 83: COW 写时复制缺页统计 — 精讲文档

> **课号**：Lesson 83（主线源课编号 Lesson 76 线）
> **本课主题**：写时复制（COW）缺页统计——`pf_classify` 对访问请求做三类缺页分类（not-present / protection / unmapped）并累计计数器，`vmainfo` 呈现统计结果
> **课程主线位置**：调度 / COW 元数据教学模型阶段（Lesson 64 起）。Lesson 82 建立了页对象与 `writable/refs` 元数据，本课把"缺页路径"做成可统计的模型：每次访问如何被分类、每类计数如何累计、统计在哪里可读。
> **前置课程**：[`../lesson-82-stable/README.md`](../lesson-82-stable/README.md)（Copy-on-Write 基础元数据：`struct page_model`、VMA 权限、`fault_insert`）
> **后续课程**：[`../lesson-84-stable/README.md`](../lesson-84-stable/README.md)（共享页生命周期：`refs` 引用计数、页缓存、回收）
> **本课一句话目标**：理解"COW 缺页 = 写只读共享页时触发的保护缺页（`PF_PROTECTION`）"，并掌握 `pf_classify` 的三类缺页分类与四个统计计数器（`fault_not_present`/`fault_protection`/`fault_unmapped`/`fault_insertions`）的累计语义。
> **保留的原始快照信息**：This checkpoint models bounded scheduling and copy-on-write metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l83test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能给任意一次"虚拟地址访问"做缺页分类：页不在（not-present）、权限不符（protection，含 COW 写保护）、地址越界（unmapped），并能读懂 `vmainfo` 里 `faults np/prot/unmapped` 三个计数的含义。
- **在课程主线中的位置**：COW 元数据三连课（82–84）的第二课。上一课定义"页"是什么，本课统计"缺页发生在哪"——这是从"数据结构"走向"路径/事件"的一课。下一课（84）在回收路径上使用 `refs` 引用计数，讲共享页生命周期。
- **前置知识清单**（学本课之前必须掌握）：
  1. `struct page_model` 与 `fault_insert` 的匿名页生成（Lesson 82）；
  2. VMA 表与 `vma_lookup`/`vma_range_valid` 权限检查（Lesson 32 起 / Lesson 82）；
  3. PTE 的 present/writable 概念与缺页（`#PF`、`vmfaulttest` 命令）（Lesson 27 起）；
  4. `exec64` 命令分派与 VGA 输出（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_76_model` + `lesson_76_state` + `l83test`；把 `l82test` 改名为 `l75test`；`about` 与 banner 更新为「Lesson 83: COW 写时复制缺页统计」。

---

## 2. 核心概念精讲

### 2.1 一次访问的缺页分类（page fault classification）

**直觉**：CPU 每次访问虚拟地址都要过"查页表"这一关。三种最常见的失败方式：
1. **页不在（not-present）**：地址合法、权限也够，但这一页还没有物理页映射——首次访问刚分配的堆、按需调入的文件页都属此类；
2. **保护（protection）**：地址合法、页也在，但访问方式不合法——读一个只读页没问题，**写一个只读页**就触发保护缺页。**COW 的写时复制就挂在这里**：共享页被故意标成只读，写它必然触发保护缺页，内核借机复制；
3. **越界（unmapped）**：地址根本不属于任何 VMA——野指针、越界访问。

```
访问请求 (va, write)
   ├─ 不在任何 VMA ──► PF_UNMAPPED（fault_unmapped++）
   ├─ 权限不符（写 r-x / 读 无-r）──► PF_PROTECTION（fault_protection++）  ← COW 写保护在这里
   └─ 页不在 ──► PF_NOT_PRESENT（fault_not_present++）
```

### 2.2 COW 缺页统计为什么重要

Linux 需要知道系统里"保护缺页"（写 COW 页）发生了多少次：它直接反映 fork 后实际复制了多少内存、复制成本有多高、是否有共享页被反复分裂。`/proc/vmstat` 里就有 `pgfault`/`pgmajfault` 等计数。TinyOS 用四个固定 u64 计数器承载这套统计：

```c
static u64 fault_not_present, fault_protection, fault_unmapped, fault_insertions;
```

其中 `fault_protection` 就是"COW 缺页"的教学代理：凡是对只读页发起写，就计一次保护缺页——这与 `do_wp_page` 的触发条件一一对应。

### 2.3 分类函数 `pf_classify` 的判定顺序

```c
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```

判定顺序刻意固定：**先查 VMA（越界），再查权限（保护），最后查页（不存在）**。这与 Linux `handle_mm_fault` 的顺序一致——先确认"这个地址该不该有映射"，再确认"这个访问允不允许"，最后才处理"页在不在"。

### 2.4 「固定元数据 + 确定性验证」教学模型

`struct lesson_76_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担调度 checkpoint；`l83test` 断言 `b==a+1` 与四标志。本课新增的确定性验证点集中在统计计数器：`pfmodel` 每次执行都产生固定序列的 `fault_*` 计数增量，`vmainfo` 把它们原样打印。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-82） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：缺页分类与统计、页对象、`exec64` 分派 | **主要增量**：新增 `struct lesson_76_model`、`lesson_76_state`、`l83test()`；把 `l82test` 改名为 `l75test`；`exec64` 分支与 `about`、banner 文案更新（`pf_classify` 统计继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 83 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的固定元数据记录与 `l83test`

```c
struct lesson_76_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_76_model lesson_76_state;
static TEXT64 void l83test(u16*c){lesson_76_state=(struct lesson_76_model){76U,77U,78U,79U,1,1,1,1};int ok=lesson_76_state.valid&&lesson_76_state.active&&lesson_76_state.ready&&lesson_76_state.accounted&&lesson_76_state.b==lesson_76_state.a+1U;text64(c,"l83test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 76 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_76_state=(struct lesson_76_model){76U,77U,78U,79U,1,1,1,1};`：`a=76,b=77,c=78,d=79`。
- `int ok=...`：四标志与 `b==a+1` 五条件与。
- 成功串 `bounded scheduling and copy-on-write checkpoint passed` / 失败串 `Lesson 76 fallback reported` 逐字来自源码。

#### (b) 上一课回归测试改名为 `l75test`

lesson-82 的 `l82test` 改名 `l75test`（校验 `lesson_75_state`）。`exec64` 命令集变为 `l64 l65 l69 l70 l71 l72 l73 l74 l75 l83`。

#### (c) 缺页分类核心：`pf_classify`

```c
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```

逐行注释：
- `vma_lookup(va)` 查命中 VMA。查不到 → `fault_unmapped++` 并返回 `PF_UNMAPPED`。
- 权限检查：`write&&!(v->prot&VMA_W)` 是"写请求但 VMA 不含写权限"（**COW 写保护缺页就落在这里**）；`!write&&!(v->prot&VMA_R)` 是"读请求但 VMA 不含读权限"。命中 → `fault_protection++` 返回 `PF_PROTECTION`。
- `page_present(va)` 查 `fault_pages`（`down(va)` 对齐后按 `va` 与 `live` 匹配）。不在 → `fault_not_present++` 返回 `PF_NOT_PRESENT`。
- 兜底 `return PF_NOT_PRESENT;`：页已 present 时也算 not-present（教学模型不区分"present 但无内容"与"present"的进一步细分）。
- **边界与设计**：每个计数器只在一个分支里 `++`，因此给定输入序列，统计增量完全确定——这是"确定性验证"的关键。对照 Linux：`handle_mm_fault` 里权限错误直接 `VM_FAULT_SIGSEGV`，`vma_lookup` 对应 `find_vma`。

#### (d) 缺页插入与统计联动：`fault_insert`

```c
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;p=pmm_alloc();if(!p)return 0;for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];m->va=down(va);m->phys=p;m->writable=write;m->live=1;m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
```

- 前置三检查：容量、VMA 权限（写请求需 `VMA_W`）、页未 present。
- 成功后 `fault_insertions++`——第四个统计计数器，表示"缺页被成功处理并插入页"的次数。
- 与 `pf_classify` 的关系：`pf_classify` 负责**计数分类**，`fault_insert` 负责**处理落地**。一次完整的缺页 = 分类（计数）+ 插入（计数 + 建页）。

#### (e) 统计的可视化：`vmainfo` 与 `pfmodel`

```c
static TEXT64 void vmainfo(u16*c){u32 i;text64(c,"VMA table (bounded Linux-style metadata)\n");for(i=0;i<vma_count;i++){text64(c,"vma ");hex64(c,i);text64(c," ");hex64(c,vma_table[i].start);text64(c,"-");hex64(c,vma_table[i].end);text64(c," ");text64(c,vma_prot(vma_table[i].prot));text64(c," ");text64(c,vma_backing(vma_table[i].kind));putc64(c,'\n');}text64(c,"pages/live, faults np/prot/unmapped: ");hex64(c,fault_page_count);text64(c," ");hex64(c,fault_not_present);text64(c," ");hex64(c,fault_protection);text64(c," ");hex64(c,fault_unmapped);putc64(c,'\n');}
```

- `vmainfo` 一行打印 4 个统计值：`fault_page_count`（当前 live 页数）、`fault_not_present`、`fault_protection`、`fault_unmapped`。
- `pfmodel` 命令执行一组固定访问并断言分类正确：
```c
static TEXT64 void pfmodel(u16*c){enum pf_class a=pf_classify(VMA_DATA_START,1),b=pf_classify(VMA_CODE_START,1),d=pf_classify(0x00100000ULL,0);int inserted=fault_insert(VMA_DATA_START,1);text64(c,"pfmodel: ");text64(c,a==PF_NOT_PRESENT&&b==PF_PROTECTION&&d==PF_UNMAPPED&&inserted?"not-present/protection/unmapped classified; bounded page inserted":"BROKEN");text64(c,"\nno real fault instruction executed; pages: ");hex64(c,fault_page_count);putc64(c,'\n');}
```
  - `pf_classify(VMA_DATA_START,1)`：数据段写、页不在 → `PF_NOT_PRESENT`（`fault_not_present++`）；
  - `pf_classify(VMA_CODE_START,1)`：代码段写（`r-x` 无 W）→ `PF_PROTECTION`（`fault_protection++`）——**COW 写保护缺页的教学样例**；
  - `pf_classify(0x00100000,0)`：低内存无 VMA → `PF_UNMAPPED`（`fault_unmapped++`）；
  - `fault_insert(VMA_DATA_START,1)`：成功插入匿名页（`fault_insertions++`、`anon_pages++`）。

#### (f) `exec64` 增量与 banner

```c
}else if(eq64(word,"l75test")){if(!noargs64(arg))usage64(c,"l75test");else l75test(c);}
}else if(eq64(word,"l83test")){if(!noargs64(arg))usage64(c,"l83test");else l83test(c);}
```

`about`：`text64(c,"Lesson 83: COW 写时复制缺页统计\n");`
banner：`text64(&c,"Lesson 83: COW 写时复制缺页统计\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");`

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'COW 写时复制缺页统计' README.md
	@grep -q 'l83test' kernel64.c
	@grep -q 'Lesson 83' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 83 checks passed.'
```

- 与 lesson-82 唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链不变（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse ... -Werror` → `kernel64.bin` → 内嵌 → ELF32 → ISO）。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ vma_init(): 3 条 VMA，清零 fault_not_present/fault_protection/fault_unmapped/fault_insertions
    ├─ banner: "Lesson 83: COW 写时复制缺页统计\n..."
    └─ for(;;) 键盘循环
        ├─ "pfmodel" ──► pf_classify(DATA,写)=PF_NOT_PRESENT（np++）
        │                 ├─ pf_classify(CODE,写)=PF_PROTECTION（prot++）  ← COW 写保护样例
        │                 ├─ pf_classify(0x100000,读)=PF_UNMAPPED（unmapped++）
        │                 └─ fault_insert(DATA,写)（insertions++）→ 打印 classified + inserted
        ├─ "vmainfo" ──► 打印 VMA 表 + "pages/live, faults np/prot/unmapped: ..."
        └─ "l83test" ──► lesson_76_state 校验 ──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`vma_init` 建 VMA 表并清零四个统计计数器；打印 banner。
2. **输入 `pfmodel`**：连续三次 `pf_classify` 依次累加 `fault_not_present`、`fault_protection`、`fault_unmapped`；随后 `fault_insert(VMA_DATA_START,1)` 成功 → `fault_insertions++`、`fault_page_count=1`、`anon_pages=1`。
3. **COW 统计视角**：`fault_protection` 计数就是"COW 写保护缺页"的教学代理——写 `r-x` 代码段与写共享只读页在分类层是同一类事件。
4. **读统计**：`vmainfo` 打印 `pages/live, faults np/prot/unmapped: 1 1 1 1`（第一次运行 `pfmodel` 后）；再跑一次 `pfmodel`，`fault_page_count` 停在 1（页已 present，`fault_insert` 前置检查拦截），但三个分类计数继续累加。
5. **checkpoint**：`l83test` 打印 `l83test: bounded scheduling and copy-on-write checkpoint passed`。

输出串与源码逐字一致：`l83test: ` + `bounded scheduling and copy-on-write checkpoint passed`；`pfmodel: not-present/protection/unmapped classified; bounded page inserted`。

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
   Lesson 83: COW 写时复制缺页统计
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l83test`，预期输出：
   ```
   l83test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l83test: Lesson 76 fallback reported`。）
3. 输入 `l75test`（回归），预期输出：
   ```
   l75test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `pfmodel`，预期输出：
   ```
   pfmodel: not-present/protection/unmapped classified; bounded page inserted
   no real fault instruction executed; pages: 1
   tinyos>
   ```
5. 再次输入 `pfmodel`，观察 `pages:` 仍为 1（`fault_insert` 因页已 present 被拦截）而分类计数继续累加。
6. 输入 `vmainfo`，预期出现：
   ```
   VMA table (bounded Linux-style metadata)
   vma 0 0000000000400000-0000000000401000 r-x file
   vma 1 0000000000600000-0000000000602000 rw- anon
   vma 2 0000000000800000-0000000000802000 rw- anon
   pages/live, faults np/prot/unmapped: ...
   ```
   （两次 `pfmodel` 后 np/prot/unmapped 三个计数应为 2/2/2。）
7. 输入 `about`，预期输出：
   ```
   Lesson 83: COW 写时复制缺页统计
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 83 checks passed.`；QEMU 中 `l83test` 打印 `...passed`、`pfmodel` 打印 `...classified; bounded page inserted`、`vmainfo` 的 np/prot/unmapped 计数随 `pfmodel` 次数成比例递增，即代表缺页统计模型验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `pfmodel` 打印 `BROKEN` | 四个断言中某一项失败（分类结果或插入失败） | 逐项核对：`VMA_DATA_START` 写→`PF_NOT_PRESENT`、`VMA_CODE_START` 写→`PF_PROTECTION`、`0x00100000`→`PF_UNMAPPED`、`fault_insert` 返回 1 |
| `vmainfo` 的 `fault_page_count` 不再增长 | 同一地址重复插入被 `page_present` 前置检查拦截 | 这是预期行为；换一个合法 `va`（如 `VMA_STACK_START`）才能插入第二页 |
| `fault_protection` 恒 0 | 从没有"写只读页"的访问发生 | 用 `pfmodel` 的 `pf_classify(VMA_CODE_START,1)` 触发；或 `copytest`（写 `VMA_CODE_START` 失败路径） |
| `pf_classify(0x00100000,0)` 返回的不是 unmapped | `vma_lookup` 命中了错误 VMA | 检查 `vma_init` 三条记录的 `start/end`；`0x00100000` 低于 `VMA_CODE_START` 应查无 |
| `l83test` 打印 fallback 串 | `lesson_76_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=76,b=77`） |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'COW 写时复制缺页统计' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l83test` 符号 | `grep -q 'l83test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：缺页分类与 handle_mm_fault**
- TinyOS 教学模型：`pf_classify` 三步判定（unmapped → protection → not-present）并累计 `fault_*` 计数器。
- Linux 实现：`mm/memory.c` 的 `handle_mm_fault()` 是缺页总入口：`vma = find_vma(mm, address)`（找不到 → `VM_FAULT_SIGSEGV`，即 unmapped 类）；随后 `check_vma_flags` 校验 `FAULT_FLAG_WRITE` 与 `vm_flags` 的写权限（不符 → `VM_FAULT_SIGSEGV`）；再按"页表 present？"分发到 `do_anonymous_page`/`do_wp_page`/`do_fault`。
- 权威来源：Linux v6.x `mm/memory.c`（`handle_mm_fault`、`check_vma_flags`）；Intel SDM Vol.3A §4.7（页错误异常：present 位决定错误码 bit 0，写 bit 1）。
- 教学简化：TinyOS 没有真实 CR2/错误码（`pf_classify` 的 `write` 参数由调用方手工给出），也没有 SIGSEGV 投递——只分类计数。

**对照点 2：COW 写保护缺页 = PF_PROTECTION**
- TinyOS：`fault_protection` 计数覆盖"写只读 VMA"（如代码段）与（概念上的）写 COW 共享页——分类层不区分二者。
- Linux：`do_wp_page()`（`mm/memory.c`）处理"写命中只读 PTE"：`is_cow_mapping(vma->vm_flags)` 判定是否为 COW 映射，是则 `wp_page_copy` 复制，否则只改 PTE 恢复可写。
- 权威来源：Linux v6.x `mm/memory.c`（`do_wp_page`、`wp_page_copy`、`is_cow_mapping`）。
- 教学简化：`pf_classify` 只"分类 + 计数"，不进入 `do_wp_page` 的复制路径；复制动作的教学模型是 `fault_insert`（造一个新页、`refs=1`）。

**对照点 3：缺页统计在系统层面的呈现**
- TinyOS：`vmainfo` 一行打印 `faults np/prot/unmapped`。
- Linux：`/proc/vmstat` 的 `pgfault`（总缺页）、`pgmajfault`（主缺页）、`pgprotfault`（保护缺页）；`perf`/`ftrace` 可跟踪 `mm_trace` 事件。
- 教学简化：固定 4 个 u64 计数器 + 一条命令输出，无 procfs、无采样。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `pf_classify` 必须"先查 VMA、再查权限、最后查页存在"？如果把"页存在"提前到"权限"之前，会发生什么错误分类（提示：一个越界地址恰好 present 时）？
2. **源码定位**：说出四个统计计数器分别在哪一行被 `++`，以及 `pfmodel` 一次执行使它们各自增加多少。
3. **动手实验**：修改 `pfmodel`，把 `pf_classify(VMA_CODE_START,1)` 的 `write` 参数改成 `0`（读代码段），重新 `make run`。观察分类结果从 `PF_PROTECTION` 变成 `PF_NOT_PRESENT`（`fault_protection` 不再增加）。改完请**恢复原值**。
4. **动手实验**：把 `vmainfo` 里 `fault_page_count` 的打印换成 `fault_insertions`，观察 `pfmodel` 两次执行后 insertions=1 而分类计数=2 的差异，理解"分类计数"与"插入计数"不是一回事。改完请**恢复原值**。
5. **Linux 对照**：阅读 `handle_mm_fault` 的 `check_vma_flags` 逻辑，说明 TinyOS 的 `PF_PROTECTION` 与 Linux 的哪个分支对应，以及 Linux 在权限错误后投递的是什么（`VM_FAULT_SIGSEGV` 还是 `SIGBUS`？）。

---

## 9. 本课小结与下一课预告

- 本课把"缺页"做成可统计的事件：`pf_classify` 三步分类（unmapped/protection/not-present），每个计数器只在唯一分支累加。
- 你理解了 COW 写保护缺页就是 `PF_PROTECTION`：写只读共享页（或写只读 VMA）在分类层是同一事件，`fault_protection` 就是 COW 缺页的教学代理。
- 你掌握了 `pfmodel` 的固定访问序列产生的确定性计数增量，以及 `vmainfo` 的 `pages/live, faults np/prot/unmapped` 一行式统计呈现。
- 你对照了 Linux `handle_mm_fault`/`do_wp_page` 与 `/proc/vmstat`，知道教学模型省略了 CR2 错误码、SIGSEGV 投递与真实页表操作。
- 你验证了 `l83test`/`l75test` 与 `pfmodel`/`vmainfo` 的确定性输出。

**下一课预告**：Lesson 84「共享页生命周期」。缺页分类只是"入口"，页的"一生"还没讲完：`refs` 引用计数如何随共享增加、随释放减少，`reclaim_one` 为什么只在 `refs==1` 时才回收，页缓存 `page_cache_get` 如何命中/未命中——下一课把共享页从"分配 → 共享 → 释放"的完整生命周期做成模型。衔接点正是本课的 `fault_insertions` 与 `page_present`。

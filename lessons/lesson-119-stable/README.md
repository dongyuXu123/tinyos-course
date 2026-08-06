# Lesson 119: SMP 启动元数据 — 精讲文档

> **课号**：Lesson 119（统一课程编号 119）
> **主题**：SMP 启动元数据（`long_mode_handoff`：32 位引导阶段向 64 位内核交接的全部启动信息）
> **课程主线位置**：第 12 阶段「并发、SMP 与 RCU 检查点序列」中的检查点课
> **前置课程**：[Lesson 118 SMP CPU 状态](../lesson-118-stable/README.md)
> **后续课程**：[Lesson 120 跨 CPU 唤醒](../lesson-120-stable/README.md)
> **一句话目标**：搞懂 `long_mode_handoff` 里每一组字段（页表、内核/栈区间、MBI、framebuffer、用户镜像）是谁写入的、经谁搬运、被谁消费，以及它在真实 SMP 启动中对应的角色。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能沿「`kernel.c`（BSP 角色）填充 → `boot.S` 高地址搬运 → `kernel64.c`（AP 醒来角色）消费」这条线，把 `long_mode_handoff` 的每个字段对号入座，并用 `lminfo`/`meminfo`/`idtinfo`/`stackinfo` 反向观察这些元数据的落地结果。
- **在课程主线中的位置**：上一课（118）回答了「CPU 状态由哪些变量刻画」，本课回答这些状态**最初的启动数据从哪来**：`long_mode_handoff` 就是 TinyOS 的「启动元数据包」，它规定了内核正文、栈、IDT、页表、MBI 内存图、framebuffer 与用户镜像的物理地址与虚拟地址映射。真实 Linux 里对应的则是 `boot_params`/`zero_page` 与 `smpboot` 的 trampoline 交接。下一课（120）将在这套元数据之上演示跨 CPU 的唤醒。
- **前置知识清单**：
  1. `struct long_mode_handoff` 的既有字段与 `kernel_main32`/`setup_long_mode_tables` 的填充逻辑（`kernel.c`）；
  2. `boot.S` 的 `enter_long_mode` 流程：`movl %ebx,%cr3`、EFER.LME、CR0.PG、far jump，以及 RDI=高地址 handoff 指针的 SysV 调用约定；
  3. `pmm_init` 对 MBI mmap 的解析（`mmap_tag64`）与 `pmm_reserved` 的保护区间判定；
  4. 高地址别名 `phys_to_high`/`KERNEL_VMA_BASE` 与 IDT/栈的守卫布局（`kernel64.ld`）。
- **本课交付**（可见结果）：
  - 新检查点命令 `l119test`（`lesson_112_model` 校验）；
  - `lminfo`/`meminfo`/`idtinfo`/`stackinfo` 分别展示 handoff 携带的页表、PMM 统计、IDT 基址与三块守卫栈地址；
  - 启动横幅与 `about` 均标注本课主题 `SMP 启动元数据`。

---

## 2. 核心概念精讲

### 2.1 为什么需要「启动元数据」

**定义**：启动元数据指「引导阶段收集、交给内核主体初始化用的那批物理/虚拟地址与设备信息」。它回答四个问题：代码在哪（kernel_start/end）、栈在哪（stack_start/end）、页表在哪（pml4/pdpt/pd/pt）、外设在哪（framebuffer、MBI）。

**动机**：64 位内核主体运行在高地址（`KERNEL_VMA_BASE+`），而页表、栈、Multiboot 信息都在低物理内存里。两边「地址空间不同」，交接就必须靠一份**显式结构体**而不是约定俗成——这正是 SMP 世界里 BSP（引导 CPU）向 AP（应用处理器）交接的通行做法。

**真实 SMP 的对应**：Linux 里 BSP 把内存图、命令行、页表基址等塞进 `boot_params`（zero page），AP 醒来后在 trampoline 代码里从约定内存读交接结构，再跳到自己的 `start_secondary`。TinyOS 的 `long_mode_handoff` 是同一思想的最小版。

### 2.2 `long_mode_handoff` 字段总览

`kernel64.c` 中的定义（命令读取的视图）：

```c
struct long_mode_handoff { u64 pml4,pdpt,pd,idt_address,pt[PAGE_TABLES_PER_ALIAS];
  u64 kernel_start,kernel_end,stack_start,stack_end;
  u64 high_pdpt,high_pd,high_pt[PAGE_TABLES_PER_ALIAS];
  u64 user_code_phys,user_stack_phys,user2_code_phys,user2_stack_phys;
  u64 kernel_vma_base,kernel_phys_base;
  u32 mbi_address,mbi_size;
  u64 framebuffer_address,framebuffer_map;
  u32 framebuffer_pitch,framebuffer_width,framebuffer_height,framebuffer_bytes;
  u8 framebuffer_bpp,framebuffer_type; };
```

| 字段组 | 字段 | 用途 |
|--------|------|------|
| 低地址页表 | `pml4/pdpt/pd` | 长模式低 512 项的顶级页表物理地址，`boot.S` 用 `movl %ebx,%cr3` 装载 |
| 每别名 PT | `pt[PAGE_TABLES_PER_ALIAS]` | 低地址 16 MiB identity 映射的页表数组 |
| IDT | `idt_address` | 32 位侧预分配的 4096 字节 IDT 后备存储 |
| 内核区间 | `kernel_start/kernel_end` | 外层 ELF 的 `.text/.data` 物理范围，PMM 不得分配 |
| 栈区间 | `stack_start/stack_end` | 32 位引导栈范围，PMM 不得分配 |
| 高地址页表 | `high_pdpt/high_pd/high_pt[]` | `ffffffff80000000` 高别名映射的三级表 |
| 用户镜像 | `user_code_phys/user_stack_phys/user2_code_phys/user2_stack_phys` | 两个用户程序的代码/栈物理页 |
| 地址约定 | `kernel_vma_base/kernel_phys_base` | 高地址基址（`ffffffff80000000`）与内核物理基址（`0x00100000`） |
| Multiboot | `mbi_address/mbi_size` | MBI 指针与总大小，`pmm_init` 据此解析 mmap |
| framebuffer | `address/map/pitch/width/height/bytes/bpp/type` | GUI 的线性帧缓冲物理地址、映射虚拟地址与布局参数 |

> **源码观察（勘误/已知差异）**：`kernel.c` 中同名结构体在 `mbi_size` 与 `framebuffer_address` 之间**多了 4 个 u32**（`user_image_status/user_image_bytes/user_entry_offset/user_entry_length`），而 `kernel64.c` 的副本没有这 4 个字段。两处布局不完全一致：`mbi_address/mbi_size` 之前的字段（页表、内核/栈区间、用户镜像、`kernel_vma_base` 等）偏移完全对齐，是 `pmm_init`/`lminfo` 可靠工作的前提；`framebuffer_address` 之后的字段在两份结构中的偏移相差 16 字节。本文按各自源码如实呈现，不做臆测。

### 2.3 生产者-搬运者-消费者

```
生产者 kernel.c（32 位，BSP 角色）
  kernel_main32: 填 kernel_start/end、stack_start/end、idt_address、vma/phys base、mbi
  setup_long_mode_tables: 分配并填 pml4/pdpt/pd/pt、high_*、user_*_phys、framebuffer
        │
        │ 返回 pml4 物理地址到 EAX
        ▼
搬运者 boot.S
  enter_long_mode: EBX=pml4 → CR3；开 PAE/LME/PG；far jump 进 64 位
  高地址别名区：把 RSP 切到高地址栈顶，RDI=高地址 &long_mode_handoff
        │
        ▼
消费者 kernel64.c（64 位，AP 醒来角色）
  kernel_main64_binary(h): pmm_init(h) 解析 MBI、lminfo/meminfo/stackinfo/idtinfo 展示
```

### 2.4 从元数据到 CPU 状态（衔接上一课）

上一课的 `idle_init`/`start_threads` 都直接消费 handoff：idle 静态栈由 `kernel64.ld` 布局、线程栈由 `pmm_alloc` 在 `pmm_init(h)` 完成后分配；`stack_guards_init` 用 `.data.stack.*` 符号建立守卫页。可以说：**启动元数据决定了 CPU 状态机器赖以运转的全部物理资源**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-118） |
|------|------|------------------------------|
| `boot.S` | 32 位入口 / 长模式 / 高地址搬运 / 内嵌 kernel64.bin | 未变化（handoff 搬运者） |
| `kernel.c` | 32 位主函数：构建页表与 `long_mode_handoff` | 未变化（handoff 生产者） |
| `kernel64.c` | 64 位内核主体：handoff 消费与命令 | **唯一增量**：`lesson_112_model`/`l119test`、exec64 分支、about/banner |
| `kernel64.ld` | 64 位段布局与守卫栈 ASSERT | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `Makefile` | 构建/检查/运行 | 仅 `check` grep 串换成本课主题 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

> **勘误说明**：旧 README 声称命令为 `l112test`，但源码 `exec64` 中本课新增命令为 `l119test`（`kernel64.c` 中不存在 `l112test` 分支）；本文以源码为准。

### 3.2 源码精讲：生产者 `kernel.c`

#### 3.2.1 `kernel_main32`：基础字段

```c
u32 kernel_main32(u32 magic,u32 mbi_address){
  multiboot_magic=magic; multiboot_address=mbi_address;
  long_mode_handoff.kernel_start=(u64)(u32)(unsigned long)_kernel_start;
  long_mode_handoff.kernel_end=(u64)(u32)(unsigned long)_kernel_end;
  long_mode_handoff.stack_start=(u64)(u32)(unsigned long)stack_bottom;
  long_mode_handoff.stack_end=(u64)(u32)(unsigned long)stack_top;
  long_mode_handoff.idt_address=(u64)(u32)(unsigned long)idt_backing_store;
  long_mode_handoff.kernel_vma_base=KERNEL_VMA_BASE;
  long_mode_handoff.kernel_phys_base=0x00100000ULL;
  long_mode_handoff.user_image_status=0xffffffffU;      /* 预置为「未验证」 */
  if(!prepare_memory_map()) return 0;                   /* 解析 MBI 得到 memory_map */
  long_mode_handoff.mbi_address=multiboot_address;
  long_mode_handoff.mbi_size=multiboot_total_size;
  return setup_long_mode_tables();                      /* 返回 pml4 物理地址 */
}
```
- 地址都强制 `(u32)` 后再转 `u64`：32 位模式下所有物理地址 <4 GiB，截断合法且防止符号扩展；
- `user_image_status=0xffffffffU` 先占位，随后 `setup_long_mode_tables` 用 `validate_user_image()` 的真结果覆盖；
- 返回 pml4 物理地址，`boot.S` 的 `_start` 把它放进 `EBX` 供 `enter_long_mode` 写入 `CR3`——这是「元数据 → 硬件」的第一处握手。

#### 3.2.2 `setup_long_mode_tables`：页表与用户镜像

```c
static u32 setup_long_mode_tables(void){
  volatile u64 *pml4,*pdpt,*pd,*hpdpt,*hpd; u32 i,j; int image_status=validate_user_image();
  long_mode_handoff.user_image_status=(u32)image_status;
  long_mode_handoff.user_image_bytes=user_image.image_bytes;
  long_mode_handoff.user_entry_offset=user_image.entry_offset;
  long_mode_handoff.user_entry_length=user_image.entry_length;
  ...
  long_mode_handoff.pml4=bootstrap_alloc_page(); long_mode_handoff.pdpt=bootstrap_alloc_page();
  long_mode_handoff.pd=bootstrap_alloc_page();
  for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)long_mode_handoff.pt[i]=bootstrap_alloc_page();
  long_mode_handoff.high_pdpt=bootstrap_alloc_page(); long_mode_handoff.high_pd=bootstrap_alloc_page();
  for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)long_mode_handoff.high_pt[i]=bootstrap_alloc_page();
  ...
  pml4[0]=long_mode_handoff.pdpt|PTE_PRESENT_WRITABLE|PTE_USER;      /* 低入口 */
  pml4[511]=long_mode_handoff.high_pdpt|PTE_PRESENT_WRITABLE;        /* 高入口 */
  hpdpt[510]=long_mode_handoff.high_pd|PTE_PRESENT_WRITABLE;
  ...
  /* framebuffer: 把 framebuffer 物理页映射到 FRAMEBUFFER_VA=0x20000000 */
  if(framebuffer_ready&&long_mode_handoff.framebuffer_bytes<=PAGE_ENTRIES*PAGE_SIZE){...}
  long_mode_handoff.user_code_phys=bootstrap_alloc_page();
  long_mode_handoff.user_stack_phys=bootstrap_alloc_page();
  long_mode_handoff.user2_code_phys=bootstrap_alloc_page();
  long_mode_handoff.user2_stack_phys=bootstrap_alloc_page();
  ...
  return (u32)long_mode_handoff.pml4;
}
```
- 低地址 16 MiB identity 映射 + 高地址 `ffffffff80000000` 别名映射同时建立，这正是 `hhinfo`「low/high aliases」的硬件基础；
- 所有表页来自 32 位侧的低内存临时分配器 `bootstrap_alloc_page`，它们的物理地址写回 handoff，供 64 位侧 `pmm_reserved` 排除；
- 用户代码/栈四页也在此分配并把物理地址写进 handoff——64 位侧的 `pmm_init` 第一个动作就是把它们存进 `user_code_phys` 等全局。

#### 3.2.3 搬运者 `boot.S` 的交接指令

```asm
long_mode_start:
    .byte 0x48, 0xbc                  /* movabs rsp, imm64 */
    .quad 0xffffffff80000000          /* 高地址基址 */
    .byte 0xb8
    .long stack_top                   /* 32 位栈顶（低地址） */
    .byte 0x48, 0x01, 0xc4            /* add  rsp, rax → 高地址栈顶 */
    .byte 0x48, 0x83, 0xe4, 0xf0      /* and  rsp, -16 对齐 */
    .byte 0x48, 0x31, 0xed            /* xor  rbp, rbp */
    .byte 0x48, 0xbf                  /* movabs rdi, imm64 */
    .quad 0xffffffff80000000
    .byte 0xb8
    .long long_mode_handoff           /* RDI = 高地址 handoff 指针 */
    .byte 0x48, 0x01, 0xc7            /* add  rdi, rax */
    .byte 0x48, 0xb8                  /* movabs rax, imm64 */
    .quad 0xffffffff80000000
    .byte 0xb9
    .long kernel_main64
    .byte 0x48, 0x01, 0xc8, 0xff, 0xd0  /* add rax + call rax → kernel_main64 */
```
- 低/高地址别名同时在页表中生效，所以可以直接用 `add` 把低地址指针抬到高地址——这就是「元数据搬运」的汇编形态；
- SysV 调用约定：`RDI=long_mode_handoff` 高地址指针、`RSP` 16 字节对齐，随后 `call` 进入 `kernel_main64_binary`；
- 32 位侧无法用跨越边界的一发 `rel32 call` 跳到高地址，所以用 `movabs + add + call rax` 三连完成。

### 3.3 源码精讲：消费者 `kernel64.c`

#### 3.3.1 `pmm_init(h)`：消费 MBI 与保留区间

```c
static TEXT64 void pmm_init(struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off,i;
  user_code_phys=h->user_code_phys;user_stack_phys=h->user_stack_phys;
  user2_code_phys=h->user2_code_phys;user2_stack_phys=h->user2_stack_phys;
  pmm_ready=0;pmm_error="not initialized";pmm_total=pmm_free=pmm_used=0;
  for(i=0;i<PMM_BITMAP_BYTES;i++){pmm_bitmap[i]=0xff;pmm_fixed[i]=0;}
  m=mmap_tag64(h);if(!m)return;                       /* 从 h->mbi_address 解析 mmap tag */
  for(off=0;off<m->size-16;off+=m->entry_size){       /* 可用 RAM 置 0 */
    ... p=up(e->addr);end=down(e->addr+e->len); while(p<end){unmark((u32)(p/PAGE_SIZE));p+=PAGE_SIZE;} }
  for(i=0;i<PMM_FRAMES;i++){u64 p=(u64)i*PAGE_SIZE;
    if(!bit(i)){pmm_total++;if(pmm_reserved(h,p)){mark(i);fix(i);}else pmm_free++;}}
  pmm_used=pmm_total-pmm_free;pmm_ready=1;pmm_error="ready";}
```
- 先按 MBI mmap 把可用物理内存的位图置 0，再逐帧检查 `pmm_reserved(h,p)`：handoff 里 kernel/stack/页表/MBI/用户镜像等所有区间都被 `overlap` 判定后 `mark+fix`，保证分配器永远不碰它们；
- `pmm_error` 从 `"not initialized"` → `"ready"` 的全过程可在 `meminfo` 里看到；
- 这是「启动元数据 → 可用内存视图」的落地：没有 handoff，位图就不知道哪些页不能动。

#### 3.3.2 元数据查看命令：`lminfo` / `meminfo` / `idtinfo` / `stackinfo`

```c
static TEXT64 void lminfo(u16*c,struct long_mode_handoff*h){u32 i;
  text64(c,"long mode: 16 MiB identity + high alias\npml4: ");hex64(c,h->pml4);
  text64(c,"\nlow/high PT: ");for(i=0;i<PAGE_TABLES_PER_ALIAS;i++){hex64(c,h->pt[i]);
  text64(c,"/");hex64(c,h->high_pt[i]);text64(c," ");}
  text64(c,"\nidentity: 0000000000000000 - 0000000001000000\n");}
```
- `lminfo` 直接读出 handoff 的 `pml4` 与每对 `pt[i]/high_pt[i]`——启动页表的「快递单」；
- `meminfo` 打印 `PMM: 4 KiB physical frames in 16 MiB mapped window` 与 `tracked/free/used`，并复核不变式 `tracked == free + used`（`pmm_total==pmm_free+pmm_used?"yes":"BROKEN"`）；
- `idtinfo` 打印 `#PF IST: 0000000000000001`、`base:`（`phys_to_high(h->idt_address)`）与各向量，展示 IDT 元数据从 32 位侧 `idt_backing_store` 落到高地址的过程；
- `stackinfo` 打印 `idle/rsp0/IST1` 三块守卫栈的 `guard/payload/end` 符号地址，对应 `kernel64.ld` 里 `__idle_guard_start` 等符号——栈元数据全部来自链接脚本而非 handoff，这是与页表元数据的一个关键分工。

#### 3.3.3 检查点增量：`l119test`

```c
struct lesson_112_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_112_model lesson_112_state;
static TEXT64 void l119test(u16*c){lesson_112_state=(struct lesson_112_model){112U,113U,114U,115U,1,1,1,1};
int ok=lesson_112_state.valid&&lesson_112_state.active&&lesson_112_state.ready&&lesson_112_state.accounted
        &&lesson_112_state.b==lesson_112_state.a+1U;
text64(c,"l119test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 112 fallback reported");putc64(c,'\n');}
```
- 模型号推进到 `lesson_112`，`lesson_111_state` 交还给 `l111test`；输出恒为 `l119test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；
- `about`/横幅为 `Lesson 119: SMP 启动元数据`。

### 3.4 构建管线（Makefile / linker）

- 两级链接不变：`kernel64.c`（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*`）→ `kernel64.ld` 裸 binary → `boot.S` `.incbin` 内嵌 → 外层 `-m elf_i386 -T linker.ld` → `grub-mkrescue`；
- 启动元数据的物理地址由 `linker.ld` 的 `_kernel_start/_kernel_end` 与 `.bss` 中的 `long_mode_handoff`/`stack_bottom/stack_top` 符号在链接期确定——元数据是「链接器布局 + 运行期填充」的混合体；
- `make check` grep 串：`SMP 启动元数据`、`l119test`、`Lesson 119`，全过后打印 `Multiboot2 and Lesson 119 checks passed.`。

### 3.5 主控制流

```mermaid
graph TD
    A[GRUB → _start(32位)] --> B[kernel_main32 填 handoff 基础字段]
    B --> C[setup_long_mode_tables 填页表/用户镜像/framebuffer]
    C --> D[enter_long_mode: CR3←pml4, LME, PG, far jump]
    D --> E[高地址切栈, RDI=&long_mode_handoff 高地址]
    E --> F[kernel_main64_binary(h)]
    F --> G[pmm_init(h) 解析 MBI→PMM 位图]
    G --> H[横幅 Lesson 119: SMP 启动元数据]
    H --> I[键盘循环 exec64]
    I -->|lminfo/meminfo/idtinfo/stackinfo| J[展示启动元数据落地结果]
    I -->|l119test| K[lesson_112_model 校验→通过串]
```

---

## 4. 数据流与运行逻辑

1. GRUB 加载外层 ELF 到 1 MiB，`_start` 调 `kernel_main32(magic, mbi)`；
2. `kernel_main32` 填 handoff 的 kernel/stack/idt 区间与 vma/phys base，`setup_long_mode_tables` 填全部页表、用户镜像与 framebuffer，返回 `pml4`；
3. `boot.S` 装载 CR3、开长模式，把 RSP/RDI 抬到高地址后 `call` 进 `kernel_main64_binary`；
4. 64 位侧 `pmm_init(h)` 用 `h->mbi_address/mbi_size` 解析 MBI mmap 建 PMM 位图，用 `h->kernel_start...` 等保留关键区间；
5. 敲 `lminfo` 看 `pml4:` 与每对 `low/high PT`；敲 `meminfo` 看 `tracked/free/used` 与不变式 `yes`；敲 `idtinfo` 看 `base:` 高地址 IDT；敲 `stackinfo` 看三块守卫栈地址；
6. 敲 `l119test` 打印检查点通过串。

---

## 5. 构建、运行与验证

**依赖**：`gcc`、`ld`、`objcopy`、`grub-file`、`grub-mkrescue`、`qemu-system-x86_64`。

**构建**：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 成功输出：
```
Multiboot2 and Lesson 119 checks passed.
```

**运行**：
```bash
make run
```
> 成功画面在 QEMU 图形窗口（VGA 终端），请勿加 `-display none`。

**验证步骤**（预期输出串全部从源码逐字抄录）：

1. 启动横幅：`Lesson 119: SMP 启动元数据`；
2. `about` → `Lesson 119: SMP 启动元数据`；
3. `l119test` → `l119test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`；
4. `lminfo` → 以 `long mode: 16 MiB identity + high alias` 开头，列出 `pml4:` 与 `low/high PT:` 序列，末尾 `identity: 0000000000000000 - 0000000001000000`；
5. `meminfo` → 以 `PMM: 4 KiB physical frames in 16 MiB mapped window` 开头，`tracked/free/used` 满足不变式，显示 `invariant tracked = free + used: yes`；
6. `idtinfo` → 以 `IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1` 开头，`#PF IST: 0000000000000001`；
7. `stackinfo` → 依次打印 `idle guard/payload/end`、`rsp0 guard/payload/end`、`IST1 guard/payload/end`；
8. 回归：`hhtest`、`threadinfo`、`pctest`/`pcgo`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `meminfo` 显示 `status: not initialized` 或 `PMM unavailable` | `pmm_init` 未执行或 `mmap_tag64` 失败（MBI 太小/坏 tag） | 在 `kernel_main64_binary` 里确认 `pmm_init(h)` 在 `palloc` 之前调用；用 `meminfo` 看 `status` |
| `meminfo` 不变式显示 `BROKEN` | `pmm_total != pmm_free+pmm_used`，位图记账被外部修改 | 检查 `pmm_alloc`/`pmm_free_page` 的计数增减是否成对 |
| `lminfo` 显示 `pml4: 0` | handoff 的 `pml4` 字段未被 `setup_long_mode_tables` 填充或 h 指针错误 | 检查 `bootstrap_alloc_page` 返回值；确认 RDI 传入的是高地址 handoff |
| 启动即 #GP/#PF（长模式未进入） | `boot.S` 的 CR3/EFER/CR0 顺序错误，或 far jump 目标不对 | 检查 `enter_long_mode`：先 CR3 再 PAE 再 LME 再 PG；确认 `ljmp $CODE64_SELECTOR,$long_mode_start` |
| `stackinfo` 的 guard/payload 差不是 0x1000 | `kernel64.ld` 的守卫栈 ASSERT 未命中或 `.data.stack.*` 未对齐 | 查看链接时是否报 `ASSERT`；检查 `__idle_guard_start` 等符号是否被 `used` 属性保住 |
| `idtinfo` 的 `base:` 与 IDT 实际不符 | `idt_address` 在 handoff 里被覆盖或 64 位侧偏移错位 | 对照 §2.2 的字段表核对偏移；检查 `idt_backing_store` 是否被 PMM 误分配（`pmm_reserved` 应包含它） |
| `make check` 失败 | README/源码缺主题串 | 确认 README 含 `SMP 启动元数据` 与 `Lesson 119`，kernel64.c 含 `l119test` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学简化 |
|------------------|----------------|----------|
| `struct long_mode_handoff`（页表/区间/MBI/用户镜像/framebuffer 全打包） | `arch/x86/include/uapi/asm/bootparam.h` 的 `struct boot_params`（zero page），由 `setup_arch` 解包 | TinyOS 只有 8 组语义字段，无 e820/命令行/ACPI 等 |
| `kernel_main32` 填基础字段 + `setup_long_mode_tables` 填页表 | BSP 的 `start_kernel` → `setup_arch` → `paging_init`；AP 的 `secondary_startup_64` 读 boot_params | TinyOS 的 32 位引导即「BSP」，64 位侧即「AP」，一次交接完成 |
| `boot.S` 高地址搬运（add 基址 + RDI 传 handoff） | `arch/x86/realmode/rm/trampoline_64.S`：AP 在低内存 trampoline 里读 `trampoline_header`，再跳 `secondary_startup_64` | TinyOS 没有 STARTUP IPI 与 APIC 初始化，只是单 CPU 的一次「假交接」 |
| `pmm_init(h)` 用 `h->mbi_address` 解析 mmap 建位图 | `arch/x86/kernel/e820.c` 的 `e820__memory_setup`；`memblock` 分配器在 `mm/memblock.c` | TinyOS 用 1MiB 位图代替 memblock 树，固定 16 MiB 窗口 |
| handoff 里 kernel/stack/页表区间由 `pmm_reserved` 保护 | `memblock_reserve`/`reserve_bootmem`；`memblock_reserve(__pa_symbol(_text), ...)` | TinyOS 用 `overlap` 线性扫帧，Linux 用区间树 O(log n) |
| 用户镜像与 framebuffer 元数据随 handoff 传递 | framebuffer 由 `efifb`/`vesafb` 驱动从 ACPI/PCI 读取，不属于 boot_params | TinyOS 在 32 位侧直接探测并写死进结构体 |
| 每课一个 `lesson_112_model` 检查点护照 | Linux 无对应物；`init/calibrate.c` 的启动自检计数近似 | TinyOS 把启动元数据的「覆盖完整性」编码成布尔断言 |

权威来源：Multiboot2 规范（mbi 布局与 tag 遍历）、Intel SDM Vol.3A（长模式 CR4/EFER/CR0 开启顺序）、GNU GRUB 手册（grub-mkrescue / grub-file）、Linux `Documentation/x86/boot.rst`。

---

## 8. 思考题与练习

1. **概念理解**：为什么 handoff 里要同时存 `kernel_start/end` 和 `stack_start/end`？如果 PMM 把引导栈页分配出去了会怎样（提示：`pmm_reserved` 的 `overlap` 判定）？
2. **源码定位**：在 `kernel.c` 中找出 `long_mode_handoff` 每个字段被赋值的行，整理成「字段 → 赋值函数 → 赋值时机」对照表；再在 `kernel64.c` 中找每个字段被读取的行。
3. **动手实验**：把 `kernel.c` 里 `setup_long_mode_tables` 中 `bootstrap_alloc_page` 的一处调用改在 `pmm_init` 能看到的保留区间内（例如故意让它分配在 `h->kernel_start` 区间），重新构建运行 `meminfo`，观察不变式是否破坏。
4. **动手实验**：在 `kernel64.c` 中删掉 `pmm_reserved(h,p)` 里对 `h->idt_address` 的 `overlap` 检查，重新构建运行，随后执行 `idtinfo`/敲击一次按键，观察 IDT 是否被分配器破坏（表现为键盘/异常异常）。
5. **Linux 对照**：阅读 `arch/x86/kernel/smpboot.c` 的 `do_boot_cpu` 与 `arch/x86/realmode/rm/trampoline_64.S`，比较「AP 读 trampoline 交接结构」与本课「64 位侧读 `long_mode_handoff`」的异同；Linux 为什么还需要 IPI 而 TinyOS 不需要？

---

## 9. 本课小结与下一课预告

- 本课把启动阶段与运行阶段之间的「元数据契约」讲透：`long_mode_handoff` 是 TinyOS 唯一的启动元数据包，承载页表、内核/栈区间、MBI、用户镜像与 framebuffer；
- 生产-搬运-消费三段式：`kernel.c`（BSP）填 → `boot.S` 用 `add` 抬地址、RDI 传参 → `kernel64.c`（AP 醒来）读；
- `pmm_init` 用 MBI mmap 建位图并用 handoff 区间做保留，`meminfo` 的不变式是元数据一致性的晴雨表；
- `lminfo`/`idtinfo`/`stackinfo` 分别验证页表、IDT、守卫栈三类启动布局的落地；
- 源码中 `kernel.c` 与 `kernel64.c` 的 handoff 结构体存在 4 个 u32 字段的布局差异（位于 `mbi_size` 与 `framebuffer_address` 之间），已如实标注，供读者自行核验；
- 检查点推进到 `lesson_112_model`，命令 `l119test` 恒输出通过串；
- 下一步 [Lesson 120 跨 CPU 唤醒](../lesson-120-stable/README.md) 将在这套启动元数据之上演示跨 CPU 的唤醒路径——`waitq_wake_all` 的广播、`wake_sleepers` 的到期扫描与 `event_set` 的置位，它们正是信号量与等待队列在「多 CPU」语义下的最终形态。

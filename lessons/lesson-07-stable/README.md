# Lesson 07: 用已分配物理页建立最小 32 位分页 — 精讲文档

> **课号**：Lesson 07（可执行课，32 位保护模式阶段）
> **主题**：把第六课从 Multiboot2 memory map 选出的物理页真正用起来：构建一张 4 KiB-page
> 页目录 + 页表，建立 `[0x00000000, 0x00400000)` 的 identity mapping，开启 `CR0.PG`，
> 且 VGA shell 全程存活。
> **课程主线位置**：第 2 阶段（内存管理起步：物理分配 → 分页）的核心转折点；分页是后续
> long mode、异常、中断、驱动等一切课的地基。
> **前置课程**：[`lesson-06-stable/README.md`](../lesson-06-stable/README.md)（reservation-aware
> 物理页分配器：只返回地址、绝不写页）
> **后续课程**：[`lesson-08-stable/README.md`](../lesson-08-stable/README.md)（进入 x86_64
> long mode，四级页表 + `kernel64.c` 双段构建）
> **一句话目标**：学完本课你能解释并亲手验证「打开分页那一刻，CPU 为什么会继续执行」——
> 因为当前 EIP、ESP 和 VGA 帧缓冲全部落在被映射的前 4 MiB identity window 内。

---

## 1. 课程定位（Mission）

**一句话目标**：用第六课的 `phys_alloc_page()` 取得两张物理页，一张做成 32 位非 PAE
page directory、一张做成 page table，把虚拟地址 0–4 MiB 恒等映射到物理地址 0–4 MiB，
然后通过「写 `CR3` → 置位 `CR0.PG`」打开分页，让 shell 命令 `pginfo` 能打印出 paging
状态与两张表各自的物理地址。

- **在课程主线中的位置**：这是从「内存管理"只记账不动作"」跨越到「内存管理"动真格"」的
  一课。前六课一直运行在 GRUB 交出的、未分页的 32 位保护模式；本课首次把第六课分配出的
  物理页承担真实工作。分页开启后，后续课的异常、中断、long mode 过渡都将运行在分页之上。
- **前置知识清单**：
  1. 第六课的全部内容：Multiboot2 type-6 memory-map tag 的布局、`phys_alloc_page()` 的
     半开区间 `[start, end)` reservation 语义、`page_is_reserved()` 的五个拒绝条件；
  2. 虚拟地址与物理地址的区别，为什么 `physical == virtual` 只是 identity mapping 的特例；
  3. 32 位 CR0/CR3 寄存器、`mov %eax, %cr0` 等特权指令的 asm volatile 写法；
  4. VGA text mode `0xb8000` 与 kernel 1 MiB 镜像布局（第一课以来一直没变）。
- **本课交付**：boot 时自动开启分页的 TinyOS；新命令 `pginfo`；以及一条重要的经验法则
  ——「分配两张页做页表」本身就是一次内存管理职责的闭环。

---

## 2. 核心概念精讲

### 2.1 概念一：分页是什么，为什么现在需要它

**直觉**：分页让 CPU 在翻译地址时插入一道「查表」工序：程序看到的虚拟地址先经过页表
逐级翻译，才变成真正打到内存条上的物理地址。本课之前 TinyOS 只有实打实的物理地址；
分页是保护模式 CPU 的地址翻译开关，不打开它，后面所有「假装虚拟内存」的课都无从谈起。

**准确定义**：当 `CR0.PG = 1` 时，CPU 对每个线性地址做两级查表（32 位非 PAE 模式）：
`CR3 → Page Directory → Page Table → 4 KiB page`。查询结果由一个 4 KiB 对齐的物理页帧
地址和一组属性位（Present、Writable、User 等）构成。

**为什么现在需要**：long mode（第八课）**必须**有分页；异常处理、隔离、higher-half 内核
全部建立在「虚拟 ≠ 物理」的翻译之上。但直接跳进 x86_64 会让「查表 + 翻译 + 状态切换」
三个新概念混在一起，难以定位故障。本课把翻译这一件事单独拿出来，在 32 位模式下做最小
可行实现。

### 2.2 概念二：PDE / PTE 的 32 位格式

非 PAE 模式下一张 page directory 有 1024 个 4 字节表项，覆盖 4 GiB 地址空间；每个
PDE 覆盖 4 MiB。一张 page table 也有 1024 个 4 字节表项，每个 PTE 覆盖 4 KiB。

```text
线性地址 0x003ff123 的切分（32 位非 PAE）：
┌───────────────┬───────────────┬──────────────────┐
│ PDE 索引 (10) │ PTE 索引 (10) │ 页内偏移 (12)     │
│ 0             │ 1023          │ 0x123             │
└───────────────┴───────────────┴──────────────────┘
CR3 → directory[0] → table[1023] → 物理页 0x003ff000 + 0x123
```

本课使用的两个表项格式（Intel SDM Vol. 3 4.3）：

```text
bit 31 ───────── bit 12 │ bit 11 ── bit 3 │ bit 2 │ bit 1 │ bit 0
物理页帧地址 (20 bits)   │ 保留/属性位        │ U/S   │ R/W   │ P
```

- **bit 0 P（Present）**：置 1 表示该映射存在。打开分页后 CPU 找不到 P=1 的表项就触发
  page fault（#PF），本课没有 IDT，结果是无诊断重启。
- **bit 1 R/W（Writable）**：置 1 允许写。本课所有表项都是 `0x003` = P | R/W。
- 物理页帧地址存放在高 20 位，所以表项值必须是 4 KiB 对齐的——这解释了为什么
  `page_is_32bit()` 要求 `(page & 0xfff) == 0`。

### 2.3 概念三：CR3、CR0.PG 与开启顺序

- **CR3（PDBR）**：保存 page directory 的物理地址。写 CR3 就完成了「下一次地址翻译
  用哪张目录」的切换。注意：CR3 只在**开启分页之后**才真正参与翻译；在分页尚未开启时
  先把 CR3 写好，是为了让「开分页」这条指令之后的第一次取指就能用上新表。
- **CR0.PG（bit 31）**：分页总开关。置位后 CPU 立即进入分页翻译模式。
- **开启顺序为什么必须是「先建表 → 再写 CR3 → 最后置 PG」**：因为在 `mov %eax, %cr0`
  指令执行后的**下一条指令取指**就开始按新状态翻译地址。如果表没建好，或 CR3 指向的目录
  没有覆盖当前代码所在页，CPU 在 PG 置位瞬间就会因为翻译失败而触发 #PF，而本课没有
  异常处理器，结果就是重启。本课代码 `enable_identity_paging()` 把这套顺序写死了。

### 2.4 概念四：identity mapping 与"实验契约"

identity mapping 的意思是虚拟地址 == 物理地址。本课把 `[0x00000000, 0x00400000)` 四个
物理兆字节原样映射，这一窗口恰好覆盖了：

| 资源 | 位置 |
|---|---|
| bootstrap stack（`stack_bottom` 起的 16 KiB BSS） | 低内存 |
| VGA text memory | `0xb8000` |
| kernel 镜像（`_kernel_start` 从 1 MiB 起） | 1 MiB 附近 |
| 本课两张页表物理页 | 视 MBI 而定，通常落在前 4 MiB |
| 常见 QEMU/GRUB 的 MBI | 视当次启动而定 |

这是一个**明确的实验契约**：本课只在这 4 MiB 内保证 `physical == virtual` 可解引用，
不是"所有物理地址都可直接解引用"的永久规则。后续 higher-half 课程会打破这个等价。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 06） |
|---|---|---|
| `boot.S` | GRUB Multiboot2 i386 入口、32 位栈、`call kernel_main32` | 未变化 |
| `linker.ld` | 1 MiB 镜像基址、`_kernel_start/_kernel_end`、RX/RW 分离 | 未变化 |
| `kernel.c` | VGA shell + MBI parser + allocator + **新：identity paging** | 新增 7 个函数、4 个宏、3 个全局量、1 个命令 |
| `grub.cfg` | 菜单标题 | 微小变化（`lesson 6` → `lesson 7`） |
| `Makefile` | 编译/校验/ISO/QEMU | 未变化 |

### 3.2 boot.S —— 未变化，但职责需要重申

`boot.S` 与第六课逐字节相同：Multiboot2 header（magic `0xe85250d6`、architecture 0、
information-request tag 请求 type 6 mmap）、`.code32` 的 `_start`、`cli`、建栈、
`pushl %ebx; pushl %eax; call kernel_main32`。**没有变化的原因正是本课的教学点**：
分页开启前后 CPU 仍处于 32 位保护模式、代码段属性不变，指令编码不变，所以入口完全无需
改动——这就是"identity mapping 让切换对执行流透明"的最直接证据。

### 3.3 kernel.c —— 增量精讲

#### 3.3.1 新增宏与全局量

```c
#define PAGE_TABLE_ENTRIES 1024      /* 一张表 1024 项，正好对应 4 MiB/4 KiB */
#define PAGE_PRESENT_WRITABLE 0x003U /* P(bit0)|R/W(bit1)，本课唯一用到的表项属性 */
#define CR0_PG 0x80000000U           /* CR0 bit31：分页使能位 */
#define IDENTITY_MAP_END 0x00400000U /* 本课 identity window 上界（半开） */
...
static u64 page_directory_page, page_table_page;  /* 两张表的物理地址，pginfo 打印用 */
static int paging_enabled;                        /* 软件态镜像 CR0.PG，pginfo 打印用 */
```

`paging_enabled` 不是 CPU 状态本身，而是**软件记录的镜像**：在成功执行
`write_cr0(read_cr0() | CR0_PG)` **之后**才置 1。这样 `pginfo` 既能报告"硬件确实开了"，
又能避免在失败路径上误报。

#### 3.3.2 `zero_page()` —— 第六课"绝不写页"边界的第一次打破

```c
/* 此处 physical == virtual 仅在启用前、以及本课的前 4 MiB identity map 内成立。 */
static void zero_page(u64 physical_page)
{
    u32 index;
    volatile u32 *words = (volatile u32 *)(unsigned long)(u32)physical_page;
    for (index = 0; index < PAGE_TABLE_ENTRIES; index++) words[index] = 0;
}
```

- **签名与职责**：输入一张 4 KiB 物理页地址，把整页按 1024 个 u32 清零。
- **为什么可以这样写**：本课仍处于未分页（清零发生在开启分页前）且 identity window
  成立（之后对 `0xb8000` 等资源的写也成立）的前提下，`physical == virtual` 成立，
  物理地址可直接当指针解引用。注释里那句"仅在启用前、以及本课的前 4 MiB identity map
  内成立"是刻意留下的边界声明。
- **为什么不直接分配静态数组**：页表必须位于物理页帧且对 `page_is_reserved()` 可见，
  用 allocator 分配才能让第六课的历史数组记录它们，防止后续 `palloc` 把页表页再分给别人。
- **为什么用 `volatile`**：防止编译器认为"清零后没人读"而优化掉；写页表是副作用。

#### 3.3.3 CR3/CR0 寄存器读写 helpers

```c
static void write_cr3(u32 value) { __asm__ volatile ("movl %0, %%cr3" : : "r" (value) : "memory"); }
static u32 read_cr0(void) { u32 value; __asm__ volatile ("movl %%cr0, %0" : "=r" (value)); return value; }
static void write_cr0(u32 value) { __asm__ volatile ("movl %0, %%cr0" : : "r" (value) : "memory"); }
```

三个 helper 都是 32 位：`movl` 进/出控制寄存器。`write_cr3` 和 `write_cr0` 用 `"memory"`
clobber 告知编译器内存语义被改动，避免它对指令做重排。`read_cr0` 必须**读改写**而不是
直接写常量，因为 CR0 里还有 ET、NE、WP、AM 等既有位，本课只应追加 `PG` 一位而不得抹掉
其他位。

#### 3.3.4 `page_is_32bit()` 与 `enable_identity_paging()` —— 本课核心函数

```c
static int page_is_32bit(u64 page) { return page != 0 && (page & (PAGE_SIZE - 1)) == 0 && page <= 0xfffff000ULL; }
static int enable_identity_paging(void)
{
    volatile u32 *directory, *table;
    u32 index;
    page_directory_page = phys_alloc_page();
    page_table_page = phys_alloc_page();
    if (!page_is_32bit(page_directory_page) || !page_is_32bit(page_table_page)) { printk("paging error: cannot allocate tables\n"); return 0; }
    zero_page(page_directory_page);
    zero_page(page_table_page);
    directory = (volatile u32 *)(unsigned long)(u32)page_directory_page;
    table = (volatile u32 *)(unsigned long)(u32)page_table_page;
    for (index = 0; index < PAGE_TABLE_ENTRIES; index++) table[index] = (index * (u32)PAGE_SIZE) | PAGE_PRESENT_WRITABLE;
    directory[0] = (u32)page_table_page | PAGE_PRESENT_WRITABLE;
    write_cr3((u32)page_directory_page);
    write_cr0(read_cr0() | CR0_PG);
    paging_enabled = 1;
    return 1;
}
```

- **签名与职责**：无参，成功返回 1；失败打印错误串并返回 0。这是本课**唯一的"状态转换"
  函数**，把"两张空闲物理页 → 分页生效"整条链封装起来。
- **算法步骤**：
  1. 调用两次 `phys_alloc_page()`，得到 directory 与 table 两张物理页；
  2. 用 `page_is_32bit()` 校验：非零、4 KiB 对齐、且能塞进非 PAE 表项的高 20 位
     （`<= 0xfffff000`）。任何一条不满足就打印 `paging error: cannot allocate tables`；
  3. `zero_page()` 清零两页——**必须先清零再填表**，否则未初始化页里残留的随机值会被
     CPU 当成表项，出现随机映射；
  4. 填充 1024 个 PTE：`table[i] = i * 0x1000 | 0x003`，把第 i 个 4 KiB 页恒等映射；
  5. 写 `directory[0] = table 物理地址 | 0x003`，让目录第 0 项覆盖整个 4 MiB 窗口；
  6. `write_cr3(directory)` —— 先指定目录，再开 PG；
  7. `write_cr0(read_cr0() | CR0_PG)` —— 读改写，只追加 PG 位；
  8. **成功后才**置 `paging_enabled = 1` 并返回。
- **边界与错误处理**：allocator 可能耗尽（返回 0）；`page_is_32bit` 是硬性前置条件；
  失败路径打印明确错误串而不是静默。
- **为什么只建一张 page table（4 MiB）**：当前代码、栈、VGA、MBI、两张表页全部落在
  前 4 MiB。这是一张 table 恰好覆盖的"够用"窗口，是教学上的刻意收敛——"映射所有 RAM"
  或更大地址空间是后续 higher-half 课的课题。

#### 3.3.5 `show_paging_info()` 与 `pginfo` 命令

```c
static void show_paging_info(void)
{
    printk("paging: "); printk(paging_enabled ? "on" : "off"); vga_putc('\n');
    printk("directory: "); print_hex64(page_directory_page); vga_putc('\n');
    printk("table:     "); print_hex64(page_table_page); vga_putc('\n');
    printk("identity:  "); print_hex32(0); printk(" - "); print_hex32(IDENTITY_MAP_END); vga_putc('\n');
}
```

输出三行关键信息：paging 状态、两张表物理地址、identity window `00000000 - 00400000`。
注意 `identity:` 行打印的是 `0` 到 `IDENTITY_MAP_END` 两个**常量**，这是对"本课契约"
的显式声明，而不是从某个结构读出来的动态范围。

#### 3.3.6 `execute_command()` 与 `kernel_main32()` 的接线

```c
if (command_equals("help")) { printk("commands: help about clear mmap pinfo palloc pginfo\n"); return 0; }
if (command_equals("about")) { printk("TinyOS lesson 7: minimal 32-bit identity paging\n"); return 0; }
...
if (command_equals("pginfo")) { show_paging_info(); return 0; }
```

`kernel_main32()` 的启动序列变为：

```c
if (prepare_memory_map()) { phys_allocator_init(); if (!enable_identity_paging()) printk("warning: paging remains disabled\n"); } else printk("warning: Multiboot2 map is invalid\n");
```

顺序是**严格线性**的：`prepare_memory_map()` 验证 MBI → `phys_allocator_init()` 统计
usable pages → `enable_identity_paging()` 建表开分页。`enable_identity_paging` 失败时
打印 `warning: paging remains disabled` 而不是死循环——这样即便故障也能继续观察。

### 3.4 构建管线

Makefile 与第六课完全相同（`-m32 -ffreestanding -fno-pie -fno-stack-protector`、
`ld -m elf_i386 -T linker.ld -nostdlib`、`grub-file --is-x86-multiboot2` 做 `check`）。
本课没有新增任何编译标志或链接步骤——分页完全是运行时的 CPU 行为，不改变 ELF 布局。
这本身是值得记住的：`readelf -lW` 仍应看到独立的 `R E` 与 `RW` LOAD segment，没有
RWX 段。

### 3.5 主控制流

```text
GRUB i386 handoff (EAX=magic, EBX=mbi)
  → _start (boot.S: cli / 建栈 / push eax,ebx)
  → kernel_main32(magic, mbi_address)
      → prepare_memory_map()              验证 MBI，定位 type-6 mmap
      → phys_allocator_init()             统计 usable_pages
      → enable_identity_paging()          分配两页 → 清零 → 填 PTE → PDE[0] → CR3 → CR0.PG
      → reset_command() + print_prompt()  进入 shell
  → 键盘轮询循环: keyboard_poll_scancode → scancode_set1_to_ascii
      → handle_input_character → execute_command
          → "pginfo" → show_paging_info() 打印 paging/directory/table/identity
```

---

## 4. 数据流与运行逻辑

- **启动路径**：`enable_identity_paging()` 内部，`phys_alloc_page()` 返回的物理地址先是
  `page_directory_page` / `page_table_page` 两个 u64 全局量（供 `pginfo` 打印），再被
  转成 `u32` 写入 PDE 表项和 CR3。
- **命令路径**：键盘 scancode → `scancode_set1_to_ascii` → `command[]` 缓冲 →
  `execute_command()` 命中 `pginfo` 分支 → `show_paging_info()` → 打印串
  `"paging: "` 与 `"on"`、`"directory: "` + 16 位十六进制地址、`"table:     "` +
  16 位十六进制地址、`"identity:  "` + `"00000000"` + `" - "` + `"00400000"`。
- **输出串索引**（全部逐字抄录自源码，验证时照抄比对）：
  - `about` → `TinyOS lesson 7: minimal 32-bit identity paging`
  - `help` → `commands: help about clear mmap pinfo palloc pginfo`
  - `pginfo` → `paging: on` / `directory: <16 hex>` / `table:     <16 hex>` /
    `identity:  00000000 - 00400000`
  - `palloc` 失败 → `palloc: out of pages`
  - 启动异常 → `paging error: cannot allocate tables`、
    `warning: paging remains disabled`、`warning: Multiboot2 map is invalid`

---

## 5. 构建、运行与验证

依赖：`gcc`、`binutils`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`（与旧 README
一致，无新增）。

```bash
cd lessons/lesson-07-stable
make clean && make -j"$(nproc)"
make check
readelf -sW build/kernel.elf | grep -E '_kernel_(start|end)|stack_(bottom|top)'
readelf -lW build/kernel.elf
objdump -d -Mintel build/kernel.elf | grep -E 'mov.*cr3|mov.*cr0'
```

要求：无编译/链接警告（`-Werror`）；`make check` 输出 `Multiboot2 header check passed.`；
边界符号存在；`readelf -lW` 不出现 RWX LOAD segment；反汇编能搜到 `mov cr3,eax`、
`mov eax,cr0`、`mov cr0,eax`。

以正常 QEMU VGA 图形窗口启动（成功画面在图形窗口，勿加 `-display none`）：

```bash
make run
```

或显式等价：

```bash
qemu-system-x86_64 -accel tcg -m 128M -boot order=d \
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在窗口内依次输入（或用 QEMU monitor 的 `sendkey`）：

```text
pinfo<Enter>
pginfo<Enter>
palloc<Enter>
mmap<Enter>
help<Enter>
about<Enter>
clear<Enter>
help<Enter>
```

验收标准：
- `pginfo` 报告 `paging: on`，`directory` 与 `table` 是两个**不同**且 4 KiB 对齐的地址，
  `identity:  00000000 - 00400000`；
- boot 后 `pinfo` 的 `allocated pages` 至少为 2（两张表页，十六进制打印）；
- 分页开启后 VGA 显示、键盘、`mmap` 输出全部正常；
- `clear` 后屏幕上只有一个 `tinyos> ` prompt。

> **本次实际验证记录（2026-08-01）**：执行 `make clean && make -j$(nproc)` 与 `make check`
> 均通过且无警告；`objdump` 确认生成 `mov cr3,eax`、`mov eax,cr0`、`mov cr0,eax`；
> `readelf -lW` 保持独立 `R E`/`RW` LOAD segment。用 `-m 128M` 的 QEMU VGA 启动后，
> paging 已在 boot 时开启。通过 monitor `sendkey` 依次执行 `pinfo`、`pginfo`、`palloc`、
> `mmap`、`help`、`about`、`clear`、`help`；VGA shell 在 `CR0.PG` 开启后持续响应，
> 最终 clear/help 路径显示单 prompt 和完整命令列表。具体物理 frame 由当次 GRUB/QEMU
> MBI 决定，课程不将其硬编码。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 设置 `CR0.PG` 后立即重启/卡死 | 开启顺序错误或映射不完整 | 对照 Intel paging enable sequence：先填完整 map → 写 CR3 → 最后置 PG；当前 EIP、ESP、VGA 必须都被映射 |
| `pginfo` 显示 `paging: off` | 软件镜像未同步 | 检查只在 `write_cr0(read_cr0() \| CR0_PG)` 成功路径后置 `paging_enabled = 1` |
| page table 与之后的 `palloc` 返回重复页 | 两张表页没走 allocator | 两个 table frame 必须来自 `phys_alloc_page()`，进入 allocation history |
| directory/table 地址被截断 | 非 PAE 表项只有高 20 位可用 | 分配页须非零、4 KiB 对齐且 `<= 0xfffff000`，用 `page_is_32bit()` 校验 |
| VGA 在 paging 后消失 | identity window 没覆盖 `0xb8000` | 确认第一个 4 MiB window 覆盖 `0xb8000`；不可只映射 kernel |
| `mmap` 在 paging 后崩溃 | MBI 不在映射范围内 | 实测 MBI 落在前 4 MiB；future 课程按需映射 MBI |
| kernel 或 stack page fault | 镜像/栈不在映射内 | image base 1 MiB、stack 边界必须在 `[0, 4 MiB)`，查 linker.ld |
| 误把 virtual 当 physical | 地址空间边界 | 本课只在 identity window 内 `physical == virtual`；不要推广到 higher-half |
| PTE 未清零导致随机映射 | 页表初始化遗漏 | 分配成功后对完整 4 KiB 页显式 `zero_page()` |
| 试图测试 unmapped 地址 | 异常边界 | 本课没有 IDT/page-fault 输出；不要以无诊断的 fault 作为验证 |
| 试图现在进入 long mode | 增量架构 | PAE、EFER、GDT 和 far jump 是下一课的单独状态转换 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux v6.12 对应实现 | 权威来源 | 简化了什么 |
|---|---|---|---|
| `zero_page()` 手动清零 | `arch/x86/kernel/head_64.S` 早期页表用 `.quad` 数据直接定义，页表在镜像 BSS 中 | Intel SDM Vol.3 4.3 | Linux 早期页表是编译期布局的静态数据，不动态分配 |
| `phys_alloc_page()` 动态取页表页 | `mm/memblock.c: memblock_alloc_range_nid()` 在早期分配中拿页 | Linux memblock | Linux 有 free/bitmap/buddy 等完整回收链；本课只分配不回收 |
| 读改写 CR0 只加 PG | `arch/x86/kernel/head_64.S` 对 EFER/CR0 的写同样基于当前值位运算 | Intel SDM Vol.3 | Linux 还要设置 PAE、LME、NW、CD 等更多位 |
| 单张 4 MiB identity window | `arch/x86/kernel/head_64.S` 建立 `__START_KERNEL_map` 初始映射，覆盖镜像+栈 | Linux 早期映射 | Linux 初始映射更大且与 final 布局不同，需二次建立 final 页表 |
| 无 #PF 处理 | Linux 内核加载后立即装 IDT、`page_fault` 处理 | Linux x86 trap | 本课异常处理从第九课开始 |

---

## 8. 思考题与练习

1. **概念理解**：把线性地址 `0x003ff123` 手工切分为 PDE 索引、PTE 索引和页内偏移，
   说明它会被翻译到哪个物理地址。
2. **源码定位**：在 `kernel.c` 中指出"先清零再填表"的两行代码，解释如果把
   `zero_page()` 两个调用删掉，最可能出现的故障现象是什么。
3. **动手实验**：把 `enable_identity_paging()` 里的表项属性从 `0x003` 改成 `0x001`
   （只 Present 不 Writable），重新构建运行，观察哪类操作会出问题，并解释为什么。
4. **动手实验**：把 `IDENTITY_MAP_END` 的语义改小（比如只映射前 2 MiB）会怎样？
   用调试地图第 5、7 行预测故障，再实际验证。
5. **Linux 对照**：阅读 Linux v6.12 `arch/x86/kernel/head_64.S`，比较 Linux 的初始
   页表是编译期数据还是动态分配，指出 TinyOS 本课的设计差异及原因。

---

## 9. 本课小结与下一课预告

**小结**：
1. 分页 = 通过页目录/页表两级查表把虚拟地址翻译成物理地址；本课在 32 位非 PAE 模式下
   做了最小实现。
2. 两张从 allocator 分配的物理页分别变成 page directory 与 page table，第一次让第六课
   的"只记账"分配器产出真实硬件结构。
3. 开启分页有固定顺序：先建表 → 写 CR3 → 最后 `CR0.PG`；顺序错误会在置位瞬间崩溃。
4. `CR0` 必须读改写，只追加 PG 位；`paging_enabled` 是成功路径之后才置位的软件镜像。
5. 4 MiB identity window 是本课的实验契约：它覆盖代码、栈、VGA、MBI 和两张表页，
   使"打开分页的瞬间"对执行流完全透明。
6. 页表项 `0x003` = Present | Writable；非 PAE 表项只有高 20 位放地址，所以页必须
   4 KiB 对齐。
7. 本课没有 IDT、没有 #PF 诊断——这决定了调试地图里"故障=重启"的现象边界。

**下一课**：[`lesson-08-stable/README.md`](../lesson-08-stable/README.md) 将把本课的
单一转换升级为完整升级：PAE 开启、EFER.LME、CR4 四级页表（PML4/PDPT/PD/PT）、GDT far
jump，并引入第一个 `kernel64.c`——用 `-m64` 编译、`objcopy -O binary` + `.incbin`
内嵌的**双段构建**，从 32 位引导代码接管到 64 位内核。本课的两张页表正是下一课五页
四级结构（PML4/PDPT/PD/PT0/PT1）的预演。

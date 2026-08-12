# Lesson B10: E820 → type-6 mmap tag — 精讲文档

> **课号**：Lesson B10（Mini-GRUB 从零写 GRUB 课程第 10 课，可执行课）
> **主题**：INT 15 E820 内存图收集与 type-6 mmap tag 构建
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」第 5 课
> **前置课程**：[`b09-stable/README.md`](../b09-stable/README.md)（MBI 结构）；
> [`b05-stable/README.md`](../b05-stable/README.md)（实模式回调）
> **后续课程**：[`b11-stable/README.md`](../b11-stable/README.md)（装载框架）
> **一句话目标**：loader 把 BIOS 的 E820 内存图放进 MBI 的 type-6 tag，内核
> walker（TinyOS L05 同构）逐条打印物理内存区间。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 生成的 mmap tag 与真 GRUB 同构——自写
walker 内核打印出 7 条 E820 区间（可用/保留/4G 空洞），TinyOS L05 可直接消费。

- **在课程中的位置**：内存图是 loader 传给内核的最关键信息（内核分配器只认
  type=1 可用区间，TinyOS L06）。B09 的 MBI 有了骨架，本课填入 type-6。
  对照 GRUB `grub_machine_mmap_iterate`（kern/i386/pc/mmap.c）与
  `grub_fill_multiboot_mmap`（multiboot_mbi2.c）。
- **前置知识清单**：
  1. B05：实模式回调（E820 必须在实模式调 INT 15）；
  2. B09：MBI tag 布局与 8 对齐；
  3. INT 15 E820：`AX=0xE820`、`EDX='SMAP'`、`EBX=续传值`、`ECX=缓冲大小`、
     `ES:DI=缓冲`；返回 `CF`、`EAX='SMAP'`、`ECX=实际字节`，`EBX==0` 结束。
- **本课交付**：`build/b10.img`；QEMU 上内核打印 7 条内存区间
  （addr/len/type），`mmap entries=07`，walker 停在 end tag。

---

## 2. 核心概念精讲

### 2.1 概念一：INT 15 E820 调用约定

**定义**：E820 是 BIOS 报告物理内存图的现代接口：循环调用 `int $0x15`，每次
返回一个 `{u64 base, u64 len, u32 type}` 区间；`EBX` 是续传值，`EBX==0` 表示
结束。type：1=可用、2=保留、3=ACPI 可回收、4=NVS、5=坏内存。

**为什么需要**：引导器必须把"哪些内存可用"告诉内核；E820 是 PC 上的权威来源
（GRUB 的 `grub_machine_mmap_iterate` 直接透传 E820 结果）。

**工作机制**（本课 `mmap_collect`，对照 GRUB `grub_get_mmap_entry`）：

```c
        regs.eax = 0x0000e820u;
        regs.edx = 0x534d4150u;         /* 'SMAP' */
        regs.ebx = cont;                /* 续传值（0 开始，非 0 结束） */
        regs.ecx = 24u;                 /* 请求 24 字节条目 */
        regs.es  = (u16)((u32)e >> 4);  /* ES:DI = 条目缓冲 */
        regs.edi = (u32)e & 0xFu;
        ...
        bios_interrupt(0x15, &regs);

        if ((regs.flags & 0x01u) || regs.eax != 0x534d4150u ||
            regs.ecx < 20u || regs.ecx > 0x400u)
            break;                      /* 出错结束 */
        cont = regs.ebx;
        n++;
        if (cont == 0)
            break;                      /* 正常结束 */
```

错误判定与 GRUB 完全一致：`CF`、`EAX != 'SMAP'`、`ECX` 越界即终止。

### 2.2 概念二：type-6 mmap tag 布局

**定义**：MBI 的 mmap tag = `{u32 type=6, u32 size}` + `u32 entry_size` +
`u32 entry_version` + 条目流；条目 = `{u64 addr, u64 len, u32 type, u32 zero}`
（24 字节）。TinyOS L05 要求 `entry_size >= 24 && %8 == 0`、
`(size-16) % entry_size == 0`、`entry_version == 0`。

**为什么需要**：内核按 `entry_size` 步进遍历条目；任何字段错都会让 TinyOS
fail-closed。生成端必须逐项满足。

**工作机制**（`mbi_build` 的 mmap 段）：

```c
    if (mmap_count > 0) {
        u32 *mm = (u32 *)p;
        mm[0] = MB2_TAG_MMAP;
        mm[1] = 16u + 24u * mmap_count;
        mm[2] = 24u;                   /* entry_size */
        mm[3] = 0u;                    /* entry_version */
        p += 16u;
        for (i = 0; i < mmap_count; i++) {
            u64 *e = (u64 *)p;
            e[0] = mmap_entries[i].addr;
            e[1] = mmap_entries[i].len;
            ((u32 *)p)[4] = mmap_entries[i].type;
            ((u32 *)p)[5] = 0;         /* 规范第 4 字段 */
            p += 24u;
        }
    }
```

### 2.3 概念三：内核侧 mmap walker（与 TinyOS L05 同构）

**定义**：内核遍历 MBI tag 时，遇到 type-6 就解析条目并打印
`addr/len/type`——结构与 TinyOS `lessons/lesson-05-stable/kernel.c` 的
`show_memory_map` 一致。

**为什么需要**：B12 将用 TinyOS L05 内核直接替换自写内核；本课的 walker 就是
"预演"，保证 loader 侧字段与 TinyOS 的校验矩阵完全兼容。

**工作机制**（test-kernel.c）：

```c
        if (t->type == MB2_TAG_MMAP) {
            const u8 *ep = p + 16u;
            u32 i, n = (t->size - 16u) / 24u;
            ...
            for (i = 0; i < n; i++) {
                u64 addr = *(const u64 *)ep;
                u64 len  = *(const u64 *)(ep + 8u);
                u32 type = *(const u32 *)(ep + 16u);
                ...
                ep += 24u;
            }
        }
```

### 2.4 概念四：u64 与 -m32 的细节

**定义**：loader 与内核都编译为 -m32（i386 SysV ABI：`u64` 对齐到 4、需要
`unsigned long long`）。加载/存储 `u64` 内联展开为 8 字节移动，不引入 libgcc
依赖（除法才需要 `__udivdi3`，本课未用）。

**为什么需要**：E820 条目与 MBI 条目都有 64 位地址/长度字段，必须用 `u64`；
- m32 下 `typedef unsigned long long u64;` 是唯一正确选择。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B09） |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | 引导链与 BIOS 回调 | 未变化 |
| `loader.c` | `mmap_collect`（E820）+ `mbi_build` 加 type-6 tag | 新增收集与 tag |
| `test-kernel.c` | walker 打印 mmap 条目 + `vga_hex64` | 新增 mmap 打印 |
| `test-kernel.S`/`test-kernel.ld` | 内核入口与链接 | 未变化 |
| `Makefile` | 检查 `mmap_collect` 符号 | 修改 |
| `build/b10.img` | 软盘镜像 | 新增 |

### 3.2 `loader.c` 精讲

**结构**：

```c
struct mmap_entry {
    u64 addr;   /* +0  */
    u64 len;    /* +8  */
    u32 type;   /* +16 */
    u32 zero;   /* +20 */
};
static struct mmap_entry mmap_entries[24] __attribute__((aligned(8)));
static u32 mmap_count = 0;
```

`mmap_entries` 在 .bss（低内存），`ES:DI = (addr>>4):(addr&0xF)` 分解后可直接
作为 E820 缓冲。`mmap_collect` 见 2.1；注意 `__attribute__((noinline))` 防止
`-Os` 内联掉符号（`make check` 的符号断言依赖它）。

**`mbi_build`**：type-2 tag → type-6 mmap tag → end tag → 回填 `total_size`。
实测 `total_size=0xE0`（224）= 8 + 24 + (16 + 24×7) + 8，与 walker 打印一致。

### 3.3 `test-kernel.c` 精讲

新增 `vga_hex64`（高 32 位 + 低 32 位打印）与 mmap 分支（见 2.3）。期望输出
（QEMU -M pc 实测）：

```text
B10 test-kernel: magic ok
mbi total_size=000000e0
tag type=0002 size=0015
tag type=0006 size=00b8
  mmap entries=07
  addr=0000000000000000 len=000000000009fc00 type=01
  addr=000000000009fc00 len=0000000000000400 type=02
  addr=00000000000f0000 len=0000000000010000 type=02
  addr=0000000000100000 len=0000000007ee0000 type=01
  addr=0000000007fe0000 len=0000000000020000 type=02
  addr=00000000fffc0000 len=0000000000040000 type=02
  addr=000000fd00000000 len=0000000300000000 type=02
tag type=0000 size=0008
B10 walker done: end tag reached
```

第 4 条 `0x100000-0x7FE0000` type=01 是常规内存之上的主可用区；最后一条
`0xFD00000000` 是 4 GiB 以上的 PCI 空洞（保留）。

### 3.4 构建管线与主控制流

```text
BIOS → stage1 → stage2 → pm32 → loader_main(C)
  → mmap_collect() 收集 E820（7 条）
  → 读内核 → mb2_header_check → elf_load
  → mbi_build()：type-2 + type-6 + end，total_size=0xE0
  → mb2_boot(entry, mbi) → kernel_main → walker 打印 mmap → hlt
```

`make check` 断言 `mmap_collect`/`kernel_main`/`mb2_boot` 符号与 grub-file。

---

## 4. 数据流与运行逻辑

```text
BIOS E820（INT 15 循环，EBX 续传）→ mmap_entries[7]（.bss，低内存）
  → mbi_build 拷贝为 type-6 tag（entry_size=24, version=0, 7 条目）
  → EBX = mbi → kernel_main → walker 按 (size+7)&~7 步进
  → type=6 → 按 entry_size=24 打印 addr/len/type → end tag → 完成
```

自动化验证 marker：`type=0006`（mmap tag 存在）、`B10 walker done`（遍历完整）。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b10-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b10 check
bootloader-course/scripts/validate-course.sh b10 qemu
```

### 5.2 期望输出

- `make check`：`B10 check PASS: stage1=512+55aa stage2=4096B mmap_collect+kernel_main present`
- QEMU：见 3.3。

### 5.3 成功判据

7 条 E820 区间完整打印、type 字段正确（1 可用 / 2 保留）、`mmap entries=07`、
walker 停在 end tag、QEMU trace 无异常。用 `-M pc` 的默认内存布局可对照
（1 MiB 标记、9FC00 EBDA、4 GiB 空洞）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `mmap entries=00` | E820 调用失败 | 检查 `regs` 字段：`EAX=0xE820`、`EDX='SMAP'`、`ES:DI` 分解；CF/EAX 判定 |
| E820 循环不结束 | `EBX` 续传值没保存 | 确认 `cont = regs.ebx` 且 `regs.ebx = cont` 回传 |
| walker 看不到 type-6 | mmap tag 没写进 MBI | 检查 `mbi_build` 的 `mmap_count > 0` 分支；`total_size` 是否包含 mmap tag |
| `mmap entries` 数量与打印不符 | entry_size 或步长错 | 对照 `(size-16)/24` 与 `ep += 24u` |
| `total_size` 偏小（如 0x28） | 构建顺序错（collect 在 build 之后）或 mmap 分支缺失 | 检查 `loader_main` 调用顺序；`mbi_build` 是否真的含 mmap 段 |
| 打印乱地址 | 条目 `zero` 字段未清零或字节序错 | E820 前先清零条目；`u64` 用 `unsigned long long` |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/kern/i386/pc/mmap.c` 的 `grub_get_mmap_entry`
（E820 寄存器组装与错误判定：`CF || EAX!='SMAP' || ECX 越界`）与
`grub_machine_mmap_iterate`（循环 + 续传），以及 `multiboot_mbi2.c` 的
`grub_fill_multiboot_mmap`（`entry_size = sizeof(struct multiboot_mmap_entry)`
= 24、`entry_version = 0`、size = 头部 + 24×条目数）。对照点：

- **相同**：E820 参数与错误判定逐字对应；mmap tag 的 entry_size/version/条目
  布局与 `include/grub/multiboot2.h` 的 `struct multiboot_mmap_entry` 一致；
- **简化**：GRUB 的 `grub_machine_mmap_register` 会挂钩 INT 12/15 保证引导期
  内存图正确（自己的占用区标保留）；本课直接透传 E820，不做挂钩（教学聚焦
  E820 本身）；
- **下一步**：B12 用 TinyOS L05 内核（其 `show_memory_map` 与 type-6 校验矩阵
  与真 GRUB 兼容）直接消费本 loader 的 mmap tag，验证"同构"。

Linux 对照：`arch/x86/kernel/e820.c` 是内核侧 E820 管理，仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么 `EBX` 续传值必须原样回传？E820 返回的 `EBX` 是"下一个
   条目"的句柄，不是连续整数（QEMU 实测 1、4、5、6、0）。
2. 动手实验（临时副本）：把 `regs.ecx` 改成 20，观察条目 `zero` 字段是否被
   清零、walker 是否仍正常。
3. 动手观察：`-M pc` 的 QEMU 内存布局（可用/保留区间）与打印的 7 条对照；
   解释 4 GiB 以上空洞。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "0xe820\|0x534d4150\|entry_size" grub-core/kern/i386/pc/mmap.c`，
   对照本课实现。
5. 综合：画出 E820 7 条区间在物理内存图上的位置（含 EBDA、1 MiB 标记、
   4 GiB 空洞），与 TinyOS L05 的 mmap 显示预期对照。

---

## 9. 本课小结与下一课预告

**小结**：本课把真实内存图送进了 MBI。关键收获：(1) E820 调用约定
（EAX/'SMAP'/EBX 续传/ES:DI/ECX）与错误判定逐字对照 GRUB；(2) type-6 mmap
tag 布局（entry_size=24、entry_version=0、条目含 zero 字段）满足 TinyOS L05
的全部硬性校验；(3) 内核 walker 与 TinyOS `show_memory_map` 同构；
(4) `-m32` 下 `u64 = unsigned long long`、noinline 防止符号被内联；
(5) 实测 7 条 E820 区间（含 4 GiB 空洞）完整传递。至此 MBI 携带了
boot-loader-name 与 mmap 两类 tag，与真 GRUB 生成的结构同构。

**下一课预告**：进入 [`b11-stable/README.md`](../b11-stable/README.md)。装载
能力齐了，开始整理**框架**：把"读文件、校验、装载、建 MBI、交接"拆成 load
注册与 boot 执行两阶段，对照 GRUB `grub_loader_set`/`grub_loader_boot`，
为 B12 的综合 checkpoint（启动 TinyOS 主线）铺路。

# Lesson B09: MBI 结构 — 精讲文档

> **课号**：Lesson B09（Mini-GRUB 从零写 GRUB 课程第 9 课，可执行课）
> **主题**：Multiboot Information：`total_size` + tag 链 + end tag 的正式 MBI 构建
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」第 4 课
> **前置课程**：[`b08-stable/README.md`](../b08-stable/README.md)（PT_LOAD 装载与首个交接）；
> 研读支线 0.6（GRUB 生成 Multiboot information tags）
> **后续课程**：[`b10-stable/README.md`](../b10-stable/README.md)（E820 → type-6 mmap tag）
> **一句话目标**：loader 构建结构合法的 MBI（含 type-2 boot loader name tag），
> 自写 C 内核 walker 逐 tag 遍历并正确停在 end tag——为 TinyOS 风格的内核
> （L05 起）做好信息通道。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 生成的 MBI 能被自写 walker 内核逐 tag
遍历（无越界、无死循环），打印出 `total_size` 与每个 tag 的 `type/size`。

- **在课程中的位置**：B08 交接时 EBX 指向 16 字节占位；本课实现正式 MBI。
  MBI 是 GRUB→内核的"信息通道"，B10 往里加内存图、B21 加 framebuffer。
  对照 GRUB `multiboot_mbi2.c` 的 `make_mbi` 框架。
- **前置知识清单**：
  1. B08：交接 ABI（EBX=MBI、8 对齐）；
  2. MBI 布局：`u32 total_size` @0、`u32 reserved` @4、tag 从 @8 开始；
  3. 研读支线 0.6（GRUB 生成 tags 清单：cmdline、boot loader name、mmap…）。
- **本课交付**：`build/b09.img`；QEMU 上内核打印 `magic ok`、`total_size=00000028`、
  两个 tag（type=0002 size=0015、type=0000 size=0008）与 `walker done`。

---

## 2. 核心概念精讲

### 2.1 概念一：MBI 布局与遍历规则

**定义**：MBI = `u32 total_size`（整个 MBI 字节数，含自身）+ `u32 reserved`(=0)
+ tag 流；tag = `{u32 type, u32 size}` + payload，8 字节对齐；end tag =
`type=0, size=8`。遍历步长 = `(size + 7) & ~7`。

**为什么需要**：内核（TinyOS L05 起）靠这套规则定位信息；`total_size` 约束
遍历边界，end tag 提供终止条件——结构不合法内核就 fail-closed。

**工作机制**（内核 walker，`test-kernel.c`）：

```c
    p = (const u8 *)mbi + 8u;
    for (;;) {
        const struct mb2_tag *t = (const struct mb2_tag *)p;
        vga_puts("tag type="); vga_hex(t->type, 4);
        vga_puts(" size=");    vga_hex(t->size, 4); vga_puts("\n");
        if (t->type == MB2_TAG_END)
            break;
        p += (t->size + 7u) & ~7u;      /* 8 字节对齐步进 */
    }
```

### 2.2 概念二：生成端（loader）的构建方法

**定义**：loader 在固定缓冲里逐个 append tag、按 8 对齐前进，最后回填
`total_size`——先占位、后结算。

**为什么需要**：`total_size` 依赖所有 tag 的总长，构建完成后才知道；先跳过
8 字节头部、最后写入是最简单的"两遍法"。

**工作机制**（`mbi_build`）：

```c
static u8 mbi_buf[128] __attribute__((aligned(8)));
static const char boot_loader_name[] = "Mini-GRUB 0.1";

static u32 mbi_build(void)
{
    u8 *p = mbi_buf + 8u;              /* 跳过 total_size + reserved */
    ...
    t->type = MB2_TAG_BOOT_LOADER_NAME;   /* type-2 tag */
    t->size = 8u + namelen;
    p += 8u;
    for (i = 0; i < namelen; i++) p[i] = (u8)boot_loader_name[i];
    p += namelen;
    while ((u32)p & 7u) *p++ = 0;        /* 8 字节对齐 */

    t = (struct mb2_tag *)p;              /* end tag */
    t->type = MB2_TAG_END;
    t->size = 8u;
    p += 8u;

    *(u32 *)mbi_buf = (u32)(p - mbi_buf); /* 回填 total_size */
    *(u32 *)(mbi_buf + 4u) = 0;
    return (u32)(u32 *)mbi_buf;
}
```

实测布局：`8（头部）+ 24（type-2：8 头 + 13 名字 + 3 对齐）+ 8（end tag）= 40 =
0x28`，walker 打印的 `total_size=00000028` 与之一致。

### 2.3 概念三：自写内核升级为 C（TinyOS 结构对齐）

**定义**：测试内核从纯汇编升级为"汇编入口 + C 逻辑"，结构对齐 TinyOS：
汇编 `_start` 建栈、传 `EAX/EBX` 给 `kernel_main(magic, mbi)`；C 里做检查与
输出。

**为什么需要**：MBI walker、内存图打印等逻辑用 C 表达更清晰，也与 TinyOS 内核
（`kernel.c`）同构——B10 的内存图 walker 将直接照搬 TinyOS L05 的结构。

**工作机制**（test-kernel.S 入口）：

```asm
_start:
    cli
    movl $stack_top, %esp        # 内核自建栈（loader 不提供）
    pushl %ebx                   # mbi 地址
    pushl %eax                   # magic
    call kernel_main             # void kernel_main(u32 magic, u32 mbi)
```

注意：**loader 不替内核建栈**（Multiboot2 ABI），内核必须在 .bss 里备好 4 KiB
栈——这正是 TinyOS `boot.S` 的 `stack_bottom: .skip 16384` 的缩小版。

### 2.4 概念四：fail-closed 的 magic 检查

**定义**：内核入口第一件事检查 `EAX == 0x36d76289`（magic）；不符则报错不继续。
loader 侧同理：交接前必须 `mb2_header_check` 通过才装载。

**为什么需要**：magic 是"loader 符合 Multiboot2"的唯一凭证；TinyOS L05 起内核
对 magic/对齐/end tag 全部零容忍。

**工作机制**（test-kernel.c）：

```c
    if (magic != MB2_BOOT_MAGIC) {
        vga_puts("B09 test-kernel: BAD magic=");
        vga_hex(magic, 8);
        vga_puts("\n");
        return;
    }
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B08） |
|---|---|---|
| `stage1.S` | 引导扇区 | 未变化 |
| `stage2.S` | 切换 + BIOS 回调 + `mb2_boot` | 未变化 |
| `loader.c` | VGA/磁盘/ELF/MB2 + `mbi_build` | 新增 MBI 构建 |
| `test-kernel.S` | 内核入口：建栈 + 传参（C 化） | 重写 |
| `test-kernel.c` | 内核逻辑：magic 检查 + MBI walker | 新增 |
| `test-kernel.ld` | 内核链接脚本 | 未变化 |
| `Makefile` | 内核改为 S+C 双目标 | 修改 |
| `build/b09.img` | 软盘镜像 | 新增 |

### 3.2 `loader.c` 精讲

新增 `mb2_tag` 结构、`mbi_buf`（8 对齐 .bss）、`mbi_build`（见 2.2）。
`loader_main` 尾部改为：

```c
    vga_puts("B09 boot: entry=");
    vga_hex(eh->e_entry, 8);
    vga_puts(" eax=36d76289 mbi=");
    vga_hex(mbi_build(), 8);
    vga_puts("\n");
    ...
    mb2_boot(eh->e_entry, mbi_build());
```

### 3.3 `test-kernel.c` 精讲

内核侧 VGA 库（`vga_putc/puts/hex`，与 loader 同构但独立）、`kernel_main`
（见 2.1/2.4）。输出：

```text
B09 test-kernel: magic ok
mbi total_size=00000028
tag type=0002 size=0015
tag type=0000 size=0008
B09 walker done: end tag reached
```

### 3.4 构建管线与主控制流

```text
test-kernel.S + test-kernel.c --as/gcc--> test-kernel.elf(5588B, 16 扇区)
stage1/stage2 同 B08

BIOS → stage1 → stage2 → pm32 → loader_main(C)
  → 读内核 → mb2_header_check → elf_load（两个 LOAD 段）
  → mbi_build() → mb2_boot(entry, mbi)
  → _start 建栈 → kernel_main(magic, mbi)
  → magic ok → walk MBI → 打印 tags → end tag → hlt
```

---

## 4. 数据流与运行逻辑

```text
loader: mbi_buf(.bss @~0x8738) ← 头部(8B) + type-2 tag(24B) + end tag(8B)
  → EBX = 0x8738 → mb2_boot → kernel_main
  → 读 mbi[0..3]=total_size=0x28 → p = mbi+8
  → tag{type=2,size=21} → tag{type=0,size=8} → break
```

自动化验证 marker：`B09 test-kernel: magic ok`（交接 magic 正确）、
`B09 walker done`（tag 链遍历完整）。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b09-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b09 check
bootloader-course/scripts/validate-course.sh b09 qemu
```

### 5.2 期望输出

- `make check`：`B09 check PASS: stage1=512+55aa stage2=4096B mbi_build+kernel_main present`
- QEMU：见 3.3（loader 日志 + 内核 walker 输出）。

### 5.3 成功判据

内核打印 `magic ok`（交接 magic 正确）、`total_size` 与 tag 布局和
`mbi_build` 的计算一致、walker 停在 end tag、QEMU trace 无异常。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 内核打印 BAD magic | EAX 在交接前被破坏 | 检查 `mb2_boot` 顺序：EAX 最后装载、`jmp *%edx` 直接跳 |
| walker 死循环/越界 | tag 对齐步长或 end tag 缺失 | 检查 `(size+7)&~7`；`mbi_build` 是否写了 end tag |
| `total_size` 与布局不符 | 回填时机/计算错 | 手算：8 + 对齐后的 type-2 tag + 8；对照打印值 |
| 内核栈异常 | 栈顶地址未对齐或越界 | 检查 `stack_top` 在 .bss 末尾、4K 对齐；装载日志的 RW 段 memsz |
| walker 打印出乱 tag | MBI 地址错（EBX 指向非 MBI） | 确认 `mb2_boot` 传 `mbi_build()` 的返回值（8 对齐） |
| 内核不执行 | 内核 bss 栈未被清零 | `elf_load` 对 memsz>filesz 的段补零（RW 段 filesz=0） |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/loader/multiboot_mbi2.c` 的 `make_mbi` /
`grub_multiboot_make_mbi2`（tag 链构建、对齐、`total_size` 汇总）与
`include/grub/multiboot2.h`（`multiboot_tag` 结构）。对照点：

- **相同**：先占位头部、逐个 append tag、8 对齐步进、end tag 收尾、最后回填
  `total_size`；type-2 boot loader name tag 是 GRUB 默认生成的 tag 之一
  （研读支线 0.6 观察过 "GNU GRUB <version>"）；
- **简化**：GRUB 的 MBI 含 mmap、cmdline、boot device、elf-sections 等更多
  tag（按 info request 与运行状态生成）；本课只做 type-2 + end；
- **内核侧**：本课 walker 的结构与 TinyOS `lessons/lesson-05-stable/kernel.c`
  的 `show_memory_map` walker 同构（对齐步进 + end tag 判定）；
- **下一步**：B10 加 type-6 mmap tag（数据源 INT 15 E820），届时 TinyOS L05
  能直接消费本 loader 的 MBI。

Linux 对照：`arch/x86/kernel/setup.c` 解析 e820 的方式与 MBI mmap walker 思路
同源；仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么 `total_size` 必须最后回填？先写死会有什么后果？
2. 动手实验（临时副本）：在 `mbi_build` 里去掉 end tag，观察内核 walker 行为
   （越界打印），再改回来。
3. 动手观察：用 `od -An -tx4 -j0 -N40` 检查运行时 MBI（0x8738 处）的
   `total_size` 与 tag 字段，对照内核打印。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME\|grub_multiboot_make_mbi2" grub-core/loader/multiboot_mbi2.c`，
   找出 GRUB 生成 type-2 tag 的代码。
5. 综合：画出 `mbi_build` 的内存布局（含每个字节的归属与对齐填充），与 walker
   的遍历路径一一对应。

---

## 9. 本课小结与下一课预告

**小结**：本课把交接时的 MBI 占位升级为正式结构。关键收获：(1) MBI = total_size
+ reserved + 8 对齐 tag 链 + end tag，遍历步长 `(size+7)&~7`；(2) 生成端先
占位、后回填 total_size 是"两遍法"；(3) type-2 boot loader name 是 GRUB 默认
tag，本课复刻；(4) 内核升级为"汇编入口 + C 逻辑"，与 TinyOS 同构，magic
检查 fail-closed；(5) 内核自建栈是 Multiboot2 ABI 的要求。walker 实测遍历
`total_size=0x28`、type-2（size=0x15）、end tag（size=8）完整无误。

**下一课预告**：进入 [`b10-stable/README.md`](../b10-stable/README.md)。MBI 里
最重要的 tag 是 type-6 内存图——用实模式回调收集 INT 15 E820 结果，构造成 mmap
tag 传给内核。届时 loader 的 MBI 与真 GRUB 同构，TinyOS Lesson 05 的 mmap
显示可以直接消费它（B12 验证）。

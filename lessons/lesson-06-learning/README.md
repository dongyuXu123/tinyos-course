# Lesson 06: 从原始内存地图分配早期物理页

> **课程状态：学习版（可编辑，尚未归档）**  
> TinyOS 仍运行在 GRUB 交接后的 32 位保护模式、未分页阶段。本课在第五课的原始 Multiboot2 memory map 上增加 reservation-aware 的 4 KiB 物理页选择器。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：让 TinyOS 从 GRUB 报告的 `available` 内存中，排除已经属于低端平台、内核、栈和 MBI 的页，选择一张可见的、4 KiB 对齐的物理页地址。

责任边界：

- **Multiboot2 specification / GNU GRUB**：定义 type-6 memory-map tag、type `1` usable range、`EAX` magic 与 `EBX` MBI physical address；GRUB 的 `multiboot2` 命令提供这份 MBI。
- **Linux v6.12 `arch/x86/include/uapi/asm/e820.h`**：`E820_TYPE_RAM == 1` 是只从 RAM-type range 取候选的类型语义对照。
- **Linux v6.12 `arch/x86/kernel/e820.c`**：`e820__update_table()`、`e820__range_update()`、`e820__range_remove()` 与 `e820__memblock_setup()` 说明真实内核会先整理 firmware map。
- **Linux v6.12 `mm/memblock.c`**：`memblock_reserve()` 和 `memblock_alloc_range_nid()` 是明确保留范围、按对齐约束分配并立即保留的真实工程锚点。
- **Linux v6.12 `arch/x86/kernel/vmlinux.lds.S`**：内核镜像边界与段布局的对照；本课 linker script 导出 `_kernel_start` / `_kernel_end`。

**简化声明**：这不是 Linux memblock。本课没有 free、bitmap、buddy、NUMA、DMA zone、页表、页清零、动态 reservation list、map 排序/合并/重叠消解，也不消费 modules、framebuffer、ACPI、EFI 等 tag。新资源一旦被使用，必须先加入同一个 reservation predicate。`palloc` **只打印并记录物理地址，绝不写入该页**。

## 第二部分：核心设计解剖（Design Anatomy）

所有物理范围均使用半开区间 `[start, end)`：起点属于范围，终点不属于范围。

```text
Multiboot2 type-1 available range
        │
        ├── start 向上对齐到 4 KiB
        └── end   向下对齐到 4 KiB
                  │
                  ▼
         对每个完整候选页 [page, page + 0x1000)
                  │
    ┌─────────────┴──────────────────────────────────┐
    │ page_is_reserved() 是否重叠下列任一范围？        │
    │ [0, 1 MiB)                                      │
    │ [_kernel_start, _kernel_end)                    │
    │ [stack_bottom, stack_top)                       │
    │ [MBI address, MBI address + total_size)         │
    │ 已返回的历史页                                  │
    └─────────────┬──────────────────────────────────┘
                  │ 否
                  ▼
         palloc 返回 page，cursor 前进一个 4 KiB 页
```

`align_up_page()` 和 `align_down_page()` 将候选范围向内收缩，因此不会返回仅有一部分位于 available range 的页。每页都先经过区间相交判断；低 1 MiB 是刻意保守的 legacy reservation，所以 `0xb8000` VGA 页不会被返回。allocator 向上扫描原始 map 的报告顺序；用历史数组避免 raw map 未排序或重叠时重复返回同一页。

## 第三部分：增量代码交付（Incremental Code Delivery）

```text
lesson-06-learning/
├── boot.S       # 导出 stack_bottom / stack_top，保留 MBI i386 交接
├── linker.ld    # 新增 _kernel_start / _kernel_end
├── kernel.c     # 共享 MBI map 验证、新增 reservation、pinfo、palloc
├── grub.cfg     # 标题改为 TinyOS lesson 6
├── Makefile     # 不变：仍以 GRUB ISO 启动
└── README.md
```

`boot.S` 仍把 `(EAX, EBX)` 按 i386 cdecl 传给 `kernel_main32()`；本课只增加可被 C 代码引用的栈边界符号。`linker.ld` 将 `_kernel_start` 放在 1 MiB 镜像起点，将 `_kernel_end` 放在 `.bss` 和 `COMMON` 之后，因此 reservation 覆盖 Multiboot2 header、代码、只读数据、可写数据和 BSS。

`prepare_memory_map()` 延续第五课的 magic、MBI 对齐、总大小、tag size、8-byte round-up、end-tag 和 runtime `entry_size` 检查。`mmap` 与 allocator 共用这一条已验证的访问路径，避免出现两份不一致的协议解析器。

命令：

```text
help                 # 列出命令
mmap                 # 显示原始 Multiboot2 map
pinfo                # 显示页大小、保留边界、usable/allocated 计数
palloc               # 返回一个新的、仅记录的物理页地址
```

## 第四部分：编译与运行验证（Verification）

```bash
cd lessons/lesson-06-learning
make clean && make -j$(nproc)
make check
readelf -sW build/kernel.elf | grep -E '_kernel_(start|end)|stack_(bottom|top)'
readelf -lW build/kernel.elf
```

`make check` 必须输出：

```text
Multiboot2 header check passed.
```

符号表必须含 `_kernel_start`、`_kernel_end`、`stack_bottom`、`stack_top`；program headers 不应重新出现 RWX `LOAD` segment。使用正常 QEMU VGA 图形窗口（不要 `-display none`）验证：

```bash
qemu-system-x86_64 -accel tcg -m 128M -boot order=d \
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

输入：

```text
pinfo<Enter>
palloc<Enter>
palloc<Enter>
palloc<Enter>
mmap<Enter>
help<Enter>
about<Enter>
```

检查三个 `palloc` 地址：非零、16 个十六进制字符、4 KiB 对齐、递增，且都不低于 1 MiB，不与 `pinfo` 显示的 kernel/stack/MBI 区间相交。`allocated pages` 在第一次 `pinfo` 时为零，三次成功后应为三；`mmap`、`help`、`about` 必须仍能工作。

> **本次实际验证记录（2026-08-01）**：已执行零警告 `make clean && make -j$(nproc)` 与 `make check`，后者输出 `Multiboot2 header check passed.`。符号检查得到 `_kernel_start=0x00100000`、`stack_bottom=0x00102000`、`stack_top=0x00106000`、`_kernel_end=0x001062a8`；`readelf -lW` 显示分离的 `R E` 与 `RW` LOAD segments，未出现 RWX。以 `-m 128M` 启动正常 QEMU VGA，依次执行 `pinfo`、三次 `palloc`、`mmap`、`help`、`about`；三次返回 `0000000000117000`、`0000000000118000`、`0000000000119000`，均为 4 KiB 对齐，按 `0x1000` 单调递增，位于 1 MiB 以上且不在上述 kernel、stack、MBI reservation 中；兼容命令输出正常。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 对照来源 | 检查 |
|---|---|---|
| 返回 `0x000b8000` 或小于 1 MiB | legacy platform reservation | `page_is_reserved()` 必须拒绝 `[0, 0x00100000)`。 |
| 覆盖自己后随机崩溃 | linker / early stack ownership | 检查 `_kernel_start/_kernel_end` 与 `stack_bottom/stack_top` 是否导出且参与 predicate。 |
| `mmap` 后 `palloc` 失效 | Multiboot2 MBI lifetime | 保留完整 `[mbi, mbi + total_size)`，并共用已验证 tag walker。 |
| 地址不是 `0x1000` 对齐 | page allocation rule | range start 上对齐、end 下对齐；返回前不应丢失低 12 bits。 |
| 高地址 wrap 成低地址 | u64 range arithmetic | 检查 `addr + len < addr` 和对齐加法溢出。 |
| 重叠 map 中重复返回一页 | raw firmware-map boundary | 历史页拒绝重复；Linux 会先在 `e820.c` 规范化，TinyOS 尚未做。 |
| available 直接当作安全页 | `e820.c` / `memblock.c` | type-1 只是候选；必须扣除所有已知 reservation。 |
| `palloc: out of pages` | allocator capacity | 可用页耗尽或超过本课 64 条历史记录；本课无 free。 |
| 把地址当虚拟地址写入 | paging boundary | 本课未分页，且刻意不写新页；未来 paging 后必须区分 physical/virtual。 |
| future module/framebuffer 被覆盖 | reservation model boundary | 请求/使用新 Multiboot2 tag 前先将它的物理范围加入 predicate。 |
| MBI tag 错位 | Multiboot2 map format | 必须以 runtime `entry_size` 步进，不能用 `sizeof(struct mb2_mmap_entry)`。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 Linux v6.12 `arch/x86/kernel/e820.c` 的 `e820__update_table()`，说明为什么 Linux 不能按 firmware 报告顺序直接 bump allocate。
2. 阅读 `mm/memblock.c` 的 `memblock_reserve()` 与 `memblock_alloc_range_nid()`，对比它们与本课单一 predicate 的能力差异。
3. 使用 `readelf -sW` 找到本课四个边界符号，画出 kernel image 与 bootstrap stack 的包含关系。
4. 思考下一课建立 x86_64 页表时：页目录本身来自哪里？答案是先用本课相同的物理页所有权规则获得页，再将其映射到明确的虚拟地址。

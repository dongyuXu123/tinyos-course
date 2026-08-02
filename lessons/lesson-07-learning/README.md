# 第七课：用已分配物理页建立最小 32 位分页

> **课程状态：学习版（可编辑，尚未归档）**  
> TinyOS 仍从 GRUB 的 Multiboot2 i386 handoff 进入 32 位保护模式。本课首次让第六课分配出的物理页承担真实工作：构建页目录、页表，开启 `CR0.PG`，并保持 VGA shell 可用。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：建立 `[0x00000000, 0x00400000)` 的 4 KiB-page identity mapping，使当前内核在打开分页后仍能继续执行和交互。

责任边界：

- **Intel® 64 and IA-32 Architectures Software Developer’s Manual, Vol. 3**：32 位 paging、PDE/PTE 格式、`CR3` 和 `CR0.PG` 的规范权威。
- **Multiboot2 specification / GNU GRUB**：GRUB i386 handoff、MBI 和 memory map 的规范/loader 权威。
- **Linux v6.12 `arch/x86/kernel/head_64.S`**：真实内核早期页表与 CPU 状态过渡的工程对照；它不是 Multiboot2 或 CPU 指令的规范来源。
- **Linux v6.12 `arch/x86/kernel/e820.c`、`mm/memblock.c`**：firmware range 整理、reservation 与 early physical allocation 的工程对照。

本课保留第五、六课的 MBI parser 和 physical allocator：只扫描 type-1 range，并保留低 1 MiB、kernel image、bootstrap stack、MBI 与历史分配页。

**简化边界**：仅使用 32 位 non-PAE paging；没有 PAE、四级页表、EFER、long mode、GDT far jump、higher-half、page fault handler、用户权限、free 或页表回收。第八课才进入 x86_64 long mode。

## 第二部分：核心设计解剖（Design Anatomy）

```text
第六课 phys_alloc_page()
       │
       ├── 4 KiB page directory physical frame
       └── 4 KiB page table physical frame
                  │
                  ▼
page directory[0] = table physical address | Present | Writable
page table[i]      = (i * 0x1000)          | Present | Writable
                  │
                  ▼
virtual 0x00000000 ───────── identity ───────► physical 0x00000000
virtual 0x003ff000 ───────── identity ───────► physical 0x003ff000
                  │
                  ▼
write CR3 → set CR0.PG → paging enabled
```

页目录的第 0 项覆盖前 4 MiB。它包含当前 kernel image（从 1 MiB 开始）、bootstrap stack、常见 QEMU/GRUB MBI 位置、VGA text memory `0xb8000`，以及本课分配给页表的物理页。这个范围是本课的明确实验契约，**不是**“所有物理地址都可直接解引用”的永久规则。

页目录和页表都通过 `phys_alloc_page()` 获得，因而立即出现在 allocation history 中，不会被之后的 `palloc` 重用。成功分配后才通过显式循环清零并写入 PTE；这是第六课“只返回地址、绝不写页”边界的第一次有目的扩展。

## 第三部分：增量代码交付（Incremental Code Delivery）

```text
lesson-07-learning/
├── boot.S       # 不变：GRUB Multiboot2 i386 entry 与 32 位栈
├── linker.ld    # 不变：1 MiB image base、边界符号、RX/RW 分离
├── kernel.c     # 新增 CR3/CR0 helpers、two-page identity map、pginfo
├── grub.cfg     # 标题改为 TinyOS lesson 7
├── Makefile     # 不变：gcc -m32、freestanding、GRUB ISO
└── README.md
```

`enable_identity_paging()` 的固定顺序：

1. 调用 `phys_alloc_page()` 两次；
2. 验证地址非零、4 KiB 对齐并可由非 PAE 32 位 page entry 表示；
3. 清零两个页；
4. 填充 1024 个 PTE，安装 `PDE[0]`；
5. 将 directory physical address 写入 `CR3`；
6. 读取 `CR0`，设置 `PG` bit，再写回；
7. 仅在成功后报告 `paging: on`。

新增命令：

```text
pginfo     # paging 状态、directory/table physical address、identity range
```

原有命令仍可用：`help about clear mmap pinfo palloc`。

## 第四部分：编译与运行验证（Verification）

```bash
cd lessons/lesson-07-learning
make clean && make -j$(nproc)
make check
readelf -sW build/kernel.elf | grep -E '_kernel_(start|end)|stack_(bottom|top)'
readelf -lW build/kernel.elf
objdump -d -Mintel build/kernel.elf | grep -E 'mov.*cr3|mov.*cr0'
```

要求：无编译/链接警告；`make check` 输出 `Multiboot2 header check passed.`；边界符号仍存在；`readelf -lW` 不出现 RWX LOAD segment；反汇编含 CR3 和 CR0 指令。

以正常 QEMU VGA 图形窗口启动（不要 `-display none`）：

```bash
qemu-system-x86_64 -accel tcg -m 128M -boot order=d \
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

输入：

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

验收：`pginfo` 报告 `paging: on`、两个不同且 4 KiB 对齐的 table frame、`00000000 - 00400000`；boot 后 `pinfo` 至少有两张已分配页，手动 `palloc` 再增加一页；分页开启后仍能显示 VGA、读键盘、显示 MBI map；`clear` 后只有一个 prompt。

> **本次实际验证记录（2026-08-01）**：执行 `make clean && make -j$(nproc)` 和 `make check` 均通过且无警告；`objdump` 确认生成 `mov cr3,eax`、`mov eax,cr0`、`mov cr0,eax`，`readelf -lW` 保持独立 `R E`/`RW` LOAD segment。用 `-m 128M` 的 QEMU VGA 启动后，paging 已在 boot 时开启。通过 monitor `sendkey` 依次执行 `pinfo`、`pginfo`、`palloc`、`mmap`、`help`、`about`、`clear`、`help`；VGA shell 在 `CR0.PG` 开启后持续响应，最终 clear/help 路径显示单 prompt 和完整命令列表。具体物理 frame 由当次 GRUB/QEMU MBI 决定，课程不将其硬编码。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 对照来源 | 检查 |
|---|---|---|
| 设置 `CR0.PG` 后立即重启/卡死 | Intel paging enable sequence | 先填完整 map，再写 CR3，最后设置 PG；当前 EIP、ESP 和 VGA 必须都被映射。 |
| `pginfo` 显示 off | Intel CR0 | 检查仅在 `write_cr0(read_cr0() | CR0_PG)` 成功路径后设置软件状态。 |
| page table 与之后 `palloc` 重复 | Lesson 06 allocation history | 两个 table frame 必须来自 `phys_alloc_page()`，不可使用静态数组。 |
| directory/table 地址被截断 | non-PAE PTE format | 分配页须非零、4 KiB aligned 且不超过 `0xfffff000`。 |
| VGA 在 paging 后消失 | identity-map coverage | `0xb8000` 必须位于第一个 4 MiB window；不可改成只映射 kernel。 |
| `mmap` 在 paging 后崩溃 | MBI accessibility | 实测 MBI 必须落在前 4 MiB；未来必须按需要映射 MBI。 |
| kernel 或 stack page fault | linker layout | image base 1 MiB、stack 边界必须在 `[0,4 MiB)`。 |
| 误把 virtual 当 physical | address-space boundary | 本课只在 identity window 内可写 `physical == virtual`；不要推广到后续 higher-half layout。 |
| PTE 未清零而出现随机映射 | page-table initialization | 分配成功后对完整 4 KiB 页显式清零。 |
| 直接映射所有 RAM | teaching boundary | 本课只建立一张 page table 的 4 MiB window；完整 policy 留待后续。 |
| 试图测试 unmapped 地址 | exception boundary | 本课没有 IDT/page-fault 输出；不要以无诊断的 fault 作为验证。 |
| 试图现在进入 long mode | incremental architecture | PAE、EFER、GDT 和 far jump 将作为下一课的单独状态转换。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 Intel SDM Vol. 3 的 32-bit paging 部分，画出 linear address 的 directory/table/offset 切分。
2. 在 `kernel.c` 找到两个 allocator page 如何变成 directory 和 table，说明为何它们必须被永久保留。
3. 阅读 Linux v6.12 `arch/x86/kernel/head_64.S`，只观察其建立早期映射的意图；列出 TinyOS 本课没有实现的 PAE、EFER 和 far control transfer。
4. 下一课：使用 allocator 得到的更多页建立 long-mode-compatible 层级，并受控进入 `.code64`。

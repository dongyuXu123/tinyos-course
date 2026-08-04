# Lesson 08: 从 GRUB 的 32 位交接进入 x86_64 long mode

> **课程状态：学习版（可编辑，尚未归档）**  
> GRUB 仍按 Multiboot2 **i386** ABI 把控制权交给 ELF32 内核；TinyOS 在自己的 32 位早期入口中建立四级页表、开启 PAE/long mode，并运行真正以 `-m64` 编译的 VGA shell。

## 1. 使命与权威边界

本课把第 6、7 课的内存图解析与物理页分配器转为 x86_64 启动资源。规范权威分别是：

- **Intel SDM**：CR3、CR4.PAE、`IA32_EFER.LME`、CR0.PG、GDT 与 far jump 的 long-mode 规则；
- **Multiboot2 specification**：GRUB 的 `EAX=0x36d76289`、`EBX=MBI` i386 交接；
- **GNU GRUB**：`multiboot2` 命令和 ISO 加载行为。

Linux v6.12 的 `arch/x86/kernel/head_64.S`、`arch/x86/kernel/e820.c` 与 `mm/memblock.c` 是工程对照，而不是替代硬件/协议规范。

关键边界是：**ELF loader ABI 不等于最终执行模式**。镜像仍是 i386 ELF，因而由 GRUB 的 Multiboot2 i386 入口装载；内嵌的独立 `-m64` continuation 则在 far transfer 后执行。

## 2. 四级映射与 CPU 状态

32 位、关闭分页的 setup 从 type-1 Multiboot memory ranges 分配并永久记录五张 4 KiB frame：

```text
PML4[0] -> PDPT[0] -> PD[0] -> PT0[0] : 0x00000000..0x001fffff
                             PD[1] -> PT1[0] : 0x00200000..0x003fffff
```

因此明确映射 `[0, 0x00400000)`；VGA (`0xb8000`)、内核、bootstrap stack、MBI 和 table pages 都位于该窗口。条目均为 `Present | Writable` (`0x003`)；不引入 huge page、NX、higher-half、IDT 或用户态。

汇编顺序必须保持：

```text
LGDT -> CR3=PML4 -> CR4.PAE=1 -> EFER.LME=1 -> CR0.PG=1
     -> far jump to 64-bit CS -> establish RSP/RDI -> 64-bit C
```

`boot.S` 传入一个 identity-mapped handoff block；它含 table 地址、MBI 范围、内核/stack 边界以及 allocator history。这样 raw embedded 64-bit binary 不需要引用 ELF32 的未重定位全局符号。

## 3. 增量代码与命令

- `kernel.c` 保留严格的 Multiboot2 tag walker，只从 type-1 ranges 分页分配，并保留 low 1 MiB、kernel、stack、MBI 与历史页。
- `boot.S` 执行敏感 CPU 状态转换；`.code64` 手动编码桥接指令以保持外层 ELF32 容器。
- `kernel64.c` 以 `-m64 -mno-red-zone -fpie` 构建。先链接成地址零的 ELF64、确认没有 relocation，再转为 raw binary，并以 `.incbin` 放回 ELF32 镜像。
- `kernel64.ld` 强制 `kernel_main64_binary` 位于 embedded binary 的 offset zero；这是 raw binary call target 的构建契约。

启动后可用命令：

```text
help     about     clear
lminfo   pinfo     palloc     mmap
```

`lminfo` 显示 five table frames 和 4 MiB window；`palloc` 在 **64 位 shell 中**再次扫描 MBI type-6 map 并更新 handoff history；`mmap` 显示至多六段可用 range。

## 4. 构建与验证

```bash
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
readelf -rW build/kernel64.elf
nm -u build/kernel.elf
objdump -d -Mintel build/kernel.elf
qemu-system-x86_64 -accel tcg -m 128M -boot order=d \
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

本学习版的最近验证记录：

- `-Werror` build 成功，`grub-file --is-x86-multiboot2` 通过；
- final ELF32 有 RX 与 RW 两个 LOAD 段，未出现 RWX LOAD segment；`nm -u` 无未定义符号；
- embedded temporary ELF64 报告“没有重定位信息”；
- QEMU monitor 确认 `CS64`，`CR0=80000011`，`CR3=0000000000107000`，`CR4=00000020`，`EFER=0000000000000500`，即 paging、PAE、LME/LMA 均有效；
- VGA 显示 64-bit C banner，PS/2 polling shell 接收 `lminfo` 等命令。

## 5. 调试地图

1. `grub-file` 失败：检查 `.multiboot` 的 8-byte alignment 和镜像前 32 KiB。
2. setup 直接 halt：检查 magic、MBI alignment、type-6 request 或 tag size/rounding。
3. 开启 PG 后 reset：检查 CR3、PAE、LME 及 64-bit GDT descriptor 的 `L=1,D=0`。
4. far jump 后 fault：检查 code selector、GDTR base、jump target 是否在 identity window。
5. 64-bit C 未到达：检查 `kernel_main64_binary` 是否在 `kernel64.bin` offset zero。
6. shell banner 缺字：确认 PT0 覆盖 VGA `0xb8000`。
7. stack fault：确认 `stack_top` 小于 4 MiB 且 RSP 在 call 前 16-byte aligned。
8. `lminfo` table 交叠：检查 allocator history 是否在每次 table allocation 后立即更新。
9. `palloc` 重复页：检查 kernel/stack/MBI/history 的 half-open overlap predicate。
10. `mmap` 乱码：永远按 runtime `entry_size` 迭代，而非假定固定步长。
11. raw binary relocation：运行 `readelf -rW build/kernel64.elf`；必须没有 relocation。
12. reset/triple fault：用 QEMU `info registers` 验证 `CS64`、CR0、CR3、CR4、EFER。

## 6. 后续阅读

阅读 Intel SDM 的 IA-32e paging 与 mode-switch 章节；对照 Linux `arch/x86/kernel/head_64.S` 的 early transition 与 early page tables。下一课可在这份真实 long-mode 入口上增加异常处理/IDT，之后再讨论更大 identity map、higher-half、页错误与完整物理内存管理策略。

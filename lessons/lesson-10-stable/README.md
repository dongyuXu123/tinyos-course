# Lesson 10: 用 iretq 从可恢复的 #BP trap 返回 shell

> **课程状态：学习版（可编辑，尚未归档）**  
> 在第九课 exception-only IDT 上增加可恢复的 breakpoint trap：`int3` 进入 vector 3，显示 frame 后以 `iretq` 回到同一份 64 位 polling shell。

## 1. 使命与权威边界

第九课已经把终止型 `#UD` 与 `#PF` 变成 VGA diagnostics，但尚未证明内核可从 CPU exception 正常返回。本课只增加最小、可控的同步 software trap：`#BP`（vector 3）。

- **Intel SDM Volume 3** 是 `INT3`、#BP delivery、long-mode frame、IDT gate 与 `IRETQ` 的规范权威。
- Linux v6.12 `arch/x86/include/asm/trapnr.h` 将 `X86_TRAP_BP` 定义为 vector 3；`arch/x86/include/asm/idtentry.h` 是 normal/error-code entry 的工程对照。
- #BP 与 #UD 都没有 CPU error code；#PF 保留由 CPU 提供 error code 的路径。

不实现：`sti`、PIC/APIC、IRQ/EOI、TSS/IST、NMI/#DF、用户态、通用 register frame 或通用 fault-recovery policy。#UD/#PF 继续 intentional halt；只有 #BP 是本课明确的可恢复 exception。

## 2. Gate、frame 与 return path

沿用第九课的 256-entry、16-byte-gate IDT：

```text
IDT[3]  -> #BP: recoverable
IDT[6]  -> #UD: terminal
IDT[14] -> #PF: terminal
```

#BP stub 在 CPU frame 上压入 synthetic error 和 vector，并保留 shell 的 callee-saved `RBX`：

```text
saved RBX | vector=3 | error=0 | CPU delivery frame
```

它把规范化 frame 交给 `breakpoint_report()`；该 reporter 遵循 SysV ABI，返回后 stub 恢复保存的 `RBX`，丢弃 synthetic 两个 qword，并执行 `iretq`。`int3` 是单字节指令，CPU 保存的 RIP 已指向它之后的代码，所以本课不调整 RIP。

reporter 不清除主 shell 区域，而在下半屏固定位置输出 frame；这样恢复后的 shell cursor 不会被 handler 私有 cursor 覆盖。

和第九课一样，embedded ELF64 在地址零链接、再 raw-embed 进 1 MiB 的 outer ELF32。IDT gate 通过 RIP-relative `lea exception_bp(%rip)` 获得真实 runtime stub 地址，临时 ELF64 不含 relocation。

## 3. 命令

```text
help     about     clear     lminfo     pinfo     palloc     mmap
idtinfo  bptest    udtest    pftest
```

- `idtinfo` 显示 vectors 3/6/14 与 #BP returning status。
- `bptest` 显示 `triggering #BP`，执行 `int3`，显示 handler frame，随后显示 `#BP returned to shell` 和新的 prompt；可立即继续执行 `help`。
- `udtest` 和 `pftest` 继承第九课的终止行为，分别验证 #UD 和 `CR2=0x400000` 的 #PF。

## 4. 构建与验证

```bash
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

已验证：

- `-Werror` build 成功，Multiboot2 header check 通过；
- outer ELF32 分离 RX/RW LOAD segments，无 RWX LOAD；`nm -u` 无未定义符号；
- temporary `kernel64.elf` 无 relocation；反汇编包含 `lidt`、`int3`、`iretq`，并保留 `ud2` 与 `mov cr2`；
- QEMU VGA 中 `bptest` 显示 #BP、vector `3`、error `0`、RIP/CS/RFLAGS 和 `returning with iretq...`；
- 同一启动内随后出现 `#BP returned to shell`、新 `tinyos>` prompt，且 `help` 仍可执行，证明真正恢复；
- 第九课的 #UD/#PF terminal paths 保持在代码与 IDT 中，供新 QEMU boot 回归。

## 5. 调试地图

1. `int3` 直接 reset：检查 IDT[3] gate、selector `0x08`、type `0x8e` 与 Present bit。
2. handler address 接近零：raw binary 的 link-time symbol 不能直接填 gate；使用 RIP-relative runtime address。
3. `iretq` 后 reset：检查 synthetic vector/error 是否恰好丢弃 16 bytes。
4. `iretq` 返回错误地址：#BP 的 saved RIP 已在 single-byte `int3` 之后，不应加减。
5. shell register 损坏：stub 必须保护额外使用的 callee-saved `RBX`；inline `int3` 必须声明 caller-clobbered registers 与 `memory`。
6. shell text 被 handler 覆盖：handler 的 VGA cursor 不能假定拥有 shell 的私有 cursor；固定到独立下半屏区域。
7. #BP 被当作 #UD：确认 vector 3，而 #UD 为 vector 6。
8. #BP 有非零 error：#BP 不带 CPU error code；stub 必须 push synthetic zero。
9. #PF frame 失效：不要把 returning stub 复用到 #PF；#PF 仍由 terminal common path 处理。
10. 64-bit continuation relocation：`readelf -rW build/kernel64.elf` 必须为空。
11. ordinary shell regression：先运行 `idtinfo` / `help`，再运行 `bptest` 后的 `help`。
12. accidental IRQ behavior：本课仍 `cli` 并 polling，不可引入 `sti`、PIC 或 EOI。

## 6. 后续阅读

阅读 Intel SDM 的 breakpoint exception 和 `IRETQ` return semantics，再对照 Linux `idtentry.h` 的普通 entry paths。下一课可以在已验证的 gate-to-return 基础上单独引入 legacy PIC remap、一个键盘 IRQ、EOI 与 `sti`；不要把这些异步问题与本课的同步 #BP return 混在一起。

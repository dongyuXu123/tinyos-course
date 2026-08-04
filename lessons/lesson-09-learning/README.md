# Lesson 09: 在 x86_64 long mode 中安装最小异常 IDT

> **课程状态：学习版（可编辑，尚未归档）**  
> 保留第八课的 GRUB Multiboot2 i386 handoff、ELF32 容器和 real x86_64 continuation；本课安装 exception-only IDT，将 `#UD` 与 `#PF` 变成可见、可复现的 VGA 诊断。

## 1. 使命与权威边界

第八课已能运行 64 位 C，但 CPU exception 没有课程自己的可见交付路径。本课在 long-mode continuation 中填充 IDT 并执行 `lidt`，让 CPU 通过 TinyOS 的 gate 和 stub 进入 fault reporter。

- **Intel SDM Volume 3** 是 long-mode IDT gate、IDTR/`lidt`、exception vector、hardware stack frame、error code、`CR2` 和 `ud2` 的规范权威。
- **Multiboot2 specification / GNU GRUB** 是继承的 i386 handoff 与 loader 行为权威。
- Linux v6.12 `arch/x86/include/asm/idtentry.h` 仅作工程对照；其中 `DECLARE_IDTENTRY` 与 `DECLARE_IDTENTRY_ERRORCODE` 区分无/有 error-code 的 entry。Linux 的 `pt_regs`、IST、NMI、IRQ 和 instrumentation 远超本课。

不实现：`sti`、PIC/APIC、IRQ/EOI、TSS/IST、`iretq` recovery、#DF/NMI/MCE、完整 page-fault policy、扩大 identity map 或 higher-half。键盘继续 polling。

## 2. IDT、frame 与地址边界

32 位 setup 在 kernel `.bss` 中保留一个 4096-byte、16-byte-aligned backing store。它位于既有 `[0, 0x00400000)` identity window，并经 handoff block 传给 64 位 continuation：

```text
IDTR.base  = handoff.idt_address
IDTR.limit = 256 * 16 - 1 = 0x0fff

IDT[6]  -> #UD stub, CPU 无 error code
IDT[14] -> #PF stub, CPU 已压入 error code
```

stub 统一形成以下 frame，后交给 C reporter：

```text
#UD: stub push synthetic error=0; push vector=6
#PF: CPU push error; stub push vector=14

RSP -> vector | error | RIP | CS | RFLAGS
```

common stub 将 RSP 作为 SysV 首参数，栈对齐后调用 reporter。reporter 显示状态后 `cli; hlt`；这是终止型教学 handler，不恢复执行。

raw embedded ELF64 在地址零链接，故不能把其 link-time absolute symbol 直接填入 IDT gate。代码通过 RIP-relative `lea exception_*(%rip)` 获取真实 runtime handler 地址，保持 `kernel64.elf` 无 relocation。

## 3. 增量代码与命令

保留 `help`、`about`、`clear`、`lminfo`、`pinfo`、`palloc`、`mmap`，新增：

```text
idtinfo  显示 IDT base/limit、vector 6 与 14
udtest   执行 ud2；预期 #UD report 后停机
pftest   读取 0x00400000；预期 #PF report 后停机
```

`0x00400000` 是当前 exclusive identity window 的第一个未映射地址，因此可在不扩大 map 的条件下稳定验证 `CR2`。`udtest` 与 `pftest` 都必须在单独的新 QEMU boot 中执行。

## 4. 构建与 QEMU 验证

```bash
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
qemu-system-x86_64 -accel tcg -m 128M -boot order=d \
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

已验证：

- warning-free `-Werror` build，`grub-file --is-x86-multiboot2` 通过；
- outer ELF32 为分离 RX/RW LOAD segments，未出现 RWX LOAD；`nm -u` 无未定义符号；
- temporary `kernel64.elf` 无 relocation，反汇编包含 `lidt`、`mov cr2`、`ud2`；
- 普通 VGA shell 的 `idtinfo` 显示 IDT base、`limit=0x0fff`、vector 6/14，`help` 正常；
- 独立 `udtest` boot 显示 `exception: #UD`、vector 6、error 0、RIP/CS/RFLAGS，并 intentional halt；
- 独立 `pftest` boot 显示 `exception: #PF`、vector 14、error 0、`CR2=0000000000400000`、RIP/CS/RFLAGS，并 intentional halt；
- QEMU monitor 在普通路径显示 `CS64`、`CR0=80000011`、`CR4=00000020`、`EFER=0000000000000500`，以及 `IDT.limit=00000fff`。

## 5. 调试地图

1. `grub-file` 失败：检查 `.multiboot` 的 8-byte alignment 与镜像前 32 KiB。
2. `lidt` 后 reset：检查 IDTR base/limit 和 gate 的 Present/type `0x8e`。
3. #UD 变 #GP：检查 gate selector 是 64-bit code selector `0x08`。
4. #UD frame 错位：无 error-code exception 必须由 stub 压 synthetic zero。
5. #PF frame 错位：CPU 已压 error code，stub 不可再压 synthetic error。
6. handler address 很小：地址零链接 raw symbol 不可直接填 gate；使用 RIP-relative runtime address。
7. #PF 不触发：test address 必须是 exclusive map end `0x00400000`。
8. `CR2` 不对：必须在 reporter 的任何后续潜在 fault 前读取 CR2。
9. reporter 再 fault：IDT、handler、VGA、stack 必须全部低于 4 MiB。
10. allocator 复用 IDT：kernel half-open reservation 必须覆盖 `.bss` IDT backing store。
11. raw binary relocation：`readelf -rW build/kernel64.elf` 必须为空。
12. keyboard regression：本课保持 `cli` 与 i8042 polling，不引入 IRQ path。

## 6. 后续阅读

阅读 Intel SDM 的 IDT gate、exception delivery、page-fault error code 与 CR2；再对照 Linux `arch/x86/include/asm/idtentry.h` 的 normal/error-code entry machinery。下一课可在可诊断 long-mode 基础上逐步引入更多 exception coverage、PIC/APIC 和 IDT-driven hardware interrupts；更大的映射或 higher-half 仍应在可见 fault diagnostics 基础上推进。

# Lesson 24: TSS, `rsp0`, and a #PF IST exception stack

> **Course status: learning tree (editable; validated checkpoint).**

Lesson 24 adds the kernel-only x86_64 privilege-transition machinery that is needed before user mode can be introduced. The Multiboot outer kernel remains ELF32 and low-linked; the freestanding 64-bit continuation still runs through the validated high runtime alias. This lesson does **not** enter CPL3.

## TSS and GDT contract

- The bootstrap GDT in `boot.S` still only enters long mode with selectors `0x08` (kernel code) and `0x10` (kernel data).
- Before interrupts are enabled, the continuation builds and loads its own high-address GDT: null, the same kernel code/data descriptors, and a 16-byte available 64-bit TSS descriptor at selector `0x18`.
- `ltr` loads that TSS. `rsp0` points at its own static, page-aligned entry-stack reserve. It is configured for a future CPL3-to-CPL0 transition; CPL3 does not yet exist, so current hardware never consumes `rsp0`.
- IST1 points to a separate static, page-aligned exception stack. Both stacks are continuation-resident, neither belongs to the PMM, worker stacks, or the independent idle context.
- Only #PF uses IST1. #BP, #UD, PIT IRQ0, and keyboard IRQ1 retain IST zero. In particular, IRQ0 remains the sole scheduler/frame-selection boundary and IRQ1 never schedules.

A #PF entered through IST saves the interrupted CPL0 stack pointer as part of the CPU frame. The fatal report distinguishes that saved pointer from the current handler stack pointer and prints the configured IST1 range. This is a CPL0-only exception-frame model, not a user-origin frame model.

## Commands

```text
tssinfo       show TR, runtime GDTR/TSS, rsp0, IST1, and IDT IST assignments
isttest       trigger a fatal controlled #PF through IST1; fresh QEMU boot only
bptest        retained recoverable #BP regression (IST zero)
idletest      retained independent-idle IRQ0 regression
pctest/pcgo   retained event wake_all and producer-consumer regression
pcinfo        inspect producer-consumer completion invariants
```

`tssinfo` must show `TR = 0x18`, high-alias GDT/TSS/stack addresses, #PF IST `1`, IRQ0/IRQ1 IST `0`, and `CPL3 entry: not implemented`.

## VGA acceptance

1. Boot normally and run `tssinfo`; verify the values above.
2. Run `bptest`; it must return to the shell, proving that #BP still uses IST zero and its recoverable `iretq` path remains intact.
3. Regress `idletest`, `pctest`/`pcgo`/`pcinfo`, preemption, sleeps, keyboard waiting, PMM, VM mapping, and normal shell keyboard input.
4. In a **fresh QEMU boot**, run `isttest`. The fatal VGA screen must identify #PF, print an IST1 handler RSP inside the displayed IST1 stack range, and show the saved interrupted RSP separately. This is intentional and ends the boot.
5. Existing `vmfaulttest` and `pftest` are likewise fatal, fresh-boot #PF/IST tests. `udtest` remains a separate fresh-boot fatal test on IST zero.

## Static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
nm -u build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
readelf -lW build/kernel.elf
```

Acceptance requires no continuation relocations or undefined symbols, materialized continuation state, and no RWX `LOAD` segment in the outer Multiboot ELF. Confirm runtime `lgdt` and `ltr`, the #PF gate IST byte of one, and the unchanged IRQ0 one-frame-selection/shared-`iretq` structure. The raw continuation linker can still warn that its own internal ELF has RWX; that is distinct from the checked outer ELF.

## Current limits

There is no user code selector, user data selector, CPL3 `iretq`, syscall ABI, process, address space, or per-thread `rsp0` update yet. `rsp0` and IST are preparation for those later lessons. Lesson 25 will add a kernel-stack guard-page and controlled runtime-mapping basis.

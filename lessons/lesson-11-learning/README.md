# Lesson 11: legacy PIC remap and a safe first IRQ1

> **Course status: learning edition (editable, not yet archived)**
>
> This lesson adds the first hardware-interrupt foundation to Lesson 10: the two legacy 8259A PICs are remapped, IDT vector `0x21` receives keyboard IRQ1, and the handler can return through `iretq`. The normal shell remains polling-based.

## Goal and boundary

Hardware interrupts are asynchronous: an IRQ can arrive while any shell or allocator instruction is using registers. Therefore the IRQ1 entry stub saves and restores every general-purpose register before `iretq`:

```text
CPU IRQ frame
RAX RBX RCX RDX RBP RSI RDI R8 R9 R10 R11 R12 R13 R14 R15
```

The stub preserves the original stack pointer in `RBP`, aligns the stack for the SysV call to `irq1_record()`, restores all GPRs, then executes `iretq`. The IDT gate is an interrupt gate (`0x8e`), so IF is cleared while the handler runs.

The legacy PIC setup follows the usual ICW sequence:

```text
master vector offset 0x20    slave vector offset 0x28
master IRQ2 cascade          slave cascade identity 2
8086/88 mode                 all IRQ lines masked initially
```

`IDT[0x21]` is the IRQ1 gate. No slave IRQ is enabled in this lesson, so IRQ1 sends only a **master EOI** to port `0x20` after reading port `0x60`.

## Safe keyboard ownership

A polling shell and an enabled keyboard IRQ cannot both consume i8042 output bytes: whichever reads port `0x60` first owns the scancode. Leaving IRQ1 permanently unmasked would make ordinary commands nondeterministic.

Lesson 11 deliberately retains the polling shell as its baseline:

- Boot starts with `cli` and all PIC lines masked. Polling commands work normally.
- `irqtest` sets IF with `sti` and unmasks only master IRQ1.
- The next keyboard byte reaches the IRQ handler. It reads port `0x60`, increments `IRQ1 count`, stores `last scancode`, sends a master EOI, and immediately masks all PIC lines again. (It may be the Enter key's break byte from the command itself.)
- That IRQ-consumed byte is intentionally **not** shell input. Because IRQ1 is masked afterward, polling immediately regains exclusive ownership and normal interaction remains usable.

The handler writes a fixed notification in the lower screen area rather than using the shell cursor. This is an intentionally small observability mechanism, not a keyboard driver, queue, modifier decoder, or IRQ-driven shell.

## Commands

```text
help     about     clear     lminfo     pinfo     palloc     mmap
idtinfo  irqinfo   irqtest    bptest    udtest    pftest
```

- `idtinfo` reports the exception vectors and `IRQ1 vector: 0x21`.
- `irqinfo` displays the PIC offsets, observed IRQ count, final scancode, and whether IRQ1 is currently masked or awaiting its one test key.
- `irqtest` arms the one-shot IRQ validation. Press one ordinary key. The lower display reports `IRQ1 observed`, count `1`, the raw set-1 scancode, and that IRQ1 has been masked again. Use a subsequent key to continue issuing polling-shell commands.
- `bptest` retains the recoverable #BP / `iretq` regression from Lesson 10.
- `udtest` and `pftest` intentionally halt in their exception diagnostics.

## Interrupt-enable order

The order is deliberate:

1. Start with `cli`.
2. Install all required IDT gates, including vector `0x21`.
3. Remap the PICs and mask every line.
4. Run the polling shell with IF clear and IRQ1 masked.
5. Only `irqtest`, after all of the above, unmasks IRQ1 and executes `sti`.

This avoids accepting an external IRQ before an IDT gate, a valid stack, handler code, and PIC masks are ready. IRQ1 is not enabled before the explicit validation request.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

Expected static evidence:

- the Multiboot2 header check passes;
- the outer ELF has separate RX/RW LOAD segments and no undefined symbols;
- `kernel64.elf` has no relocations, despite being raw-embedded at 1 MiB;
- the disassembly includes PIC port writes, `lidt`, `sti`, the IRQ1 full-GPR push/pop sequence, master EOI, and `iretq`.

## QEMU VGA validation

Run `make run`, wait for the `tinyos>` prompt, then validate in this order:

1. Type `idtinfo` and verify vector `0x21`.
2. Type `irqinfo`; it must say IRQ1 is masked and count is zero.
3. Type `irqtest`. The command's Enter-release byte, or the next key if no byte is pending, triggers the lower-screen `IRQ1 observed` report with a count and raw scancode.
4. Type `irqinfo` again and verify the count/scancode and masked state.
5. Type `help` to prove polling continues after the test.
6. Optionally run `bptest`, then `help`, to regress the established synchronous return path.

## Debugging map

1. Reset after `sti`: verify `lidt`, PIC remap and the IRQ gate are complete before unmasking any line.
2. IRQ reaches #UD/#GP: master offset must be `0x20`; IRQ1 must therefore use vector `0x21`.
3. Handler never runs: check master mask bit 1 is clear only while `irqtest` is armed.
4. IRQ storm: send an EOI to master command port `0x20` before returning from IRQ1.
5. Polling commands lose characters: do not leave IRQ1 unmasked while the polling loop also reads port `0x60`.
6. Return faults after handler: save and restore every GPR in exact reverse order before `iretq`.
7. C handler corrupts interrupted code: the IRQ stub must preserve caller- and callee-saved registers, not only `RBX` like the controlled #BP path.
8. C call faults or misbehaves: realign RSP to 16 bytes before calling C and restore the saved frame pointer afterward.
9. Slave IRQ later hangs: it requires slave EOI followed by master EOI; IRQ1 is a master-only line and uses only master EOI here.
10. Handler target is near zero: use RIP-relative `lea` for the embedded raw ELF64 handler address instead of its zero-linked symbol value.
11. A stale pending key triggers immediately: this is expected for one-shot `irqtest`; inspect `last scancode` and then use a fresh key.
12. Regression after PIC work: independently boot `udtest` and `pftest`, because their reporters intentionally halt.

## Limitations carried intentionally into the next lesson

- Only IRQ1 has an IDT handler, and only for a one-shot diagnostic.
- The PIC remains fully masked outside `irqtest`; no timer IRQ or concurrent IRQ policy exists.
- `irq1_record()` reads one i8042 byte. It does not decode it, enqueue it, handle prefixes/modifiers, or feed the shell.
- There is no spurious-IRQ logic, slave EOI path, APIC, TSS/IST, nesting policy, locking, or user mode.
- VGA reporting from the IRQ handler is only acceptable here because the shell is paused in its polling loop and this lesson masks IRQ1 before returning. A real asynchronous console path needs stronger synchronization.

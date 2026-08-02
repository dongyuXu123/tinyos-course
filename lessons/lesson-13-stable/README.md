# Lesson 13: PIT IRQ0 timer and IRQ1 keyboard shell

> **Course status: learning edition (editable until validation is complete)**

Lesson 13 adds a periodic Programmable Interval Timer (PIT) channel-0 interrupt while retaining Lesson 12's IRQ1-owned keyboard ring buffer and recoverable #BP path.

## Design

Boot keeps interrupts disabled until the IDT, PIT, and PIC are configured:

1. Install exception gates for #BP (3), #UD (6), #PF (14), plus IRQ0 (`0x20`) and IRQ1 (`0x21`).
2. Program PIT channel 0 in mode 3 with divisor 11932, yielding approximately 100 Hz from the 1,193,182 Hz PIT input clock.
3. Remap the 8259A PICs to `0x20`/`0x28` and unmask master IRQ0 and IRQ1 only (`0xfc`); all slave lines remain masked.
4. Enable interrupts and run the queue-driven shell.

IRQ0 and IRQ1 have separate full-GPR stubs. Each saves `RAX` through `R15`, clears DF, aligns the stack for its C call, restores all registers, then finishes with `iretq`. The IRQ0 handler increments volatile `ticks` and sends master PIC EOI. IRQ1 remains the only code reading i8042 port `0x60`; it records and decodes keyboard make codes into the 64-entry ring buffer and sends master EOI.

The raw embedded x86_64 continuation must include its global mutable state. `kernel64.ld` explicitly places `.bss` and `COMMON` in the output `.data` section with `BYTE(0)`, materializing them as `PROGBITS` before `objcopy -O binary`.

## Commands

```text
help about clear lminfo pinfo palloc mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `tickinfo` and `uptime` snapshot and display the 100 Hz tick counter; repeated commands must show a larger value.
- `idtinfo` shows IRQ0 at `0x20` and IRQ1 at `0x21`.
- `kbdinfo` reports IRQ1/ring-buffer state.
- `bptest` still returns to the shell via #BP and `iretq`.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

Expected evidence: Multiboot2 validation passes; the continuation has no relocations; the linker maps persistent globals into file-backed data; the IDT installs IRQ0/IRQ1 gates; PIC mask `0xfc` leaves both master lines enabled; PIT setup writes command port `0x43` and channel-0 port `0x40`; and both handlers issue master EOI and end in `iretq`.

## QEMU VGA validation

Run `make run`, wait for `tinyos>`, then use QEMU monitor `sendkey`:

1. Run `tickinfo`, wait at least one second, and run it again. The displayed tick value increases by roughly 100 per second.
2. Run `help`, `kbdinfo`, and normal commands to verify queued keyboard input remains functional.
3. Run `bptest`, then `help`, to confirm #BP returns and both timer/keyboard operation continue.

## Current limits

This remains PIC/PIT based: no APIC, slave IRQ support, spurious-IRQ handling, TSS/IST, user mode, scheduler, or wall-clock calibration. The keyboard driver still supports only listed set-1 make codes and has no modifiers/prefix handling.

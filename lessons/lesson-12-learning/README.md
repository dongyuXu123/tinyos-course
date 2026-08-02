# Lesson 12: IRQ1 keyboard producer and ring-buffer shell

> **Course status: learning edition (editable, not yet archived)**

Lesson 12 replaces Lesson 11's one-shot IRQ/polling ownership experiment with a single IRQ-driven keyboard ownership model. IRQ1 remains unmasked after setup, the IRQ handler is the only code that reads i8042 data port `0x60`, and the shell consumes decoded characters from a bounded ring buffer.

## Design

Boot keeps interrupts disabled until the IDT and PIC are ready:

1. Install #BP (3), #UD (6), #PF (14), and IRQ1 (0x21) IDT gates.
2. Remap the master/slave 8259A PICs to `0x20`/`0x28`.
3. Mask every line except master IRQ1 (`0xfd` on the master and `0xff` on the slave).
4. Print the prompt, execute `sti`, and enter the queue-consuming shell.

The IRQ1 stub still saves and restores every GPR (`RAX` through `R15`) around the C call, aligns the stack, clears DF, and returns with `iretq`. The interrupt gate keeps IF clear during the handler. The handler reads port `0x60` exactly once, records raw stats, ignores break codes, decodes supported set-1 make codes (letters, `;`, Backspace, Enter), enqueues a decoded character in a 64-entry ring (counting full-queue drops), and sends master EOI to port `0x20`.

The shell never polls `0x60` or `0x64`. It dequeues under a short `cli` critical section, then uses `sti; hlt` when empty. A pending keyboard IRQ either runs before `hlt` or wakes it, so the empty-check/wait transition cannot lose an input wakeup.

## Commands

```text
help about clear lminfo pinfo palloc mmap idtinfo kbdinfo bptest udtest pftest
```

- `kbdinfo` reports IRQ ownership, raw-byte and decoded make-code counts, overflow drops, last raw scancode, and queue indices.
- `idtinfo` confirms IRQ1 at vector `0x21`.
- `bptest` retains the recoverable #BP/`iretq` path.
- `udtest` and `pftest` retain intentional halt diagnostics.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

Expected evidence: the Multiboot2 check passes; the raw embedded 64-bit ELF has no relocations; IRQ1 is vector `0x21`; the PIC master mask leaves bit 1 clear; the handler reads `0x60`, sends master EOI, and the full-GPR stub ends in `iretq`. The shell must contain no reads from `0x60` or status polling from `0x64`.

## QEMU VGA interactive validation

Run `make run`, wait for `tinyos>`, and use the QEMU monitor `sendkey` interface:

1. Type `help` and confirm normal queued command input.
2. Type `kbdinfo` and confirm IRQ1 is enabled and counters advance.
3. Type part of a command, Backspace, then complete it to confirm editing.
4. Type `idtinfo` and confirm vector `0x21`.
5. Type `bptest`, then `help`, to confirm #BP returns and IRQ input stays usable.

## Current limits

The driver supports only the listed set-1 make codes. It has no modifiers or prefix handling and drops new decoded characters when the ring is full. There is no timer, slave IRQ, APIC, TSS/IST, user mode, or spurious-IRQ logic. Queue sharing intentionally stays simple: byte indices plus a short interrupt-disabled consumer critical section.

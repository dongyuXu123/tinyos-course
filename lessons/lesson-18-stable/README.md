# Lesson 18: PIT preemptive scheduler

> **Course status: stable snapshot (validated and read-only).**

Lesson 18 replaces Lesson 17's cooperative `ret`-based continuation with a PIT IRQ0 preemptive scheduler. The Lesson 16 high runtime alias, bitmap PMM, controlled low VM slot, IDT, and IRQ-driven keyboard shell remain in place.

## IRQ0 return-frame ABI

Only kernel-mode IRQ0 entries use this ABI. IRQ0 saves all general-purpose registers, then the CPU-provided same-CPL interrupt return state follows it:

```text
lowest address / struct irq0_frame
  r15 r14 r13 r12 r11 r10 r9 r8 rdi rsi rbp rdx rcx rbx rax
  rip cs rflags
highest address
```

The IRQ0 scheduler receives a pointer to that frame, records it in the outgoing TCB, chooses a runnable TCB when its quantum expires, and returns the exact frame to restore. IRQ0 assembly changes `rsp` to that pointer, restores every GPR, and reaches its single common `iretq` path. It does not call the Lesson 17 cooperative `context_switch` routine.

The model is CPL0-only: same-CPL `iretq` restores `RIP`, `CS`, and `RFLAGS`; it does not restore a separate `RSP`/`SS`. A synthetic first-run worker frame seeds `r12` with its high stack top. The trampoline transfers that seed into `rsp` before calling C. Subsequent PIT frames preserve the normal runtime `rsp` through their hardware return state.

## PIT scheduling and ownership

There are three fixed TCBs:

```text
thread 0  shell context
thread 1  non-yielding preempttest worker A
thread 2  non-yielding preempttest worker B
```

PIT remains approximately 100 Hz. `TIME_SLICE_TICKS` is two IRQ0 ticks. Both workers advance through four finite busy-loop steps without calling `yield`; their completion therefore demonstrates that IRQ0, not voluntary switching, causes the round-robin execution.

Each worker owns one 4 KiB PMM frame as a high-alias stack:

```text
stack high VA = 0xffffffff80000000 + stack PA
```

Live worker stacks reject `pfree` with `cannot free: thread stack`. A finished worker is reaped only while another stack is current, so its active stack is never freed beneath the running `rsp`.

## Commands

```text
help about clear lminfo hhinfo hhtest preempttest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `preempttest` atomically creates both non-yielding workers.
- `threadstart` is a compatibility alias for `preempttest`.
- `yield` is retained only as a diagnostic command: direct cooperative switching was replaced by PIT preemption.
- `ps` displays each TCB state, saved IRQ frame, stack PA/high alias, switches, and progress.
- `threadinfo` displays the active quantum, ticks, preemptive switch count, worker steps, and `IRQ0 schedules: yes`.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
nm -u build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
readelf -lW build/kernel.elf
```

Expected evidence: valid Multiboot2; no inner relocation or undefined symbols; file-backed `.data` (`PROGBITS`); no outer RWX LOAD segment; IRQ0 saving/restoring a full frame with one `iretq`; and retained `invlpg`, IRQ1, and exception `iretq` paths.

## QEMU VGA validation

1. Run `meminfo`, then `preempttest` and `ps`. Workers 1 and 2 have distinct PMM stack pages.
2. Do **not** run `yield`. After PIT time passes, run `threadinfo` and `ps`: both worker progress counts advance, preemptive switches increase, and `IRQ0 schedules: yes` is shown.
3. After completion, run `meminfo`; both worker pages have been reaped and PMM accounting returns to its prior value.
4. Regress `hhinfo`, `hhtest`, `vmtest`, `tickinfo`, `kbdinfo`, and recoverable `bptest`.
5. In separate boots run `vmfaulttest`, `pftest`, and `udtest`; expected fatal results remain CR2 `00000000003ff000`, CR2 `0000000000400000`, and `#UD`.

## Current limits

This is a kernel-only, CPL0-only teaching scheduler. It has no user frame support, guard pages, sleeping/blocking queues, dynamic TCBs, priorities, SMP, APIC, address-space isolation, or user-mode preemption.

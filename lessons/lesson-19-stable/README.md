# Lesson 19: PIT timer sleep and wakeup

> **Course status: stable snapshot (validated and read-only).**

Lesson 19 keeps Lesson 18's PIT IRQ0 return-frame scheduler and adds a deliberately bounded timer blocking model. The high runtime alias, bitmap PMM, controlled low VM slot, IDT, and IRQ-driven keyboard shell remain unchanged.

## Thread model

There are still three fixed TCBs:

```text
thread 0  always-runnable shell context
thread 1  timed worker A
thread 2  timed worker B
```

Workers have the states `empty`, `running`, `runnable`, `sleeping`, and `finished`. A sleeping worker retains its saved IRQ0 frame and its single PMM-backed 4 KiB high-alias stack. It is therefore still a live stack owner and `pfree` rejects that page.

Each TCB includes a `wake_tick`. IRQ0 increments the monotonic teaching tick and performs a fixed scan of worker slots 1–2. A sleeping worker whose deadline has arrived becomes runnable; this bounded scan does not allocate memory, print output, or manipulate a dynamic queue.

## IRQ0 ownership and sleep

The IRQ0 frame ABI remains CPL0-only:

```text
lowest address / struct irq0_frame
  r15 r14 r13 r12 r11 r10 r9 r8 rdi rsi rbp rdx rcx rbx rax
  rip cs rflags
highest address
```

IRQ0 saves the outgoing frame, calls the scheduler, receives the exact frame to restore, installs it in `rsp`, restores GPRs, and reaches the sole common `iretq`. It never invokes Lesson 17's `ret`-based context switch.

`thread_sleep_ticks()` is worker-only. With an IF-preserving critical section it records `ticks + delta` and marks the current worker `sleeping`; it then executes interrupt-enabled `hlt` instructions until a later IRQ0 restores the saved continuation. Scheduling remains owned by IRQ0 rather than an ordinary thread-context switch.

Thread 0 never sleeps in this lesson. It is both the interactive shell and the safe fallback when both workers are sleeping. A dedicated idle context is intentionally deferred.

## Commands

```text
help about clear lminfo hhinfo hhtest preempttest sleeptest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `sleeptest` atomically starts two finite workers with different delays: A sleeps 120 ticks and B sleeps 270 ticks between progress steps.
- `preempttest` and compatibility alias `threadstart` retain Lesson 18's non-yielding CPU-bound worker regression.
- `yield` remains a diagnostic command; it does not change the saved IRQ0 frame.
- `ps` shows state, saved frame, PMM stack PA/high alias, switches, progress, and wake tick.
- `threadinfo` reports the active mode, PIT ticks, preemptive switches, timer wakeups, and shell-only ticks.

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

Expected evidence: valid Multiboot2; no inner relocation or undefined symbol; file-backed `.data` (`PROGBITS`); no outer RWX LOAD segment; the IRQ0 frame replacement still has one `iretq`; and retained `invlpg`, IRQ1, and exception paths.

## QEMU VGA validation

1. Run `meminfo`, then `sleeptest` and `ps`. Workers have distinct PMM stack pages and enter `sleeping` with distinct wake ticks.
2. While both workers sleep, run `threadinfo`, `tickinfo`, `kbdinfo`, and `ps`. PIT ticks advance and the shell remains responsive.
3. After each deadline, verify the worker becomes runnable/running, advances progress, sleeps again, and eventually finishes.
4. After completion, run `meminfo`; finished worker stacks are reaped only from another current stack and PMM accounting returns to baseline.
5. Regress `preempttest`, `hhinfo`, `hhtest`, `vmtest`, `tickinfo`, `kbdinfo`, and recoverable `bptest`.
6. In separate boots run `vmfaulttest`, `pftest`, and `udtest`; expected fatal results remain CR2 `00000000003ff000`, CR2 `0000000000400000`, and `#UD`.

## Current limits

This is a fixed two-worker timer scan, not a general wait queue. It has no dynamic TCBs, priorities, dedicated idle thread, keyboard blocking wait queue, semaphores, guard pages, TSS/IST, user frame support, SMP, APIC, address-space isolation, or user-mode preemption. The next natural increments are keyboard blocking, wake-one/wake-all synchronization, then kernel-stack/exception reliability before user mode.

# Lesson 20: keyboard blocking wait queue (`wake_one`)

> **Course status: stable snapshot (validated and read-only).**

Lesson 20 retains Lesson 19's PIT IRQ0 return-frame scheduler and timer sleep model. It adds a bounded keyboard-event wait queue for workers, with FIFO `wake_one` delivery from IRQ1. The high runtime alias, bitmap PMM, controlled low VM slot, IDT, PIT, and interactive keyboard shell remain in place.

## State and ownership

The fixed TCB set remains:

```text
thread 0  always-runnable shell
thread 1  worker A
thread 2  worker B
```

Workers may now be `empty`, `running`, `runnable`, `sleeping`, `blocked-kbd`, or `finished`. `blocked-kbd` is distinct from timer sleep. A blocked worker retains its saved IRQ0 frame and its one 4 KiB PMM-backed high-alias stack, so `pfree` continues to reject that live page.

The keyboard wait queue is a static FIFO of exactly two worker IDs. It has fixed head, tail, and count fields; it allocates no memory and a worker may be queued only while it is `blocked-kbd`.

## No-lost-wakeup protocol

`kbd_wait_char()` is worker-only. With interrupts disabled while preserving the caller's original IF state, it does one of two things:

1. consumes its already delivered mailbox byte; or
2. publishes the current worker in the bounded keyboard FIFO and marks it `blocked-kbd`.

Only after that atomic decision does it restore IF and execute `sti; hlt` until IRQ1 makes it runnable. It then rechecks the mailbox in a loop. Therefore a key cannot arrive in the gap between observing no delivery and publishing the waiter.

Thread 0 never enters this API. It remains runnable as the shell and safe scheduler fallback; this lesson intentionally has no dedicated idle context.

## IRQ1 direct delivery and `wake_one`

IRQ1 translates one keyboard make code. If a worker waiter exists, IRQ1 removes exactly the oldest FIFO waiter, writes the character into that worker's one-byte mailbox, marks the mailbox ready, and changes only that worker to `runnable`. This is a direct delivery path, so the shell cannot consume the key intended for the awakened worker.

If no worker waits, IRQ1 keeps the existing ring-buffer path for shell input. IRQ1 does not run the scheduler, replace an IRQ frame, allocate/free PMM memory, print VGA output, or halt. It sends exactly one PIC EOI and returns. IRQ0 remains the only code that selects a saved frame and reaches the shared `iretq` switch boundary.

## Commands

```text
help about clear lminfo hhinfo hhtest preempttest sleeptest kbdwaittest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `kbdwaittest` atomically starts two workers. Each waits for four keyboard characters, records its receive count and last byte, then exits.
- Supply one character at a time: the first key wakes only worker A, the second wakes only worker B, and later keys continue FIFO ordering as workers requeue.
- `ps` prints worker state, saved frame, stack ownership, progress, wake tick, received count, and last byte.
- `threadinfo` reports keyboard waiter count and enqueue/wake-one counters.
- `kbdinfo` reports shell ring state plus direct worker delivery counters.
- `sleeptest`, `preempttest`, and `threadstart` are retained regressions. Only one test can own the fixed worker TCB set at a time.

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

Expected evidence: valid Multiboot2; no inner relocation or undefined symbol; file-backed `.data` (`PROGBITS`); no outer RWX LOAD segment; retained `invlpg`, IRQ0 frame replacement, IRQ1, and exception paths. IRQ0 retains its single return-frame `iretq` scheduler route; IRQ1 does bounded queue/mailbox mutation and one EOI without calling the scheduler.

## QEMU VGA validation

1. Run `kbdwaittest`, then `ps` and `threadinfo`. Workers 1 and 2 must be `blocked-kbd`, queue occupancy must be two, and PMM stacks remain allocated.
2. Enter one non-command test key. Verify exactly one worker advances/receives a byte and the other remains `blocked-kbd`.
3. Enter a second key. Verify the other worker receives next. Repeat enough keys to complete both workers, checking FIFO wake-one counters and last-byte diagnostics.
4. During waits, verify PIT continues through the scheduler counters. In this deliberately direct-delivery test mode, ordinary typed characters are test input for the oldest waiter rather than shell commands; after the wait queue empties, shell typing again uses the ring buffer.
5. After completion, verify finished stacks are reaped only from another current stack and `meminfo` returns to the initial PMM free count.
6. Regress `sleeptest`, `preempttest`, `hhinfo`, `hhtest`, `vmtest`, `tickinfo`, `kbdinfo`, and recoverable `bptest`. Use separate boots for `vmfaulttest`, `pftest`, and `udtest`.

## Current limits

This is a fixed two-worker keyboard wait queue, not a generic synchronization primitive. It has no `wake_all`, priorities, dynamic TCBs, general semaphores, shell blocking, idle thread, guard pages, TSS/IST, user frames, SMP, APIC, address-space isolation, or user-mode preemption. The next natural step is extracting the bounded queue into a generic `wake_one` / `wake_all` event or semaphore primitive.

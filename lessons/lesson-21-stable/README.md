# Lesson 21: bounded generic wait queues

> **Course status: stable snapshot (validated and read-only).**

Lesson 21 extracts Lesson 20's keyboard-specific FIFO into a fixed-capacity, allocation-free `wait_queue`. The existing keyboard path is its first user: IRQ1 directly delivers one character to the oldest blocked worker and invokes generic `wake_one`; normal shell input still uses the ring when no worker waits.

## Wait queue contract

A `wait_queue` stores at most two worker IDs with FIFO head, tail, count, enqueue, wake-one, and wake-all accounting. All changes occur with IF preserved and interrupts disabled.

- `waitq_push()` publishes a worker before it is marked `blocked-kbd`.
- `waitq_wake_one()` removes exactly one FIFO waiter and makes it runnable.
- `waitq_wake_all()` drains the queue and makes every valid waiter runnable.
- A waiter verifies its mailbox in a loop after resuming; therefore a wakeup itself is not mistaken for a delivered character.

IRQ1 remains bounded: it reads one make code, uses `wake_one` only when a waiter exists, stores the byte in that worker mailbox, sends one EOI, and returns. It never chooses an IRQ frame or calls the scheduler. IRQ0 remains the sole frame-selection and `iretq` switch boundary.

## Commands

```text
kbdwaittest  start two FIFO keyboard waiters
ps           show thread state, progress, and mailbox diagnostics
threadinfo   show generic wait queue counters
kbdinfo      show ring and direct-delivery counters
```

`kbdwaittest` verifies normal FIFO wake-one delivery. `waitq_wake_all()` is intentionally exported as the generic companion primitive for Lesson 22's event/semaphore producer; it drains valid FIFO waiters without manufacturing mailbox data. Only one fixed worker test may run at once.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
nm -u build/kernel64.elf
readelf -SW build/kernel64.elf
readelf -lW build/kernel.elf
```

QEMU VGA checks: start `kbdwaittest`; verify both workers block and `threadinfo` reports two waiters. Supply characters one at a time and verify one FIFO worker receives each character. Run `wqall` while workers wait to show no progress occurs without a mailbox delivery, then complete the workers with keyboard input. Regress `sleeptest`, `preempttest`, PMM/VM, PIT, keyboard, #BP, and isolated fatal tests.

## Current limits

This is a bounded teaching primitive, not a dynamic Linux wait queue. It has no priority ordering, timeout integration, cancellation, lock abstraction, or idle thread. Lesson 22 will use it to implement a fixed event/counting semaphore and producer-consumer demonstration.

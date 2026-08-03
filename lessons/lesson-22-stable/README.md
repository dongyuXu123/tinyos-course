# Lesson 22: fixed event and counting semaphore

> **Course status: stable snapshot (validated and read-only).**

Lesson 22 keeps Lesson 21's fixed, allocation-free `wait_queue` and makes both wake paths observable at runtime. Two PMM-backed workers implement a finite producer-consumer demonstration: a manual-reset start event releases both workers with `wake_all`; bounded counting semaphores then transfer individual buffer slots and items with `wake_one`.

## Synchronization contract

The system still has exactly three TCBs: shell thread 0, producer worker 1, and consumer worker 2. No heap, dynamic thread creation, or scheduler path outside PIT IRQ0 is introduced.

- The `start_event` has a predicate (`signaled`) and its own bounded wait queue. `event_set()` first sets the predicate, then calls `waitq_wake_all()` while interrupts are disabled.
- Each semaphore has `count`, `max`, a bounded wait queue, and operation accounting. `sem_down()` first consumes an available token; otherwise it enqueues the worker and publishes `blocked-sem` before restoring IF and halting.
- `sem_up()` makes one token available before using `waitq_wake_one()`. A wakeup is advisory: the resumed worker rechecks and decrements the count under the same critical-section rule.
- Queue primitives require their callers to hold an IF-preserving interrupt-disabled critical section. IRQ1 is already entered through an interrupt gate and remains limited to keyboard delivery plus EOI. It never schedules or chooses an IRQ frame.
- IRQ0 remains the sole saved-frame selector and the only path that returns through the scheduler's shared `iretq` boundary.

The producer writes the deterministic byte sequence `0..3` into a two-slot ring after `down(spaces)` and signals `items`. The consumer obtains `items`, validates sequence order, removes the byte, and signals `spaces`.

## Commands

```text
pctest       initialize the ring and start two workers; both wait on start_event
pcgo         set start_event and broadcast wake_all to both workers
pcinfo       show event, semaphore, ring, sequence, and completion invariants
ps           show worker states, stack ownership, and progress
threadinfo   show scheduler and retained keyboard wait-queue counters
kbdwaittest  retain Lesson 21 FIFO keyboard wake-one regression
sleeptest    retain Lesson 19 timer blocking regression
preempttest  retain Lesson 18 preemptive scheduling regression
```

Only one fixed worker test may run per boot. Run `pctest`, inspect `ps` / `pcinfo` while both workers are `blocked-event`, then run `pcgo`. The shell stays usable because event signaling does not use keyboard direct delivery.

## Required observations

After `pctest` and before `pcgo`:

- both workers report `blocked-event`;
- event waiter count is two;
- event `signaled` is zero;
- both worker stacks are allocated.

After `pcgo` and completion, `pcinfo` must report:

- event `wake-all` is two;
- produced and consumed both equal four;
- sequence errors are zero;
- ring `used` is zero;
- `spaces.count == 2`, `items.count == 0`;
- both semaphore waiter counts are zero;
- `complete` is `yes`.

After a further PIT scheduling boundary, `ps` must show worker stack physical addresses reaped to zero. `meminfo` must retain `tracked = free + used` and return to the pre-test free count.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
nm -u build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
readelf -lW build/kernel.elf
```

Static acceptance requires no continuation relocations or undefined symbols, file-backed mutable data, no outer ELF RWX `LOAD` segment, retained `invlpg`, and the IRQ0-only frame-selection / `iretq` path. The raw continuation linker may retain its longstanding RWX `LOAD` warning; that warning must not be confused with the checked outer Multiboot ELF.

QEMU acceptance is VGA-visible: boot with `make run`, perform the `pctest` / `pcgo` sequence above, then regress `kbdwaittest`, `sleeptest`, `preempttest`, PMM/VM, PIT, keyboard, and recoverable `bptest`. Run `vmfaulttest`, `pftest`, and `udtest` only in isolated fresh boots. No unexpected QEMU exception, triple fault, CR2, invalid opcode, or page fault is acceptable.

## Current limits

This remains a fixed teaching model: two worker slots, bounded queues, no cancellation, timeout integration, lock abstraction, idle context, user mode, or dynamically allocated synchronization objects. Lesson 23 will introduce an independent idle context so all ordinary threads may block without treating the shell as the implicit fallback runnable task.

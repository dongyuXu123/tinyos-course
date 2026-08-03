# Lesson 38: bounded wait queues and scheduling-class dispatch

> **Course status: learning implementation; no stable snapshot.**

Lesson 38 preserves the Lesson 37 task model and adds a Linux-style teaching model for synchronization and scheduling. Wait queues use a fixed FIFO array of thread IDs with bounded enqueue/dequeue operations. `wake_one` removes one waiter and changes `THREAD_BLOCKED_*` to `THREAD_RUNNABLE`; `wake_all` broadcasts the same transition. Existing keyboard, event, and semaphore behavior is unchanged.

The scheduler now exposes a small `struct sched_class` containing `pick_next`, `enqueue`, and `dequeue` hooks. The active `tiny_rr` class delegates to the inherited round-robin policy, so this is an abstraction boundary rather than a new scheduling policy. `schedinfo` reports the selected class and operation counters; `threadinfo`, `kbdinfo`, and `pcinfo` expose wait-queue activity.

## Commands and tests

After boot, run:

- `help` and `about` — Lesson 38 banner and command list.
- `schedinfo` — active class and enqueue/dequeue/pick counters.
- `kbdwaittest`, then type characters — FIFO keyboard waiters and `wake_one`.
- `pctest`, `pcgo`, `pcinfo` — event `wake_all`, semaphore waiters, and bounded producer/consumer checks.
- `threadinfo`, `kbdinfo`, `tasklist`, `taskvalidate` — runnable/blocking and task-model state.

## Linux source references

The model is intentionally smaller than Linux, but follows the shape of:

- `include/linux/wait.h` — wait queues, waiter entries, and wake helpers.
- `kernel/sched/core.c` — scheduler core and runnable state transitions.
- `include/linux/sched.h` — `task_struct` and scheduler-facing task state.
- `kernel/sched/sched.h` — `struct sched_class` operations and class chaining.

These are engineering references only; the teaching kernel has no libc, dynamic allocation, SMP, or Linux ABI.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
```

The inherited behavior remains the validation baseline: `processtest`, `vmtest`, `taskvalidate`, and the existing PIT, IRQ, syscall, and address-space commands should continue to pass. Do not use this learning directory as a stable snapshot.

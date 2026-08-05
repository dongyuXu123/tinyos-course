# Lesson 37: Linux 风格 task_struct 与任务状态机教学模型

> **Course status: stable snapshot (validated; canonical learning implementation promoted).**

Lesson 37 begins the Linux-core transition without importing Linux's unbounded scheduler implementation. It adds a fixed four-entry `task_struct` analogue with PID, TID, parent PID, kernel/user kind, Linux-inspired task states, and transition counters. Existing process/thread and PIT behavior remain bounded and intact. `tasklist` displays the table and `taskvalidate` checks identity uniqueness, parent ordering, state validity, and bounded initialization.

The model follows concepts from Linux `include/linux/sched.h` (`struct task_struct`, task-state bits) and `kernel/sched/core.c` state-transition paths, while intentionally using a small teaching subset: `TASK_RUNNING`, interruptible/uninterruptible sleep, stopped/traced, zombie, and dead states. It is metadata and validation, not a claim of Linux ABI or scheduler equivalence.

The inherited user-image validation, CPL3 syscall ABI, and PIT save/restore boundary are preserved. `processinfo`, `threadinfo`, `tasklist`, and `taskvalidate` expose the relevant bounded runtime state.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

From a fresh QEMU boot, run `tasklist` and `taskvalidate`, then `processtest`, `cpl3test`, and `processinfo`. Run `syscallinfo` and `threadinfo` to confirm the inherited ABI, PIT rate, and scheduler policy. The banner, `about`, and `help` text identify the bounded task table and preserved CPL3 IRQ0 save/restore boundary.

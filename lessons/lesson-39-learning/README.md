# Lesson 39: bounded fork/clone and resource boundaries

> **Course status: learning version (copied from Lesson 38 stable; not a stable snapshot).**

Lesson 39 adds a deliberately bounded Linux-style `fork`/`clone` teaching model while preserving inherited kernel behavior. It creates metadata only: a child receives distinct PID/TID and explicit parent metadata, copied user-image metadata, and a distinct address-space object. Kernel-owned resources such as console, PIT policy, and PMM accounting are marked shared. No child user instruction pointer is scheduled or executed.

## Commands and tests

After boot, run `help`, `about`, `forktest`, `cloneinfo`, `forkinfo`, and `forklifecycle`. The first creates one bounded metadata-only child; the info command displays PID/TID/parent, address-space identity, and copy/share boundaries; lifecycle validates the no-execution invariant. Existing `processtest`, `vmtest`, scheduler, wait queue, IRQ, syscall, and address-space commands remain unchanged.

## Linux source references

- `kernel/fork.c` — process creation and resource setup.
- `include/linux/sched.h` — `task_struct`, PID/TID identity, parent linkage, and task metadata.

These are engineering references only. This kernel has no libc, dynamic allocation, SMP, or Linux ABI; fork/clone is bounded metadata and never executes a real child.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

The Makefile uses `-Wall -Wextra -Werror` for 32-bit and 64-bit compilation. Do not use this learning directory as a stable snapshot.

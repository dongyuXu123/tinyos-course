# Lesson 55: blocking wait/wake and WNOHANG

> **Course status: learning checkpoint.**

Lesson 55 extends the bounded parent/child wait model with a wait-before-exit blocked state, exit wake-one publication, retry, and nonblocking `WNOHANG` observation. The model remains fixed-capacity metadata: no scheduler-wide wait set, dynamic allocation, arbitrary user pointers, or real child execution.

## Commands and tests

Run `waitblockinfo` for counters. Run `waitblocktest` for blocked wait → child exit → wake-one → retry → reap. Run `nohangtest` for empty and ready `WNOHANG` results. `waittest`, `waitinfo`, `shellrun`, `fdtest`, `pathtest`, `pipetest`, `polltest`, `signaltest`, `timertest`, `softirqtest`, `lockatomictest`, and `moduletest` remain regression tests.

## Linux source references

- `kernel/wait.c` — wait queue selection and nonblocking wait.
- `kernel/sched/core.c` — sleep/wake transition boundaries.
- `kernel/exit.c` — child exit status publication and reaping.

## Build and QEMU VGA validation

```bash
make clean && make -j"$(nproc)"
make check
../../scripts/qemu-vga-check.sh . waitblockinfo waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

The validator injects commands through the QEMU monitor and checks physical VGA text memory. Serial output is diagnostic only.

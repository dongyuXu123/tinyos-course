# Lesson 56: init adoption and bounded parent reparenting

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 56 models the fixed init process (PID 1) adopting one orphan child when its original parent exits. Wait ownership moves to init exactly once, while the child identity and bounded lifecycle remain explicit metadata.

## Commands and tests

Run `adoptioninfo` for parent ownership metadata. Run `reparenttest` for orphan detection, init adoption, wait ownership transfer, and one-shot reparenting. Lesson 55 wait/wake, WNOHANG, shell, file, pipe, signal, timer, softirq, lock, and module tests remain regression coverage.

## Linux source references

- `kernel/exit.c` — orphan handling and parent ownership.
- `kernel/wait.c` — adopted-child wait selection.
- `kernel/pid.c` — stable PID identity boundaries.

TinyOS models one init and one orphan only; it does not implement arbitrary process trees, namespaces, or unrestricted reparenting.

## Build and QEMU VGA validation

```bash
make clean && make -j"$(nproc)"
make check
../../scripts/qemu-vga-check.sh . adoptioninfo reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

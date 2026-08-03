# Lesson 50: locks, atomics, per-CPU data, and memory ordering

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 50 adds explicit IRQ-safe lock metadata, acquire/release/relaxed atomic helpers, and a one-CPU per-CPU model. The lock uses interrupt masking plus an atomic exchange; publication and observation are annotated with memory-order intent while preserving the freestanding single-CPU boundary.

## Commands and tests

Run `lockatomicinfo` for lock state, `NR_CPUS`, per-CPU deferred state, and memory-order vocabulary. Run `lockatomictest` to validate lock acquisition/release, atomic publication, and per-CPU ownership. Earlier `softirqtest`, `pcinfo`, `threadinfo`, and scheduler commands remain available.

## Linux source references

- `kernel/locking/spinlock.c` — spinlock ownership and IRQ-safe critical sections.
- `include/linux/atomic/atomic-instrumented.h` — atomic operations and ordering.
- `include/linux/percpu.h` — per-CPU data ownership.
- `Documentation/memory-barriers.txt` — acquire/release publication rules.

TinyOS models `NR_CPUS=1`; IRQ masking is local serialization, not proof of SMP safety. It does not claim a multicore scheduler, dynamic lock debugging, or arbitrary user-memory synchronization.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

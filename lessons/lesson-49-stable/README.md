# Lesson 49: softirq, tasklet, and workqueue

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 49 adds a fixed deferred-work model: softirq bits, coalescing tasklet slots, and a FIFO workqueue. A bounded budget drains tasklets before work items and carries pending work to the next PIT tick, modeling Linux bottom-half ordering without dynamic allocation or unbounded execution.

## Commands and tests

Run `softirqinfo` for pending bits, run/drop counters, budget exhaustion, and queue state. Run `softirqtest` to validate tasklet coalescing, tasklet-before-work ordering, FIFO work submission, bounded overflow, and budget carry-over. Earlier clock, timer, sleep, pipe, VFS, and process commands remain available.

## Linux source references

- `kernel/softirq.c` — softirq pending bits and bounded bottom-half execution.
- `kernel/tasklet.c` — historical tasklet scheduling and coalescing.
- `kernel/workqueue.c` — FIFO deferred work execution.

TinyOS invokes a fixed budget from the PIT path and stores metadata in static arrays. It does not implement real interrupt-context locking, dynamic work allocation, blocking bottom halves, or production Linux worker threads.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

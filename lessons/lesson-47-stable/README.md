# Lesson 47: signals, exception notification, and user return

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 47 adds bounded per-process signal records for user-originated breakpoint, illegal-instruction, and page-fault notifications. Exception metadata is preserved and consumed at a validated user-return boundary; default actions terminate the modeled user process for SIGILL and SIGSEGV. No user signal frame or handler memory is written.

## Commands and tests

Run `signalinfo` for pending records and delivery counters. Run `signaltest` to validate vector-to-signal mapping, bounded queueing, CPL3-only notification, and default actions. Run `userreturntest` to validate exactly-once delivery while preserving RIP/RSP/CS/SS. Earlier `pipeinfo`, `pipetest`, `polltest`, `ramfsinfo`, `pathtest`, and `fdtest` commands remain available.

## Linux source references

- `kernel/signal.c` — pending signal queues and delivery bookkeeping.
- `arch/x86/kernel/traps.c` — exception-to-signal paths.
- `kernel/entry/common.c` — notification processing before user return.
- `include/uapi/asm-generic/signal.h` — signal numbers and semantics.

TinyOS uses fixed records and metadata-only delivery. It does not install handlers, construct user signal frames, access arbitrary user memory, or implement asynchronous signal races.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

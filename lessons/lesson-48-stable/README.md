# Lesson 48: clock, timerfd-like model, and sleep

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 48 builds on the 100 Hz PIT tick source with a bounded monotonic clock, a timerfd-like metadata object, and deadline-based sleep accounting. Clock values are derived deterministically from ticks; timer expiration and sleep wake are modeled without adding a real file descriptor, syscall, or user-pointer access.

## Commands and tests

Run `clockinfo` for monotonic ticks/nanoseconds. Run `clocktest` to validate monotonicity and the 100 Hz conversion. Run `timerinfo` and `timertest` to validate one-shot and periodic deadlines, readable expiration counts, and cancellation. Run `sleeptimetest` to validate no early wake and zero remaining ticks at the deadline. Earlier lesson commands remain available.

## Linux source references

- `kernel/time/timekeeping.c` — monotonic clock accounting.
- `kernel/time/hrtimer.c` — timer deadlines and expiration.
- `fs/timerfd.c` — timerfd-readable expiration model.
- `kernel/time/sleep_timeout.c` — timed sleep and wake deadlines.

TinyOS retains the PIT as its sole time source at 100 Hz. It does not use RTC/HPET, implement a real timer file descriptor or nanosleep syscall, dereference user memory, or provide asynchronous timer signal delivery.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

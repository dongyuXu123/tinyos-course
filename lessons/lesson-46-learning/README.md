# Lesson 46: pipes, blocking I/O, and poll readiness

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 46 adds a fixed-capacity pipe ring with explicit empty/full blocking transitions, reader/writer wake accounting, and Linux-style `POLLIN`/`POLLOUT` readiness. The model is deterministic and metadata-only: it never dereferences user buffers or performs indefinite blocking in a shell command.

## Commands and tests

Run `pipeinfo` for ring and wait accounting. Run `pipetest` to validate FIFO order plus empty-reader and full-writer blocking transitions. Run `polltest` to validate `POLLIN`/`POLLOUT` readiness as the ring changes from empty to non-empty, full, and readable again. `ramfsinfo`, `pathtest`, and `fdtest` remain available as regressions.

## Linux source references

- `fs/pipe.c` — pipe ring storage, read/write blocking and wakeups.
- `fs/select.c` — poll wait and readiness semantics.
- `fs/eventpoll.c` — event registration and readiness propagation.
- `include/linux/poll.h` — `POLLIN`/`POLLOUT` vocabulary.

TinyOS uses a four-byte fixed ring, synthetic bounded wait queues, and nonblocking test helpers. It does not copy arbitrary user memory, sleep forever, implement file-descriptor endpoint close semantics, or provide a production poll table.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

# Lesson 52: integrated init, shell, files, processes, and pipes

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 52 integrates the teaching subsystems into a bounded user-space-style coordination model. A fixed init record owns shell command accounting while RAMFS path lookup, file descriptors, process metadata, pipes, signals, timers, and deferred work remain explicit cooperating objects.

## Commands and tests

Run `initinfo` for init readiness and coordination counters. Run `shelltest` to resolve `/bin/sh`, open a bounded file reference, observe the pipe state, and record process/signal coordination without executing arbitrary file contents. `moduleinfo`, `softirqinfo`, `fdinfo`, `ramfsinfo`, `pipeinfo`, `signalinfo`, and timing commands remain available.

## Linux source references

- `init/main.c` — init task and startup sequencing.
- `init/do_mounts_initrd.c` — early filesystem handoff.
- `kernel/exit.c` — process lifecycle and coordination.
- `fs/file.c` and `fs/pipe.c` — file and pipe ownership.
- `kernel/signal.c` — process notification.

TinyOS uses fixed metadata and deterministic shell tests. It does not execute a real shell binary, parse arbitrary commands into user processes, access disk-backed files, or provide unrestricted IPC or user-memory operations.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

# Lesson 53: controlled shell runtime and bounded command execution

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 53 closes the user-program path with a deterministic shell runtime model. A fixed `/bin/sh` RAMFS path resolves to an inode and file descriptor, then to a validated built-in image descriptor. The model records bounded argv/environment metadata, process start/exit, and links to the existing pipe, signal, timer, and deferred-work subsystems.

## Commands and tests

Run `initinfo`, `shellrun` (or `execpath`), `fdtest`, `pathtest`, `pipetest`, `polltest`, `signaltest`, `timertest`, `softirqtest`, `lockatomictest`, and `moduletest`. `shellrun` never executes arbitrary bytes: it validates one fixed image hash and bounded metadata, then accounts for lifecycle and subsystem coordination.

## Linux source references

- `fs/exec.c` and `fs/binfmt_elf.c` — executable lookup and image admission.
- `kernel/fork.c` and `kernel/exit.c` — process creation and exit accounting.
- `fs/pipe.c`, `kernel/signal.c`, and `kernel/time/timer.c` — cooperating runtime resources.

TinyOS remains freestanding, fixed-capacity, and libc-free. It does not parse unrestricted shell syntax, allocate dynamically, execute disk-backed programs, or dereference arbitrary user pointers.

## Build and QEMU VGA validation

```bash
make clean && make -j"$(nproc)"
make check
../../scripts/qemu-vga-check.sh . initinfo shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

The validator injects keys through the QEMU monitor, reads physical VGA text memory with `pmemsave`, and retains `build/qemu-check/` on failure. Serial output is diagnostic only.

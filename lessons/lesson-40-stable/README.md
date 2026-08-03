# Lesson 40: bounded execve and ELF-like image loading

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 40 adds a bounded Linux-style `execve`/ELF loader teaching model while preserving inherited kernel behavior. A tiny embedded ELF-like image is validated without allocation: magic, type/machine, program-header bounds, segment file/memory bounds, R/W/X flags, and entry-point range. The model builds deterministic argc/argv/envp stack metadata and never starts a real child or executes loaded bytes.

## Commands and tests

After boot, run `help`, `about`, `execinfo`, `exectest`, and `stacklayout`. `exectest` validates the embedded image and stack invariants; `execinfo` reports loader metadata; `stacklayout` shows the fixed argc/argv layout. Existing fork/clone, process, scheduler, wait queue, IRQ, syscall, and address-space commands remain unchanged.

## Linux source references

- `fs/exec.c` — execve lifecycle and replacement boundary.
- `fs/binfmt_elf.c` — ELF header, program-header, segment, and entry validation.

These are engineering references only. This kernel has no libc, dynamic allocation, SMP, or Linux ABI; loading is bounded metadata and never performs unsafe real execution.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

The Makefile uses `-Wall -Wextra -Werror` for 32-bit and 64-bit compilation. Do not use this learning directory as a stable snapshot.

# Lesson 51: module boundaries, exported symbols, and initialization

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 51 adds a bounded module metadata model with explicit initialization order and exported-symbol lookup. Core and VFS module records are initialized before shell use; only fixed exported names resolve, with no dynamic loader or writable symbol table.

## Commands and tests

Run `moduleinfo` for loaded module and export counters. Run `moduletest` to validate initialization order, exported-symbol resolution, and rejection of an unknown symbol. Earlier lock, softirq, VFS, process, and timing commands remain available.

## Linux source references

- `kernel/module/main.c` — module loading and lifecycle.
- `kernel/module/kallsyms.c` — exported-symbol lookup.
- `include/linux/module.h` — module metadata and boundaries.
- `init/main.c` — kernel initialization sequencing.

TinyOS uses fixed records initialized during boot. It does not parse ELF relocations for modules, load external code, expose writable exports, or execute untrusted module constructors.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

# Lesson 28: one-way CPL3 entry and #UD rsp0 proof

> **Course status: stable snapshot (validated; verified build artifacts included).**

## Goal

Enter ring 3 exactly once using fixed user code and stack pages, user-readable/writable page-table entries, GDT selectors `USER_DS=0x2b` and `USER_CS=0x33`, and an `iretq` frame with `IF=0`. The user stub executes `ud2`; the conditional #UD report proves the saved CPL3 frame and that the handler runs on TSS `rsp0`.

## Boundaries

This lesson is intentionally one-way. There is no syscall ABI, user scheduler, return-to-shell path, or general user address-space manager. Existing kernel-only shell, interrupts, scheduler, VM registry, TSS, and IST behavior remain inherited from Lesson 27.

## Validation

```bash
cd lessons/lesson-28-learning
make clean && make -j"$(nproc)"
make check
make run
```

After boot, `tssinfo` reports `USER_DS`, `USER_CS`, and the configured `rsp0`. Run `cpl3test` from a fresh boot. It enters the fixed user `ud2` stub with interrupts disabled; the #UD screen must identify `cs=0x33` and show handler `rsp` in the `rsp0` range. The path intentionally halts after reporting.

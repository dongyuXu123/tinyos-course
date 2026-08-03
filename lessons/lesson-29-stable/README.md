# Lesson 29: minimal CPL3 `int 0x80` syscall ABI

> **Course status: stable snapshot (validated; verified build artifacts included).**

The fixed CPL3 user stub loads `SYS_GETTICKS` (0) into `EAX`, executes `int 0x80`, then enters a `hlt` loop. Vector `0x80` is a DPL3 interrupt gate. The handler saves all GPRs, reports the syscall frame, returns ticks in `RAX`, and uses `iretq`. The user frame uses `RFLAGS=0x002` (reserved bit set, IF clear). Scheduler and user IRQs remain disabled; existing CPL3, exception, TSS/IST, IRQ, and VM constraints are inherited unchanged.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

Run `idtinfo`, then `cpl3test` from a fresh boot.

The inherited Lesson 28 published stable tag may still describe the pre-fix baseline; this learning tree intentionally includes the corrected frame, read-only user code mapping, and PMM-fixed user pages.

# Lesson 30: bounded syscall dispatcher and error returns

> **Course status: learning implementation; no stable snapshot.**

The fixed CPL3 user stub exercises a bounded `int 0x80` syscall table. `SYS_GETTICKS` (0) returns PIT ticks, `SYS_GETPID` (1) returns the fixed process id, and `SYS_WRITE_CONSOLE` (2) writes a bounded kernel-owned message without dereferencing a user pointer. Unknown syscall numbers return `-ENOSYS`. The handler saves all GPRs and returns through `iretq`; scheduler and user IRQs remain disabled.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

Run `idtinfo`, then `cpl3test` from a fresh boot.

The inherited Lesson 29 stable snapshot remains unchanged. This learning tree preserves its all-GPR frame, `iretq`, IF=0 user frame, read-only user code mapping, PMM-fixed user pages, and Lesson 29 maintenance corrections while adding the bounded dispatcher.

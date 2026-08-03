# Lesson 31: controlled user return and SYS_EXIT

> **Course status: learning implementation; no stable snapshot.**

The fixed CPL3 user stub invokes the inherited bounded `int 0x80` dispatcher for `SYS_GETTICKS` (0), `SYS_GETPID` (1), `SYS_WRITE_CONSOLE` (2), an unknown call (99), then `SYS_EXIT` (3). The first calls return normally through the all-GPR frame and `iretq`, preserving the user return frame with IF=0. `SYS_EXIT` reports the valid user frame in the kernel and intentionally halts in the terminal state; it does not return to user mode and adds no user IRQ or scheduler handling.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

Run `idtinfo`, then `cpl3test` from a fresh boot.

The inherited Lesson 29 stable snapshot remains unchanged. This learning tree preserves its all-GPR frame, `iretq`, IF=0 user frame, read-only user code mapping, PMM-fixed user pages, and Lesson 29 maintenance corrections while adding the bounded dispatcher.

# Lesson 32: validated kernel-embedded user image

> **Course status: learning implementation; no stable snapshot.**

The CPL3 program is now a kernel-embedded user image with a descriptor validated before any physical page allocation, mapping, or entry. Validation bounds image bytes and entry length, checks magic/version and offset arithmetic, and reports an image validation/load failure instead of entering an invalid image. The validated image retains the inherited syscall sequence: `SYS_GETTICKS` (0), `SYS_GETPID` (1), `SYS_WRITE_CONSOLE` (2), unknown call (99), then `SYS_EXIT` (3).

The code and stack remain fixed PMM pages. User code is mapped read-only/user, user IF remains disabled, syscall entry saves all GPRs and returns with `iretq`, and `SYS_EXIT` reports the valid frame then intentionally halts. No scheduler or user IRQ handling is added.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

Run `idtinfo`, then `cpl3test` from a fresh boot. The banner and `about` text identify Lesson 32 and the image validation/load boundary.

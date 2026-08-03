# Lesson 34: bounded address-space object over the validated kernel-embedded user image

> **Course status: learning implementation; no stable snapshot.**

The address-space object owns the current single low/high page-table pair. Its bounded window accepts only explicit user mappings; kernel-only/high-alias addresses, duplicate slots, duplicate frames, and release of an already released slot are rejected. The CPL3 program is now a kernel-embedded user image with a descriptor validated before any physical page allocation, mapping, or entry. Validation bounds image bytes and entry length, checks magic/version and offset arithmetic, and reports an image validation/load failure instead of entering an invalid image. The validated image retains the inherited syscall sequence: `SYS_GETTICKS` (0), `SYS_GETPID` (1), `SYS_WRITE_CONSOLE` (2), unknown call (99), then `SYS_EXIT` (3).

The code and stack remain fixed PMM pages. User code is mapped read-only/user, user IF remains disabled, syscall entry saves all GPRs and returns with `iretq`, and `SYS_EXIT` reports the valid frame then intentionally halts. No scheduler or user IRQ handling is added.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

Run `vmtest`, then try `vmap 0xffffffff80000000 <phys>`, duplicate `vmap`, and duplicate `vunmap` to observe validation. Run `idtinfo`, then `cpl3test` from a fresh boot. The banner and `about` text identify Lesson 34 and the image validation/load boundary.

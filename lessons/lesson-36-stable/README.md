# Lesson 36: 有界多用户程序运行时与退出回收

> **Course status: stable snapshot (validated; canonical learning implementation promoted).**

Lesson 36 completes the bounded user-program microkernel stage. Two fixed user program descriptors are created at boot. Each has its own process object, address-space object, user thread object, code page, stack page, PID, entry, and saved CPL3 context metadata. The objects are bounded arrays of length two; there is no dynamic process creation, fork, arbitrary image loading, or unbounded scheduler.

The inherited syscall ABI remains unchanged (`GETTICKS`, `GETPID`, `WRITE_CONSOLE`, `EXIT`, unknown `-ENOSYS`). IRQ0 recognizes a CPL3 frame by `CS`, acknowledges PIT, copies all GPRs plus `RIP/CS/RFLAGS/RSP/SS` into the active thread context, validates the return range, and restores the exact frame through one `iretq` path. The user IF policy remains disabled; no user IRQ callback or cross-address-space scheduling is introduced.

Lifecycle accounting covers READY → RUNNING → EXITED and bounded reclaim to EMPTY, with exit/reclaim counters shown by `processinfo`. PMM reserves and owns both embedded code/stack pairs and refuses to free owned frames.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

From a fresh QEMU boot, run `processtest` to check both process/address-space/thread objects, then `processinfo` to inspect both-program bounds and exit/reclaim counters. Run `userpitest` or `cpl3test` to verify the safe CPL3 IRQ0 frame path and unchanged syscall ABI. The banner, `about`, and `help` text identify Lesson 36.

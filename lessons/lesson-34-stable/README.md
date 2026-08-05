# Lesson 34: bounded process/thread object with saved user context

> **Course status: stable snapshot (validated; canonical learning implementation promoted).**

Lesson 34 keeps Lesson 33's validated embedded image, bounded address space, dual aliases, syscall ABI, and controlled exit. It adds one bounded process object (fixed PID 1) and one user thread (fixed TID 1). The process owns the inherited address-space object and image code/stack metadata. The thread owns kernel-stack metadata plus a saved user return context containing the complete syscall frame, syscall/result bookkeeping, and lifecycle state.

CPL3 entry is now an explicit `READY -> RUNNING` process/thread transition. Every non-exit syscall saves and validates the user context (`USER_CS`, `USER_DS`, image entry, and user stack top). `SYS_EXIT` saves the final context, validates it, performs `RUNNING -> EXITED` for both objects, reports the transition, and intentionally halts. `processinfo` displays ownership and context metadata; `processtest` validates the initial bounded lifecycle. This lesson deliberately adds no user scheduling and no user IRQ handling.

The image remains read-only/user code with a writable/user stack, user IF remains disabled, syscall entry saves all GPRs and returns with `iretq`, and the inherited syscall sequence remains `GETTICKS`, `GETPID`, `WRITE_CONSOLE`, unknown call, then `EXIT`.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

From a fresh QEMU boot, run `processtest`, `processinfo`, then `cpl3test`. The final syscall output must report a validated saved context and process/thread exit. Also run `vmtest`, `vminfo`, and `syscallinfo` to confirm Lesson 33 behavior remains intact. The banner and `about` text identify Lesson 34 and the bounded process/thread context boundary.

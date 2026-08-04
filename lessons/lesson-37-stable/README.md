# Lesson 37: Linux 风格 task_struct 与任务状态机教学模型

> **Course status: learning implementation; no stable snapshot.**

Lesson 35 extends the validated embedded user image and bounded process/thread objects from Lesson 34. IRQ0 now distinguishes a CPL3-origin frame by its `CS` selector. For the single user thread, the scheduler copies and validates the complete privilege-return frame (`RIP`, `CS`, `RFLAGS`, `RSP`, `SS`, and all GPRs) into the thread context, then restores that exact context through the one `iretq` path.

The PIT remains bounded at 100 Hz. A user-origin IRQ0 is acknowledged, records one preemption and one resume, and returns to the same validated user thread; it never dispatches an unsafe user IRQ callback or switches to another user address space. The saved `RFLAGS` preserves the user IF policy (the demo enters with IF clear), and invalid selectors or ranges are not accepted as a valid context. `processinfo` reports PIT user preempt/resume counters.

## Validation

```bash
make clean && make -j"$(nproc)"
make check
```

From a fresh QEMU boot, run `processtest`, `cpl3test`, and `processinfo`. Run `syscallinfo` and `threadinfo` to confirm the inherited ABI, PIT rate, and scheduler policy. The banner, `about`, and `help` text identify Lesson 35 and the bounded CPL3 IRQ0 save/restore boundary.

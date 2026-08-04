# Lesson 54: bounded shell wait, exit status, and zombie reaping

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 54 extends the controlled shell runtime with one fixed parent/child wait relationship. The child publishes a bounded exit code, enters `EXIT_ZOMBIE`, is selected by a wait-like operation, and transitions to `EXIT_DEAD` only after reaping. This is metadata-only and deterministic; it does not execute a real child or allocate dynamically.

## Commands and tests

Run `waitinfo` for parent/child state. Run `waittest` or `reaptest` to validate the running → zombie → waited → dead lifecycle and exit status. `shellrun`, `fdtest`, `pathtest`, `pipetest`, `polltest`, `signaltest`, `timertest`, `softirqtest`, `lockatomictest`, and `moduletest` remain regression tests.

## Linux source references

- `kernel/exit.c` — exit status, zombie state, and release.
- `kernel/wait.c` — bounded wait selection and status observation.
- `fs/exec.c` — inherited controlled shell image admission.

TinyOS models one fixed child and one parent, with fixed metadata and no arbitrary user pointers, dynamic allocation, unrestricted wait sets, or real process execution.

## Build and QEMU VGA validation

```bash
make clean && make -j"$(nproc)"
make check
../../scripts/qemu-vga-check.sh . waitinfo waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

The validator injects commands through the QEMU monitor and checks physical VGA text memory. Serial output is diagnostic only.

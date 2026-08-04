# Lesson 57: process exit resource teardown ledger

> **Course status: learning checkpoint.**

Lesson 57 records fixed address-space, file, pipe, signal, timer, and deferred-work references. Zombie retention preserves wait status; reap performs ordered release and rejects a second teardown.

Commands: `resourceinfo`, `teardowntest`, plus all Lesson 56 regression tests.

```bash
../../scripts/qemu-vga-check.sh . resourceinfo teardowntest reparenttest waitblocktest nohangtest waittest shellrun fdtest pathtest pipetest polltest signaltest timertest softirqtest lockatomictest moduletest
```

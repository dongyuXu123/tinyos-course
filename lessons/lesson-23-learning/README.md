# Lesson 23: independent idle context

> **Course status: learning edition (editable until validation is complete).**

Lesson 23 removes the scheduler's former implicit assumption that the shell is always runnable. The ordinary scheduler population remains exactly three TCBs—shell 0 and two PMM-backed workers—but it now has a separate static-stack idle continuation for periods in which no ordinary context is runnable.

## Idle contract

- Idle is **not** a fourth TCB: it is not in `threads[]`, cannot wait on queues, owns no PMM frame, and is excluded from worker-stack ownership and reaping.
- A synthetic IRQ0 frame starts `idle_trampoline` on a static, page-aligned kernel stack. Its only activity is `sti; hlt` in a loop.
- IRQ0 remains the only saved-frame selection and shared `iretq` return boundary. When it interrupted idle it saves the frame into `idle_frame`, never into a blocked ordinary TCB.
- If no ordinary `RUNNING` or `RUNNABLE` TCB exists, IRQ0 returns `idle_frame`. When PIT or another producer makes an ordinary context runnable, the following IRQ0 return selects that ordinary frame.
- Idle neither allocates, frees, prints, emits EOI, nor invokes the scheduler. IRQ1 retains its Lesson 21 boundary: keyboard bookkeeping/direct worker delivery plus EOI only; no scheduling or frame choice.

The previous `idle_worker_ticks` diagnostic remains historical compatibility data. `idle switches/ticks` is the actual Lesson 23 idle-context accounting.

## Commands

```text
idletest      sleep the shell for 150 PIT ticks; idle must run and IRQ0 must resume shell
threadinfo    show current ordinary/idle state and idle switch/tick accounting
ps            show the three TCBs plus static idle frame/stack status
pctest        retain event waiters; pcgo releases both with wake_all
pcgo / pcinfo retain the Lesson 22 producer-consumer test
kbdwaittest   retain FIFO keyboard wake-one regression
sleeptest     retain timer blocking regression
preempttest   retain PIT preemption regression
```

Only one fixed worker test may run per boot.

## VGA acceptance

1. Run `idletest`. VGA must show `shell sleeping while idle runs`, followed by `shell resumed through IRQ0`; the shell must accept another command.
2. `threadinfo` must show nonzero `idle switches/ticks`, proving an actual idle frame was selected while the shell slept.
3. Run `pctest`, then inspect `pcinfo`: two event waiters must be present. Run `pcgo`; after completion `pcinfo` must show event wake-all two, produced/consumed four, no sequence errors, empty ring, `spaces == 2`, `items == 0`, and `yes` completion.
4. After another PIT boundary, `ps` must show both finished worker stack physical addresses as zero; `meminfo` must preserve `tracked = free + used`.
5. Regress `kbdwaittest`, `sleeptest`, `preempttest`, PMM/VM, PIT, keyboard, and recoverable `bptest`. Fatal `vmfaulttest`, `pftest`, and `udtest` use separate fresh QEMU boots.

## Static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
nm -u build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
readelf -lW build/kernel.elf
```

The continuation must have no relocations or undefined symbols and file-backed mutable data. The outer Multiboot ELF must have no RWX `LOAD` segment. The longstanding raw-continuation RWX linker warning is not an outer-ELF claim. Inspect the IRQ0 path to retain one C-selected return frame and one shared register-restore/`iretq` sequence; IRQ1 must not reference idle scheduling.

## Current limits

This is still a fixed teaching kernel: no cancellation, timeout queue, lock abstraction, user mode, or dynamic synchronization objects. Lesson 24 will add TSS, `rsp0`, IST, and an exception stack to prepare reliable privilege transitions.

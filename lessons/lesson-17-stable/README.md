# Lesson 17: cooperative threads

> **Course status: learning edition (editable until validation is complete)**

Lesson 17 retains Lesson 16's double-mapped high runtime alias, bitmap PMM, controlled low VM slot, IDT, PIT, and IRQ-driven keyboard shell. It adds a deliberately small **cooperative** round-robin scheduler. A context switch happens only when ordinary kernel execution calls `yield`; neither IRQ0 nor IRQ1 selects a thread.

## Thread model

There are three fixed TCBs:

```text
thread 0  shell context
thread 1  deterministic worker A
thread 2  deterministic worker B
```

`threadstart` atomically creates both workers or creates none. Each worker owns exactly one 4 KiB PMM physical frame for its stack:

```text
stack PA       = one PMM-allocated 4 KiB frame
runtime stack  = KERNEL_VMA_BASE + stack PA
```

The existing Lesson 16 high alias already covers all PMM frames in the 4 MiB teaching window, so worker stacks need no new page-table mapping and never consume the single low VM slot at `0x3ff000`.

A worker stays `allocated` while runnable/running. `pfree <stack-pa>` reports `cannot free: thread stack`. When it finishes, it becomes `finished`; the next cooperative scheduler pass reclaims its stack frame and PMM accounting returns to its prior value.

Each worker performs four deterministic progress steps and calls `yield` after each step. This makes round-robin behavior observable without placing scheduling decisions in timer or keyboard interrupt context.

## Context switch contract

`context_switch` is AT&T assembly in the raw 64-bit continuation. It saves `rbx`, `rbp`, and `r12`–`r15`, stores the old `rsp`, loads the next `rsp`, restores those callee-saved registers, then returns into the target context.

New worker stacks contain a synthetic callee-saved frame and return target `thread_trampoline`. The trampoline enters C with normal SysV x86_64 stack alignment, runs the selected worker, marks it finished, then yields away permanently.

Run-queue state transitions run under the existing IF-preserving critical-section helpers. The shell context resumes after workers yield back to it. IRQ0 remains only `ticks++` plus PIC EOI; IRQ1 remains keyboard input plus PIC EOI.

## Commands

```text
help about clear lminfo hhinfo hhtest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `threadstart` creates both fixed workers once. It fails without partial creation if PMM allocation cannot provide both stack frames.
- `yield` voluntarily transfers to the next runnable context. It returns to the shell once the workers yield back.
- `ps` shows id, state, stack physical/high addresses, switch count, and worker progress.
- `threadinfo` shows scheduler current/round-robin state, aggregate switch count, worker progress, and explicitly reports `IRQ0 schedules: no`.

Argument-taking commands still require exactly one complete hexadecimal argument; all argumentless commands reject extra input with `usage:`.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
nm -u build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

Expected evidence: a valid Multiboot2 outer ELF; no continuation relocation or undefined symbol; file-backed `.data` (`PROGBITS`) for raw-continuation state; no outer RWX LOAD segment; and `context_switch`, retained `invlpg`, and IRQ `iretq` instructions in disassembly.

## QEMU VGA validation

1. Boot to `tinyos>`; run `meminfo` and record the initial free count.
2. Run `threadstart`, then `ps`. Threads 1 and 2 are `runnable`, each has a distinct allocated stack PA/high alias.
3. Run `yield` repeatedly. `threadinfo` shows increasing cooperative switches and worker progress; IRQ0 still states `schedules: no`.
4. After enough yields, `ps` reports both workers `finished` and their stack PA fields clear. `meminfo` returns to its prior accounting.
5. Regress `hhinfo`, `hhtest`, `vmtest`, PIT ticks, keyboard input, and `bptest`.
6. In separate boots run `vmfaulttest`, `pftest`, and `udtest`. Their expected fatal results remain CR2 `00000000003ff000`, CR2 `0000000000400000`, and `#UD`.

## Current limits

This is cooperative kernel-only scheduling with three fixed TCB slots and one-page stacks. It has no user mode, process isolation, guard pages, sleeping/blocking queues, dynamic TCB allocation, priorities, SMP, APIC, or timer-driven preemption. Lesson 18 will introduce scheduler-safe PIT preemption at the IRQ-return boundary.

# Lesson 27: bounded final-PT dual-alias mapping registry

> **Course status: stable snapshot (validated; canonical learning implementation promoted).**

## Goal

Lesson 27 turns Lesson 26's one dynamic VM window into a fixed 16-slot mapping registry while retaining the same 16 MiB bootstrap mapping horizon and preallocated paging topology.

## Contract

- The final low/high page-table pair reserves exactly 16 initially non-present matching PTEs:
  - low `0x0000000000ff0000`–`0x0000000000ffffff`
  - high `0xffffffff80ff0000`–`0xffffffff80ffffff`
- A live map owns exactly one PMM-allocated physical page and synchronizes both aliases to that page.
- `vmap <low-va> <phys>` accepts only page-aligned low VAs in that region and an allocated PMM page. It rejects occupied slots, an already-owned frame, and an inconsistent PTE pair.
- `vunmap <low-va>` clears both PTEs and releases only the VM ownership; it never frees the physical page.
- The PTE pair and registry are updated under a saved-IF critical section, then both aliases receive `invlpg`.
- `pfree <phys>` rejects any frame owned by a live mapping, as well as live worker-stack pages. It succeeds only after explicit `vunmap`.
- `vminfo [low-va]` reports the region live count; with an address, it also prints the slot owner and both PTEs.

## Commands and visible validation

Use `vminfo` after boot to see 0 live mappings. Allocate pages with `palloc`, then map with `vmap 0xff0000 <phys>` and inspect with `vminfo 0xff0000`. `pfree <phys>` must report `cannot free: mapped` while live. `vunmap 0xff0000` followed by `pfree <phys>` releases it.

`vmtest` uses two distinct slots and proves allocation, map, mapped-frame free rejection, low-write/high-read, high-write/low-read, unmap, explicit free, and exact PMM free-count restoration. `vmfaulttest` accesses the non-present first slot and is intentionally fatal; run it only in a fresh boot.

## Boundaries preserved

This lesson does not allocate page tables at runtime, map arbitrary RAM beyond the fixed 16 MiB horizon, introduce address spaces or user mappings, or alter the fixed three-TCB scheduler, independent idle context, TSS/`rsp0`/IST1, static stack guards, IRQ behavior, Multiboot handoff, or raw PIE continuation contract.

## Build and static check

```bash
cd lessons/lesson-27-learning
make clean && make -j"$(nproc)"
make check
make run
```

`make run` opens the VGA teaching shell. Regress `lminfo`, `hhinfo`, `meminfo`, `tssinfo`, `stackinfo`, `preempttest`, `idletest`, `pctest`/`pcgo`, keyboard behavior, and `bptest`. Run each intentional fatal fault (`vmfaulttest`, `isttest`, stack guards, `udtest`, `pftest`) from a new boot.

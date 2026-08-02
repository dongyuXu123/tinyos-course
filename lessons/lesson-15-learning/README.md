# Lesson 15: controlled dynamic page mapping

> **Course status: learning edition (editable until validation is complete)**

Lesson 15 keeps Lesson 14's bitmap physical page manager (PMM) and adds one deliberately constrained virtual-memory operation. It does **not** introduce a general VM allocator: exactly one 4 KiB virtual slot can be mapped and unmapped.

## Design

The 32-bit bootstrap still allocates only five page-table frames and establishes the existing 4 MiB low identity window. It now leaves one entry non-present:

```text
DYNAMIC_TEST_VA = 0x00000000003ff000
PML4[0] -> PDPT[0] -> PD[1] -> PT1[511]
```

`PT1[511]` is initially zero. The 64-bit continuation obtains PT1 solely through the physical address in the long-mode handoff, so no compile-time kernel address is assumed.

`vmap <hex>` accepts only a page-aligned physical frame that the PMM currently reports as `allocated`. It rejects invalid, free, fixed/reserved, and already-mapped input. It writes `physical | 0x003` (present and writable) to the one PTE and executes `invlpg`.

`vunmap` clears that PTE and executes `invlpg`, but intentionally does **not** free its physical page. The caller must run `pfree <hex>` after the mapping is removed. Conversely, `pfree` rejects an allocated page while it remains the VM slot owner, preventing a dangling mapping. PTE update plus TLB invalidation runs in a short IF-preserving critical section.

The PMM remains limited to the existing 4 MiB window. The original `pftest` remains an independent access to `0x0000000000400000`; it must still fault.

## Commands

```text
help about clear lminfo meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `vmap <hex>` maps one already-allocated PMM page at `0x3ff000`.
- `vunmap` removes the mapping without releasing its physical frame.
- `vminfo` prints the slot VA, mapped/unmapped state, owned PA, and raw PTE.
- `vmtest` allocates a page, maps it, writes and reads a fixed 64-bit pattern through the slot, unmaps it, frees it, and checks PMM accounting returned to its starting value.
- `vmfaulttest` intentionally accesses the unmapped slot. Run it only in a separate QEMU boot; it must cause a fatal #PF with CR2 `00000000003ff000`.

Hex arguments allow an optional `0x`. Leading and separating spaces/tabs are accepted. Argument-taking commands require exactly one complete argument, while argumentless commands reject extra text with `usage:`.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
readelf -SW build/kernel64.elf
readelf -lW build/kernel.elf
objdump -d -Mintel build/kernel64.elf
```

Expected evidence: `make check` reports a valid Multiboot2 header; `readelf -rW` has no continuation relocations; `.data` remains `PROGBITS` so continuation state is present in raw `kernel64.bin`; outer ELF LOAD segments are not RWX; and disassembly contains `map_page`, `unmap_page`, and `invlpg`.

## QEMU VGA validation

Run `make run`, wait for `tinyos>`, and use QEMU monitor `sendkey` commands:

1. Run `vminfo`; `state` is `unmapped` and `pte` is zero.
2. Run `meminfo`, `palloc`, record the allocated PA, and run `vmap <PA>`. `vminfo` reports the same owner PA and a present writable PTE; PMM counts do not change during `vmap`.
3. Run `pfree <PA>` while mapped; it reports `cannot free: mapped`. Run `vunmap`, then `pfree <PA>`; it succeeds.
4. Run `vmtest`; it reports `map/write/read/unmap/free passed` and `meminfo` still shows `tracked = free + used: yes`.
5. Verify rejections: `vunmap` while unmapped, a second `vmap` while mapped, `vmap 1001`, `vmap 0`, `vmap 400000`, `vmap <PA> extra`, and `vminfo extra`.
6. In a separate boot, run `vmfaulttest`; the fatal #PF reports CR2 `00000000003ff000`. In another boot, run `pftest`; CR2 remains `0000000000400000`.
7. Re-run `tickinfo`, `kbdinfo`, `bptest`, and `udtest` in their appropriate boot sessions to regress PIT, keyboard IRQ1, recoverable #BP, and fatal #UD.

## Current limits

This lesson provides one low-address, supervisor-only 4 KiB slot. It has no higher-memory mapping, demand paging, address-space isolation, page-table allocation, page replacement, reference counting, user mode, APIC, or scheduler.

# Lesson 14: bitmap physical page manager

> **Course status: learning edition (editable until validation is complete)**

Lesson 14 replaces Lesson 13's intentionally limited 64-entry allocation history with a physical page manager (PMM). The manager is initialized in the 64-bit continuation from the Multiboot2 memory map and manages 4 KiB frames in the existing 4 MiB identity-mapped window.

## Design

The 32-bit bootstrap now has only a temporary allocator for the five pages needed to enter long mode (`PML4`, `PDPT`, `PD`, `PT0`, and `PT1`). Its handoff contains those addresses plus kernel, stack, IDT, and Multiboot information; it no longer owns allocation history or lasting PMM state.

The x86_64 continuation contains two static 128-byte bitmaps (one bit per frame): allocation state and immutable reservations. They are emitted into raw `kernel64.bin` by `kernel64.ld`, so their storage is present at runtime after `objcopy`. During PMM initialization, available Multiboot type-1 ranges are made free, then these frames are permanently reserved:

- low 1 MiB;
- complete kernel image and bootstrap stack;
- Multiboot information block;
- all preexisting page-table frames;
- IDT backing page; and
- the PMM bitmap metadata itself.

`palloc` marks and returns a free frame. `pfree` requires exactly one aligned hexadecimal frame address, rejects invalid, fixed/reserved, and already-free frames, and never clears a fixed-reservation bit. Statistics track available frames managed by the PMM, free frames, and allocated/reserved frames; the visible invariant is `tracked = free + used`, with fixed reservations counted as used.

PMM initialization is fail-closed: malformed, truncated, or missing Multiboot2 mmap data leaves the manager unavailable and exposes its reason through `meminfo` and PMM commands instead of presenting it as ordinary allocator exhaustion.

PIT IRQ0, IRQ1 keyboard ring-buffer operation, `#BP` recovery, and fatal `#UD`/`#PF` paths remain unchanged.

## Commands

```text
help about clear lminfo meminfo palloc pfree <hex> pageinfo <hex> mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `meminfo` prints PMM readiness, the 4 KiB frame statistics, the `tracked = free + used` invariant, and both bitmap addresses.
- `palloc` prints one allocated physical frame, or an explicit unavailable/exhausted result.
- `pageinfo <hex>` reports exactly one of `free`, `allocated`, `fixed/reserved`, `invalid`, or `PMM unavailable`.
- `pfree <hex>` frees only a currently allocated, non-reserved 4 KiB frame and reports the exact rejected state otherwise.

Arguments are hexadecimal with optional `0x`, for example `pageinfo 101000` or `pfree 0x101000`. Leading and separating spaces/tabs are accepted, but every argument-taking command requires exactly one complete argument; trailing tokens are rejected. All argumentless commands reject extra input with a `usage:` message.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
readelf -SW build/kernel64.elf
objdump -d -Mintel build/kernel64.elf
```

Expected evidence: `make check` reports a valid Multiboot2 header; `readelf -rW` reports no relocations in the raw continuation; `.data` is `PROGBITS`, materializing PMM bitmaps; and the disassembly contains `pmm_init`, `pmm_alloc`, and `pmm_free_page` alongside retained IRQ stubs and `iretq`.

## QEMU VGA validation

Run `make run`, wait for `tinyos>`, and use QEMU monitor `sendkey` commands:

1. Run `meminfo`; its status is `ready` and it prints `invariant tracked = free + used: yes`.
2. Run `palloc`, record its printed address, and run `pageinfo <address>`; it reports `allocated`.
3. Run `pfree <address>`, then `pageinfo <address>`; it reports `free`. A second `pfree` reports `cannot free: free`; a later `palloc` reuses the earliest available frame.
4. Run `pageinfo 0`, `pageinfo 100000`, and `pfree 0`; low memory is `fixed/reserved` and cannot be freed. Also check `pageinfo 1001`, `pageinfo 400000`, `pfree xyz`, `palloc extra`, and `pageinfo 101000 extra`; each is rejected with its appropriate state or usage diagnostic.
5. Run `tickinfo`, wait at least one second, and run it again; ticks increase by roughly 100 per second.
6. Run `help`, `kbdinfo`, and `bptest`, then `help`; keyboard input remains responsive and `#BP` returns to the shell.
7. In separate QEMU boots, run `udtest` and `pftest`; each displays its expected fatal exception report, with `pftest` reporting CR2 `0000000000400000`.

## Current limits

The PMM deliberately covers only the existing 4 MiB identity window. It has no higher-memory mapping, NUMA awareness, contiguous/multi-frame allocation, zeroing policy, reference counts, virtual-memory allocator, APIC, user mode, or scheduler.

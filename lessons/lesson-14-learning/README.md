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

`palloc` marks and returns a free frame. `pfree` requires an aligned hexadecimal frame address, rejects invalid, reserved, and already-free frames, and never clears a fixed-reservation bit. Statistics track available frames managed by the PMM, free frames, and allocated/reserved frames.

PIT IRQ0, IRQ1 keyboard ring-buffer operation, `#BP` recovery, and fatal `#UD`/`#PF` paths remain unchanged.

## Commands

```text
help about clear lminfo meminfo palloc pfree <hex> pageinfo <hex> mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `meminfo` prints the PMM's 4 KiB frame statistics and both bitmap addresses.
- `palloc` prints one allocated physical frame.
- `pageinfo <hex>` reports `free`, `used/reserved`, or `invalid` for a frame address.
- `pfree <hex>` frees only a currently allocated, non-reserved 4 KiB frame.

Arguments are hexadecimal with optional `0x`, for example `pageinfo 101000` or `pfree 0x101000`.

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

1. Run `meminfo`, then `palloc`, record its printed address, and run `pageinfo <address>`; it reports `used/reserved`.
2. Run `pfree <address>`, then `pageinfo <address>`; it reports `free`. A second `pfree` rejects the page.
3. Run `pageinfo 0`, `pageinfo 100000`, and `pfree 0`; low memory is reported used/reserved and cannot be freed.
4. Run `tickinfo`, wait at least one second, and run it again; ticks increase by roughly 100 per second.
5. Run `help`, `kbdinfo`, and `bptest`, then `help`; keyboard input remains responsive and `#BP` returns to the shell.

## Current limits

The PMM deliberately covers only the existing 4 MiB identity window. It has no higher-memory mapping, NUMA awareness, contiguous/multi-frame allocation, zeroing policy, reference counts, virtual-memory allocator, APIC, user mode, or scheduler.

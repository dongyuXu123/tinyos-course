# Lesson 16: double-mapped higher-half kernel

> **Course status: learning edition (editable until validation is complete)**

Lesson 16 retains Lesson 15's bitmap PMM and single low-address VM slot, then adds a controlled higher-half execution alias. The kernel enters long mode through the retained low identity alias, switches its stack and continuation control flow to the canonical higher-half alias, and installs the IDT with high virtual addresses.

## Address layout

```text
KERNEL_VMA_BASE  = 0xffffffff80000000
KERNEL_PHYS_BASE = 0x0000000000100000

low alias:
  0000000000000000 - 00000000003fefff -> identical physical pages
  00000000003ff000                    -> non-present Lesson 15 VM slot

high alias:
  ffffffff80000000 - ffffffff803fffff -> PA 00000000 - 003fffff
```

The high base is canonical in four-level x86_64 paging. This lesson intentionally aliases the whole existing 4 MiB bootstrap window: it keeps the kernel image, raw continuation, stack, IDT backing store, Multiboot handoff, and bootstrap page tables safely reachable while the lower alias remains available for compatibility and debugging.

The page-table topology is explicit and independent for the two aliases:

```text
PML4[0]   -> LPDPT[0]   -> LPD[0] -> LPT0
                          LPD[1] -> LPT1 (PT1[511] is the non-present VM slot)

PML4[511] -> HPDPT[510] -> HPD[0] -> HPT0
                          HPD[1] -> HPT1
```

The bootstrap temporarily allocates nine page-table frames. Their handoff fields remain physical addresses and are reserved by the PMM as fixed pages. The transition keeps interrupts disabled while paging is enabled, the high stack is selected, and a high-IDT base is installed. It uses an absolute indirect transfer rather than a low-to-high `rel32` call.

The raw `kernel64.bin` continuation remains position-independent internally: it is linked at offset zero, embedded in the outer image, then entered through its high alias. Its mutable state remains file-backed `.data`, and it must have no residual relocations.

## Commands

```text
help about clear lminfo hhinfo hhtest meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest
```

- `hhinfo` displays the high VMA base, physical base, high alias range, high page-table topology, CR3, active stack, and high IDT base.
- `hhtest` writes a dedicated static test word through its low alias and reads it through the high alias. It reports whether both aliases observe the same value.
- `vmap`, `vunmap`, `vminfo`, `vmtest`, and `vmfaulttest` retain Lesson 15's single low-only VM slot semantics.

All argument-taking commands require exactly one complete hexadecimal argument; argumentless commands reject extra input with `usage:`.

## Build and static validation

```bash
make clean && make -j"$(nproc)"
make check
readelf -rW build/kernel64.elf
readelf -SW build/kernel64.elf
readelf -lW build/kernel.elf
objdump -d -Mintel build/kernel.elf
objdump -d -Mintel build/kernel64.elf
```

Expected evidence: valid Multiboot2 header; no continuation relocations or undefined symbols; inner `.data` is `PROGBITS`; outer LOAD segments are not RWX; bootstrap setup allocates the four high-alias table frames and installs `PML4[511]` / `PDPT[510]`; the transition contains an absolute indirect high-alias transfer; and `invlpg`, PIC IRQ stubs, and `iretq` remain present.

## QEMU VGA validation

1. Boot to `tinyos>` and run `hhinfo`; it reports base `ffffffff80000000`, a four-MiB high alias, and a high active RSP/IDT address.
2. Run `hhtest`; it reports `low/high aliases agree`.
3. Run `lminfo` and `meminfo`; all low/high table addresses are visible, PMM status is ready, and `tracked = free + used` is true.
4. Regress Lesson 15: `palloc`, `vmap <PA>`, mapped-page `pfree` rejection, `vunmap`, `pfree <PA>`, and `vmtest`.
5. Regress `tickinfo`, `kbdinfo`, `bptest`, and ordinary shell input.
6. In separate boots, run `vmfaulttest`, `pftest`, and `udtest`. Expected outcomes are fatal #PF CR2 `00000000003ff000`, fatal #PF CR2 `0000000000400000`, and fatal #UD respectively.

## Current limits

The low identity alias deliberately remains. This is not a general virtual-memory allocator, a fine-grained W^X/NX mapping design, a higher-memory direct map, user-mode isolation, dynamically allocated page tables, APIC support, or a scheduler.

# Lesson 25: guarded static kernel stacks and dual-alias VM window

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 25 turns the static idle, future `rsp0`, and IST1 stack layout into an explicit guarded runtime contract. It also turns the former low-only dynamic mapping slot into one controlled mapping window with low and high aliases.

## Contracts

- Each downward-growing static stack has one page of payload and a non-present **high-alias** guard page immediately below it.
- Low identity aliases remain mapped for bootstrap compatibility. The guards protect high-runtime-alias accesses only; they do not protect worker stacks or the bootstrap stack.
- Linker symbols, rather than declaration order, define every guard/payload/end range. `rsp0` and IST1 point to their payload ends.
- The mapping window has synchronized low `0x003ff000` and high `0xffffffff803ff000` aliases. Both start non-present; map/unmap updates and invalidates both while preserving IF.
- No CPL3, process, address-space switch, general page-table allocator, or worker-stack guard is added.

## Commands

```text
stackinfo                         print guard/payload/end ranges
stackguardtest idle|rsp0|ist1     fresh-boot fatal #PF guard test
vminfo                            show both runtime-window aliases and PTEs
vmtest                            prove low-write/high-read and reverse aliasing
```

`stackguardtest` is a deterministic volatile read of a selected guard page, not a compiler-dependent actual stack overflow. #PF keeps using IST1 and intentionally halts after reporting CR2, saved RSP, handler RSP, and IST1 range.

## Validation

Clean-build, `make check`, no continuation relocations/undefined symbols, inspect linker stack symbols, and check no RWX outer `LOAD`. QEMU VGA normal tests cover `stackinfo`, `vminfo`, `vmtest`, `bptest`, `idletest`, producer-consumer, timer, keyboard, PMM and VM regressions. Every fatal command runs in its own fresh boot.

## Current limits

A guard catches a downward crossing of its high-alias boundary, not high-end corruption. `rsp0` remains preparatory until a later CPL3 transition.

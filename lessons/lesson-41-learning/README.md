# Lesson 41: bounded VMA and demand page-fault model

Lesson 41 adds a fixed Linux-style virtual-memory-area table and a safe demand-fault teaching model while preserving real exception behavior. Each VMA records bounded start/end, protection, and file/anonymous backing metadata. Lookup and range checks classify simulated accesses as not-present, protection, or unmapped; only a bounded number of PMM-backed pages can be inserted and accounted. No command executes an unsafe memory fault.

## Commands and tests

After boot, run `help`, `about`, `vmainfo`, `vmatest`, and `pfmodel`. `vmainfo` displays the fixed VMA table and page/fault counters. `vmatest` checks lookup, range, and protection validation. `pfmodel` exercises all three simulated fault classifications and inserts one bounded page. Existing `pftest`, `isttest`, and real exception handlers remain unchanged and intentionally retain their fatal behavior.

## Linux source references

- `mm/mmap.c` — VMA creation, range organization, and lookup concepts.
- `mm/memory.c` — page-fault handling and page-table insertion concepts.
- `include/linux/mm.h` — Linux VMA/MM interfaces and protection vocabulary.

These are engineering references only. This kernel has no libc, dynamic allocation, SMP, or Linux ABI; the model uses fixed storage, bounded PMM accounting, and never triggers a real fault for teaching tests.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

The Makefile uses `-Wall -Wextra -Werror` for 32-bit and 64-bit compilation. This is a learning directory, not a stable snapshot.

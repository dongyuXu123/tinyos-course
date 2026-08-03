# Lesson 42: bounded user access and copy_to_user model

> **Course status: learning snapshot; derived from lesson-41-stable.**

Lesson 42 adds a fixed Linux-style user-pointer validation and bounded `copy_to_user`/`copy_from_user` teaching model while preserving real exception behavior. Validation checks canonical address form, user range and overflow, VMA membership, and read/write permission. Copies only account simulated bytes; they never dereference arbitrary user pointers or touch source/destination memory.

## Commands and tests

After boot, run `help`, `about`, `ptrinfo`, `ptrtest`, `copytest`, `vmainfo`, `vmatest`, and `pfmodel`. `ptrinfo` reports bounded uaccess limits and accounting. `ptrtest` exercises canonical/range/VMA/permission failures. `copytest` simulates successful and rejected `copy_to_user`/`copy_from_user` operations without dereferencing pointers. `vmainfo` displays the fixed VMA table and page/fault counters. `vmatest` checks lookup, range, and protection validation. `pfmodel` exercises all three simulated fault classifications and inserts one bounded page. Existing `pftest`, `isttest`, and real exception handlers remain unchanged and intentionally retain their fatal behavior.

## Linux source references

- `mm/mmap.c` — VMA creation, range organization, and lookup concepts.
- `mm/memory.c` — page-fault handling and page-table insertion concepts.
- `include/linux/mm.h` — Linux VMA/MM interfaces and protection vocabulary.
- `include/linux/uaccess.h` — user-pointer access and copy interface vocabulary.
- `mm/usercopy.c` — bounded usercopy hardening concepts.
- `arch/x86/include/asm/uaccess.h` — x86 access-range and fault-aware implementation reference.

These are engineering references only. This kernel has no libc, dynamic allocation, SMP, or Linux ABI; the model uses fixed storage, bounded PMM accounting, and never triggers a real fault for teaching tests.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

The Makefile uses `-Wall -Wextra -Werror` for 32-bit and 64-bit compilation. This is a learning directory, not a stable snapshot.

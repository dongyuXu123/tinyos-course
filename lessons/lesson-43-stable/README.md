# Lesson 43: page cache, anonymous pages, and reclaim

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 43 adds a fixed Linux-style model for anonymous pages, page-cache entries, references, dirty state, and bounded reclaim. Anonymous fault pages carry backing, accessed, dirty, reference, and reclaimable metadata. A bounded page-cache table models cache hits and misses. Reclaim scans fixed anonymous-page storage, skips pinned or non-reclaimable entries, and releases only validated PMM-owned frames. No disk I/O, swap, writeback thread, or arbitrary page fault is executed.

## Commands and tests

After boot, run `help`, `about`, `anoninfo`, `reclaimtest`, `vmainfo`, `vmatest`, and `pfmodel`. `anoninfo` reports anonymous/page-cache counts, cache hits and misses, and reclaim scan accounting. `reclaimtest` inserts an anonymous page, exercises a dirty page-cache miss followed by a cache hit, then reclaims the anonymous page. The test verifies bounded metadata and PMM release without dereferencing user memory.

## Linux source references

- `mm/filemap.c` — page-cache lookup and page state concepts.
- `mm/memory.c` — anonymous page and fault insertion concepts.
- `mm/vmscan.c` — reclaim scanning and reclaimability concepts.
- `include/linux/mm_types.h` — page, mapping, and reference metadata vocabulary.
- `include/linux/writeback.h` — dirty/writeback state vocabulary.

These are engineering references only. TinyOS has no libc, dynamic allocation, disk, swap, SMP, or Linux ABI; this lesson uses fixed arrays, bounded PMM accounting, and metadata-only cache/reclaim operations.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

The Makefile uses `-Wall -Wextra -Werror` for 32-bit and 64-bit compilation. This is a learning directory, not a stable snapshot.

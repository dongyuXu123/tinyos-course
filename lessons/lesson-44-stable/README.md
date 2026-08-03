# Lesson 44: file descriptors and VFS object references

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 44 adds a fixed Linux-style model of the file-descriptor table, `struct file`, `inode`, and `dentry`. Descriptors reference file objects; file objects retain offsets and flags and reference inodes; dentries provide bounded name-to-inode metadata. Open, read-offset, and close operations update reference counters without disk I/O or arbitrary pointer access.

## Commands and tests

Run `fdinfo` and `fdtest`. `fdtest` opens two descriptors, performs bounded offset accounting, closes both, and validates fd/file/inode reference ownership.

## Linux source references

- `fs/file.c` — descriptor-table operations.
- `fs/open.c` — open/file object semantics.
- `fs/inode.c` — inode lifetime and references.
- `fs/dcache.c` — dentry lookup and lifetime.
- `include/linux/fs.h` — VFS object vocabulary.

TinyOS uses fixed arrays, metadata-only operations, and no persistent storage; this is a teaching model rather than a Linux-compatible VFS.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

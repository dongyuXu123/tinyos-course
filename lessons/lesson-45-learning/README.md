# Lesson 45: ramfs, initramfs, and bounded VFS path lookup

> **Course status: stable snapshot (validated; verified build artifacts included).**

Lesson 45 adds a fixed, memory-backed ramfs/initramfs tree and a minimal absolute-path lookup model. The model walks bounded root-to-child metadata from path strings, resolving dentries to inode indices without dereferencing user pointers or performing block-device I/O.

## Commands and tests

Run `ramfsinfo` to inspect the embedded root, directories, files, and lookup counters. Run `pathtest` to validate `/`, `/etc`, `/etc/motd`, `/bin/sh`, missing paths, relative paths, and traversal through a regular file. `fdtest` and `fdinfo` remain available as Lesson 44 regressions.

## Linux source references

- `fs/ramfs/inode.c` — memory-backed inode and file operations.
- `init/initramfs.c` — early boot initramfs handoff.
- `fs/namei.c` — pathname component lookup and dentry walking.
- `fs/dcache.c` — dentry cache and parent/child relationships.
- `include/linux/fs.h` — VFS object vocabulary.

TinyOS uses a fixed metadata array and known paths (`/`, `/etc`, `/etc/motd`, `/bin`, `/bin/sh`). It does not parse a real cpio archive, access a block device, or execute resolved files; this is a bounded teaching model.

## Build and validation

```bash
make clean && make -j"$(nproc)"
make check
```

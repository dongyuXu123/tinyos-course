# 模块：文件系统

## 职责

提供 block buffer、superblock、inode、目录路径、文件描述符、pipe 和 exec 所需的文件抽象。

## 主要源码

- `source/fs/buffer.c`、`super.c`、`inode.c`
- `source/fs/namei.c`、`open.c`、`read_write.c`
- `source/fs/file_table.c`、`pipe.c`、`exec.c`

## 控制流

```text
path → namei/open → inode → file table → device/buffer
exec → inode → program image → user address space
```

## 工作示例：`open("/dev/tty0")`

```text
open_namei → namei/get_dir → find_entry → iget
  → bread/getblk → block-device request
  → file table / character-device open
```

## 工作示例：`exec("/bin/sh")`

```text
namei(filename) → bread(program header)
  → validate a.out/ZMAGIC
  → copy_strings → change_ldt → create_tables
  → demand-loaded user image
```

## 依赖

文件系统依赖内存分配和块设备；`exec` 还依赖进程地址空间和用户栈布局。

## 只读练习

```bash
grep -RIn 'bread\|iget\|namei\|do_execve' source/fs
```

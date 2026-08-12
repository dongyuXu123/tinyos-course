# 模块：用户空间与镜像加载

## 职责

提供用户态系统调用包装、退出/等待、文件操作和由 `exec` 装入的初始程序接口。

## 主要源码

- `source/lib/`
- `source/fs/exec.c`
- `source/fs/open.c`
- `source/kernel/system_call.s`
- `source/init/main.c:init`

## 加载顺序

```text
init()
  → setup() → sys_setup() → mount_root()
  → open /dev/tty0 → dup descriptors
  → fork child → exec /bin/sh /etc/rc → wait
  → repeat fork → exec login shell
```

`init()` 不是打开 console 后直接 exec 一个 shell；它先让子进程运行 `/etc/rc`，父进程等待，然后才进入 login shell 循环。

## 与 TinyOS 对照

Linux 0.11 的 `a.out`/用户镜像和早期 32 位 segment 语义与 TinyOS 的 Multiboot2 ELF、x86-64 用户模型不同；只比较概念和控制流。

## 只读练习

```bash
grep -RIn 'execve\|do_execve\|_exit\|wait' source/init source/fs source/lib
```

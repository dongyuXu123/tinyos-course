# 模块：内核初始化

## 职责

`init/main.c` 把 early assembly 建立的最小环境扩展为可运行的内核，并创建第一个用户空间执行路径。

## 主要源码

- `source/init/main.c:main`
- `source/init/main.c:init`
- `source/kernel/sched.c`
- `source/mm/memory.c`
- `source/fs/super.c`
- `source/kernel/chr_drv/`、`source/kernel/blk_drv/`

## 阅读顺序

先在 `main` 中记录实际调用顺序，再进入每个初始化函数；不要用现代 Linux 的初始化表替代本版本的显式调用。

源码中的初始化顺序是：

```text
mem_init → trap_init → blk_dev_init → chr_dev_init → tty_init
  → time_init → sched_init → buffer_init → hd_init → floppy_init
  → sti → move_to_user_mode → fork
```

`main()` 不直接挂载文件系统。task 1 的 `init()` 后续执行：

```text
init → setup → sys_setup → mount_root → open /dev/tty0
```

随后 `init()` 复制标准描述符，fork 子进程执行 `/etc/rc`，等待其结束，再循环创建 login shell。

## 关键问题

每个初始化步骤都要记录写入的全局状态、依赖的中断/页表条件和失败时的行为。特别标记 `sched_init()` 安装 timer gate 与 `int 0x80` system gate 的位置。`init` 负责从内核初始化进入 root mount、console 和初始程序。

## 只读练习

```bash
grep -nE 'main|init\(' source/init/main.c
grep -RIn 'mount_root\|fork\|exec' source/init source/fs source/kernel
```

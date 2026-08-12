# Linux 0.11 跨模块调用图

固定源码版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`。

## 启动到 C

```text
boot/bootsect.s:go/load_setup/read_it
  → boot/setup.s
  → boot/head.s:startup_32
  → boot/head.s:setup_paging
  → init/main.c:main
```

boot sector 负责镜像内的读取；setup 准备 BIOS 参数和模式切换；head 建立 C 入口所需的地址空间和栈。三者是汇编启动阶段，不要用 `main()` 的 C 初始化顺序替代它们。

## main 初始化与第一个用户进程

```text
main()
  → mem_init
  → trap_init
  → blk_dev_init → chr_dev_init → tty_init
  → time_init → sched_init → buffer_init
  → hd_init → floppy_init
  → sti → move_to_user_mode → fork
                                      ↓
                              task 1: init()
                                      ↓
                    setup → sys_setup → mount_root
                                      ↓
                         open /dev/tty0 → dup
                                      ↓
               fork child → exec /bin/sh /etc/rc → wait
                                      ↓
                         fork → exec login shell
```

源码锚点：`source/init/main.c:main/init`、`source/kernel/blk_drv/hd.c:sys_setup`、`source/fs/super.c:mount_root`。

## 系统调用到子系统

```text
user int 0x80
  → kernel/system_call.s:system_call
  → eax range check + register arguments
  → include/linux/sys.h:sys_call_table[eax]
  → sys_open / sys_execve / sys_fork / sys_waitpid
  → fs/namei.c:open_namei
    or fs/exec.c:do_execve
    or kernel/fork.c:copy_process
  → memory / inode / buffer / scheduler
```

Linux 0.11 使用历史 32 位 `int 0x80` ABI；不要用现代 x86-64 `syscall` 指令、`rdi/rsi/rdx` 参数规则替代它。

## fork 与调度

```text
sys_fork
  → kernel/fork.c:copy_process
  → copy_mem
  → mm/memory.c:copy_page_tables
  → task table + TSS/LDT
  → scheduler selects TASK_RUNNING child
  → switch_to
```

## `/dev/tty0` 与 `/bin/sh` 路径

```text
pathname
  → fs/namei.c:get_dir/namei/open_namei
  → fs/inode.c:iget
  → fs/buffer.c:bread/getblk
  → block-device request path
```

```text
/bin/sh or /etc/rc
  → namei
  → fs/exec.c:do_execve
  → read executable header
  → copy_strings/create_tables/change_ldt
  → user image and entry point
```

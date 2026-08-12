# Linux 0.11 源码地图

固定版本见 [`../source-revision.txt`](../source-revision.txt)。路径均相对于 `linux011-course/source/`。

| 模块 | 职责 | 主要文件 | 关键入口/对象 | 依赖 |
|---|---|---|---|---|
| Boot | BIOS 后的启动扇区、setup、保护模式入口 | `boot/bootsect.s`, `boot/setup.s`, `boot/head.s` | `startup_32` | BIOS、GDT、页表 |
| Init | 内核启动编排和第一个用户进程 | `init/main.c` | `main`, `init` | 显式初始化 memory/traps/devices/scheduler；root fs 在 task 1 中延后挂载 |
| Process | 任务表、调度、fork、exit、signal | `kernel/sched.c`, `kernel/fork.c`, `kernel/exit.c`, `kernel/signal.c` | `schedule`, `switch_to`, `copy_process`, `do_exit` | TSS、IRQ、页表 |
| Traps/syscalls | IDT、异常和系统调用入口 | `kernel/traps.c`, `kernel/system_call.s`, `include/asm/system.h` | trap handlers、system-call dispatch | GDT、IDT、PIC |
| Memory | 页表、页分配、COW 基础 | `mm/memory.c`, `mm/page.s` | page-table helpers | boot memory state |
| Filesystem | buffer cache、superblock、inode、路径、exec | `fs/buffer.c`, `fs/super.c`, `fs/inode.c`, `fs/namei.c`, `fs/exec.c` | `bread`, inode/path operations, `do_execve` | block devices、memory |
| Devices/TTY | 块/字符设备、键盘、控制台、TTY、串口 | `kernel/blk_drv/`, `kernel/chr_drv/` | device init、IRQ handlers | PIC、PIT、I/O ports |
| User space | 系统调用封装和最小 libc | `lib/`, `include/unistd.h` | `_exit`, `execve`, `wait`, `open` | syscall ABI |
| Image tools | 合并 boot/setup/system 为 Image | `tools/build.c` | image builder | boot objects、ELF output |

## 读图方法

先确认源码版本，再用 `grep -RIn` 定位符号。不要把同名文件从其他 Linux 版本复制进本模块；Linux 0.11 的结构和现代 Linux 并不等价。

# 模块学习顺序

| 顺序 | 模块 | 先读 | 后读 |
|---:|---|---|---|
| 1 | [boot](modules/boot.md) | `bootsect.s`、`setup.s`、`head.s` | init |
| 2 | [init](modules/init.md) | `init/main.c` | process、devices、filesystem |
| 3 | [process-scheduler](modules/process-scheduler.md) | `sched.c`、`fork.c`、`exit.c` | memory、syscalls |
| 4 | [memory](modules/memory.md) | `memory.c`、`page.s` | process、filesystem |
| 5 | [filesystem](modules/filesystem.md) | buffer、super、inode、namei、exec | devices、user-space |
| 6 | [devices-tty](modules/devices-tty.md) | block/char drivers、TTY | init、syscalls |
| 7 | [traps-syscalls](modules/traps-syscalls.md) | traps、system_call.s | process、user-space |
| 8 | [user-space](modules/user-space.md) | `lib/` wrappers、exec | startup checkpoint |

每个模块都区分：源码事实、硬件/ABI 假设、与 TinyOS 的工程对照。
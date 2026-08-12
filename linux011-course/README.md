# Linux 0.11 源码教学模块

本模块使用用户指定的 `karottc/linux-0.11` 源码副本，讲解 Linux 0.11 的启动、加载、初始化和主要内核模块。它是 TinyOS/GRUB 课程之外的源码阅读轨道，不是现代 Linux，也不默认构建或启动外部内核。

## 快速入口

- 来源与版本：[`SOURCE-PROVENANCE.md`](SOURCE-PROVENANCE.md)
- 源码地图：[`docs/source-map.md`](docs/source-map.md)
- 启动顺序源码串讲：[`docs/startup-source-tour.md`](docs/startup-source-tour.md)
- 启动和加载顺序速查：[`docs/startup-load-order.md`](docs/startup-load-order.md)
- 跨模块调用图：[`docs/call-graphs.md`](docs/call-graphs.md)
- Image 线性布局：[`docs/image-layout.md`](docs/image-layout.md)
- 系统调用 ABI：[`docs/syscall-abi.md`](docs/syscall-abi.md)
- 模块索引：[`docs/module-index.md`](docs/module-index.md)
- 只读检查：`python3 scripts/check-source-study.py`

## 模块

| 模块 | 说明 |
|---|---|
| [boot](docs/modules/boot.md) | bootsect、setup、head 和保护模式入口 |
| [init](docs/modules/init.md) | `init/main.c` 的内核初始化和第一个用户进程 |
| [process-scheduler](docs/modules/process-scheduler.md) | task、调度、fork、exit、signal |
| [memory](docs/modules/memory.md) | 页表、物理页和内存管理 |
| [filesystem](docs/modules/filesystem.md) | buffer、superblock、inode、路径、文件、pipe、exec |
| [devices-tty](docs/modules/devices-tty.md) | 块设备、字符设备、键盘、控制台、TTY、串口 |
| [traps-syscalls](docs/modules/traps-syscalls.md) | 异常、中断和系统调用入口 |
| [user-space](docs/modules/user-space.md) | 用户库、系统调用封装和初始 shell/program |

重点源码逐段讲解见 [`docs/annotations/`](docs/annotations/)。注释以源码路径、符号和固定 commit 为锚点，不复制第二份完整源码。

## 启动/加载主线

```text
BIOS
  → boot/bootsect.s
  → boot/setup.s
  → boot/head.s:startup_32
  → init/main.c:main
  → trap / memory / scheduler / device / filesystem 初始化
  → fork 第一个用户进程
  → init() 挂载根文件系统、打开控制台、exec 初始程序
```

推荐先读 `docs/startup-source-tour.md`，再用 `docs/startup-load-order.md`、`docs/call-graphs.md` 和模块文档逐个回到源码；模块顺序为 `boot → init → process-scheduler → memory → filesystem → devices-tty → traps-syscalls → user-space`。

## 安全与验证边界

源码副本保持来源内容；不要执行其中的 `Makefile`、安装脚本、磁盘写入目标或未经审阅的二进制。验证脚本只执行文件清单、SHA-256、路径和文档检查：

```bash
python3 scripts/check-source-study.py
sha256sum -c source.sha256
```

需要编译或在 QEMU 中运行 Linux 0.11 时，应在隔离、可恢复的临时副本中自行准备对应的历史工具链；这不属于本模块的默认验证。

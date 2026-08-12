# Linux 0.11 启动与加载顺序

以下顺序针对 `source-revision.txt` 记录的源码。硬件/BIOS 协议来自平台约定，函数调用关系来自源码；遇到发行版补丁应重新核对。

## 总流程

```mermaid
flowchart TD
  BIOS[BIOS 读取 boot sector] --> B[boot/bootsect.s]
  B --> S[boot/setup.s\n读取参数并准备过渡]
  S --> H[boot/head.s:startup_32\n保护模式、描述符、分页、栈]
  H --> M[init/main.c:main]
  M --> I[trap/memory/scheduler/device/fs 初始化]
  I --> F[fork 第一个用户进程]
  F --> U[init()\n挂载 root、打开 console]
  U --> E[exec 初始 shell/program]
```

## 分阶段说明

| 阶段 | 源码锚点 | 输入 | 结果 |
|---|---|---|---|
| 1. BIOS handoff | 平台启动协议；`boot/bootsect.s` | 启动设备上的第一个 512 字节 | CPU 开始执行 boot sector |
| 2. boot sector | `boot/bootsect.s` | 磁盘布局、磁盘读服务 | 将 setup 和 system 的后续扇区加载到内存 |
| 3. setup | `boot/setup.s` | BIOS 参数和已加载内容 | 保存硬件信息并完成进入保护模式前的准备 |
| 4. early protected mode | `boot/head.s:startup_32` | setup 传递的状态 | 设置 GDT/IDT 相关基础、页表和内核栈，进入 C 入口 |
| 5. kernel C init | `init/main.c:main` | early assembly 建立的运行环境 | 依次初始化陷阱、内存、调度、设备、buffer 和文件系统 |
| 6. first user process | `init/main.c:init` 及 process code | 可调度的 task 和初始化后的内核 | 挂载根文件系统、建立控制台并执行初始用户程序 |

## C 初始化的阅读方法

不要先假设“现代 Linux 初始化顺序”。在固定源码中直接阅读 `init/main.c:main` 的调用序列，再沿每个调用进入模块：

`init/main.c:main` 的实际调用顺序是：

```text
mem_init
  → trap_init
  → blk_dev_init
  → chr_dev_init
  → tty_init
  → time_init
  → sched_init
  → buffer_init
  → hd_init
  → floppy_init
  → sti
  → move_to_user_mode
  → fork
```

这里没有单独的 filesystem-init 调用。根文件系统挂载被延后到 task 1 的 `init()`：

```text
main:fork
  → init()
  → setup()
  → sys_setup()
  → mount_root()
```

实际函数名和调用顺序以固定源码为准。每个调用需要回答：它写入什么全局状态？下一个初始化步骤依赖什么？`sti` 之前哪些中断仍被屏蔽？

## 第一个用户进程

`init/main.c:init` 是 kernel-to-user-space 的阅读重点。结合 `kernel/fork.c`、`fs/super.c`、`fs/open.c` 和 `fs/exec.c` 跟踪：

1. 第一个用户任务如何产生；
2. root filesystem 如何挂载；
3. 标准输入/输出/错误如何关联 console；
4. `exec` 如何装载用户程序；
5. 父任务如何等待子任务。

## 验证（只读）

```bash
grep -n 'startup_32' source/boot/head.s
grep -nE 'main|init\(' source/init/main.c
grep -RIn 'schedule\|switch_to\|copy_process\|do_exit' source/kernel source/include
sha256sum -c source.sha256
```

这些命令只读取文本和校验源码，不运行 Linux 0.11 的构建或安装流程。

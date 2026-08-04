# TinyOS 从零操作系统课程

从零开始、逐课增量构建的一个教学操作系统。每课包含可编辑的学习版
`lesson-XX-learning/` 与只读的稳定快照 `lesson-XX-stable/`。

- 引导：GRUB + Multiboot2（i386 交接 ABI），随后在 TinyOS 内部进入 x86_64 long mode。
- 汇编：AT&T 语法。
- 运行时约束：无 libc（无 `printf`/`malloc`/`memcpy`/`memset`）。
- 验证：每课都在 QEMU VGA 上可见验证，不以串口为准。
- 规范权威：Intel SDM、Multiboot2 规范、GNU GRUB；Linux 内核源码仅作工程对照。

## 课程进度

| 课次 | 主题 | 状态 |
|---|---|---|
| 00 | 加电 → BIOS → GRUB → Multiboot2 → 保护模式交接 | 稳定 |
| 01 | 可见的 VGA Hello 内核 | 稳定 |
| 02 | VGA 控制台：清屏、定位、换行、滚屏 | 稳定 |
| 03 | PS/2 轮询键盘回显 | 稳定 |
| 04 | 最小命令 shell：help/about/clear | 稳定 |
| 05 | Multiboot2 type-6 内存图解析 | 稳定 |
| 06 | 早期保留感知的物理页分配器 | 稳定 |
| 07 | 最小 32 位 identity paging | 稳定 |
| 08 | 进入 x86_64 long mode + 64 位 C shell | 稳定 |
| 09 | 最小异常 IDT：#UD/#PF 终止诊断 | 稳定 |
| 10 | 可恢复 #BP：`int3` → report → `iretq` 返回 shell | 稳定 |
| 11 | 8259A PIC 重映射与首个 IRQ1 硬件中断路径 | 稳定 |
| 12 | IRQ 驱动 PS/2 键盘 + ring buffer shell | 稳定 |
| 13 | 8254 PIT 周期 tick（约 100 Hz） | 稳定 |
| 14 | bitmap 物理页管理器（alloc/free/reserve） | 稳定 |
| 15 | 受控单槽动态页映射/解除映射 | 稳定 |
| 16 | 双映射高半运行时别名 | 稳定 |
| 17 | 协作式线程调度 | 稳定 |
| 18 | PIT 抢占式调度 | 稳定 |
| 19 | PIT 定时休眠、阻塞与唤醒 | 稳定 |
| 20 | 有界键盘阻塞等待队列（wake_one） | 稳定 |
| 21 | 有界通用等待队列（wake_one / wake_all） | 稳定 |
| 22 | 固定 event / 计数信号量与生产者—消费者 | 稳定 |
| 23 | 独立 idle context（无普通 runnable 时的 IRQ0 回退） | 稳定 |
| 24 | 运行时 GDT/TSS、`rsp0` 与 #PF IST 异常栈 | 稳定 |
| 25 | 高别名静态栈 guard page 与双别名运行时映射窗口 | 稳定 |
| 26 | 16 MiB 映射物理范围内的 PMM 扩展 | 稳定 |
| 27 | final-PT 16 槽双别名 map/unmap 注册表与 PMM 所有权 | 稳定 |
| 28 | 首次 CPL3 进入与 TSS rsp0 异常栈证明 | 稳定 |
| 29 | CPL3 `int 0x80` 最小 syscall ABI | 稳定 |
| 30 | 有界 syscall dispatcher 与错误返回 | 稳定 |
| 31 | 受控用户返回与 SYS_EXIT 终止路径 | 稳定 |
| 32 | 校验后的内置用户程序镜像与最小加载器 | 稳定 |
| 33 | 有界 address-space 对象与内核/用户映射所有权 | 稳定 |
| 34 | 有界 process/thread 对象、保存的用户上下文与受控生命周期 | 稳定 |
| 35 | CPL3-origin IRQ0：单用户线程 RIP/CS/RFLAGS/RSP/SS 保存恢复与有界 PIT 抢占 | 稳定 |
| 36 | 有界多用户程序运行时与退出回收 | 稳定 |
| 37 | Linux 风格 task_struct 与任务状态机教学模型 | 稳定 |
| 38 | Linux 风格有界等待队列、wake_one/wake_all 与 scheduling-class 抽象 | 稳定 |
| 39 | Linux 风格有界 fork/clone：PID/TID/parent 与资源复制/共享边界 | 稳定 |
| 40 | Linux 风格有界 execve/ELF 段校验与确定性用户栈布局 | 稳定 |
| 41 | Linux 风格固定 VMA、范围校验与有界 demand page-fault 分类 | 稳定 |
| 42 | Linux 风格有界 user-pointer 校验与 copy_to_user/copy_from_user 教学模型 | 稳定 |
| 43 | Linux 风格页缓存、匿名页、脏页元数据与有界回收接口模型 | 稳定 |
| 44 | Linux 风格文件描述符表、file/inode/dentry 引用与偏移模型 | 稳定 |
| 45 | Linux 风格 ramfs/initramfs 与最小 VFS 路径查找 | 稳定 |
| 46 | Linux 风格管道、阻塞 I/O 与 poll/wait 机制 | 稳定 |
| 47 | Linux 风格信号、异常通知与用户态返回语义 | 稳定 |
| 48 | Linux 风格时间系统、timerfd-like 模型、睡眠与时钟抽象 | 稳定 |
| 49 | Linux 风格软中断、tasklet 与 workqueue 有界模型 | 稳定 |
| 50 | Linux 风格锁、原子操作、per-CPU 数据与内存序 | 稳定 |
| 51 | Linux 风格模块边界、导出符号与启动初始化序列 | 稳定 |
| 52 | 综合用户空间：init、shell、文件/进程协同与管道 | 稳定 |
| 53 | 受控 shell runtime、内置用户镜像与有界命令执行 | 稳定 |
| 54 | 有界 shell wait、exit status 与 zombie 回收 | 稳定 |
| 55 | 阻塞 wait/wake 与 WNOHANG | 稳定 |
| 56 | init adoption 与有界父进程重挂接 | 稳定 |
| 57 | 进程退出资源清理账本 | 稳定 |
| 58 | 有界多子进程 waitpid 选择 | 稳定 |
| 59 | fork → exec → exit 完整元数据生命周期 | 稳定 |
| 60 | 受控用户空间 job/session 模型 | 稳定 |
| 61 | Multiboot2 framebuffer 与像素绘制 | 已完成 |
| 62 | 固定 bitmap 字体、canvas 与基本绘图 | 已完成 |
| 63 | 键盘/鼠标输入事件队列 | 已完成 |
| 64 | 窗口、widget 与事件分发 | 已完成 |
| 65 | 桌面 compositor 与窗口管理器 | 已完成 |
| 66 | 图形 shell 与系统状态面板 | 已完成 |
| 67 | 图形桌面综合验证 | 已完成 |
| 68 | 进程组与 session 元数据 | 已完成 |
| 69 | session 首领与控制终端所有权 | 已完成 |
| 70 | 前台进程组切换与停止组保护 | 已完成 |
| 71 | 进程组/调度/COW 元数据 checkpoint | 已完成 |
| 72 | 进程元数据 checkpoint | 已完成 |
| 73 | 孤儿进程组检测与安全 reparent | 已完成 |
| 74 | job-control 信号路由 | 已完成 |
| 75 | 终端 stop/continue 状态转换 | 已完成 |
| 76 | 调度策略元数据 | 已完成 |
| 77 | priority/nice 优先级状态 | 已完成 |
| 78 | runqueue 运行队列统计 | 已完成 |
| 79 | voluntary preemption 主动抢占 | 已完成 |
| 80 | 定时器驱动调度 | 已完成 |
| 81 | context switch 上下文切换元数据 | 已完成 |
| 82 | Copy-on-Write 基础元数据 | 已完成 |
| 83 | COW 写时复制缺页统计 | 已完成 |
| 84 | 共享页生命周期 | 已完成 |
| 85 | fork 内存屏障与一致性 | 已完成 |
| 86 | 调度公平性验证 | 已完成 |
| 87 | 负载均衡与进程组调度综合 checkpoint | 已完成 |
| 88 | VFS 层次与 mount 元数据 | 已完成 |
| 89 | 超级块与文件系统注册 | 已完成 |
| 90 | inode 生命周期与引用 | 已完成 |
| 91 | dentry 缓存与路径组件 | 已完成 |
| 92 | 路径解析与遍历边界 | 已完成 |
| 93 | mount namespace 元数据 | 已完成 |
| 94 | 文件权限与访问检查 | 已完成 |
| 95 | 文件打开与 file_operations | 已完成 |
| 96 | 文件偏移与引用计数 | 已完成 |
| 97 | 目录读取与固定缓冲区 | 已完成 |
| 98 | 字符设备注册 | 已完成 |
| 99 | 设备节点与 major/minor | 已完成 |
| 100 | 设备打开与 ioctl 元数据 | 已完成 |
| 101 | 块设备请求队列 | 已完成 |
| 102 | 设备生命周期与卸载 | 已完成 |
| 103 | poll 就绪队列 | 已完成 |
| 104 | epoll 实例与固定 watch 表 | 已完成 |
| 105 | epoll 边沿触发 | 已完成 |
| 106 | epoll 水平触发 | 已完成 |
| 107 | epoll wait/wake 集成 | 已完成 |
| 108 | 服务状态机 | 已完成 |
| 109 | 服务依赖拓扑 | 已完成 |
| 110 | 服务启动与失败回滚 | 已完成 |
| 111 | 守护进程生命周期 | 已完成 |
| 112 | VFS/设备/epoll/服务综合验证 | 已完成 |
| 113 | mutex 与 spinlock 竞争 | 已完成 |
| 114 | 原子操作与内存序 | 已完成 |
| 115 | 信号量与等待队列并发 | 已完成 |
| 116 | per-CPU 数据访问 | 已完成 |
| 117 | 竞态窗口与屏障 | 已完成 |
| 118 | SMP CPU 状态 | 已完成 |
| 119 | SMP 启动元数据 | 已完成 |
| 120 | 跨 CPU 唤醒 | 已完成 |
| 121 | per-CPU runqueue | 已完成 |
| 122 | SMP 负载均衡 | 已完成 |
| 123 | RCU reader 临界区 | 已完成 |
| 124 | RCU grace period | 已完成 |
| 125 | RCU callback 队列 | 已完成 |
| 126 | RCU 对象回收 | 已完成 |
| 127 | RCU 与调度集成 | 已完成 |
| 128 | tracing ring buffer | 已完成 |
| 129 | 事件过滤与采样 | 已完成 |
| 130 | 锁依赖图 | 已完成 |
| 131 | 死锁检测元数据 | 已完成 |
| 132 | 崩溃诊断快照 | 已完成 |
| 133 | 异常路径与故障分类 | 已完成 |
| 134 | 内存压力诊断 | 已完成 |
| 135 | 调度与并发综合诊断 | 已完成 |
| 136 | SMP/RCU 回归验证 | 已完成 |
| 137 | 并发、SMP、RCU、诊断综合 checkpoint | 已完成 |
| 138 | 网络 buffer pool | 已完成 |
| 139 | 网络接口与链路状态 | 已完成 |
| 140 | 收发队列与包记账 | 已完成 |
| 141 | loopback 接口 | 已完成 |
| 142 | IPv4 地址元数据 | 已完成 |
| 143 | UDP socket 状态 | 已完成 |
| 144 | socket 端口分配 | 已完成 |
| 145 | 连接状态机 | 已完成 |
| 146 | socket poll/epoll 集成 | 已完成 |
| 147 | 网络错误与超时 | 已完成 |
| 148 | 进程 namespace | 已完成 |
| 149 | mount namespace 隔离 | 已完成 |
| 150 | network namespace | 已完成 |
| 151 | PID namespace | 已完成 |
| 152 | user namespace | 已完成 |
| 153 | cgroup 层级 | 已完成 |
| 154 | cgroup CPU 统计 | 已完成 |
| 155 | cgroup 内存限制 | 已完成 |
| 156 | cgroup 设备策略 | 已完成 |
| 157 | 资源限制与回收 | 已完成 |
| 158 | capability 权限检查 | 已完成 |
| 159 | syscall 安全边界 | 已完成 |
| 160 | 审计事件缓冲区 | 已完成 |
| 161 | 安全策略决策 | 已完成 |
| 162 | 网络、namespace、cgroup、安全综合 checkpoint | 已完成 |

Lesson 40 对照 Linux `fs/exec.c` 与 `fs/binfmt_elf.c`。Lesson 41 对照 Linux `mm/mmap.c`、`mm/memory.c` 与 `include/linux/mm.h`，仅实现固定元数据、模拟 fault 分类和有界页记账，不执行不安全真实 fault。Lesson 42 对照 Linux `include/linux/uaccess.h`、`mm/usercopy.c` 与 `arch/x86/include/asm/uaccess.h`，仅验证 canonical/range/overflow/VMA 权限并模拟有界 copy，绝不解引用任意用户指针。

## 构建与运行

```bash
cd lessons/lesson-XX-learning
make clean && make -j"$(nproc)"
make check
make run        # QEMU TCG，VGA 交互验证
```

每课 README 含完整的构建/静态检查/QEMU 验证步骤与调试地图。

图形界面阶段参考 LVGL/uGUI 的显示驱动、输入设备、控件树和脏区域分层思想，但 TinyOS 保持 freestanding、无 libc、固定容量和确定性构建，不引入宿主 GUI 库或网络依赖。图形输出使用受控 Multiboot2 framebuffer，VGA 文本仍是验证标记的权威诊断通道。


统一课程清单见 [COURSE-MANIFEST.md](COURSE-MANIFEST.md)。GUI 不再使用独立课号；GUI 架构说明保留在 Lesson 61～67。

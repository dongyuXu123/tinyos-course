# TinyOS 从零操作系统课程

从零开始、逐课增量构建的一个教学操作系统。每课只发布一个可直接学习和验证的稳定目录
`lesson-XX-stable/`；历史 learning 版本已经审阅并合并到 canonical stable 后移除。

- 引导：GRUB + Multiboot2（i386 交接 ABI），随后在 TinyOS 内部进入 x86_64 long mode。
- 汇编：AT&T 语法。
- 运行时约束：无 libc（无 `printf`/`malloc`/`memcpy`/`memset`）。
- 验证：每课都在 QEMU VGA 上可见验证，不以串口为准。
- 规范权威：Intel SDM、Multiboot2 规范、GNU GRUB；Linux 内核源码仅作工程对照。
- GUI 调试经验：[`docs/gui-debugging-playbook.md`](docs/gui-debugging-playbook.md)，覆盖 Lesson 61–67 的 framebuffer、绘图、鼠标、compositor、Terminal 和 QEMU 验收。
- 初学者学习文档：[`docs/learning-guide.md`](docs/learning-guide.md)，从零起步的全课程学习路线、启动链（Lesson 00–08）逐行源码精讲与各阶段学习指引。
- Linux 0.11 源码教学模块已拆分为独立仓库 **https://github.com/dongyuXu123/linux011-course**（karottc/linux-0.11 固定 commit 完整副本、模块总结、源码锚点注释与启动/加载顺序说明；不属于 TinyOS 可执行课程）。

## AI 编写与验证声明

本课程的内核代码、课程说明、验证脚本和部分调试文档由 AI 辅助编写，并由维护者审阅、整理和验收。当前仓库已对 Lesson 01–162 的 learning/stable 变体执行逐课结构检查、build、`make check` 和 QEMU 启动冒烟验证；Lesson 61–67 还进行了图形输出、键盘和 PS/2 鼠标的专项验收。自动化模型测试可以证明确定性状态机和边界条件，但不等同于每一种 GUI 视觉、鼠标轨迹或真实硬件场景的人工验收。

## 单一 stable 课程版本

课程整理后每节课只发布一个目录：`lessons/lesson-XX-stable/`。这样学习者不需要在 learning/stable 之间选择，也能直接使用随源码发布的稳定构建产物和验证配置。

- 已确认完全相同的课程统一采用 stable 内容；
- 有差异的课程先审阅 learning 实现，再将正确实现提升为 stable，不能直接沿用过时的 stable 快照；
- Lesson 34–37 使用已审阅的 learning-derived 实现，保留进程、进程生命周期和任务模型的正确课程递进；
- Lesson 61、71 的稳定版本包含已审阅的 graphics handoff 和 Makefile 检查修复；
- stable 目录包含课程源码、Makefile、GRUB/linker 配置、`build/` 产物及本地启动验证所需脚本；stable 快照默认按只读规则保护。

历史的 learning/stable 对比结果保留在 [`docs/learning-stable-diff-report.md`](docs/learning-stable-diff-report.md)，比较工具为 [`scripts/compare-course-variants.py`](scripts/compare-course-variants.py)。该报告用于说明 canonical 选择，不代表仓库继续发布两个可选版本。

## 新下载后的本地验证

先安装 GCC multilib、binutils、GNU GRUB 工具、xorriso/mtools、Make、QEMU、Python 3；GUI VGA 验收另需 `socat`。以 Ubuntu/Debian 为例：

```bash
sudo apt install build-essential gcc-multilib binutils grub-pc-bin grub-common \
  xorriso mtools qemu-system-x86 python3 socat
```

克隆后可直接验证单课：

```bash
scripts/validate-course.sh 162 check   # build + make check
scripts/validate-course.sh 162 qemu    # 隔离副本 build/check + QEMU smoke
```

脚本会复制 stable 课程到临时可写目录，不改写随仓库发布的 `build/` 和 ISO。Lesson 00 是文档课，执行 `scripts/validate-course.sh 00 check` 会提示使用 Lesson 01 stable 作为可执行基线。GUI 课程的真实窗口、键盘和鼠标验收仍应使用 [`scripts/qemu-vga-check.sh`](scripts/qemu-vga-check.sh) 和 [`docs/gui-debugging-playbook.md`](docs/gui-debugging-playbook.md) 中的专项流程。

## 课程验证分层

1. **结构检查**：确认课程文件、Makefile 目标和课程标记完整。
2. **build**：生成内核 ELF 和 ISO，并检查编译器/链接器错误。
3. **`make check`**：执行 Multiboot2 和课程特定的静态断言。
4. **QEMU 启动冒烟**：确认 ISO 能启动、VGA 可见、没有 triple fault 或异常退出。
5. **专项验收**：对 GUI 课程检查 framebuffer、窗口、Terminal、键盘和真实 PS/2 鼠标；后续课程检查其新功能及保留的 GUI/VGA 回归层。

## GRUB 源码研读支线

Lesson 00 与 Lesson 01 之间新增 10 个文档型 stable 小节，专门讲解 GNU GRUB 源码和启动产物：[`0.1`](lessons/lesson-0.1-stable/README.md) → [`0.2`](lessons/lesson-0.2-stable/README.md) → [`0.3`](lessons/lesson-0.3-stable/README.md) → [`0.4`](lessons/lesson-0.4-stable/README.md) → [`0.5`](lessons/lesson-0.5-stable/README.md) → [`0.6`](lessons/lesson-0.6-stable/README.md) → [`0.7`](lessons/lesson-0.7-stable/README.md) → [`0.8`](lessons/lesson-0.8-stable/README.md) → [`0.9`](lessons/lesson-0.9-stable/README.md) → [`0.10`](lessons/lesson-0.10-stable/README.md)。共享说明见 [`docs/grub-source-study.md`](docs/grub-source-study.md)。这些小节只读源码和工具输出，不改变 Lesson 01–162 的接口和验证基线。

```mermaid
flowchart LR
  L00[Lesson 00 总览] --> G01[0.1 源码树] --> G02[0.2 配置分发] --> G03[0.3 文件系统] --> G04[0.4 ELF 装载] --> G05[0.5 Multiboot2 ABI] --> G06[0.6 MBI tags] --> G07[0.7 BIOS/UEFI] --> G08[0.8 镜像构建] --> G09[0.9 故障调试] --> G10[0.10 端到端] --> L01[Lesson 01]
```

## 从零写 GRUB（Mini-GRUB 实现支线）

Mini-GRUB 课程已拆分为**独立仓库** **https://github.com/dongyuXu123/grub-course**：
B01–B23 共 23 课，从零复刻 GRUB 2.14 的 i386-pc 核心路径（实模式引导 → 保护
模式 → ELF/Multiboot2 装载 → MBI → ISO9660/El Torito → 配置脚本 → 模块系统
→ VBE 图形 → type-8 tag 交接 → 故障 rescue），全部已实现并验证
（`validate-course.sh all check|qemu`：23/23 PASS）；核心文件与 GRUB 2.14 源码
逐字节一致（26 个文件原样归档，`verify-reference.sh` 两层 sha256 校验）。
B12/B21 checkpoint 课只读复用本仓库（tinyos-course）的
`lessons/lesson-0X-stable/build/kernel.elf` 产物，详见 grub-course README。

## 课程前后关系

每个编号节点代表一节课，箭头表示“完成前一课后进入后一课”。课程主题的完整索引见 [`COURSE-MANIFEST.md`](COURSE-MANIFEST.md)。

```mermaid
flowchart LR
  subgraph S0[启动链与基础输出]
    L00 --> L01 --> L02 --> L03 --> L04 --> L05 --> L06 --> L07
  end
  subgraph S1[64 位内核、异常、中断与调度]
    L08 --> L09 --> L10 --> L11 --> L12 --> L13 --> L14 --> L15 --> L16 --> L17 --> L18 --> L19 --> L20 --> L21 --> L22 --> L23 --> L24 --> L25 --> L26 --> L27 --> L28 --> L29 --> L30 --> L31
  end
  subgraph S2[用户程序、进程、虚拟内存与用户空间]
    L32 --> L33 --> L34 --> L35 --> L36 --> L37 --> L38 --> L39 --> L40 --> L41 --> L42 --> L43 --> L44 --> L45 --> L46 --> L47 --> L48 --> L49 --> L50 --> L51 --> L52 --> L53 --> L54 --> L55 --> L56 --> L57 --> L58 --> L59 --> L60
  end
  subgraph GUI[图形桌面主线]
    L61 --> L62 --> L63 --> L64 --> L65 --> L66 --> L67
  end
  subgraph S3[进程组、session、调度与 COW]
    L68 --> L69 --> L70 --> L71 --> L72 --> L73 --> L74 --> L75 --> L76 --> L77 --> L78 --> L79 --> L80 --> L81 --> L82 --> L83 --> L84 --> L85 --> L86 --> L87
  end
  subgraph S4[VFS、设备、epoll 与服务]
    L88 --> L89 --> L90 --> L91 --> L92 --> L93 --> L94 --> L95 --> L96 --> L97 --> L98 --> L99 --> L100 --> L101 --> L102 --> L103 --> L104 --> L105 --> L106 --> L107 --> L108 --> L109 --> L110 --> L111 --> L112
  end
  subgraph S5[并发、SMP、RCU 与诊断]
    L113 --> L114 --> L115 --> L116 --> L117 --> L118 --> L119 --> L120 --> L121 --> L122 --> L123 --> L124 --> L125 --> L126 --> L127 --> L128 --> L129 --> L130 --> L131 --> L132 --> L133 --> L134 --> L135 --> L136 --> L137
  end
  subgraph S6[网络、namespace、cgroup 与安全]
    L138 --> L139 --> L140 --> L141 --> L142 --> L143 --> L144 --> L145 --> L146 --> L147 --> L148 --> L149 --> L150 --> L151 --> L152 --> L153 --> L154 --> L155 --> L156 --> L157 --> L158 --> L159 --> L160 --> L161 --> L162
  end
  L07 --> L08
  L31 --> L32
  L60 --> L61
  L67 --> L68
  L87 --> L88
  L112 --> L113
  L137 --> L138
```

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
| 61 | 可靠 framebuffer handoff 与图形输出前置 | 稳定 |
| 62 | backbuffer、像素格式、bitmap font 与 canvas | 稳定 |
| 63 | 键盘、PS/2 AUX 鼠标与输入事件队列 | 稳定 |
| 64 | 桌面对象模型与事件分发 | 稳定 |
| 65 | scene/compositor 与 Xfce 风格桌面 | 稳定 |
| 66 | 图形 Terminal 与安全命令 dispatcher | 稳定 |
| 67 | 图形桌面综合验收（GUI 结课） | 稳定 |
| 68 | 进程组与 session 元数据 | 稳定 |
| 69 | session 首领与控制终端所有权 | 稳定 |
| 70 | 前台进程组切换与停止组保护 | 稳定 |
| 71 | 进程组/调度/COW 元数据 checkpoint | 稳定 |
| 72 | 进程元数据 checkpoint | 稳定 |
| 73 | 孤儿进程组检测与安全 reparent | 稳定 |
| 74 | job-control 信号路由 | 稳定 |
| 75 | 终端 stop/continue 状态转换 | 稳定 |
| 76 | 调度策略元数据 | 稳定 |
| 77 | priority/nice 优先级状态 | 稳定 |
| 78 | runqueue 运行队列统计 | 稳定 |
| 79 | voluntary preemption 主动抢占 | 稳定 |
| 80 | 定时器驱动调度 | 稳定 |
| 81 | context switch 上下文切换元数据 | 稳定 |
| 82 | Copy-on-Write 基础元数据 | 稳定 |
| 83 | COW 写时复制缺页统计 | 稳定 |
| 84 | 共享页生命周期 | 稳定 |
| 85 | fork 内存屏障与一致性 | 稳定 |
| 86 | 调度公平性验证 | 稳定 |
| 87 | 负载均衡与进程组调度综合 checkpoint | 稳定 |
| 88 | VFS 层次与 mount 元数据 | 稳定 |
| 89 | 超级块与文件系统注册 | 稳定 |
| 90 | inode 生命周期与引用 | 稳定 |
| 91 | dentry 缓存与路径组件 | 稳定 |
| 92 | 路径解析与遍历边界 | 稳定 |
| 93 | mount namespace 元数据 | 稳定 |
| 94 | 文件权限与访问检查 | 稳定 |
| 95 | 文件打开与 file_operations | 稳定 |
| 96 | 文件偏移与引用计数 | 稳定 |
| 97 | 目录读取与固定缓冲区 | 稳定 |
| 98 | 字符设备注册 | 稳定 |
| 99 | 设备节点与 major/minor | 稳定 |
| 100 | 设备打开与 ioctl 元数据 | 稳定 |
| 101 | 块设备请求队列 | 稳定 |
| 102 | 设备生命周期与卸载 | 稳定 |
| 103 | poll 就绪队列 | 稳定 |
| 104 | epoll 实例与固定 watch 表 | 稳定 |
| 105 | epoll 边沿触发 | 稳定 |
| 106 | epoll 水平触发 | 稳定 |
| 107 | epoll wait/wake 集成 | 稳定 |
| 108 | 服务状态机 | 稳定 |
| 109 | 服务依赖拓扑 | 稳定 |
| 110 | 服务启动与失败回滚 | 稳定 |
| 111 | 守护进程生命周期 | 稳定 |
| 112 | VFS/设备/epoll/服务综合验证 | 稳定 |
| 113 | mutex 与 spinlock 竞争 | 稳定 |
| 114 | 原子操作与内存序 | 稳定 |
| 115 | 信号量与等待队列并发 | 稳定 |
| 116 | per-CPU 数据访问 | 稳定 |
| 117 | 竞态窗口与屏障 | 稳定 |
| 118 | SMP CPU 状态 | 稳定 |
| 119 | SMP 启动元数据 | 稳定 |
| 120 | 跨 CPU 唤醒 | 稳定 |
| 121 | per-CPU runqueue | 稳定 |
| 122 | SMP 负载均衡 | 稳定 |
| 123 | RCU reader 临界区 | 稳定 |
| 124 | RCU grace period | 稳定 |
| 125 | RCU callback 队列 | 稳定 |
| 126 | RCU 对象回收 | 稳定 |
| 127 | RCU 与调度集成 | 稳定 |
| 128 | tracing ring buffer | 稳定 |
| 129 | 事件过滤与采样 | 稳定 |
| 130 | 锁依赖图 | 稳定 |
| 131 | 死锁检测元数据 | 稳定 |
| 132 | 崩溃诊断快照 | 稳定 |
| 133 | 异常路径与故障分类 | 稳定 |
| 134 | 内存压力诊断 | 稳定 |
| 135 | 调度与并发综合诊断 | 稳定 |
| 136 | SMP/RCU 回归验证 | 稳定 |
| 137 | 并发、SMP、RCU、诊断综合 checkpoint | 稳定 |
| 138 | 网络 buffer pool | 稳定 |
| 139 | 网络接口与链路状态 | 稳定 |
| 140 | 收发队列与包记账 | 稳定 |
| 141 | loopback 接口 | 稳定 |
| 142 | IPv4 地址元数据 | 稳定 |
| 143 | UDP socket 状态 | 稳定 |
| 144 | socket 端口分配 | 稳定 |
| 145 | 连接状态机 | 稳定 |
| 146 | socket poll/epoll 集成 | 稳定 |
| 147 | 网络错误与超时 | 稳定 |
| 148 | 进程 namespace | 稳定 |
| 149 | mount namespace 隔离 | 稳定 |
| 150 | network namespace | 稳定 |
| 151 | PID namespace | 稳定 |
| 152 | user namespace | 稳定 |
| 153 | cgroup 层级 | 稳定 |
| 154 | cgroup CPU 统计 | 稳定 |
| 155 | cgroup 内存限制 | 稳定 |
| 156 | cgroup 设备策略 | 稳定 |
| 157 | 资源限制与回收 | 稳定 |
| 158 | capability 权限检查 | 稳定 |
| 159 | syscall 安全边界 | 稳定 |
| 160 | 审计事件缓冲区 | 稳定 |
| 161 | 安全策略决策 | 稳定 |
| 162 | 网络、namespace、cgroup、安全综合 checkpoint | 稳定 |

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

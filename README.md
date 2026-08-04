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
| 55 | 阻塞 wait/wake 与 WNOHANG | 学习中 |
| 56 | init adoption 与有界父进程重挂接 | 稳定 |
| 57 | 进程退出资源清理账本 | 学习中 |

Lesson 40 对照 Linux `fs/exec.c` 与 `fs/binfmt_elf.c`。Lesson 41 对照 Linux `mm/mmap.c`、`mm/memory.c` 与 `include/linux/mm.h`，仅实现固定元数据、模拟 fault 分类和有界页记账，不执行不安全真实 fault。Lesson 42 对照 Linux `include/linux/uaccess.h`、`mm/usercopy.c` 与 `arch/x86/include/asm/uaccess.h`，仅验证 canonical/range/overflow/VMA 权限并模拟有界 copy，绝不解引用任意用户指针。

## 构建与运行

```bash
cd lessons/lesson-XX-learning
make clean && make -j"$(nproc)"
make check
make run        # QEMU TCG，VGA 交互验证
```

每课 README 含完整的构建/静态检查/QEMU 验证步骤与调试地图。

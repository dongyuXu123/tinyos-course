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
| 34 | 有界 process/thread 对象、保存的用户上下文与受控生命周期 | 学习中 |

## 构建与运行

```bash
cd lessons/lesson-XX-learning
make clean && make -j"$(nproc)"
make check
make run        # QEMU TCG，VGA 交互验证
```

每课 README 含完整的构建/静态检查/QEMU 验证步骤与调试地图。

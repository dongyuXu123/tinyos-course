# Lesson 01: 从 GRUB 启动并在 VGA 屏幕显示 TinyOS

> **课程状态：学习版（可编辑，尚未归档）**  
> 目标架构：x86_64 课程的启动第 0 阶段；本课实际接收 GRUB 的 32 位 Multiboot2 交接。  
> 本课完成后，QEMU **图形窗口**直接显示 TinyOS 的三行文字。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：让 GRUB 按 Multiboot2 装载内核，并由教学内核直接写 VGA text buffer，在屏幕上显示第一句 TinyOS 问候。

**固定参考 Linux v6.12：**

- `arch/x86/boot/header.S`：x86 启动镜像和早期启动布局的背景。
- `arch/x86/kernel/head_64.S:119`：早期入口保持中断关闭的受控启动原则。
- `arch/x86/kernel/vmlinux.lds.S`：显式控制内核镜像段布局的链接脚本背景。
- Linux x86 boot video / console 相关源码：真实 Linux 会处理多种显示设备和 framebuffer；本课只选择 legacy VGA text mode 作为最小、可观察的教学设备。

## 第二部分：核心设计解剖（Design Anatomy）

```text
QEMU BIOS
  │
  ▼
GRUB
  │  识别前 32 KiB 内、8 字节对齐的 Multiboot2 header
  ▼
_start (32-bit protected mode)
  │  cli → 16 KiB 临时栈 → kernel_main32()
  ▼
printk() / vga_putc()
  │  每个字符单元：低 8 位 ASCII，高 8 位属性
  ▼
VGA text memory at 0xb8000
  │
  ▼
QEMU 图形窗口
```

`kernel.c` 把字符写入 `0xb8000`。VGA 文本模式的一个单元是 16 位：

```text
bit 15                     bit 8 bit 7                    bit 0
┌──────────────────────────────┬──────────────────────────────┐
│ 属性：0x0f（亮白前景/黑背景） │ ASCII 字符                    │
└──────────────────────────────┴──────────────────────────────┘
```

**分层简化声明**：直接写 `0xb8000` 是教学级 legacy VGA text-mode 路径。现代 Linux 的 runtime console 不依赖这种单一假设，而涉及 framebuffer、DRM/KMS、VT、字体、滚屏和设备管理。本课不复刻它们；目标只是先做出可见的最小 OS。

**架构边界**：课程最终目标是 x86_64，但 Multiboot2 的 GRUB i386 交接在这里仍处于 32 位保护模式。因此本课以 `gcc -m32` / `ld -m elf_i386` 构建。long mode、页表和 64 位内核会在后续课程作为可见功能扩展引入，而不是隐藏在 Hello 之前。

## 第三部分：增量代码交付（Incremental Code Delivery）

这是重启后的第一课，所有文件均从零建立：

```text
lesson-01-learning/
├── Makefile      # freestanding ELF 和 GRUB ISO
├── README.md     # 本课教案
├── boot.S        # Multiboot2 header、32 位入口、临时栈
├── grub.cfg      # GRUB 菜单项
├── kernel.c      # 无 libc 的 VGA printk
└── linker.ld     # 镜像布局与 header 保留
```

核心 VGA 写入：

```c
VGA_TEXT_BUFFER[cursor++] = ((unsigned short)VGA_ATTRIBUTE << 8) |
                            (unsigned char)c;
```

`printk()` 不调用 `printf`；它逐字符调用 `vga_putc()`。本课只支持固定文本和 `\n` 换行；定位、清屏和滚屏将是下一课的最小增量。

## 第四部分：编译与运行验证（Verification）

### Ubuntu/Debian 依赖

```bash
sudo apt-get update
sudo apt-get install build-essential gcc-multilib grub-pc-bin xorriso mtools qemu-system-x86
```

### 构建和协议检查

在本目录执行：

```bash
make clean && make -j$(nproc)
make check
```

### 运行并观看 VGA 窗口

```bash
make run
```

不要加入 `-display none`：本课的成功输出在 **QEMU 图形窗口**，不是串口终端。窗口应显示：

```text
TinyOS lesson 1
Hello from the VGA text console!
Multiboot2 boot succeeded.
```

内核输出后进入 `hlt` 循环。使用 QEMU 窗口关闭按钮或 `Ctrl-a x` 退出。

> **本次实际验证记录（2026-07-31）**：已执行 `make clean && make -j$(nproc)`，`gcc -m32`、`ld -m elf_i386` 和 `grub-mkrescue` 无警告完成。`make check` 输出 `Multiboot2 header check passed.`。随后以 QEMU 正常 VGA 显示启动 ISO，通过 monitor `screendump` 捕获画面；截图为 720×400，逐个核验三行中全部非空字符的 VGA glyph 均已出现。该图形验证没有使用 `-display none`，证明文字来自 VGA 窗口而非串口。内核按设计在输出后进入 `hlt` 循环。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 首先对照的来源 | 基于来源的检查 |
|---|---|---|
| `grub-file` 失败或 GRUB 说没有 header | `arch/x86/boot/header.S` 的启动镜像布局背景 | 检查 `.multiboot` 是否 `ALIGN(8)`、`KEEP()` 是否保留它、header 是否仍在镜像前 32 KiB。 |
| 调用 C 后卡死或重启 | `head_64.S:119` 的受控早期入口原则 | 检查 `_start` 是否先 `cli`，是否先把 `%esp` 指向有效的 `.bss` 临时栈。 |
| QEMU 能启动但窗口无 TinyOS 文本 | VGA 文本单元布局；现代 Linux video stack 的复杂性边界 | 检查写入地址是否为 `0xb8000`，每个 cell 是否同时写字符和属性，运行命令是否错误地加入 `-display none`。 |
| 字符横向错位或颜色异常 | VGA 80×25 文本单元布局 | 检查 cursor 以字符单元为单位递增，属性必须位于高 8 位。 |
| 换行位置不对 | 本课 `vga_putc()` 的行尾计算 | `cursor += 80 - cursor % 80` 只前进到下一行首；本课尚不滚屏。 |
| 想在本课提前进入 long mode | `head_64.S` 的分阶段启动设计 | 保持本课为可见启动最小阶段；long mode 需要 GDT、页表和有序 CR/MSR 状态转换，应独立讲解和验证。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 `linux-v6.12/arch/x86/boot/header.S`，记录一个启动镜像为什么需要受控的早期头部布局。
2. 阅读 `linux-v6.12/arch/x86/kernel/head_64.S` 的入口初段，记录 Linux 为什么在早期启动阶段首先建立可控执行环境。
3. 观察 `vga_putc()`：它只会输出、不会滚屏。下一课将在不改变本课启动链的前提下，实现可定位、清屏、换行和滚屏的 VGA `printk`。

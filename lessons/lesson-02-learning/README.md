# 第二课：把 VGA Hello 扩展为 80×25 文本控制台

> **课程状态：学习版（可编辑，尚未归档）**  
> 目标架构：x86_64 课程的启动第 0 阶段；本课仍接收 GRUB 的 32 位 Multiboot2 交接。  
> 本课完成后，QEMU **图形窗口**会证明清屏、软件定位、换行、自动折行和单行滚屏均可用。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：不改变第一课已经验证的 GRUB → Multiboot2 → 32 位 C 入口链，只将无边界 `vga_putc()` 变成有边界的 80×25 VGA 文本控制台。

**固定参考 Linux v6.12：**

- `drivers/video/console/vgacon.c`：`vga_con_putc()` / `vga_con_putcs()` 分别承担字符输出，`vga_con_clear()` 清除显示区域，`vga_con_scroll()` 移动显示内容。
- `drivers/tty/vt/vt.c`：`gotoxy()` 管理逻辑输出位置，`lf()` 处理换行与到达底部后的动作，`con_scroll()` 协调终端层的滚动请求。
- `drivers/video/console/vgacon.c`：`vga_con_cursor()` 是 Linux 更新硬件 CRTC 光标的真实路径；本课**明确不做**端口 I/O 和硬件光标。
- `arch/x86/boot/header.S`：启动镜像布局背景。
- `arch/x86/kernel/head_64.S:119`：早期入口保持中断关闭的受控启动原则。
- `arch/x86/kernel/vmlinux.lds.S`：显式控制内核镜像段布局的背景。

**简化边界**：Linux 真实控制台还涉及 VT、console driver、字体、多虚拟终端、锁、framebuffer 和 DRM/KMS。本课直接写 legacy VGA text buffer，只保留“字符写入、清除、逻辑定位、换行、滚动”这些可观察语义；这不是现代 Linux console 的复刻。

## 第二部分：核心设计解剖（Design Anatomy）

启动链保持不变：

```text
QEMU BIOS → GRUB → Multiboot2 header → _start (32-bit)
                                      │ cli → 临时栈
                                      ▼
                                kernel_main32()
                                      │
                                      ▼
 vga_clear() → vga_set_cursor() → printk() → vga_putc() / vga_newline()
                                      │                 │
                                      │                 └→ vga_scroll_one_line()
                                      ▼
                         VGA text memory: 0xb8000
                                      │
                                      ▼
                              QEMU 图形窗口
```

一个 VGA cell 是 16 位；低 8 位是 ASCII，高 8 位是属性。本课固定使用 `0x0f`（亮白前景、黑背景）：

```text
bit 15                     bit 8 bit 7                    bit 0
┌──────────────────────────────┬──────────────────────────────┐
│ 属性：0x0f                   │ ASCII 字符                    │
└──────────────────────────────┴──────────────────────────────┘
```

屏幕共有 `80 × 25 = 2000` 个 cell。`cursor` 不是字节地址，而是 `[0, 1999]` 内的 cell 下标：

```text
row 0:  cursor 0      ... cursor 79
row 1:  cursor 80     ... cursor 159
...
row 24: cursor 1920   ... cursor 1999

cursor = row * VGA_COLUMNS + column
```

换行先移至当前行末后的下一 cell；若越过第 24 行，滚屏函数将第 1–24 行复制到第 0–23 行、清空第 24 行，并将 cursor 放回最后一行的第 0 列：

```text
滚动前                         滚动后
row 0   [旧内容 0]              row 0   [旧内容 1]
row 1   [旧内容 1]      ───►    ...
...                              row 23  [旧内容 24]
row 24  [旧内容 24]             row 24  [空白，继续输出]
```

## 第三部分：增量代码交付（Incremental Code Delivery）

从第一课复制而来的文件保持启动和构建方式不变：

```text
lesson-02-learning/
├── Makefile      # 未变：freestanding ELF 和 GRUB ISO
├── boot.S        # 未变：Multiboot2 header、32 位入口、临时栈
├── grub.cfg      # 未变：GRUB multiboot2 菜单项
├── linker.ld     # 未变：镜像布局与 header 保留
├── kernel.c      # 本课唯一功能代码增量
└── README.md     # 本课教案
```

相对于 `lesson-01-learning/kernel.c`，本课新增屏幕边界与职责明确的原语：

```diff
 #define VGA_COLUMNS 80
+#define VGA_ROWS    25
+#define VGA_CELLS   (VGA_COLUMNS * VGA_ROWS)
 
-static void vga_putc(char c) { ...无边界 cursor... }
+vga_make_cell(c)         // 组成属性 + ASCII cell
+vga_clear_row(row)       // 显式循环清空一行
+vga_scroll_one_line()    // 上移 24 行，清空最后一行
+vga_set_cursor(row, col) // 软件位置，约束到 80×25 内
+vga_clear()              // 清空全部 2000 个 cell
+vga_newline()            // 前进到下一行；必要时滚屏
+vga_putc(c)              // 普通字符、列尾折行、底部滚屏
```

关键滚屏代码不用 `memcpy`/`memmove`，而是直接表达每个 cell 的移动方向：

```c
for (cell = 0; cell < VGA_CELLS - VGA_COLUMNS; cell++)
    VGA_TEXT_BUFFER[cell] = VGA_TEXT_BUFFER[cell + VGA_COLUMNS];

vga_clear_row(VGA_ROWS - 1);
cursor = (VGA_ROWS - 1) * VGA_COLUMNS;
```

启动演示按以下顺序输出：先清屏，在 `(0, 0)` 输出标题；再从 `(23, 0)` 写三条换行文本。第二次换行会恰好触发一次滚屏，标题随第 0 行离开屏幕；最后才在 `(2, 10)` 输出 `positioned text`。因此最终可见状态是：

```text
row 0   空白（标题已经被滚出屏幕）
row 2             positioned text
row 22  scroll line A
row 23  scroll line B
row 24  scroll line C
```

这同时证明了清屏、定位、换行和滚屏。`vga_set_cursor()` 只改变软件变量，故 QEMU 不会显示闪烁的硬件插入光标；这是本课有意保留的下一步边界。

## 第四部分：编译与运行验证（Verification）

### Ubuntu/Debian 依赖

```bash
sudo apt-get update
sudo apt-get install build-essential gcc-multilib grub-pc-bin xorriso mtools qemu-system-x86
```

### 构建与 Multiboot2 协议检查

在本目录执行：

```bash
make clean && make -j$(nproc)
make check
```

必须看到：

```text
Multiboot2 header check passed.
```

### 运行并观看 VGA 窗口

```bash
make run
```

不要加入 `-display none`：本课验收对象是 **QEMU 图形窗口**，不是串口。核对如下最终布局：

```text
第 0 行没有 “TinyOS lesson 2”，因为它已在滚动后离开屏幕。
第 2 行第 10 列开始：positioned text
第 22 行：scroll line A
第 23 行：scroll line B
第 24 行：scroll line C
```

内核输出后进入 `hlt` 循环。使用 QEMU 窗口关闭按钮或 `Ctrl-a x` 退出。

> **本次实际验证记录（2026-08-01）**：已执行 `make clean && make -j$(nproc)`；`gcc -m32`、`ld -m elf_i386` 与 `grub-mkrescue` 均无警告完成。`make check` 输出 `Multiboot2 header check passed.`。随后以 QEMU 正常 VGA 显示启动 ISO，并用 monitor `screendump` 捕获 720×400 画面；按 80 列、每 cell 9×16 像素核验：`positioned text` 的全部非空 glyph 位于第 2 行第 10 列开始，`scroll line A/B/C` 分别位于第 22/23/24 行，滚动后的第 0 行为空白。该图形验证未使用 `-display none`，证明清屏、定位和单行滚屏均来自 VGA 窗口。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 首先对照的 Linux / 本课来源 | 基于来源的检查 |
|---|---|---|
| `grub-file` 失败或 GRUB 找不到 header | `arch/x86/boot/header.S`、本课 `linker.ld` | `.multiboot` 必须 `ALIGN(8)`、由 `KEEP()` 保留，并位于镜像前 32 KiB。 |
| 调用 C 后卡死或重启 | `head_64.S:119`、本课 `boot.S` | `_start` 必须先 `cli`，再把 `%esp` 放到有效临时栈。 |
| 文字乱码、颜色不对或横向错位 | `vgacon.c` 的 cell 输出职责、本课 `vga_make_cell()` | 地址必须是 `0xb8000`，一个 cell 必须同时写属性高字节与 ASCII 低字节，cursor 必须按 cell 而非字节递增。 |
| 清屏后仍残留旧字符 | `vgacon.c:vga_con_clear()`、本课 `vga_clear()` | 必须覆盖所有 25 行、每行 80 个 cell；清屏后 cursor 必须回到 0。 |
| 定位超出屏幕后出现异常位置 | `vt.c:gotoxy()`、本课 `vga_set_cursor()` | 行必须限制到 `0..24`，列必须限制到 `0..79`；本课采取 clamp，而不是访问越界 cell。 |
| 行尾第 80 个字符后覆盖下一行或越界 | `vt.c:lf()`、本课 `vga_putc()` / `vga_newline()` | 普通字符写完后也要检查 `cursor >= VGA_CELLS`；`\n` 必须前进到下一行首。 |
| 到最后一行时文字消失或屏幕内容损坏 | `vgacon.c:vga_con_scroll()`、本课 `vga_scroll_one_line()` | 复制方向必须从低 cell 到高 cell读取 `cell + 80`，复制范围只能是前 24 行；最后一行需清空。 |
| 链接出现 `memcpy`、`memset` 或未定义 libc 符号 | 本课的 freestanding 约束 | 不要用 libc；清除、滚动均使用显式 `for` 循环，保持 `-ffreestanding`。 |
| 期望看到硬件文本插入光标却没有 | `vgacon.c:vga_con_cursor()` | 这是有意省略的 CRTC I/O；`vga_set_cursor()` 只维护软件写入位置，不能当作硬件光标编程。 |
| QEMU 图形窗口没有画面 | 第一课 VGA 验收路径 | 检查是否错误加入 `-display none`；本课可视结果只在 QEMU VGA 窗口中验证。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 Linux v6.12 `drivers/video/console/vgacon.c` 的 `vga_con_putc()`、`vga_con_clear()` 与 `vga_con_scroll()`：分别列出它们在真实驱动中还要处理、而本课尚未处理的一个条件。
2. 阅读 `drivers/tty/vt/vt.c` 的 `gotoxy()`、`lf()` 与 `con_scroll()`：说明为什么 Linux 将“终端语义”和“VGA 硬件写入”分在不同层。
3. 阅读 `vgacon.c:vga_con_cursor()`：找出硬件光标与本课 `cursor` 软件变量的差异。
4. 下一课将保持此控制台输出，并加入 **PS/2 键盘轮询与按键回显**；届时文字不再只来自固定字符串，而开始响应用户输入。

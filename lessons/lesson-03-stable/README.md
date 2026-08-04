# Lesson 03: 让 TinyOS 轮询 PS/2 键盘并回显到 VGA

> **课程状态：学习版（可编辑，尚未归档）**  
> 目标架构：x86_64 课程的启动第 0 阶段；本课仍接收 GRUB 的 32 位 Multiboot2 交接。  
> 本课完成后，点击 QEMU **图形窗口**并按键，TinyOS 会把支持的字符立即显示在 VGA 控制台。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：在第二课已经可清屏、定位、换行、自动折行和滚屏的 VGA 控制台上，增加最小 PS/2 键盘轮询与字符回显，让系统第一次响应用户输入。

**固定参考 Linux v6.12：**

- `drivers/input/serio/i8042.c`：`i8042_wait_read()`、`i8042_read_status()`、`i8042_read_data()` 与 `i8042_interrupt()`。Linux 先检查 `I8042_STR_OBF`，再读取控制器数据。
- `drivers/input/keyboard/atkbd.c`：`atkbd_receive_byte()` 与 `atkbd_unxlate_table[128]`。真实 Linux 解析协议、前缀、break 码并转换为 input keycode。
- `drivers/tty/vt/keyboard.c`：`kbd_event()`、`kbd_rawcode()`、`kbd_keycode()` 与 `x86_keycodes[256]`。真实 Linux 将原始扫描码、键码、修饰键状态和终端动作分层。
- `drivers/video/console/vgacon.c`：`vga_con_putc()`、`vga_con_clear()`、`vga_con_scroll()`；回显沿用第二课的字符输出和滚屏语义。
- `drivers/tty/vt/vt.c`：`gotoxy()`、`lf()`、`con_scroll()`；回显的换行与滚屏继续遵循终端层语义。
- `arch/x86/boot/header.S`、`arch/x86/kernel/head_64.S:119`、`arch/x86/kernel/vmlinux.lds.S`：延续第一、二课的启动与镜像布局锚点。

**简化边界**：本课不是 Linux 的 i8042 或 AT keyboard driver。Linux 的 `i8042_interrupt()` 会识别 AUX/mouse 数据、奇偶校验、超时和多个 serio port；`atkbd_receive_byte()` 还维护扫描码协议状态。本课只在 QEMU 默认 PS/2 键盘环境中：检查 output-buffer-full（OBF）位、读取一个 byte、忽略未知码，并把一个很小的 Set-1 make-code 子集直接映射成 ASCII。

## 第二部分：核心设计解剖（Design Anatomy）

```text
用户按下键盘按键
        │
        ▼
QEMU PS/2 keyboard / i8042 controller
        │  状态端口 0x64：bit 0 = OBF（数据是否已到）
        ▼
keyboard_poll_scancode()
        │  OBF = 1 时才读数据端口 0x60
        ▼
Set-1 make code
        │  例如 a = 0x1e，Enter = 0x1c
        ▼
scancode_set1_to_ascii()
        │  仅 a-z、0-9、空格、Enter
        ▼
vga_putc()
        │  使用第二课的换行、自动折行和滚屏
        ▼
VGA text memory 0xb8000 → QEMU 图形窗口
```

端口 I/O 不是普通内存访问。`0x64` 和 `0x60` 只能经 x86 `inb` 指令访问：

```c
__asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port));
```

其对应 Linux `i8042_read_status()` / `i8042_read_data()` 的“从 I/O port 取 byte”行为。若 OBF 为 0 就读取数据端口，读到的不是一条可靠的新键盘事件；因此本课始终先读状态再决定是否读数据。

### 支持和忽略的键

| 分类 | 本课行为 |
|---|---|
| `a-z` | 回显小写字符。 |
| `0-9` | 回显数字字符。 |
| 空格 | 回显一个空格。 |
| Enter | 回显换行，并复用第二课的滚屏。 |
| break code（bit 7 为 1） | 忽略，保证一次按下只输出一次。 |
| Shift、Ctrl、Alt、Caps Lock、Backspace、方向键、功能键、keypad | 忽略；尚未实现修饰键/编辑状态。 |
| `0xe0` 扩展序列、鼠标数据、未知码 | 忽略；尚未实现完整控制器分发与前缀状态机。 |

本课假定 QEMU 提供 conventional translated **Set-1 make code**。不同控制器状态或真实硬件扫描码集不能直接套用这张小映射表。

## 第三部分：增量代码交付（Incremental Code Delivery）

新目录从已冻结的第二课建立：

```text
lesson-03-learning/
├── Makefile      # 未变：freestanding ELF 与 GRUB ISO
├── boot.S        # 未变：Multiboot2、32 位入口、临时栈
├── linker.ld     # 未变：镜像布局
├── grub.cfg      # 仅菜单标题更新为 TinyOS lesson 3
├── kernel.c      # 保留 VGA console，新增键盘轮询和映射
└── README.md     # 本课教案
```

相对于第二课，VGA 控制台原语完全保留；新增输入原语：

```diff
+#define I8042_DATA_PORT   0x60
+#define I8042_STATUS_PORT 0x64
+#define I8042_STATUS_OBF  0x01
+
+inb(port)                         // x86 I/O port read
+keyboard_poll_scancode()          // OBF 为 1 才读 0x60
+scancode_set1_to_ascii(scancode)  // 明确的小型 Set-1 → ASCII 表
+
+for (;;) {
+    scancode = keyboard_poll_scancode();
+    if (scancode == 0 || (scancode & 0x80) != 0)
+        continue;                 // 无数据或 break code
+    character = scancode_set1_to_ascii(scancode);
+    if (character != 0)
+        vga_putc(character);      // 回显到已验证的 VGA console
+}
```

`kernel_main32()` 不返回：启动汇编会在 C 函数返回后 `hlt`，但本课是主动轮询，必须继续检查控制器。轮询会占用一个 CPU；Linux 的生产实现使用 `i8042_interrupt()` 和输入子系统，而不是本课的 busy loop。

## 第四部分：编译与运行验证（Verification）

### Ubuntu/Debian 依赖

```bash
sudo apt-get update
sudo apt-get install build-essential gcc-multilib grub-pc-bin xorriso mtools qemu-system-x86
```

### 构建与协议检查

在本目录执行：

```bash
make clean && make -j$(nproc)
make check
```

必须看到：

```text
Multiboot2 header check passed.
```

### 图形窗口中的人工键盘测试

```bash
make run
```

不要加入 `-display none`：输入和验收都必须发生在 **QEMU 图形窗口**。

1. 点击 QEMU VGA 窗口，使它拥有键盘焦点。
2. 输入 `tinyos 3`，按 Enter。
3. 输入 `keyboard echo`，按 Enter。

预期：两行文字会从提示文本下方开始回显；空格与 Enter 生效。按住一个支持按键时，可能出现重复字符——这代表键盘/QEMU 继续产生 make event，本课尚未实现软件层重复抑制。

> **本次实际验证记录（2026-08-01）**：已执行 `make clean && make -j$(nproc)`；`gcc -m32`、`ld -m elf_i386` 和 `grub-mkrescue` 均无警告完成。`make check` 输出 `Multiboot2 header check passed.`。随后以正常 QEMU VGA 显示启动 ISO（未使用 `-display none`），通过 QEMU monitor 的 `sendkey` 向该图形实例注入 `tinyos 3`、Enter、`keyboard echo`、Enter，并在 monitor `screendump` 捕获 720×400 画面。按 80 列、每 cell 9×16 像素核验：固定标题位于第 0 行，`tinyos 3` 位于第 4 行，`keyboard echo` 位于第 5 行；证明 PS/2 轮询、Set-1 make-code 映射、空格、Enter 和 VGA 回显均已生效。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 首先对照的 Linux / 本课来源 | 基于来源的检查 |
|---|---|---|
| GRUB 找不到内核 header | `arch/x86/boot/header.S`、`linker.ld` | `.multiboot` 必须 8 字节对齐、被 `KEEP()` 保留且位于镜像前 32 KiB。 |
| 标题出现但按键没有回显 | `i8042.c:i8042_interrupt()`、本课 `keyboard_poll_scancode()` | 先点击 QEMU 窗口取得焦点；检查状态端口为 `0x64`、数据端口为 `0x60`，并且仅 OBF 为 1 时读取。 |
| 编译器把端口读写当作普通内存，或代码根本不读设备 | `i8042_read_status()` / `i8042_read_data()` | 端口必须使用 `inb`，不能将 `0x60`/`0x64` 强制转换成指针。 |
| 每个字符输出两次 | `atkbd_receive_byte()` 的 make/break 处理背景 | 检查 `scancode & 0x80` 的 break code 是否先被忽略。 |
| 键盘按下后输出乱码 | `atkbd.c` 的协议和转换层 | 本课仅接受 QEMU translated Set-1 make code；不要将 Set-2 的 `0xf0` break 约定与 Set-1 的 bit 7 break 约定混用。 |
| Shift 后仍是小写、退格或方向键无效 | `keyboard.c:kbd_event()` 的修饰键/键码层 | 这是本课边界；没有 modifier 状态、编辑缓冲或 `0xe0` 前缀状态机。 |
| 移动鼠标后偶发异常字符 | `i8042_interrupt()` 的 AUX/mouse 分发 | 本课没有读取 `I8042_STR_AUXDATA`；避免鼠标交互，未知 byte 会尽量静默忽略。 |
| QEMU CPU 占用较高 | Linux 的 `i8042_interrupt()` | 这是轮询 busy loop 的必然结果；中断、`hlt` 和调度将在后续课程单独建立。 |
| 没有闪烁文本插入光标 | `vgacon.c:vga_con_cursor()` | 本课仍只有软件写入位置，未进行 CRTC 端口编程。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 Linux v6.12 `drivers/input/serio/i8042.c` 的 `i8042_interrupt()`：找出 Linux 在读取一个 byte 前后额外检查的两类状态。
2. 阅读 `drivers/input/keyboard/atkbd.c` 的 `atkbd_receive_byte()`：列出本课未处理的一个前缀或协议状态。
3. 阅读 `drivers/tty/vt/keyboard.c` 的 `kbd_event()` 与 `kbd_keycode()`：说明为何 Linux 不直接将扫描码映射为 ASCII。
4. 下一课会在本课的键盘回显之上增加**最小命令循环**：接收一行有限长度的文本，识别少量固定命令，并在 VGA 上显示回应。

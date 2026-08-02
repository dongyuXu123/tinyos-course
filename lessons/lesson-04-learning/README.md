# 第四课：在 VGA 上构建最小命令循环

> **课程状态：学习版（可编辑，尚未归档）**  
> 本课保留 GRUB Multiboot2 的 32 位保护模式交接、VGA console 和 PS/2 轮询。  
> 完成后 TinyOS 能读取一行输入，执行 `help`、`about`、`clear` 并在 QEMU VGA 窗口回应。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：将第三课“按键即回显”升级为完整的 **键盘 → 行缓冲 → 命令分发 → VGA 回应 → prompt** 回路。

**固定参考 Linux v6.12：**

- `drivers/input/serio/i8042.c`：`i8042_read_status()`、`i8042_read_data()`、`i8042_wait_read()`、`i8042_interrupt()`；只在 OBF 指示数据就绪后读取端口。
- `drivers/input/keyboard/atkbd.c`：`atkbd_receive_byte()`、`atkbd_unxlate_table[128]`；真实 Linux 处理完整键盘协议，本课只处理 QEMU Set-1 小子集。
- `drivers/tty/vt/keyboard.c`：`kbd_event()`、`kbd_keycode()`、`fn_enter()`、`put_queue()`；真实系统将按键、Enter 和终端输入队列分层。
- `drivers/tty/vt/vt.c`：`gotoxy()`、`lf()`、`con_scroll()`；cursor、换行、滚屏的语义对照。
- `drivers/video/console/vgacon.c`：`vga_con_putc()`、`vga_con_clear()`、`vga_con_scroll()`；字符输出、清屏和滚屏职责的对照。

**简化边界**：`command[64]` 不是 Linux TTY line discipline。没有 Unicode、信号、进程、作业控制、权限、历史、参数解析、并发或用户态；它只是零 libc、单核、轮询式教学缓冲区。

## 第二部分：核心设计解剖（Design Anatomy）

```text
QEMU 键盘
  │ Set-1 make code
  ▼
keyboard_poll_scancode() ── OBF=1 才读 0x60
  ▼
scancode_set1_to_ascii() ── a-z / 0-9 / space / Enter / Backspace
  ▼
handle_input_character()
  ├─ 普通字符：append command[]，并回显
  ├─ Backspace：缩短 command[]，擦除最后一个字符
  └─ Enter：execute_command() → reset → 下一条 prompt
                │
                ▼
      help / about / clear / unknown
                │
                ▼
           VGA memory 0xb8000
```

`command` 固定为 64 bytes，最多存 63 个字符，最后一个 byte 永远保留给 `\0`：

```text
[ command[0] ... command[command_length - 1] ][ '\0' ]
0 <= command_length <= 63
```

超过上限的字符被忽略，直到 Backspace 腾出空间或 Enter 提交。命令是精确、无参数、全小写匹配：

| 输入 | 行为 |
|---|---|
| `help` | 输出 `commands: help about clear`。 |
| `about` | 输出 `TinyOS lesson 4: minimal command loop`。 |
| `clear` | 清屏并只显示一条新 prompt。 |
| 空行 | 只显示一条新 prompt。 |
| 其他 | 输出 `unknown command: <输入>`。 |

Backspace 的教学契约很小：仅当 `command_length != 0` 时回退一个 cell 并写空格。它不能删除 `tinyos> `，不支持跨行编辑、箭头键或历史。

## 第三部分：增量代码交付（Incremental Code Delivery）

本课从冻结的 `lesson-03-stable/` 复制：

```text
lesson-04-learning/
├── Makefile      # 未变：freestanding ELF、GRUB ISO、check、run
├── boot.S        # 未变：Multiboot2、32 位入口、临时栈
├── linker.ld     # 未变：镜像布局
├── grub.cfg      # 菜单标题改为 TinyOS lesson 4
├── kernel.c      # 本课：行缓冲、Backspace、命令分发
└── README.md     # 本课教案
```

相对第三课的最小增量：

```diff
+#define COMMAND_MAX 64
+static char command[COMMAND_MAX];
+static unsigned short command_length;
+
+case 0x0e: return '\b';
+
+print_prompt()
+reset_command()
+command_equals()
+vga_backspace()
+execute_command()
+handle_input_character()
```

提交时：

```c
vga_putc('\n');
prompt_printed = execute_command();
reset_command();
if (!prompt_printed)
    print_prompt();
```

`clear` 自己清屏并显示 prompt，返回值阻止公用路径打印第二条 prompt。没有 `printf`、`strcmp`、`memcpy`、`memset`、malloc 或其他 libc。

## 第四部分：编译与运行验证（Verification）

在本目录执行：

```bash
make clean && make -j$(nproc)
make check
make run
```

`make check` 必须输出：

```text
Multiboot2 header check passed.
```

不要加 `-display none`：输入和验收都发生在 **QEMU VGA 图形窗口**。点击窗口获得焦点后输入：

```text
help<Enter>
about<Enter>
cleax<Backspace>r<Enter>
nonsense<Enter>
<Enter>
```

`cleax` 经 Backspace 和 `r` 后成为 `clear`，执行后屏幕应清空并只留下一个 prompt。超过 63 个支持字符应被安全忽略。

可为相同 VGA 配置附加 QEMU monitor，用 `sendkey` 注入确定性输入、用 `screendump` 截图。先发送 `clear`，然后测试 `help`、`about`、`nonsense` 和空行，避免 clear 擦除待验收输出。截图按 80×25、每 cell 9×16 像素核验。

> **本次实际验证记录（2026-08-01）**：已执行 `make clean && make -j$(nproc)`；`gcc -m32`、`ld -m elf_i386` 和 `grub-mkrescue` 均无警告完成，`make check` 输出 `Multiboot2 header check passed.`。随后以正常 QEMU VGA 显示启动 ISO（未使用 `-display none`），通过 monitor `sendkey` 注入 `cleax`、Backspace、`r`、Enter，截图核验清屏后仅第 0 行存在一条 `tinyos> ` prompt。接着注入 `help`、`about`、`nonsense` 和空行并捕获第二张 720×400 VGA 画面；按 80 列、每 cell 9×16 像素核验：第 0/1 行为 help 与命令表，第 2/3 行为 about 与回应，第 4/5 行为 unknown 命令与回应，第 6/7 行各有一条 prompt。这证明 Backspace、clear 单 prompt、命令分发和空行处理均已生效。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 首先对照的来源 | 检查方法 |
|---|---|---|
| 标题出现但按键没响应 | `i8042.c:i8042_interrupt()` | 点击 QEMU 窗口；确认 `0x64` OBF 后才读 `0x60`。 |
| 一个按键输出两次 | `atkbd_receive_byte()` 的 make/break 背景 | 忽略 `scancode & 0x80` 的 break code。 |
| Backspace 擦掉 prompt 或越界 | `handle_input_character()` | 仅 `command_length != 0` 时调用 `vga_backspace()`。 |
| 64 字符后破坏内存 | 固定 buffer 边界 | 只在 `command_length < COMMAND_MAX - 1` 时追加，始终补 `\0`。 |
| `help` 无法识别或 unknown 内容乱码 | `command_equals()` / `reset_command()` | 精确比较到双方 `\0`；每次提交/删除均保持终止符。 |
| `clear` 后有两个 prompt | `execute_command()` 返回契约 | clear 已打印 prompt，必须返回 1。 |
| 跨行退格不正确 | `vt.c:lf()` 与本课边界 | 本课只承诺当前单行命令尾部删除。 |
| Shift、箭头、Tab 或大写无效 | `keyboard.c:kbd_event()` | 有意不实现 modifier、`0xe0`、历史和补全。 |
| CPU 占用高 | `i8042_interrupt()` | 本课是忙轮询；IRQ、`hlt`、PIC/IDT 后续独立建立。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 Linux v6.12 `drivers/tty/vt/keyboard.c` 的 `fn_enter()` 与 `put_queue()`：说明真实 Linux 为何不直接在键盘驱动调用 VGA 写函数。
2. 阅读 `drivers/input/keyboard/atkbd.c:atkbd_receive_byte()`：找出本课尚未处理的一种协议状态。
3. 阅读 `drivers/tty/vt/vt.c:lf()` 与 `drivers/video/console/vgacon.c:vga_con_scroll()`，比较本课 Backspace 的极小范围与完整终端状态。
4. 下一课读取并显示 **Multiboot2 memory map**，让 TinyOS 开始认识可用物理内存。

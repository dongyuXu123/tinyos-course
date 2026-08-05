# Lesson 63: 键盘、PS/2 AUX 鼠标与事件队列

> **Course status: stable snapshot (validated; verified build artifacts included).**

本课把 IRQ1 键盘、IRQ12 PS/2 AUX 鼠标和定时器事件收敛到固定容量队列。重点是三字节 packet 同步、符号位、overflow、坐标裁剪，以及避免轮询和 IRQ 同时读取 `0x60`。

Commands: `inputtest`, `mousetest`, `mouseinfo`，以及 `fonttest`、`canvastest`、`guiinfo` 和 Lesson 60 回归命令。

主要内容：键盘、PS/2 AUX 鼠标与输入事件队列
统一课程编号：Lesson 63

## 责任边界

本课不负责 icon/window hit-test、光标合成或 Terminal 命令。`icontest` 不能替代 QEMU 中的真实鼠标验收。

## Verification

```bash
make -C lessons/lesson-63-learning
make -C lessons/lesson-63-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-63-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在 `tinyos>` 运行 `inputtest`、`mousetest`、`mouseinfo`。真实验收还要确认鼠标能到 `(0,0)`；不要添加不存在的 `-device ps2-mouse`。

## 下一课

Lesson 64 使用这些事件实现窗口、widget、focus 和 hit-test。鼠标排错和模型/物理验收区别见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

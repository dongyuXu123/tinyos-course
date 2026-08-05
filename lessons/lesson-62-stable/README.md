# Lesson 62: backbuffer、像素格式、bitmap font 与 canvas

> **Course status: stable snapshot (validated; verified build artifacts included).**

本课在 Lesson 61 的可靠 framebuffer 上分离绘制目标和真实显存：实现 RGB mask packing、固定 stride 的 backbuffer/scene、5×7 bitmap font、边界裁剪和 canvas 统计。

Commands: `fonttest`, `canvastest`, `guiinfo`, `drawtest`，以及 Lesson 60 回归命令。

主要内容：backbuffer、像素格式、bitmap font 与 canvas
统一课程编号：Lesson 62

## 责任边界

本课不负责键盘/鼠标、窗口命中或 compositor 策略。字体只覆盖有限 ASCII，不是 Unicode 排版系统；文本和矩形都必须有界。

## Verification

```bash
make -C lessons/lesson-62-learning
make -C lessons/lesson-62-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-62-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在 `tinyos>` 运行 `fonttest`、`canvastest`、`guiinfo`。`ready/mapped: 1/1` 且出现 `passed` 才表示真实绘制路径有效。

## 下一课

Lesson 63 将输入设备接入有界事件队列。backbuffer/scene stride、花屏和字体问题见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

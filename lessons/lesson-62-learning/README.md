# Lesson 62: 固定 bitmap 字体、canvas 与基本绘图

> **Course status: learning checkpoint.**

A bounded 5x7 bitmap font, clipped text drawing, canvas colors, and dirty-region accounting build on GUI-01 framebuffer primitives without external font files or dynamic memory.

Commands: `fonttest`, `canvastest`, plus `guiinfo`, `drawtest`, and Lesson 60 regressions.

主要内容：固定 bitmap 字体、canvas 与基本绘图
统一课程编号：Lesson 62

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-62-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

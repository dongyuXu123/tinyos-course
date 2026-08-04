# Lesson 67: 图形桌面综合验证

> **Course status: stable snapshot.**

The complete bounded desktop integrates framebuffer drawing, bitmap text, input events, windows, compositor ownership, graphical shell, and system status metadata. VGA markers and framebuffer summaries remain mandatory validation channels.

Commands: `desktest`, `shellgui`, `inputtest`, `windowtest`, `fonttest`, `canvastest`, `guiinfo`, `drawtest`, and Lesson 60 regressions.

主要内容：图形桌面综合验证
统一课程编号：Lesson 67

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-67-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

# Lesson 64: 窗口、widget 与事件分发

> **Course status: stable snapshot.**

A fixed window and widget table provides bounded z-order, focus, hit testing, and event dispatch. No dynamic allocation or arbitrary application callbacks are used.

Commands: `windowtest`, plus `inputtest`, `fonttest`, `canvastest`, `guiinfo`, `drawtest`, and Lesson 60 regressions.

主要内容：窗口、widget 与事件分发
统一课程编号：Lesson 64

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-64-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

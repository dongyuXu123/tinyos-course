# Lesson 61: Multiboot2 framebuffer 与像素绘制

> **Course status: learning checkpoint.**

The first graphical checkpoint adds a bounded Multiboot2 framebuffer handoff and deterministic pixel/rectangle drawing while retaining VGA text diagnostics. Unsupported formats fall back safely instead of writing unknown memory.

Commands: `guiinfo`, `drawtest`, plus the Lesson 60 process/session regressions.

主要内容：Multiboot2 framebuffer 与像素绘制
统一课程编号：Lesson 61

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-61-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

# Lesson 65: 桌面 compositor 与窗口管理器

> **Course status: learning checkpoint.**

A bounded compositor draws a desktop background, title bars, taskbar, cursor, and fixed windows while preserving framebuffer ownership and dirty-region accounting.

Commands: `desktest`, plus `windowtest`, `inputtest`, `fonttest`, `canvastest`, `guiinfo`, `drawtest`, and Lesson 60 regressions.

主要内容：桌面 compositor 与窗口管理器
统一课程编号：Lesson 65

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-65-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

# Lesson 63: 键盘/鼠标输入事件队列

> **Course status: stable snapshot.**

A bounded input event queue models keyboard, mouse, and timer events without dynamic allocation. PS/2 scan-code conversion and three-byte mouse packet metadata remain deterministic and safely report unavailable devices.

Commands: `inputtest`, plus `fonttest`, `canvastest`, `guiinfo`, `drawtest`, and Lesson 60 regressions.

主要内容：键盘/鼠标输入事件队列
统一课程编号：Lesson 63

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-63-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

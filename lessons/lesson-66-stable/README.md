# Lesson 66: 图形 shell 与系统状态面板

> **Course status: stable snapshot.**

The fixed desktop gains a graphical terminal view and a bounded system status panel linked to init, shell, job, session, memory, pipe, timer, and workqueue metadata without executing arbitrary graphical user code.

Commands: `shellgui`, plus `desktest`, `windowtest`, `inputtest`, `fonttest`, `canvastest`, `guiinfo`, `drawtest`, and Lesson 60 regressions.

主要内容：图形 shell 与系统状态面板
统一课程编号：Lesson 66

## Visible QEMU GUI

From the repository root, launch this stable image with QEMU's standard graphics adapter:

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-66-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

At the `tinyos>` prompt, run `desktest` or `shellgui`. The GUI path requires a Multiboot2 framebuffer; `ready/mapped: 1/1` and a `passed` result confirm that the desktop is drawing rather than using the safe text fallback.

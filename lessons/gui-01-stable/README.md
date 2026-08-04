# GUI-01: Multiboot2 framebuffer and pixel primitives

> **Course status: stable snapshot (validated; verified build artifacts included).**

The first graphical checkpoint adds a bounded Multiboot2 framebuffer handoff and deterministic pixel/rectangle drawing while retaining VGA text diagnostics. Unsupported formats fall back safely instead of writing unknown memory.

Commands: `guiinfo`, `drawtest`, plus the Lesson 60 process/session regressions.

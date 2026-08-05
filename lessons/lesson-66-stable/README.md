# Lesson 66: 图形 Terminal 与安全命令 dispatcher

> **Course status: stable snapshot (validated; verified build artifacts included).**

本课加入有界图形 Terminal、输入/输出缓存、命令回显和安全白名单。字符输入通过 `gui_term_input_dirty` 与 `framebuffer_present_rect()` 局部刷新，避免逐键整屏重绘。

Commands: `shellgui`, `help`, `about`, `clear`, `shellrun`, `guiinfo`, `mouseinfo` 及各类 `*info` 安全诊断命令。

主要内容：图形 Terminal、局部输入刷新与安全命令分发
统一课程编号：Lesson 66

## 责任边界

GUI dispatcher 不直接复用会写 VGA 光标/显存的完整 `exec64()`，也不执行危险异常测试或任意用户输入。Terminal 仍是 bounded metadata 模型，不是完整用户态 Shell。

## Verification

```bash
make -C lessons/lesson-66-learning
make -C lessons/lesson-66-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-66-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在 QEMU 中打开 Terminal，连续输入较长命令，确认字符即时回显；再执行 `help`、`about`、`guiinfo`、`mouseinfo`、`clear`、`shellrun`。

## 下一课

Lesson 67 进行跨层综合验收并结束 GUI 主线。输入性能、坐标布局和 VGA/GUI 后端边界见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

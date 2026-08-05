# Lesson 61: 可靠 framebuffer handoff 与图形输出前置

> **Course status: stable snapshot (validated; canonical learning implementation promoted).**

本课建立 GUI 的硬件前置：GRUB/Multiboot2 framebuffer handoff、严格 tag 校验、低地址与高半区映射、PMM 保留和 VGA fallback。只有先证明 framebuffer 来源可靠，才进入像素绘制。

Commands: `guiinfo`, `drawtest`，以及 Lesson 60 回归命令。

主要内容：图形输出前置与可靠 framebuffer handoff
统一课程编号：Lesson 61

## 责任边界

本课不负责字体、输入、窗口、compositor 或图形 Shell。`ready/mapped: 1/1` 是后续 GUI 的输入条件，不是装饰性状态。

## Verification

```bash
make -C lessons/lesson-61-learning
make -C lessons/lesson-61-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d \
  -cdrom lessons/lesson-61-stable/build/kernel.iso \
  -serial stdio -no-reboot -no-shutdown
```

在 `tinyos>` 运行 `guiinfo`、`drawtest`。确认 `ready/mapped: 1/1` 和 `passed`，否则先排查 handoff，不要修改窗口代码。

## 下一课

Lesson 62 在可靠 framebuffer 上加入像素格式、backbuffer、scene、bitmap font 和 canvas。跨课程排错见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

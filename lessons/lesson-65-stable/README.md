# Lesson 65: scene/compositor 与 Xfce 风格桌面

> **Course status: stable snapshot (validated; verified build artifacts included).**

本课把对象模型绘制成可见桌面：顶部 panel、底部 taskbar、SHELL 图标、Terminal 窗口、scene 快照、cursor 恢复和 dirty 状态。重点是 backbuffer 所有权以及整屏/局部提交的边界。

Commands: `desktest`, `windowtest`, `desktopinfo`，以及 `inputtest`、`fonttest`、`canvastest`、`guiinfo` 和 Lesson 60 回归命令。

主要内容：scene/compositor 与 Xfce 风格桌面
统一课程编号：Lesson 65

## 责任边界

本课不实现完整窗口拖动、resize、硬件 vsync 或用户态 Shell。无 page-flip 时整屏 present 仍可能出现扫描级撕裂；局部 present 只是减少提交范围。

## Verification

```bash
make -C lessons/lesson-65-learning
make -C lessons/lesson-65-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-65-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在 `tinyos>` 运行 `desktest`、`windowtest`、`desktopinfo`，并目视检查顶部、底部和窗口边界没有被截断。

## 下一课

Lesson 66 加入图形 Terminal 和安全 dispatcher。花屏、scene stride、光标和撕裂排错见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

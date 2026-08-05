# Lesson 67: 图形桌面综合验收与 GUI 课程结课

> **Course status: stable snapshot (validated; verified build artifacts included).**

本课统一回归 framebuffer、绘图、输入、鼠标、icon、窗口、scene/compositor 和图形 Terminal。GUI 课程到此结束；Lesson 68 恢复进程组与 session 主线。VGA marker、framebuffer summary 和真实 QEMU 画面仍是强制证据。

Commands: `desktest`, `shellgui`, `help`, `guiinfo`, `mouseinfo`, `iconinfo`, `icontest`, `desktopinfo`, `inputtest`, `windowtest`, `fonttest`, `canvastest`, `drawtest`，以及 Lesson 60 回归命令。

主要内容：图形桌面综合验证与调试回归
统一课程编号：Lesson 67

## 结课验收

`icontest`、`desktest` 等命令只验证确定性模型；不能替代真实 GUI 验收。必须在 QEMU 中检查桌面完整、底部未截断、鼠标能到左上角、SHELL 图标单击/双击行为、Terminal 输入速度和安全命令输出。

## Verification

```bash
make -C lessons/lesson-67-learning
make -C lessons/lesson-67-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-67-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在 `tinyos>` 运行 GUI 回归命令；进入桌面后实测 `help`、`about`、`guiinfo`、`mouseinfo`、`clear`、`shellrun` 和 Shell 图标双击。确认 `ready/mapped: 1/1` 与 `passed`，并保存截图证据。

## 后续课程

Lesson 68 起回到进程组与 session 元数据，不再把 GUI 功能继续堆入后续课。跨课程经验见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

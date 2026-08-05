# Lesson 64: 桌面对象模型与事件分发

> **Course status: stable snapshot (validated; verified build artifacts included).**

本课用固定 window/widget 表实现 hit-test、focus、z-order、事件分发、图标选中和有界双击状态，不引入动态分配或任意应用回调。

Commands: `windowtest`, `iconinfo`, `icontest`，以及 `inputtest`、`fonttest`、`canvastest`、`guiinfo` 和 Lesson 60 回归命令。

主要内容：窗口、widget、图标与事件分发
统一课程编号：Lesson 64

## 责任边界

本课只验证对象模型和状态转换，不证明真实显存提交、光标恢复或物理鼠标。确定性 `icontest` 是模型覆盖，不是 GUI 验收。

## Verification

```bash
make -C lessons/lesson-64-learning
make -C lessons/lesson-64-learning check
```

## Visible QEMU GUI

```bash
qemu-system-x86_64 -accel tcg -vga std -boot order=d -cdrom lessons/lesson-64-stable/build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

在 `tinyos>` 运行 `windowtest`、`iconinfo`、`icontest`；随后在 QEMU 窗口中用真实鼠标检查单击选中和有界双击。

## 下一课

Lesson 65 负责 scene/compositor、panel、taskbar 和 cursor 提交。对象模型误判与窗口残留问题见 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。

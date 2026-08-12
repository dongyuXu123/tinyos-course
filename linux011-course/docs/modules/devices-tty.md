# 模块：设备与 TTY

## 职责

实现块设备、字符设备、键盘、控制台、串口和 TTY 数据路径。

## 主要源码

- `source/kernel/blk_drv/`
- `source/kernel/chr_drv/console.c`
- `source/kernel/chr_drv/keyboard.S`
- `source/kernel/chr_drv/tty_io.c`
- `source/kernel/chr_drv/serial.c`

## 数据流

```text
IRQ/device → driver buffer → tty_io → line discipline → process read/write
```

## 阅读重点

确认设备号如何分派、IRQ handler 如何入队、TTY 如何阻塞/唤醒进程，以及 console 输出如何到达 VGA。

## 只读练习

```bash
grep -RIn 'rs_init\|tty_init\|keyboard_interrupt\|do_tty' source/kernel/chr_drv source/init
```

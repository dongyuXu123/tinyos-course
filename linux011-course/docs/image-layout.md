# Linux 0.11 Image 镜像布局

来源锚点：`source/tools/build.c` 与 `source/boot/bootsect.s`。这是**磁盘镜像中的线性偏移**，不是 boot sector 将代码复制到内存后的运行时地址。

## 线性布局

```text
offset 0
┌──────────────────────────────┐
│ boot sector                  │ 512 bytes
│ boot signature 0xAA55       │
└──────────────────────────────┘
offset 512
┌──────────────────────────────┐
│ setup                         │ 4 × 512 bytes
│ padded to 2048 bytes         │ offsets 512..2559
└──────────────────────────────┘
offset 2560
┌──────────────────────────────┐
│ linked system payload        │ tools/system
│ up to SYS_SIZE × 16 bytes    │
└──────────────────────────────┘
```

## `tools/build.c` 做什么

- 校验 boot 输入的 MINIX header、长度和 `0xAA55`；
- 在 boot buffer 的固定字节写入 root device；
- 写出恰好 512 字节 boot sector；
- 读取 setup，并补齐到 `SETUP_SECTS * 512`；
- 追加 linked system，并限制 system 最大尺寸。

## 与 bootsect 的区别

`tools/build.c` 的 offset 是 Image 内的位置。`boot/bootsect.s` 运行时会把 setup/system 读到由段寄存器和加载逻辑决定的内存位置，然后跳转到 setup；因此不能把 offset 2560 直接当作 `startup_32` 的线性地址。

## 只读练习

```bash
grep -nE 'SETUP_SECTS|SYS_SIZE|0xAA55|0xaa55|root_dev|write' source/tools/build.c
grep -nE 'SETUP_SECTS|load_setup|read_it|go' source/boot/bootsect.s
```

不要在真实磁盘上运行 `make disk` 或任何重定向到设备的命令；本模块只读取源码和已有文件。

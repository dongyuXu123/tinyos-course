# GRUB 源码研读支线

这条支线位于总览课 Lesson 00 和第一个可执行 TinyOS 内核 Lesson 01 之间，编号为 `0.1`–`0.10`。它研究 GNU GRUB 如何读取配置、查找文件、装载 ELF 并生成 Multiboot2 handoff；不复制 GRUB 源码，也不改变 Lesson 01–162 的内核接口。

## 推荐源码版本与获取

课程以 GNU GRUB 2.x 为对象。发行版源码目录和上游源码目录的布局可能不同，优先使用本机发行版对应版本，并记录：

```bash
grub-file --version
grub-mkrescue --version
find /usr/src /usr/share/doc -maxdepth 3 -iname '*grub*' 2>/dev/null
```

如果源码已经放在 `$GRUB_SRC`，使用以下搜索方式适配版本差异：

```bash
cd "$GRUB_SRC"
grep -R "multiboot2" grub-core include util | head -50
grep -R "grub_multiboot" grub-core include | head -50
grep -R "iso9660" grub-core/fs grub-core/disk | head -50
```

课程不自动下载、解压或执行外部源码；学习者应从 GNU 官方渠道取得源码，并自行核对发行版签名或上游发布校验和。

## 源码地图

| 主题 | 首选路径 | 观察重点 |
|---|---|---|
| 公共类型与协议 | `include/grub/` | loader、device、file、video、Multiboot 结构 |
| 核心运行时 | `grub-core/kern/` | 内存、设备、文件、模块和平台抽象 |
| 磁盘与文件系统 | `grub-core/disk/`、`grub-core/fs/` | ISO9660、设备枚举、路径查找 |
| 命令/脚本 | `grub-core/commands/`、`grub-core/normal/`、`grub-core/script/` | 命令注册、配置解析、menuentry 状态 |
| Multiboot/ELF loader | `grub-core/loader/` | header 搜索、ELF PT_LOAD、MBI tags、entry transfer |
| BIOS 平台 | `grub-core/boot/i386/`、`grub-core/kern/i386/pc/` | core image、实模式/保护模式启动 |
| EFI 平台 | `grub-core/loader/efi/`、`grub-core/efi/` | PE/COFF、EFI handoff 与服务边界 |
| 构建工具 | `util/` | `grub-mkimage`、`grub-install`、`grub-mkrescue` |

文件名在不同 GRUB 发行版间可能变化；以 `grep -R` 的符号搜索结果为准，不把单一发行版的路径当作 ABI。

## 观察工具

```bash
# TinyOS 现有 ISO/ELF
readelf -h -l -S -W lessons/lesson-01-stable/build/kernel.elf
readelf -x .multiboot lessons/lesson-01-stable/build/kernel.elf
objdump -h -d lessons/lesson-01-stable/build/kernel.elf | less
grub-file --is-x86-multiboot2 lessons/lesson-01-stable/build/kernel.elf
xorriso -indev lessons/lesson-01-stable/build/kernel.iso -ls
xorriso -indev lessons/lesson-01-stable/build/kernel.iso -report_el_torito plain
```

这些命令只读取课程产物。故障实验应在临时副本中进行，不能覆盖 stable 的 ISO 或源码。

## 证据边界

- Multiboot2 规范定义 header、handoff 和 information tags；GRUB 是实现者；TinyOS 是消费者。
- Intel SDM 解释 CPU 在 `_start`、分页和 long mode 中的行为，不替代 GRUB 源码说明。
- Linux 源码只能作为工程对照，不能当成 GNU GRUB 的实现来源。
- `grub-file` 通过不等于 GRUB 一定能完成运行时装载；应结合 ISO、QEMU 和 TinyOS VGA 输出观察。
- 不执行未知脚本、安装脚本或外部用户指针；只阅读源码和运行明确的工具查询命令。

## 支线关系

```text
Lesson 00 总览
    ↓
0.1 → 0.2 → 0.3 → 0.4 → 0.5 → 0.6 → 0.7 → 0.8 → 0.9 → 0.10
                                                                    ↓
                                                           Lesson 01 可执行内核
```

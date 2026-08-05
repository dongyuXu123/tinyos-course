# GRUB 研读 0.8：GRUB 构建、安装和镜像组成

> **Course status: stable study note.** 这是文档和工具分析课，不生成 TinyOS 内核。

## 学习目标

grub-mkimage、grub-install、grub-mkrescue、prefix、模块列表和 core.img。

## 源码定位

优先阅读共享文档 [`docs/grub-source-study.md`](../../docs/grub-source-study.md)，在本机 `$GRUB_SRC` 中使用：

```bash
grep -R "multiboot2" "$GRUB_SRC/grub-core" "$GRUB_SRC/include" | head -50
readelf -h -l -S -W ../../lessons/lesson-01-stable/build/kernel.elf
grub-file --is-x86-multiboot2 ../../lessons/lesson-01-stable/build/kernel.elf
```

不同发行版的文件名可能不同；使用符号搜索确认路径，不执行未知源码或脚本。

## 观察结果

记录命令输出中与本课相关的源码文件、调用关系、ELF/ISO 字段和 GRUB→TinyOS 边界。所有故障实验必须在临时副本中进行，不能改写 stable 产物。

## 前后关系

上一节点：`0.7`。下一节点：`0.9`。完成本课后进入下一节点；0.10 完成后进入 Lesson 01。

## 安全边界

只阅读 GNU GRUB 源码并执行明确的只读工具查询，不自动下载、安装或执行第三方源码；不把 Linux 源码当作 GRUB 实现；不覆盖课程 ISO、build 或 stable 文件。

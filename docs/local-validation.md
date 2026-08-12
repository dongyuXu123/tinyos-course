# 本地课程验证

仓库现在每节可执行课只发布 `lesson-XX-stable/`；Lesson 00 和新增的 `lesson-0.1-stable`–`lesson-0.10-stable` 是文档型 stable 课程。可执行 stable 目录包含课程源码、Makefile、GRUB/linker 配置和已保存的 `build/` 产物；验证脚本不会依赖仓库外的内核文件。

## 依赖

Ubuntu/Debian：

```bash
sudo apt install build-essential gcc-multilib binutils grub-pc-bin grub-common \
  xorriso mtools qemu-system-x86 python3 socat
```

## 单课验证

```bash
scripts/validate-course.sh 0.1 check  # GRUB 源码研读小节
scripts/validate-course.sh 34 check
scripts/validate-course.sh 162 qemu
```

验证脚本把 stable 课程复制到临时可写目录，然后执行 `make`、`make check`；`qemu` 模式再执行无图形 QEMU 启动冒烟，并扫描 trace 中的 fatal、非法指令和 triple fault。原始 stable `build/` 和 ISO 不会被清理或覆盖。

Lesson 00 只有启动链说明，没有独立内核；它的可执行基线是 Lesson 01 stable。

## Linux 0.11 源码模块

Linux 0.11 源码阅读课程已拆分为**独立仓库** **https://github.com/dongyuXu123/linux011-course**（不属于 TinyOS build/QEMU 验证矩阵）。克隆后在 `linux011-course/` 目录内执行：

```bash
python3 scripts/check-source-study.py
sha256sum -c source.sha256
```

不会默认运行 Linux 0.11 的 Makefile、安装脚本、磁盘写入目标或 QEMU。源码来源、固定 commit、模块总结和启动/加载顺序见其仓库 README。完整源码副本仍保留来源、commit、注释归属和未发现许可证文件的事实；在继续分发前，维护者仍需完成第三方许可证与归属审查。

## GUI 验收

Lesson 61–67 的完整 GUI 验收使用：

```bash
scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui
```

该流程检查 framebuffer、VGA/PPM 证据、图形命令和 QEMU trace。确定性 `desktest` 或模型测试不能替代真实窗口视觉和物理鼠标验收；真实鼠标应在 QEMU 图形窗口中单独测试。

## 全量验证

可按编号串行执行，避免同时启动大量 QEMU：

```bash
for n in $(seq 1 162); do
  scripts/validate-course.sh "$n" check || exit $?
done
for n in $(seq 1 10); do
  scripts/validate-course.sh "0.$n" check || exit $?
done
```

QEMU 启动冒烟可将 `check` 改为 `qemu`，但这会显著增加耗时。稳定版本的历史 build 产物保留用于审计；重新验证在临时副本中完成。

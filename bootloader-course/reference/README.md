# reference：GRUB 2.14 核心文件逐字复刻层

本目录是 Mini-GRUB 课程（B01–B23）的**文本级一致性层**：把 GNU GRUB 2.14
的 26 个核心源码文件**原样逐字节复制**（保留全部版权头与 GPL 声明），与各课
教学实现形成三层一致性对照：

| 层 | 含义 | 位置 |
|---|---|---|
| 契约级 | MBI tag 字节布局 / 引导协议 / 错误消息与 GRUB 一致 | 各课 `make check` 字节级断言 |
| 结构级 | 函数/流程与 GRUB 对应（如 `err_set`↔`grub_error`） | 各课精讲 README「对照」节 |
| **文本级** | 核心源码文件逐字一致 | **本目录 `grub-2.14/`** |

## 来源与版本

- 上游：GNU GRUB **2.14**（与研读支线、实现指南固定的版本一致）；
- 本机副本：`~/grub-src/grub-2.14`（`GRUB_SRC` 可覆盖）；课程红线：**不自动
  下载**，只读对比本机已取得的源码副本；
- 复刻方式：`cp` 原样复制，**不改一个字节**；`grub-2.14/` 镜像上游相对路径，
  可用 `diff -r` 直接对照。

## 文件清单（26 个）

见 [`grub-2.14/SHA256SUMS`](grub-2.14/SHA256SUMS)（sha256 清单）；课程对应
关系见 [`docs/consistency.md`](../docs/consistency.md)。

## 验证命令

```bash
bootloader-course/reference/verify-reference.sh          # 两层校验
GRUB_SRC=/path/to/grub-2.14 reference/verify-reference.sh  # 覆盖源码路径
```

- 第 1 层：复刻文件与仓库内 `SHA256SUMS` 一致（防篡改）；
- 第 2 层：与 `GRUB_SRC` 逐字节一致（`GRUB_SRC` 不存在时跳过并提示，不自动下载）。

## 许可证

GRUB 2.14 为 GPL-3.0 授权。本目录所有文件保留上游版权头与许可证文本，未作
修改；作为教学对照归档，不改变上游署名与许可。重新分发时请遵守 GPL-3.0 条款。

# Linux 0.11 来源与再分发说明

本目录中的 `source/` 来自用户指定的仓库：

```text
https://github.com/karottc/linux-0.11
commit: f8d044e078f5e5ee20a3ad2f72c243f041526983
```

抓取时使用浅克隆并在复制后移除了 `.git/` 元数据；源码文件内容未被教学注释脚本改写。来源仓库 README 说明：

- 项目名称为 Linux-0.11；
- 注释大部分来源于赵炯《Linux-0.11 源码完全注释》；
- 驱动部分注释并不一定完整；
- Linux 0.11 本身不包含现代网络子系统。

来源仓库根目录未发现 `LICENSE`、`COPYING` 或 `NOTICE` 文件。因此本项目不声称拥有 Linux 原始代码或来源仓库注释的版权，也不把这些内容重新许可为 TinyOS 代码。使用者应在继续分发前自行核对 Linux 0.11 原始发行版、karottc 仓库及赵炯注释的适用许可和归属要求；原始 README 已保留在 `source/README.md`。

本项目新增的教学总结和 `docs/` 注释属于本课程新增内容，与 `source/` 中的历史代码及原有注释区分。源码仅用于用户要求的源码学习和本地对照；不执行其中的 Makefile、安装脚本或生成的二进制。

校验命令：

```bash
sha256sum -c source.sha256
python3 scripts/check-source-study.py
```

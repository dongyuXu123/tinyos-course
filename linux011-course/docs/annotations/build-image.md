# 注释：`tools/build.c`

- 源码：`linux011-course/source/tools/build.c`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：把 boot sector、setup 和 linked system 合并成可启动 Image。

镜像布局由源码验证：

```text
0..511       boot sector
512..2559    setup（填充到四个 512 字节扇区）
2560..end    system payload
```

```bash
grep -nE '0xAA55|0xaa55|setup|system|boot' linux011-course/source/tools/build.c
```

这只是读取和解释构建工具；不要在未隔离环境中运行其磁盘写入目标。

# 注释：`boot/bootsect.s`

- 源码：`linux011-course/source/boot/bootsect.s`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：最初 512 字节启动阶段，读取后续 setup/system 并准备跳转。

## 阅读问题

1. 代码假设 BIOS 将什么内容放到哪里？
2. 磁盘读取参数如何决定 setup 和 system 的位置？
3. 最后的签名和镜像布局如何由 `tools/build.c` 配合验证？

```bash
grep -nE 'load|setup|system|0xaa55|0xAA55' linux011-course/source/boot/bootsect.s
```

不要把摘录复制成第二份源码；以固定 commit 的实际文件为准。

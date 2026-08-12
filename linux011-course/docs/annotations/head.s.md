# 注释：`boot/head.s:startup_32`

- 源码：`linux011-course/source/boot/head.s`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：早期保护模式入口，建立内核继续运行所需的地址转换、栈和 C 调用环境。

```bash
grep -nE 'startup_32|setup_paging|L6|main' linux011-course/source/boot/head.s
```

阅读时逐条标注寄存器输入、页目录/页表输出、栈位置和最终 C 入口；不要以现代 Linux `head64.S` 代替本文件。

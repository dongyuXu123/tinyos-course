# 注释：`boot/setup.s`

- 源码：`linux011-course/source/boot/setup.s`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：收集 BIOS 参数并为保护模式阶段准备处理器状态。

重点跟踪参数区、GDT/IDT 相关设置、A20/模式转换以及跳往 `head.s` 的控制流：

```bash
grep -nE 'A20|gdt|idt|protected|head|startup' linux011-course/source/boot/setup.s
```

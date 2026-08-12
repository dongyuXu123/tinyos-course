# 模块：Boot 启动代码

## 职责

把 BIOS 提供的启动入口转换为内核可用的保护模式执行环境，并把控制权交给 C 初始化代码。

## 主要源码

| 文件 | 作用 |
|---|---|
| `source/boot/bootsect.s` | 启动扇区、后续扇区加载和跳转准备 |
| `source/boot/setup.s` | BIOS 参数整理与模式切换准备 |
| `source/boot/head.s` | `startup_32`、描述符、分页、栈和 C 入口 |

## 控制流

```text
BIOS → bootsect.s → setup.s → head.s:startup_32 → init/main.c:main
```

## 不变量

- boot sector 必须适合 512 字节并具有 `0xAA55` 签名；
- setup 和 system 的磁盘偏移必须与 `tools/build.c` 的镜像布局一致；
- `startup_32` 建立栈和页表后才能调用 C；
- 机器状态的具体含义以 x86 手册和本版本源码为准。

## 只读练习

```bash
grep -nE '^(entry|startup_32|setup)' source/boot/*.s
sha256sum -c source.sha256
```

## 与 TinyOS 对照

TinyOS 使用 GRUB Multiboot2 直接交接 ELF 和 MBI；Linux 0.11 使用 boot sector/setup 自己加载后续镜像，二者不能互换启动协议。

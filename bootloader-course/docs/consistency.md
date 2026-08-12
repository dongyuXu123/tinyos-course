# 三层一致性：Mini-GRUB 与 GRUB 2.14 的对齐声明

> **归属**：Lesson B23（终课验收）配套文档；实现细节见
> [`reference/README.md`](../reference/README.md)。

本课程从零复刻 GRUB 2.14 的 i386-pc 核心路径，与上游源码的"一致"分三个层次
明确定义。**用户期望的「最后成品代码应与下载的源码一致」按以下第三层落实**。

## 1. 三层一致性定义

| 层 | 定义 | 达成方式 | 验证 |
|---|---|---|---|
| **L1 契约级** | MBI tag 字节布局、Multiboot2 交接协议、错误消息语义与 GRUB/规范一致 | 各课教学实现逐字段对齐 `include/multiboot2.h` 与 `multiboot_mbi2.c` | 各课 `make check` 字节级断言（如 B21 type-8 tag size=32/type=1） |
| **L2 结构级** | 函数/流程与 GRUB 对应（同名或同义） | 教学实现按源码流程复刻（如 `err_set`↔`grub_error`、`mbi_build`↔`make_mbi`） | 各课精讲 README「对照 GRUB 源码」节 |
| **L3 文本级** | 核心源码文件逐字节一致 | `reference/grub-2.14/` 原样复制 26 个核心文件（含 GPL 头） | `reference/verify-reference.sh` 两层 sha256 校验 |

三层的关系：L1 是**行为等价**（保证 TinyOS 内核可启动），L2 是**教学对应**
（保证能读懂 GRUB 源码），L3 是**逐字归档**（保证"成品代码与源码一致"——
针对核心文件）。

## 2. 核心文件对照表（L3 逐字复刻）

| GRUB 2.14 文件（reference/grub-2.14/ 下） | 课程 | 教学实现对照点 |
|---|---|---|
| `grub-core/boot/i386/pc/boot.S` | B01 | `stage1.S`（512B/0x55AA/INT 10） |
| `grub-core/boot/i386/pc/diskboot.S` | B02 | `stage1.S` INT 13 读 stage2 |
| `grub-core/kern/i386/pc/startup.S` | B03/B05 | `stage2.S`（A20/GDT/CR0.PE；prot_to_real） |
| `grub-core/kern/main.c` | B04 | `loader.c` 初始化序 |
| `grub-core/kern/elf.c` | B06 | `elf_load`（header/phdrs/PT_LOAD） |
| `grub-core/loader/multiboot.c` | B07 | `mb2_header_check`（magic/checksum/对齐） |
| `grub-core/loader/multiboot_elfxx.c` | B08 | `elf_load` PT_LOAD 装载 + bss 清零 |
| `grub-core/commands/boot.c` | B08/B11 | `cmd_boot_fn`（跳 entry） |
| `grub-core/loader/multiboot_mbi2.c` | B09/B21 | `mbi_build`（mmap/fb/end tag） |
| `grub-core/kern/i386/pc/mmap.c` | B10 | `mmap_collect`（E820→type-6） |
| `include/grub/loader.h` | B11 | load/boot 分离状态 |
| `grub-core/fs/iso9660.c` | B13/B14 | `find_in_dir`/`file_open`（含 8.3 截断与 RR 名处理） |
| `grub-core/kern/disk.c` | B14 | `cd_read_lba` 磁盘层 |
| `grub-core/kern/file.c` | B14 | `grub_file` 文件抽象 |
| `grub-core/kern/fs.c` | B14 | fs 层注册 |
| `grub-core/kern/device.c` | B14 | device 层 |
| `grub-core/kern/env.c` | B16 | `env_set`/`env_get` |
| `include/grub/command.h` | B16 | `cmd` 注册表 |
| `grub-core/script/script.c` | B17 | `script_tokenize`/`cmd_execute` |
| `grub-core/commands/menuentry.c` | B18 | menuentry 菜单 |
| `grub-core/kern/dl.c` | B19 | `.mod` 加载 |
| `grub-core/video/i386_pc/vbe.c` | B20 | `vbe_get_info`/`vbe_set_mode` |
| `grub-core/kern/err.c` | B22 | `err_set`/`err_print` |
| `include/grub/err.h` | B22 | 错误码枚举 |
| `include/multiboot2.h` | 契约 | L1 字节布局基准 |

## 3. 已知边界（诚实声明）

- **L3 覆盖核心文件，不含完整源码**：GRUB 2.14 全量源码上万文件（含各平台
  驱动、EFI、字体、翻译等），逐字归档全部源码超出本课程"从零复刻核心路径"
  的目标；`reference/` 只归档与 B01–B22 直接对照的 26 个核心文件；
- **教学实现仍是自写代码**：L3 归档的是上游源码原样副本，不是课程代码的
  改版；课程代码（`lessons/bNN-stable/`）为教学简化实现（无完整错误栈、
  无宏展开通用 loader 等），其与上游的关系由 L1/L2 定义；
- **验证范围**：`verify-reference.sh` 保证 reference 层与上游逐字节一致；
  教学实现与上游的行为一致性由 23 课 QEMU 回归（含启动 TinyOS L01/L05/L61）
  保证。

## 4. 验证命令

```bash
bootloader-course/reference/verify-reference.sh                # L3 逐字校验
bootloader-course/scripts/validate-course.sh all check         # L1/L2 回归（快）
bootloader-course/scripts/validate-course.sh all qemu          # L1/L2 回归（完整）
```

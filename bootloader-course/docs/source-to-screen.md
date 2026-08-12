# Mini-GRUB source-to-screen：从加电到图形桌面的一整条链路

> **归属**：Lesson B23（终课验收）的配套文档。
> **用途**：把 B01–B22 的能力按"加电 → 桌面"的时序串成一条可验证的链路；
> 每阶段标注**对应课程**、**对照 GRUB 2.14 源码**与 **QEMU 验证证据（marker）**。

## 0. 一句话时序

```
加电 → SeaBIOS → El Torito 光盘 → stage1(0x7C00) → stage2(读盘/进保护模式)
→ 实模式回调读 ISO9660 → grub.cfg 脚本 → multiboot2 装载(ELF/header/PT_LOAD)
→ MBI(mmap+fb tags) → 交接 → TinyOS 内核 → 图形桌面
```

## 1. 逐阶段分解

| # | 阶段 | 能力来源 | 对照 GRUB 2.14 | QEMU 验证证据 |
|---|---|---|---|---|
| 1 | **加电**：CPU 实模式、复位向量 | BIOS 固有 | — | SeaBIOS 自检后进入引导设备枚举 |
| 2 | **引导设备枚举 → El Torito**：BIOS 读 PVD/引导目录，加载 2048B boot image 到 0x7C00 | B15 | `util/grub-mkrescue.c`（`-b … -no-emul-boot -boot-load-size 4 -boot-info-table`） | `xorriso -report_el_torito` 含 `boot-info-table`；QEMU `-boot order=d` 从 CD 启动 |
| 3 | **stage1（0x7C00）**：INT 13 读 stage2，INT 10 打印阶段横幅 | B01/B02 | `grub-core/boot/i386/pc/boot.S` + `diskboot.S` | `B01 Mini-GRUB stage1` / `B02 stage2` |
| 4 | **进保护模式**：A20、GDT、CR0.PE，直接写 0xB8000 | B03 | `grub-core/kern/i386/pc/startup.S` | `B03 Mini-GRUB in 32-bit protected mode` |
| 5 | **C 运行时与 VGA 库**：loader 主体转 C | B04 | `grub-core/kern/main.c` 初始化序 | `B04 Mini-GRUB loader_main` |
| 6 | **实模式回调读盘**：prot_to_real/real_to_prot + INT 13（光盘 E0） | B05 | `grub_bios_interrupt` | `signature 55 aa`（软盘签名校验） |
| 7 | **ISO9660 文件系统**：PVD → 目录记录 → extent；长名（8.3 截断与 `-iso-level 3 -relaxed-filenames` 修复，见 B22） | B13/B14 | `grub-core/fs/iso9660.c`（`:805-870` RR 名回退 + 剥 `;` + 转小写） | `B13 iso9660` `root dir extent` `content match`；`B14 file: open /BOOT/KERNEL.ELF` |
| 8 | **grub.cfg 脚本**：tokenizer、变量展开、命令执行、错误继续 | B16/B17/B18 | `include/grub/command.h`、`kern/env.c`、`grub-core/script/`、`commands/menuentry.c` | `grub>` `foo=1`；`B17 script: grub.cfg executing`；菜单 timeout/default（B18） |
| 9 | **ELF 解析与校验**：magic/machine/PT_LOAD | B06 | `grub-core/kern/elf.c` | `B06 elf: entry=` |
| 10 | **Multiboot2 header 校验**：首 32KiB、8 对齐、magic/checksum | B07 | `grub-core/loader/multiboot.c` | `B07 mb2 header ok` |
| 11 | **PT_LOAD 装载 + bss 清零 + 交接** | B08 | `multiboot_elfxx.c` + `commands/boot.c` | `B08 boot: jumping` `B08 test-kernel` |
| 12 | **MBI 构造**：total_size、tag 链、end tag | B09 | `multiboot_mbi2.c` | `B09 walker done`（内核遍历 tag 链） |
| 13 | **内存图**：E820 → type-6 mmap tag | B10 | `grub_machine_mmap_iterate`（INT 15 E820） | `type=0006` |
| 14 | **load/boot 分离**：`grub_loader_set` 思想 | B11 | `grub-core/kern/loader.c` | `B11 error: bad kernel rejected`（坏镜像拒绝装载） |
| 15 | **VBE 图形模式**：INT 10 4F00/4F01/4F02，协商 800x600x32 LFB | B20 | `grub-core/video/i386_pc/vbe.c` | `B20 vbe: VBE controller OK` `mode= 800x600x32`；screendump 像素探针三色锚点 |
| 16 | **type-8 framebuffer tag + graphics request**：读内核 type-5 请求 tag 并设模式，填 fb tag 进 MBI | B21 | `multiboot_mbi2.c`（`retrieve_video_parameters`） | `B21 vbe: mode set` `B21 mmap:` `B21 boot: jumping` |
| 17 | **交接内核**：跳到 entry，传 magic 0x36d76289 + mbi 指针 | B08/B11/B21 | `commands/boot.c` | `B08 test-kernel: magic ok`（自写内核）；`Lesson 61: Multiboot2 framebuffer` + `tinyos>`（TinyOS L61） |
| 18 | **内核图形桌面**：L61 校验 type-8 tag（bpp/pitch/页对齐/上限），映射 LFB，`guiinfo`/`drawtest` | 消费 B21 交接 | TinyOS `lessons/lesson-61-stable` | 主线 GUI 约定：bochs-display + screendump；`tinyos>` 交互 shell |
| 19 | **故障与降级**：坏 ELF/坏 header/缺文件/缺模块 → `error:` + 继续；cfg 缺失 → `rescue>` | B22 | `grub-core/kern/err.c`（`:27/:41/:111`）、`normal/main.c:319` | `B22 error: invalid ELF header` … `rescue>` |
| 20 | **回归验收**：`validate-course.sh all check/qemu` 全课程矩阵 | B23 | 全课程源码汇总 | 23 课 PASS 汇总表（见 `build/acceptance.txt`） |

## 2. 关键交接证据链（B21 → TinyOS L61）

这是本课程**唯一一条"自己写的 GRUB → 主线内核"的完整图形链路**：

```
loader（B21）                                      TinyOS L61 内核
────────────────────                              ──────────────────
读 kernel.elf 的 type-5 graphics request           prepare_memory_map():
  请求 800x600x32        ──────────►              magic==0x36d76289 && mbi 8 对齐
VBE INT 10 4F02 设置模式                           遍历 MBI tag：
填 type-8 fb tag（addr/pitch/宽高/bpp/type=1）        type=6 mmap（entry_size>=24, %8==0）
填 type-6 mmap tag（E820 24 条）                     type=8 fb（bpp==32,type==1,页对齐,
构造 mbi（名称+mmap+fb+end tag）                          pitch>=width*4, bytes<=2MB）
boot 跳到 entry=00100040                              handoff 结构 → long_mode_handoff
```

已知边界（已在 B21 README 5.3 记录）：L61 内核的 `guiinfo ready/mapped`
在 bochs-display 下为 0/0——根因是**内核自身 32/64 位 handoff 结构不一致**
（`kernel.c` 的 `long_mode_handoff` 含 `user_image_*` 16 字节，`kernel64.c`
没有 → 64 位侧读 framebuffer 字段错位）。真 GRUB 同配置下同样失败，故不属于
引导器可达成判据；type-8 tag 的字节级正确性由 B21 `make check` 校验。

## 3. 能力追溯：每项 loader 能力 → 课程 → GRUB 源码

| loader 能力 | 课程 | GRUB 源码锚点 |
|---|---|---|
| 两段式引导 | B02/B15 | `boot.S` / `diskboot.S` / `grub-mkrescue.c` |
| 保护模式切换 | B03 | `kern/i386/pc/startup.S` |
| 实模式回调 | B05 | `grub_bios_interrupt` |
| ELF 装载 | B06/B08 | `kern/elf.c` / `multiboot_elfxx.c` |
| mb2 header 校验 | B07 | `loader/multiboot.c` |
| MBI/tag 构造 | B09/B10/B21 | `multiboot_mbi2.c` |
| 光盘文件系统 | B13/B14 | `fs/iso9660.c` |
| 配置脚本 | B16/B17/B18 | `script/` / `normal/main.c` |
| 模块系统 | B19 | grub-mkimage 模块模型 |
| VBE 图形 | B20 | `video/i386_pc/vbe.c` |
| 错误与 rescue | B22 | `kern/err.c` / `normal/main.c` |

## 4. 验证命令

```bash
# 单课
scripts/validate-course.sh bNN check
scripts/validate-course.sh bNN qemu
# 全课程（终课验收）
scripts/validate-course.sh all check     # 快速结构/build/check 矩阵（~2 分钟）
scripts/validate-course.sh all qemu      # 完整 QEMU 电池（~10 分钟）
```

所有验证在**临时副本**执行（`cp -a` 到 `${TMPDIR}/mini-grub-validate-*`），
绝不改写提交的 stable 产物；`build/` 产物按只读权限提交。

# Mini-GRUB 实现指南（共享参照文档）

本课程从零复刻一个能启动 TinyOS 主线的引导器，参照 GNU GRUB 2.x 源码。本文档固定
版本、给出源码地图与证据边界、约定构建/验证方式；每课 README 不再重复这些内容，
只做增量说明。

## 1. 推荐源码版本与获取

课程以 **GNU GRUB 2.14** 为观察对象（与本机发行版工具一致，实测
`grub-file (GRUB) 2.14-2ubuntu2`）。本课程**不自动下载、解压或执行第三方源码**；
学习者应从 GNU 官方渠道取得源码，并自行核对发行版签名或上游发布校验和。

> **仓库开发基线（2026-08-08）**：GRUB 2.14 源码已下载到本机
> `~/grub-src/grub-2.14`（`https://ftp.gnu.org/gnu/grub/grub-2.14.tar.xz`，
> 7.7 MB），`gpg --verify` 用 GNU GRUB 发布密钥 `BE5C23209ACDDACEB20DB0A28C8189F1988C2166`
> （Daniel Kiper）验证签名完好。B05 起的实现均对照该源码**逐字段/逐流程**落实
> （三层一致性见 [`docs/consistency.md`](consistency.md)：契约级字节布局一致、
> 结构级函数/流程对应；核心文件逐字复刻归档于 `reference/grub-2.14/`）。

```bash
grub-file --version
grub-mkrescue --version
find /usr/src /usr/share/doc -maxdepth 3 -iname '*grub*' 2>/dev/null
```

如果源码已经放在 `$GRUB_SRC`，用符号搜索适配版本差异（不把单一路径当 ABI）：

```bash
cd "$GRUB_SRC"
grep -R "multiboot2" grub-core include util | head -50
grep -R "grub_bios_interrupt" grub-core/kern/i386/pc | head -20
grep -R "grub_multiboot" grub-core/loader | head -50
grep -R "iso9660" grub-core/fs grub-core/disk | head -50
```

## 2. 复刻目标：GRUB 的 i386-pc 核心路径

课程按 GRUB 的启动顺序逐课复刻以下零件（详细源码地图见 TinyOS 主线
`docs/grub-source-study.md`）：

| 顺序 | GRUB 源码/产物 | 本课程对照课 | 职责 |
|---|---|---|---|
| 1 | `grub-core/boot/i386/pc/boot.S`（boot.img，512B 引导扇区） | B01–B02 | 实模式入口、定位并读入 core image |
| 2 | `grub-core/boot/i386/pc/diskboot.S`（core image 第一段） | B02 | 用 INT 13 把 core image 其余部分读入内存 |
| 3 | `grub-core/kern/i386/pc/startup.S`（core 的 PM 切换） | B03–B05 | A20、GDT、CR0.PE、prot_to_real/real_to_prot |
| 4 | `grub-core/kern/main.c`（grub_main 初始化序） | B04 | 设备→文件→模块的初始化顺序 |
| 5 | `grub-core/kern/elf.c`（grub_elf32_open/phdrs） | B06 | ELF header 与 program headers 解析 |
| 6 | `grub-core/loader/multiboot.c`（multiboot2 命令） | B07 | Multiboot2 header 搜索与校验 |
| 7 | `grub-core/loader/multiboot_elfxx.c`（grub_multiboot_load_elf） | B08 | PT_LOAD 段装载、bss 清零 |
| 8 | `grub-core/loader/multiboot_mbi2.c`（make_mbi） | B09–B10 | MBI 与 information tags 生成 |
| 9 | `grub-core/kern/loader.c`（grub_loader_set/boot） | B11 | load 注册与 boot 执行分离 |
| 10 | `grub-core/fs/iso9660.c` + `kern/{device,disk,file}.c` | B13–B14 | ISO9660 与四层文件抽象 |
| 11 | `eltorito.img` / `util/grub-mkrescue.c` | B15 | El Torito 光盘引导与镜像打包 |
| 12 | `include/grub/command.h` + `kern/env.c` | B16 | 命令注册表与环境变量 |
| 13 | `grub-core/script/` + `commands/menuentry.c` + `normal/main.c` | B17–B18 | grub.cfg 解析、菜单与选择 |
| 14 | grub-mkimage 模块模型 | B19 | .mod 模块格式与按需加载 |
| 15 | `grub-core/video/i386_pc/vbe.c` | B20 | VBE framebuffer 模式设置 |
| 16 | `multiboot_mbi2.c` 的 framebuffer tag | B21 | type-8 framebuffer tag 生成 |

## 3. Multiboot2 交接 ABI（本课程必须满足的契约）

TinyOS 主线（Lesson 05 起）对引导器有硬性要求，任何一课的交接都必须满足：

- **Header**：位于镜像前 32768 字节内、8 字节对齐；`magic=0xe85250d6`、
  `architecture=0`（i386）、`length`、`checksum=-(magic+arch+length) mod 2^32`；
  四个 32 位字求和为 0。
- **装载**：按 ELF `PT_LOAD` 段装载（`p_filesz` 从文件读、`p_memsz > p_filesz`
  的尾部清零）；入口取 `e_entry`。
- **交接状态**：32 位保护模式、平坦 GDT、关中断、关分页；
  `EAX = 0x36d76289`，`EBX = MBI 物理地址`（8 字节对齐）。
- **MBI**：`u32 total_size`（16..0x100000）+ tags；end tag `type=0, size=8`；
  type-6 mmap tag（`entry_version=0`、`entry_size>=24` 且 `%8==0`、
  `(size-16)%entry_size==0`），entry `{u64 addr, u64 len, u32 type, u32 reserved}`，
  type 1 = available。
- **Lesson 08+**：MBI 必须在低内存可再读（内核切换 long mode 后按物理地址重走 MBI）。
- **Lesson 61+**：若内核请求 graphics（header type-5 tag），需设置 VBE/LFB 并生成
  type-8 framebuffer tag（`bpp==32`、`type_field==1`、address 4 KiB 对齐、
  `pitch>=width*4`、`pitch*height<=2 MiB`）。缺失时内核降级 VGA 文本（fail-closed）。

TinyOS 从不消费 boot-loader-name（type-2）、cmdline（type-1）、modules（type-3）
等 tag——walk 程序跳过未知类型。课程可在 MBI 中生成 type-2 作为教学演示。

## 4. 证据边界

- **GRUB 是实现者，Multiboot2 规范是契约，TinyOS 是消费者**；本课程复刻的是
  「实现者」一侧。
- Intel SDM 解释 CPU 在实模式/保护模式/分页中的行为；Linux 源码只能作工程对照，
  不能当作 GNU GRUB 的实现来源。
- `grub-file` 通过不等于运行时一定能装载；以 QEMU + VGA 输出为准。
- 只阅读 GRUB 源码、只执行明确的只读工具查询；不执行未知脚本或外部用户指针。
- 故障实验（坏 ELF、坏 header、缺文件）在个人副本/临时目录进行，绝不覆盖
  提交的 stable 产物。

## 5. 构建约定

- 汇编：GNU `as`，AT&T 语法；引导扇区 `.code16`，核心代码 `.code32`。
- C：`gcc -m32 -ffreestanding -fno-pie -fno-stack-protector
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`（与 TinyOS 主线 flags 一致）。
- 链接：`ld -m elf_i386 -nostdlib` + 每课 `linker.ld`；引导扇区链到 `0x7C00`；
  floppy 版 stage2 链到 `0x7E00`，CD 版（B13 起）stage2 链到 `0x8400`
  （= 0x7C00 + 2048，El Torito 整包镜像布局）。
- 产物：`objcopy -O binary` 得到平坦二进制；`dd` 拼装 1.44 MB floppy 镜像
  （80 柱面 / 2 磁头 / 18 扇区，CHS 几何）。
- 每课 `build/` 产物按只读权限提交（与 `lessons/lesson-XX-stable/` 一致）；验证
  脚本一律在临时副本中执行。
- 本课程早期用 floppy/raw disk 启动（`-fda` / `-drive if=floppy`），B13 起用
  自建 El Torito 光盘（`xorriso -b boot.bin -no-emul-boot -boot-load-size N`），
  不使用 `grub-mkrescue` 生成的 GRUB。

## 6. 验证约定

| 层次 | 命令 | 判定 |
|---|---|---|
| 结构 | `scripts/validate-course.sh bNN check` 前置 | 目录/Makefile/README 齐全 |
| build | `make -j$(nproc)` | 无汇编/编译/链接错误 |
| check | `make check` | 扇区大小、0x55AA、符号存在等静态断言 |
| 冒烟 | `scripts/validate-course.sh bNN qemu` | QEMU 启动、trace 无 triple fault |
| VGA 文本 | `scripts/qemu-text-check.sh <dir> <marker>` | `pmemsave 753664 4000` dump 0xB8000 文本，grep 期望字符串 |

`make run` 打开 QEMU 窗口供人工视觉验收；自动化冒烟用 `-display none` + monitor
`pmemsave` 读取 VGA 文本内存（复用 TinyOS 主线 `scripts/qemu-vga-check.sh` 的手法）。

## 7. 与 TinyOS 主线的对接

- 研读支线（0.1–0.10）提供源码地图与观察方法，本课程提供实现；两者共用本指南。
- B12/B21 等 checkpoint 课消费 `lessons/lesson-0X-stable/build/kernel.elf` 等
  冻结产物（只读），不改写 TinyOS 主线任何文件。
- 最终目标：本课程引导器在 QEMU 中替代真 GRUB，启动 TinyOS 主线多课并保持
  VGA 验收基线不变。

## 8. 光盘环境记录（SeaBIOS 1.17.0 / QEMU 10.2.1，B13 实测）

B13 起课程在 CD 上运行。以下结论在开发中经 QEMU + SeaBIOS 1.17.0
（Ubuntu/Debian 打包）实测验证，并对照 SeaBIOS 源码（rel-1.17.0）确认：

- **CD 的 BIOS 盘号从 0xE0 起**（`src/block.h`：`EXTSTART_CD 0xE0`，盘按
  `EXTTYPE_CD` 依次编入 `IDMap`）。El Torito no-emul 引导时 SeaBIOS 把
  `CDEmu.emulated_drive`（= 0xE0 + cdid）放入 DL 传给引导镜像
  （`src/boot.c` `boot_cdrom`）。引导代码直接用入口 DL 读盘即可；
  也可用 INT 13 AH=4B01 查询（`src/disk.c` `cdemu_134b` 把 CDEmu 结构
  复制到 DS:SI，偏移 2 是盘号；GRUB `biosdisk.c` 的做法）。
- **AH=42 对不存在/错误的盘号返回 AH=1**（`DISK_RET_EPARAM`，
  `getDrive` 找不到盘）。若探测到 AH=1，先确认盘号（0xE0 系），
  而不是怀疑 DAP 结构。
- **CD 扇区 = 2048 字节**，AH=42 的 DAP 中 count/lba 均为 2048 字节单位；
  LBA 与 ISO9660 卷空间一致（PVD 在 16，Boot Record 卷描述符在 17）。
- **DAP 必须放在 < 64KB 地址**：`bios_interrupt` 把 `regs.esi`（u32）装入
  16 位 SI（DS=0），故 DAP 物理地址须 < 0x10000；数据缓冲经 DAP 的
  段:偏移描述，可在 1MB 内任意位置（本课程用 0x68000 的 GRUB scratch 区）。
- **16 位实模式位移只有 16 位**：`movb %dl, (0x60000)` 会被静默截断成
  `(0x0000)`——超过 64KB 的绝对地址必须用 ES/DS 段寄存器组合寻址。
- **裸 `objcopy` 不做重定位**：`movw $msg, %si` 在无链接器时汇编成段内
  偏移，运行时会读到 IVT 垃圾。引导代码一律 `ld -Ttext` 链接。
- **串口陷阱**：sputc 用 CX 做等待计数器会破坏调用方 `loop` 计数；
  `serial_init` 会冲掉 DL。调试优先用 VGA 文本输出（`pmemsave 753664 4000`
  抓 0xB8000），QEMU monitor 的 pmemsave 对**绝对路径**文件名解析不稳
  （`/` 被当作表达式除法），用相对文件名最可靠。
- **ISO9660 根目录的 "." / ".." 条目名字是单字节 0x00 / 0x01**（ECMA-119），
  不是 `'.'`/`'..'`；GRUB `iso9660.c` 在 iterate 时映射回字面量。

## 9. 模块加载区与符号重定位经验（B19 实测）

- **模块加载区避开核心/缓冲/内核**：核心 0x8400、CD 缓冲 0x68000–0x6C000、
  内核 1MB 起，模块固定 `MODULE_BASE = 0x200000`（2MB）最稳妥；加载前校验
  模块文件 ≤ `MOD_FILE_MAX`（本课 4KB）与节总数（≤16，固定 `sec_addr[16]`）。
- **模块编译必须 `-fno-pie`**：否则 gcc 会为 32 位 PIC 生成 GOT/PLT 类重定位
  （`R_386_GOT32` 等），加载器只实现 `R_386_32`/`R_386_PC32` 会直接报
  `unsupported relocation type`。`-Os` 下同理保证指令内嵌绝对地址。
- **`-Os` 内联吞掉符号**：`mod_load`/`core_sym_lookup`/`name_eq` 等静态
  函数被内联进调用者后 `objdump -t` 找不到符号，`make check` 断言失败——
  用 `static __attribute__((noinline))` 保护教学关键符号。属性必须放在
  `static` 之后、返回类型之前；写成分号结尾的声明再接函数体会产生
  "attributes should be specified before the declarator" 或孤立 `{` 错误。
- **重定位是就地加和**：`*loc += sym_addr`（R_386_32）与
  `*loc += sym_addr - (u32)loc`（R_386_PC32）读的是模块文件里的原始 addend
  （gcc 对 PC 相对调用会填 -4），不能先清零再加。
- **ET_REL 校验用 `LC_ALL=C`**：`readelf -h` 输出随 locale 变化（中文系统
  `Type:` 变成 `类型:`），make check 里 `LC_ALL=C readelf` 保证 awk 匹配。
- **模块与核心的共享声明**：模块文件里重复声明核心的 `struct cmd`/
  `struct grub_file`——GRUB 用共享头文件解决，本课教学简化；改核心布局时
  模块会静默错位，B19 思考题给出了校验方向。

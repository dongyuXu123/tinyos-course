# Lesson 0.1: GRUB 源码树与启动产物 — 精讲文档

> **课号**：Lesson 0.1（GRUB 源码研读支线第 1 课，文档观察课，不生成内核）
> **主题**：GNU GRUB 源码树布局，以及「能启动 TinyOS 的镜像」到底由哪些零件拼成
> **课程主线位置**：第 1 阶段支线；位于 [`Lesson 00`](../lesson-00-stable/README.md) 之后、
> Lesson 01 之前（0.1 → 0.2 → … → 0.10 → [`Lesson 01`](../lesson-01-stable/README.md)）
> **前置课程**：Lesson 00（启动链总览、Multiboot2 i386 交接、只读工具）
> **后续课程**：[`lesson-0.2-stable/README.md`](../lesson-0.2-stable/README.md)（grub.cfg 配置语言与命令分发）
> **一句话目标**：合上源码树地图后，能说出「哪个源码目录产出镜像的哪个部分」，并用只读命令从
> `kernel.elf` / `kernel.iso` 中读出这些产物的真实证据。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能画出「GRUB 源码区域 → 二进制产物 → ISO 上文件」的三层对应表，
并独立复现本文给出的全部只读观察命令。

- **在课程主线中的位置**：这是 0.1–0.10 支线的第一课。Lesson 00 给了启动链全景，但没有回答
  「GRUB 自己是怎么被拼出来的」。本课建立后续 9 课共用的一套公共坐标：源码树在哪、产物是什么、
  如何只读观察。后续课程的承接关系见 [`Lesson 00`](../lesson-00-stable/README.md) 的支线示意图。
- **前置知识清单**：
  1. Lesson 00 的启动链分层模型（BIOS → GRUB → Multiboot2 → TinyOS），能复述各层责任；
  2. 会认 `readelf`、`xorriso`、`grub-file` 的基本输出（Lesson 00 已演示）；
  3. 知道课程约定：`$GRUB_SRC` 指本机发行版 GRUB 源码目录，由学习者自行取得，课程不下载源码
     （见 [`docs/grub-source-study.md`](../../docs/grub-source-study.md)）。
- **本课交付**：一张「源码目录 → 产物」对照表 + 一组可直接复制的只读观察命令与实测输出记录。

---

## 2. 核心概念精讲

### 2.1 概念一：GRUB 源码树的五大区域

GNU GRUB 2.x 的源码树（本课以 GRUB 2.14 为观察对象，本机实测 `2.14-2ubuntu2`）可粗分为五区：

| 区域 | 典型目录 | 职责 | 对应产物/作用 |
|---|---|---|---|
| 公共头 | `include/grub/` | 类型、磁盘、文件、loader、Multiboot 结构定义 | 编译期契约，`multiboot2.h` 定义 tag 结构 |
| 核心运行时 | `grub-core/kern/` | 启动初始化、内存、设备、文件、模块、分区 | 打入 core image |
| 驱动与子系统 | `grub-core/{disk,fs,commands,normal,script,loader,term,video}/` | 磁盘驱动、文件系统、命令、菜单、脚本、装载器 | 以 `.mod` 模块形式动态加载 |
| 平台代码 | `grub-core/boot/i386/`、`grub-core/kern/i386/pc/`、`grub-core/efi/` | BIOS 启动段、i386-pc 初始化、EFI 服务 | 决定 core image 的启动形态 |
| 主机工具 | `util/` | `grub-mkimage`、`grub-mkrescue`、`grub-install`、`grub-file` | 在开发机运行，拼装上述产物 |

**为什么这样分层？** 设备、文件系统、命令各有大量实现，若全部焊死在 core image 里镜像会
巨大且无法适配硬件。GRUB 把最小内核打进 core image，其余能力做成 `.mod` 模块启动后再按需
`insmod`——与 Linux「内核 + 模块」思想同源（见第 7 节）。

### 2.2 概念二：一份可启动镜像的三段式

以本课观察对象（Lesson 01 的 `kernel.iso`）为例，光盘启动链上的 GRUB 部分由三层零件组成：

```text
boot.img / eltorito.img（启动扇区代码）
   └─ core image（GRUB 最小内核 + 预置模块）
        └─ /boot/grub/i386-pc/*.mod（按需加载的模块）
```

`grub-mkrescue` 生成的 ISO 里，`eltorito.img` 承载「光盘第一段」，BIOS 按 El Torito 记录先执行
它，再由它把 GRUB 核心与模块带起来；TinyOS 的 `kernel.elf` 只是 ISO 上的普通文件，由 GRUB
读入内存后按 Multiboot2 协议启动。

### 2.3 概念三：i386-pc 与 x86_64-efi 两套产物

同一份 `kernel.iso` 上同时存在两套 GRUB 产物（本课实测 El Torito 记录有两条）：BIOS 路径用
`i386-pc`（实模式启动段 + core.img），UEFI 路径用 `x86_64-efi`（PE/COFF 的 `grub.efi`，打包在
`/efi.img` 中）。课程 QEMU 实验走 BIOS 路径（`-boot order=d`），UEFI 细节留给 Lesson 0.7。

### 2.4 概念四：文档课的观察方法

本课（以及 0.2–0.10）不复制、不编译 GRUB 源码，只做三件事：读源码定位关键符号、对 Lesson 01
冻结产物跑**只读**工具、把真实输出与源码地图对应起来（证据边界见
[`docs/grub-source-study.md`](../../docs/grub-source-study.md)）。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（文件与符号）

在 `$GRUB_SRC` 中先跑一遍定位命令，把「文档说的目录」换成「本机实际文件」：

```bash
cd "$GRUB_SRC"
grep -R "multiboot2" grub-core include util | head -50      # loader 与头文件
grep -R "grub_register_command" grub-core/commands | head -20 # 命令注册点
grep -R "grub_multiboot" grub-core/loader | head -50          # Multiboot 装载器
grep -R "iso9660" grub-core/fs grub-core/disk | head -50      # 文件系统驱动
```

**预期输出解读**：第一条应同时命中 `include/grub/multiboot2.h`（tag 结构）与
`grub-core/loader/`（生成 MBI 的代码）；第三条是 0.4/0.6 两课的主战场；`iso9660` 命中
`grub-core/fs/iso9660.c`（0.3 课对象）。发行版改名时以符号搜索结果为准，不把单一路径当 ABI。

### 3.2 观察一：工具版本（建立观察基线）

```bash
grub-file --version
grub-mkrescue --version
xorriso --version | head -1
readelf --version | head -1
```

本机实测（2026-08-06）：

```text
grub-file (GRUB) 2.14-2ubuntu2
grub-mkrescue (GRUB) 2.14-2ubuntu2
xorriso 1.5.6 : RockRidge filesystem manipulator, libburnia project.
GNU readelf (GNU Binutils for Ubuntu) 2.46
```

**解读**：GRUB 工具 2.14 对应源码 2.14 分支；记录版本是为了让后续观察输出可复现——不同 GRUB
版本生成的 MBI tag 集合可能不同（0.6 课细讲）。

### 3.3 观察二：kernel.elf 身份

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
file "$K"
readelf -h -W "$K"
```

实测输出（节选）：

```text
kernel.elf: ELF 32-bit LSB executable, Intel i386, ... statically linked, not stripped
类别: ELF32; 系统架构: Intel 80386; 入口点地址: 0x100020
Number of program headers: 3; Number of section headers: 9
```

**解读**：ELF32 + i386 说明 TinyOS 第一课按 Multiboot2 的 i386 交接约定构建（`-m32`）；
入口 `0x100020` = 物理 1 MiB + 0x20，即 `.multiboot`（0x18=24 字节）之后 `.text` 的
`_start`，与 `linker.ld` 的 `. = 1M` 完全一致。

### 3.4 观察三：段表与程序头（源码产物对应）

```bash
readelf -S -W "$K"
readelf -l -W "$K"
```

实测输出（节选）：

```text
[ 1] .multiboot PROGBITS 00100000 001000 000018 00  A  0   0  8
[ 2] .text      PROGBITS 00100020 001020 0000e9 00  AX  0   0  1
[ 3] .rodata    PROGBITS 00100110 001110 000052 00  A  0   0  4
[ 4] .bss       NOBITS   00101000 002000 004002 00  WA  0   0 16
LOAD  0x001000 0x00100000 0x00100000 0x00162 0x00162 R E 0x1000
LOAD  0x001000 0x00101000 0x00101000 0x00000 0x04002 RW  0x1000
GNU_STACK ... RW
```

**解读**：`.multiboot` 位于文件首 4 KiB 内、地址 1 MiB、对齐 8——满足 Multiboot2
「前 32768 字节内 + 8 字节对齐」；两个 LOAD 段把只读（R E）与可写（RW，`0x101000` 起
`.bss`）分开，没有 RWX 段——这是 `linker.ld` 里 `. = ALIGN(CONSTANT(MAXPAGESIZE))` 的功劳。

### 3.5 观察四：ISO 的 El Torito 启动记录

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -report_el_torito plain
```

实测输出（节选）：

```text
Volume id    : 'ISOIMAGE'
El Torito boot img :   1  BIOS  y   none  0x0000  0x00      4        3040
El Torito boot img :   2  UEFI  y   none  0x0000  0x00   5760          74
El Torito img path :   1  /boot/grub/i386-pc/eltorito.img
El Torito img opts :   1  boot-info-table grub2-boot-info
El Torito img path :   2  /efi.img
```

**解读**：这条 ISO 同时携带 BIOS 与 UEFI 两条启动记录；第 1 条 `Ldsiz 4` 扇区 = 2048 字节的
引导扇区（`grub2-boot-info` 会把启动信息表写入镜像），第 2 条 5760 扇区是 UEFI 用的
`/efi.img`。QEMU 的 SeaBIOS 正是按第 1 条记录找到 GRUB 的。

### 3.6 观察五：ISO 文件树里的 GRUB 零件

```bash
xorriso -indev "$ISO" -find /boot -exec lsdl --
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -E '\.mod' | wc -l
```

实测关键文件（节选）：

```text
/boot/kernel.elf                            5352   ← TinyOS 内核本体
/boot/grub/grub.cfg                          102   ← 菜单配置（0.2 课对象）
/boot/grub/i386-pc/eltorito.img            31723   ← BIOS 启动镜像
/boot/grub/i386-pc/normal.mod             115532   ← 菜单/交互模块
/boot/grub/i386-pc/multiboot2.mod          15972   ← multiboot2 命令模块
/boot/grub/i386-pc/multiboot.mod           14908   ← multiboot(1) 命令模块
/boot/grub/i386-pc/iso9660.mod              9436   ← ISO9660 文件系统驱动
/boot/grub/i386-pc/biosdisk.mod             4540   ← BIOS 磁盘访问
/efi.img                                  2949120   ← UEFI 启动镜像
/boot.catalog                               2048
```

实测：`i386-pc` 目录共 **297 个 `.mod` 模块**——「核心 + 模块」策略的直接证据。

---

## 4. 数据流与运行逻辑

本课的「数据流」是产物拼装链，不是内核执行链：

```text
grub-mkimage（util/） ──► core.img / grub.efi（i386-pc / x86_64-efi）
grub-mkrescue（util/） ──► ISO9660 布局 + El Torito 记录（BIOS/UEFI 两条）
                              ├─ /boot/grub/grub.cfg
                              ├─ /boot/grub/i386-pc/*.mod（297 个）
                              └─ /boot/kernel.elf（TinyOS，由 0.2 课命令装载）
QEMU: SeaBIOS 按 El Torito → 执行 eltorito.img → 加载 core image → 读模块与配置
```

---

## 5. 观察与验证

### 5.1 依赖

`binutils`（readelf/objdump）、`grub-common`（grub-file）、`xorriso`、`file`，读源码需自备
`$GRUB_SRC`；不涉及安装脚本。

### 5.2 复现命令清单

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
ISO="lessons/lesson-01-stable/build/kernel.iso"
file "$K" "$ISO"                    # ELF 32-bit i386 / ISO 9660 bootable
readelf -h -S -l -W "$K"            # entry 0x100020; .multiboot; 两个 LOAD 段
grub-file --is-x86-multiboot2 "$K"; echo $?   # 期望 0
xorriso -indev "$ISO" -report_el_torito plain # BIOS + UEFI 两条记录
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -c '\.mod'  # 297
```

### 5.3 实测记录（2026-08-06，全部只读）

`file` 识别 ELF32/i386 与 ISO 9660 bootable；`grub-file` 退出码 0；El Torito 两条记录
（BIOS LBA 3040 / UEFI LBA 74）；`i386-pc` 297 个模块；`/boot/kernel.elf` 5352 字节。

### 5.4 安全边界（本课红线）

只阅读 GNU GRUB 源码、只执行明确的只读工具查询；不自动下载/解压/执行第三方源码；
不把 Linux 源码当作 GRUB 实现来源；故障实验必须在临时副本中进行，绝不覆盖 stable 目录。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `grub-file` 命令不存在 | 缺 `grub-common`/`grub-pc-bin` 工具包 | 检查 `which grub-file`，安装对应发行版包 |
| `$GRUB_SRC` 为空、`grep` 无输出 | 源码未取得或路径不对 | 用 `find /usr/src /usr/share/doc -maxdepth 3 -iname '*grub*'` 找线索；课程不代下载 |
| `readelf` 找不到 `.multiboot` 节 | 用错了 ELF（如非 stable 产物） | 只观察 `lesson-01-stable/build/kernel.elf`；临时副本实验 |
| `xorriso -report_el_torito` 只有一条记录 | 旧版 grub-mkrescue 或手工 ISO | 对照 GRUB 2.14 实测输出；UEFI 记录缺失不影响 QEMU BIOS 路径 |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/arch/x86/kernel/vmlinux.lds.S`：Linux 用链接脚本显式控制内核镜像布局——
  TinyOS 的 `linker.ld` 与 GRUB 的模块分区思想都与之同源；
- `linux-v6.12/init/main.c` 的内核初始化顺序：与 `grub-core/kern/main.c` 的 `grub_main`
  初始化顺序（先设备后文件、先核心后模块）在工程结构上类似。

**边界提醒**：GRUB 是实现者、Multiboot2 规范是契约、Linux 只能作工程对照（依据
[`docs/grub-source-study.md`](../../docs/grub-source-study.md) 的证据边界）。

---

## 8. 思考题与练习

1. 概念理解：为什么 GRUB 不把 297 个模块全部焊进 core image？把这种「核心 + 模块」策略
   与 Lesson 01 的 `Makefile` 对比，TinyOS 目前是「静态链接」还是「模块化」？
2. 源码定位：在 `$GRUB_SRC` 中运行 `grep -R "grub_multiboot" grub-core/loader`，
   记录命中的文件，说出哪个文件很可能负责「生成 Multiboot2 information tags」。
3. 动手观察：运行 `readelf -l -W` 确认两个 LOAD 段一个只读可执行、一个可写，
   解释为什么 `linker.ld` 里 `. = ALIGN(CONSTANT(MAXPAGESIZE))` 会导致这一结果。
4. Linux 对照：读 `linux-v6.12/arch/x86/kernel/vmlinux.lds.S`，找出与 TinyOS
   `linker.ld` 中 `KEEP(*(.multiboot))` 作用相似的写法。
5. 综合：把 3.3–3.6 的四个观察输出整理成「源码区域 → 产物 → 证据」三列表格，
   标注每个证据在 `kernel.iso` 或 `kernel.elf` 中的位置。

---

## 9. 本课小结与下一课预告

**小结**：GRUB 源码树按「公共头 / 核心运行时 / 驱动子系统 / 平台代码 / 主机工具」五区组织，
`util/` 的 `grub-mkimage` 与 `grub-mkrescue` 把最小内核、模块和 TinyOS 内核拼成同时支持 BIOS
与 UEFI 的可启动 ISO；本课用只读命令拿到了入口点 `0x100020`、`.multiboot` 节、两条 El Torito
记录、297 个模块等硬证据，为后续 9 课建立了共同观察坐标。

**下一课预告**：进入 [`lesson-0.2-stable`](../lesson-0.2-stable/README.md)，精讲
`/boot/grub/grub.cfg` 的三行配置如何变成 GRUB 的一条菜单与两条命令
（`menuentry` / `multiboot2` / `boot`），以及 `normal`、`script`、`commands` 在命令分发链中的分工。

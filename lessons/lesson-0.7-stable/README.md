# Lesson 0.7: BIOS/legacy 与 UEFI 平台分支 — 精讲文档

> **课号**：Lesson 0.7（GRUB 源码研读支线第 7 课，文档观察课，不生成内核）
> **主题**：同一张 ISO 上并存的两套 GRUB：i386-pc（BIOS/legacy，实模式启动段 + INT 13/15）
> 与 x86_64-efi（UEFI，PE/COFF + EFI 服务）
> **课程主线位置**：第 1 阶段支线；0.6 讲完 MBI，本课解释「tag 为什么因平台而异」
> **前置课程**：[`lesson-0.6-stable/README.md`](../lesson-0.6-stable/README.md)
> （GRUB 生成 Multiboot information tags）
> **后续课程**：[`lesson-0.8-stable/README.md`](../lesson-0.8-stable/README.md)
> （GRUB 构建、安装和镜像组成）
> **一句话目标**：说清 BIOS 与 UEFI 两条启动路径各自的固件接口、core image 形态与
> MBI 差异，并指出课程实验走哪条、为什么。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能对比 i386-pc 与 x86_64-efi 在「固件怎么把 GRUB 拉起来」、
「GRUB 用什么服务访问硬件」、「MBI 多出哪些 tag」三方面的不同。

- **在课程主线中的位置**：前几课讲的装载/校验/ MBI 与平台无关；本课第一次出现「平台分支」，
  是 0.8（grub-mkimage 按平台拼 core image）与 0.10（端到端）的必要背景。
- **前置知识清单**：
  1. [`lesson-0.1-stable`](../lesson-0.1-stable/README.md) 的两套产物概念
     （`i386-pc` / `x86_64-efi`、`eltorito.img` / `efi.img`）；
  2. [`lesson-0.6-stable`](../lesson-0.6-stable/README.md) 的 MBI tag 类型表（BIOS 与 UEFI 的 tag 集合不同）；
  3. 实模式/保护模式概念（Lesson 00）。
- **本课交付**：两条启动路径的时序对比图 + El Torito 实测证据 + 平台差异清单。

---

## 2. 核心概念精讲

### 2.1 概念一：i386-pc（BIOS/legacy）路径

传统 BIOS 启动光盘的时序：

```text
BIOS（SeaBIOS）按 El Torito 启动记录读 eltorito.img（1 个扇区的引导段）
  → grub-core/boot/i386/pc/boot.S：实模式小代码，定位并装入 core.img
  → grub-core/boot/i386/pc/startup_raw.S + kern/i386/pc/startup.S
      └─ 实模式 → 保护模式切换（GDT、A20、CR0.PE）
  → grub_main 初始化 → normal 读配置（0.2 课）
```

BIOS 路径的硬件访问全部走**固件中断**：`INT 13` 读磁盘（`biosdisk.mod`）、`INT 15 E820`
探测内存（0.6 课 type-6 mmap 的数据源）、VGA 文本/图形。GRUB 只依赖「BIOS 已初始化硬件」，
自己不驱动芯片组。

### 2.2 概念二：x86_64-efi（UEFI）路径

UEFI 完全不同：

```text
UEFI 固件从 ESP（或 efi.img 的 FAT 镜像）读 EFI/BOOT/BOOTX64.EFI
  → 该文件是 PE/COFF 格式的 grub.efi（x86_64-efi 的 core image）
  → GRUB 作为 UEFI 应用程序运行：用 EFI Boot Services/System Table 访问硬件
  → 交接前调用 ExitBootServices，把 EFI 信息放进 MBI（0.6 的 tag 11/12、17…）
```

关键差异：UEFI 路径**没有实模式**、**没有 INT 中断**，一切走 EFI 协议（句柄 + 协议接口），
磁盘、内存、时钟都是 `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` 之类的协议对象。

### 2.3 概念三：平台分支如何影响 MBI

| tag | BIOS（i386-pc） | UEFI（x86_64-efi） |
|---|---|---|
| type 6 mmap | 来自 `INT 15 E820` | 来自 `EFI Memory Map` |
| type 5 boot device | 生成 | 一般不生成 |
| type 11/12 EFI 系统表 | 无 | 生成（32/64 位指针） |
| type 17 EFI memory map | 无 | 生成 |
| type 18 boot services | 无 | 若未终止则生成 |

所以 0.6 说的「tag 集合因平台而异」在 UEFI 侧体现为一批 EFI 专属 tag。

### 2.4 概念四：课程为何走 BIOS 路径

`Makefile` 的 `make run` 用 `qemu-system-x86_64 -accel tcg -boot order=d -cdrom kernel.iso`：
QEMU 默认固件是 SeaBIOS（`/usr/share/seabios/bios.bin`），按 El Torito 第 1 条记录
（BIOS）启动。好处：调试链短、可观察（串口、VGA）、与 Multiboot2 的 i386 交接直接对应
TinyOS 第一课。UEFI 路径在课程里是「对照物」，不作为主线。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（平台目录）

```bash
ls /usr/lib/grub/          # 本机平台目录：i386-pc、x86_64-efi、x86_64-efi-signed
cd "$GRUB_SRC"
ls grub-core/boot/i386/pc/  # boot.S、startup_raw.S ...
ls grub-core/kern/i386/pc/  # init.c、startup.S ...
ls grub-core/efi/ grub-core/kern/efi/ grub-core/loader/efi/   # UEFI 侧
```

本机实测（2026-08-06）：`/usr/lib/grub/` 含 `i386-pc`、`x86_64-efi`、
`x86_64-efi-signed`——发行版同时提供两套平台，正对应 ISO 上两套模块目录。

### 3.2 观察一：El Torito 双记录（平台分叉的入口证据）

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -report_el_torito plain
```

实测（节选）：

```text
El Torito boot img :   1  BIOS  y   none  0x0000  0x00      4        3040
El Torito boot img :   2  UEFI  y   none  0x0000  0x00   5760          74
El Torito img path :   1  /boot/grub/i386-pc/eltorito.img
El Torito img path :   2  /efi.img
```

**解读**：BIOS 记录只有 4 扇区（`eltorito.img`，纯引导段），UEFI 记录 5760 扇区
（`efi.img`，是含 EFI/BOOT/BOOTX64.EFI 的 FAT 镜像）。**固件选哪条记录，就决定了 GRUB
走哪个平台分支**——这是最直观的平台分叉点。

### 3.3 观察二：两套模块目录并存

```bash
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -E 'normal|multiboot2|biosdisk|eltorito'
xorriso -indev "$ISO" -find /boot/grub/x86_64-efi -exec lsdl -- | grep -E 'normal|multiboot2'
```

实测（2026-08-06）：

```text
i386-pc/normal.mod       115532
i386-pc/multiboot2.mod    15972
i386-pc/biosdisk.mod       4540
i386-pc/eltorito.img      31723

x86_64-efi/normal.mod    180592
x86_64-efi/multiboot2.mod 25096
```

**解读**：同名模块两套，且大小不同——因为目标格式不同（i386-pc 是 32 位 ELF 模块，
x86_64-efi 是 64 位模块）。`biosdisk.mod` 只有 i386-pc 有，UEFI 用 EFI 协议不需要它；
`eltorito.img` 也只有 BIOS 侧有。**模块目录名即平台名**，这是「平台 = 模块集合」的最强证据。

### 3.4 观察三：grub-mkimage 支持的格式清单

```bash
grub-mkimage --help | grep -A3 'format'
```

实测（节选，GRUB 2.14）：可用格式含 `i386-pc`、`i386-pc-eltorito`、`i386-efi`、
`x86_64-efi`、`arm64-efi`、`riscv64-efi` 等。**解读**：`-O` 参数决定产出 core image 的
形态——`i386-pc-eltorito` 正是 `eltorito.img` 的来源，`x86_64-efi` 产出 PE/COFF 的
`grub.efi`。0.8 课会细讲这张表。

### 3.5 观察四：QEMU 固件侧（只读）

```bash
ls -l /usr/share/seabios/bios.bin 2>/dev/null   # 默认固件路径
```

**解读**：QEMU `-boot order=d` 时由 SeaBIOS 做传统 BIOS 启动；想走 UEFI 需另给
`-bios`/OVMF 固件并配合 `efi.img`。课程主线锁定 BIOS 分支，OVMF 只在延伸实验中出现。

---

## 4. 数据流与运行逻辑

```text
BIOS 分支（课程主线）：
  SeaBIOS → El Torito#1 → eltorito.img(4 扇区)
    → boot.S 装入 core.img → 实模式→保护模式
    → grub_main → normal → grub.cfg → multiboot2/boot
    → MBI（mmap 来自 INT15 E820）→ jmp _start

UEFI 分支（对照）：
  UEFI 固件 → efi.img 的 FAT → EFI/BOOT/BOOTX64.EFI
    → PE/COFF grub.efi 运行 → EFI 协议访问硬件
    → 同样读 grub.cfg → multiboot2/boot
    → MBI 含 EFI tags（11/12、17…）→ jmp _start
```

两条路径在 `grub_main` 之后几乎汇合——平台差异集中在「固件接口」与「MBI 附加 tag」。

---

## 5. 观察与验证

### 5.1 依赖

`xorriso`、`grub-mkimage`（仅 `--help`/`--version` 查询）、`$GRUB_SRC`。

### 5.2 复现命令清单

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -report_el_torito plain          # BIOS(4扇区)/UEFI(5760扇区)
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -E 'normal|multiboot2'
xorriso -indev "$ISO" -find /boot/grub/x86_64-efi -exec lsdl -- | grep -E 'normal|multiboot2'
grub-mkimage --help | grep -E 'i386-pc-eltorito|x86_64-efi'
ls /usr/lib/grub/
```

### 5.3 实测记录（2026-08-06，全部只读）

El Torito 双记录：BIOS LBA 3040 / UEFI LBA 74；`i386-pc/normal.mod` 115532 与
`x86_64-efi/normal.mod` 180592；`biosdisk.mod` 仅 i386-pc；`/usr/lib/grub/` 含 i386-pc 与
x86_64-efi 两套平台目录。

### 5.4 安全边界（本课红线）

只读查询固件与模块文件；不启动 QEMU/不改 BIOS；`grub-mkimage` 只用 `--help` 不生成镜像；
延伸实验（OVMF）需在个人副本环境准备。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| QEMU 显示「No bootable device」 | El Torito 记录损坏或 BIOS 未按 CD 启动 | 检查 `-cdrom` 与 `-boot order=d`；`xorriso -report_el_torito` |
| 走 BIOS 却加载 UEFI 模块 | 模块目录名与固件不匹配 | 检查 `prefix` 与 `/boot/grub/<平台>/` 目录 |
| UEFI 记录存在但 BIOS 机不启动 | 固件只认自己平台的记录 | 记住 El Torito#1 是 BIOS、#2 是 UEFI |
| `biosdisk.mod` 缺失导致读盘失败 | 模块集裁剪过度 | 检查 `i386-pc/` 清单（0.1 观察五） |
| 想对比 UEFI 行为 | 需要 OVMF 固件 | 用 `-bios`/`-drive` 指定 OVMF，另建个人副本 |
| MBI 缺 EFI tags | 走的是 BIOS 分支 | 平台分支决定 tag 集合（0.6 表） |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/arch/x86/boot/` 的 BIOS 启动路径与 `linux-v6.12/drivers/firmware/efi/`：
  Linux 同样要同时支持 legacy（实模式启动）与 UEFI（EFI stub），平台分叉思想一致；
- `linux-v6.12/arch/x86/kernel/e820.c`：BIOS 侧内存图来源，与 GRUB type-6 mmap 同源。

**边界提醒**：Intel SDM 定义 CPU 与实/保护模式，QEMU 的 SeaBIOS 是固件，GRUB 是 bootloader，
UEFI 规范定义 EFI 接口——本课事实分层引用，不混用。

---

## 8. 思考题与练习

1. 概念理解：BIOS 路径为什么需要 `INT 13`/`INT 15`，而 UEFI 不需要？
2. 源码定位：在 `$GRUB_SRC/grub-core/boot/i386/pc/` 里找 boot.S，指出它用什么手段把
   core.img 读入内存。
3. 动手观察：对比 `i386-pc/normal.mod` 与 `x86_64-efi/normal.mod` 的大小差异，
   说明为什么 64 位模块更大。
4. 实验（延伸）：在个人环境用 OVMF 启动 `kernel.iso`，观察 UEFI 分支的启动画面与
   MBI 差异（需要自备 OVMF）。
5. 综合：画出两条路径从「固件读取」到「grub_main」的对比表，标出课程主线走哪条。

---

## 9. 本课小结与下一课预告

**小结**：BIOS（i386-pc）与 UEFI（x86_64-efi）是 GRUB 的两套平台实现：前者靠实模式引导段 +
INT 中断，后者靠 PE/COFF 的 grub.efi + EFI 协议；一张 ISO 用两条 El Torito 记录承载二者，
模块目录名即平台名；平台分支决定了 MBI 的 tag 集合。课程主线走 BIOS 分支。

**下一课预告**：进入 [`lesson-0.8-stable`](../lesson-0.8-stable/README.md)，这些 core image、
eltorito.img、efi.img 到底是怎么拼出来的：`grub-mkimage` 与 `grub-mkrescue` 的职责、
prefix 与模块列表、以及 Makefile 里那一条 `grub-mkrescue -o kernel.iso`。

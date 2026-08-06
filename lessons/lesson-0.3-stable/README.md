# Lesson 0.3: GRUB 设备、文件系统和路径解析 — 精讲文档

> **课号**：Lesson 0.3（GRUB 源码研读支线第 3 课，文档观察课，不生成内核）
> **主题**：`multiboot2 /boot/kernel.elf` 里的路径是怎么被 GRUB 一步步找到的
> **课程主线位置**：第 1 阶段支线；0.2 讲完「命令分发」，本课回答「命令拿到的路径如何变成文件」
> **前置课程**：[`lesson-0.2-stable/README.md`](../lesson-0.2-stable/README.md)
> （grub.cfg 与命令分发）
> **后续课程**：[`lesson-0.4-stable/README.md`](../lesson-0.4-stable/README.md)
> （GRUB ELF 装载器路径）
> **一句话目标**：讲清设备（`(cd0)`）→ 磁盘 → 分区 → 文件系统（ISO9660）→ 文件 五层抽象
> 的查找流程，并用 ISO 观察命令验证 `/boot/kernel.elf` 真实存在。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能解释 GRUB 为什么把「路径」拆成「设备 + 文件系统 + 文件」三层，
并说出 `multiboot2 /boot/kernel.elf` 打开文件时经过的每个抽象层。

- **在课程主线中的位置**：0.2 的 `multiboot2` 命令要工作，第一步就是打开文件；本课解决
  「文件从哪来」。它是 0.4（ELF 解析读的就是这个文件句柄）的直接前置。
- **前置知识清单**：
  1. [`lesson-0.1-stable`](../lesson-0.1-stable/README.md) 的源码树地图
     （`grub-core/kern/device.c|disk.c|file.c`、`grub-core/fs/iso9660.c`）；
  2. [`lesson-0.2-stable`](../lesson-0.2-stable/README.md) 的命令分发机制（命令处理器拿到 argv）；
  3. 会用 `xorriso` 列 ISO 文件树、用 `grep -R` 搜源码。
- **本课交付**：五层抽象的职责表 + `grub_file_open` 调用链 + 用 ISO 实测证明
  `/boot/kernel.elf`（5352 字节）与 `/boot/grub/grub.cfg`（102 字节）可被找到。

---

## 2. 核心概念精讲

### 2.1 概念一：GRUB 的设备命名

GRUB 用括号语法描述设备：`(cd0)` 是第 0 个光驱，`(hd0,msdos1)` 是第 0 块磁盘的第 1 个
MSDOS 分区。设备名在 `grub-core/kern/device.c`（`grub_device_open`）中被解析：

```text
(hd0,msdos1)
 │   └── 分区名（grub-core/kern/partition.c 解析分区表）
 └── 磁盘名（grub-core/kern/disk.c 枚举 BIOS 磁盘）
```

TinyOS 的 `grub.cfg` 里写的是**裸路径** `/boot/kernel.elf`（没有 `(cd0)` 前缀），
GRUB 会把它补在当前 `root` 环境变量所指的设备上。El Torito 光盘启动时 root 就是光驱，所以
裸路径等价于 `(cd0)/boot/kernel.elf`。

### 2.2 概念二：五层抽象栈

GRUB 把「读一个文件」拆成五层，每一层只依赖下一层：

```text
grub_file（字节流，含偏移/长度）         ← grub-core/kern/file.c
  ↑ 文件系统驱动：把「目录项+内容」变文件   ← grub-core/fs/iso9660.c
  ↑ 分区层：把磁盘切成逻辑块区间            ← grub-core/kern/partition.c
  ↑ 磁盘层：按扇区读写                     ← grub-core/kern/disk.c
  ↑ 设备层：名字 → 磁盘+分区               ← grub-core/kern/device.c
```

**为什么五层？** 因为「同一份 ISO 在不同机器上可能是光驱、USB 或硬盘」——把硬件访问（磁盘层）
与格式解析（文件系统层）分开，新增一种磁盘或文件系统都无需动其他层。

### 2.3 概念三：文件系统驱动 = 一张函数表

`grub-core/fs/iso9660.c` 实现一个 `struct grub_fs`，核心是 `open` / `read` / `dir` / `close`：
ISO9660 目录记录里有文件名、起始 LBA、长度，驱动据此返回字节流。GRUB 对每个文件系统都注册
这样一个结构体，打开文件时按设备/磁盘类型找到对应驱动。ISO 上那份 `iso9660.mod`（9436 字节）
就是这个驱动的模块形态。

### 2.4 概念四：Rock Ridge 与路径可读性

`grub-mkrescue` 用的 xorriso 会在 ISO 上写 Rock Ridge 扩展（`rr`），让长文件名、目录、
符号链接信息完整保存。这解释了为什么 ISO 里有 `/boot/grub/i386-pc/multiboot2.mod` 这样的长
路径——没有 Rock Ridge，纯 ISO9660 level 1 只允许 8.3 短名。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（device / disk / partition / fs / file）

```bash
cd "$GRUB_SRC"
grep -R "grub_file_open" grub-core/kern grub-core/commands | head -10
grep -R "grub_disk_open" grub-core/kern | head -10
grep -R "struct grub_fs" include/grub | head -10
grep -R "iso9660" grub-core/fs | head -20
```

**预期输出解读**：`grub_file_open` 是命令侧入口（`multiboot2` 处理器调用它打开文件）；
`struct grub_fs` 定义在 `include/grub/fs.h`，`iso9660` 命中 `grub-core/fs/iso9660.c`。
若发行版把 ISO9660 拆成多文件（如 `iso9660.h`），同样以 grep 结果为准。

### 3.2 观察一：ISO 路径查找目标真实存在

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --
xorriso -indev "$ISO" -find /boot -exec lsdl --
```

实测输出（节选，2026-08-06）：

```text
-r-xr-xr-x ... 5352 Jul 31 23:48 '/boot/kernel.elf'
-r--r--r-- ...  102 Jul 31 23:48 '/boot/grub/grub.cfg'
dr-xr-xr-x ... '/boot'          （目录项）
dr-xr-xr-x ... '/boot/grub'
dr-xr-xr-x ... '/boot/grub/i386-pc'
```

**解读**：这是 GRUB 打开文件时要走完的目录链：`/boot` → `/boot/grub` → `/boot/kernel.elf`。
每个目录项都对应 ISO9660 目录记录；`multiboot2` 处理器最终拿到的就是 `kernel.elf` 的
5352 字节流。`-find` 的成功结果等价于「ISO9660 驱动在该路径上可解析」，是路径解析的只读预演。

### 3.3 观察二：磁盘/文件系统的扇区证据

```bash
xorriso -indev "$ISO" -report_el_torito plain   # 已有：LBA 3040 等
xorriso -indev "$ISO" -find /boot.catalog -exec lsdl --
```

实测：`/boot.catalog` 2048 字节（1 扇区），位于 El Torito 目录；`eltorito.img` LBA 3040
（4 扇区）。**解读**：ISO9660 的世界观就是「逻辑块地址（LBA）→ 扇区」；GRUB 的 `grub_disk_read`
按扇区读写，ISO9660 驱动把这些扇区解释成目录与文件内容。`xorriso` 报告里的 LBA 正是磁盘层
的观察窗口。

### 3.4 观察三：模块清单里的「驱动」证据

```bash
xorriso -indev "$ISO" -find /boot/grub/i386-pc -exec lsdl -- | grep -E 'iso9660|biosdisk|cd'
```

实测：`iso9660.mod` 9436 字节、`biosdisk.mod` 4540 字节。**解读**：光盘路径需要 `biosdisk`
（BIOS INT 13 磁盘访问）+ `iso9660`（文件系统）+ `normal`（脚本/命令）三个模块协同；
缺任何一个，`multiboot2 /boot/kernel.elf` 都会在「打开文件」这步失败。

### 3.5 观察四：TinyOS 侧路径如何被写进 grub.cfg

对照 0.2 的 grub.cfg：

```text
multiboot2 /boot/kernel.elf
```

`/boot/kernel.elf` 是一个**逻辑路径**：它不关心文件在物理盘的哪个扇区，只描述「文件系统里的
相对位置」。`Makefile` 里 `cp build/kernel.elf $(ISO_ROOT)/boot/kernel.elf` 负责把产物放对位置，
`grub-mkrescue` 负责把整棵目录树写进 ISO。**TinyOS 工程师的职责**：保证逻辑路径与 ISO 布局
一致；**GRUB 的职责**：用五层抽象把逻辑路径翻译成扇区。

---

## 4. 数据流与运行逻辑

`multiboot2 /boot/kernel.elf` 的执行瞬间：

```text
loader/multiboot.c 处理器（argv[1] = "/boot/kernel.elf"）
  → grub_file_open("/boot/kernel.elf", ...)
      → 解析路径：无设备前缀 → 用 root 设备（光驱）
      → grub_device_open("cd0")：名字 → 磁盘
      → grub_disk_open：打开 cdrom 磁盘
      → 找到 iso9660 驱动（fs 表查找）
      → fs->dir 逐级 / → boot → kernel.elf（读目录记录：LBA、长度 5352）
      → 建立 grub_file：偏移 0、长度 5352
  → multiboot2 处理器接着做 header 校验与 ELF 段装载（0.4/0.5 课）
```

GRUB 命令行里用 `ls (cd0)/` 或 `cat (cd0)/boot/grub/grub.cfg` 可以手动复现同一解析路径
（0.9 课 rescue 模式会用到）。

---

## 5. 观察与验证

### 5.1 依赖

`xorriso`；源码阅读需 `$GRUB_SRC`。

### 5.2 复现命令清单

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/kernel.elf -exec lsdl --        # 5352 字节
xorriso -indev "$ISO" -find /boot/grub/grub.cfg -exec lsdl --     # 102 字节
xorriso -indev "$ISO" -report_el_torito plain                     # LBA/扇区证据
cd "$GRUB_SRC" && grep -R "grub_file_open" grub-core/kern/file.c  # 文件层入口
```

### 5.3 实测记录（2026-08-06，全部只读）

`/boot/kernel.elf` 5352 字节、`/boot/grub/grub.cfg` 102 字节均可被 `xorriso -find` 定位；
`/boot` 目录链完整；`eltorito.img` 位于 LBA 3040；`iso9660.mod` / `biosdisk.mod` 在
`i386-pc` 模块集中存在。

### 5.4 安全边界（本课红线）

只读观察 ISO；`grep -R` 只读源码；不执行 `grub-mount`/`mount` 等挂载写入操作；故障实验在
个人副本进行。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `error: file not found`（`multiboot2 /boot/kernel.elf`） | 路径与 ISO 布局不一致 | `xorriso -find /boot` 核对真实路径 |
| 裸路径打开失败但 `(cd0)/boot/kernel.elf` 成功 | root 设备不是光驱 | 在个人副本 grub.cfg 里显式 `set root=(cd0)` |
| 能列目录但读文件报错 | ISO9660 驱动异常/扇区损坏 | 用 `xorriso -toc`、`-check_media` 查 ISO 完整性 |
| 缺 `iso9660.mod` 启动即失败 | 模块集不含文件系统驱动 | 检查 `i386-pc/` 模块清单（0.1 观察五） |
| Rock Ridge 失效、文件名被截断 | 构建时未启用 rr 扩展 | 用 `xorriso -report_rockridge` 检查 |
| 想模拟「坏路径」 | 改路径做故障实验 | 只改个人副本 grub.cfg 重建 ISO |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/fs/iso9660/`（`inode.c`、`dir.c`）：Linux 把 ISO9660 做成 VFS 文件系统，
  GRUB 的 `struct grub_fs` 与 Linux `struct file_operations` 是同一设计模式（函数表抽象）；
- `linux-v6.12/block/` 的块设备层与 `grub_disk_open`/`grub_disk_read`：都是「按块读写」的
  硬件抽象；
- 差异：Linux 有页缓存、inode 等复杂状态，GRUB 的文件系统层只做「够启动」的最小实现。

**边界提醒**：Linux 的 VFS 不是 GRUB 文件系统层的实现来源，只是同构对照。

---

## 8. 思考题与练习

1. 概念理解：为什么 `multiboot2 /boot/kernel.elf` 里不写 `(cd0)` 也能找到文件？root 设备
   是怎么定的？
2. 源码定位：在 `$GRUB_SRC` 里找 `struct grub_fs` 的定义与 `iso9660` 的注册，
   记录 `open/read/dir` 三个回调各自的作用。
3. 动手观察：运行 `xorriso -indev kernel.iso -find / -exec lsdl --`，挑一个模块文件，用
   `xorriso` 输出推断它在 ISO 上的逻辑块位置。
4. 实验（个人副本）：把 grub.cfg 里路径改成 `/boot/does-not-exist.elf`，重建 ISO 启动，
   记录 GRUB 报错文本，对照第 6 节调试地图。
5. Linux 对照：比较 GRUB `grub_fs` 与 Linux `file_operations` 提供的操作集合，列出至少
   两处设计差异。

---

## 9. 本课小结与下一课预告

**小结**：GRUB 用「设备→磁盘→分区→文件系统→文件」五层抽象把 `/boot/kernel.elf` 翻译成扇区；
裸路径自动落到 root 设备（光驱）；ISO9660 驱动（模块 `iso9660.mod`）负责目录与文件内容的
解析；`xorriso -find` 的实测结果证明了路径链上每个节点都存在。

**下一课预告**：进入 [`lesson-0.4-stable`](../lesson-0.4-stable/README.md)，文件句柄拿到手之后，
看 `multiboot2` 如何按 ELF program header（`PT_LOAD`）把段装进内存、记下入口点 `0x100020`，
为 `boot` 交接做准备。

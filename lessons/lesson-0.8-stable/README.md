# Lesson 0.8: GRUB 构建、安装和镜像组成 — 精讲文档

> **课号**：Lesson 0.8（GRUB 源码研读支线第 8 课，文档观察课，不生成内核）
> **主题**：`grub-mkimage` / `grub-mkrescue` / `grub-install` 各自拼装什么，
> 以及 Lesson 01 Makefile 里那一条 `grub-mkrescue -o kernel.iso` 到底做了什么
> **课程主线位置**：第 1 阶段支线；0.7 讲完平台分支，本课讲「产物是怎么造出来的」
> **前置课程**：[`lesson-0.7-stable/README.md`](../lesson-0.7-stable/README.md)
> （BIOS/legacy 与 UEFI 平台分支）
> **后续课程**：[`lesson-0.9-stable/README.md`](../lesson-0.9-stable/README.md)
> （GRUB 故障与调试实验）
> **一句话目标**：把 `make` 产物链（boot.o → kernel.elf → iso 目录 → kernel.iso）中
> GRUB 工具参与的部分逐行讲透。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能说出 `grub-mkimage`（造 core image）、`grub-mkrescue`
（造可启动 ISO）、`grub-install`（装到设备）三者的分工，并解释 `prefix` 与模块列表的作用。

- **在课程主线中的位置**：0.1 的「源码目录 → 产物」表里的 `util/` 一行，本课展开。
  0.10 的端到端检查点会复用这里的产物组成知识。
- **前置知识清单**：
  1. 0.1 的 ISO 产物清单（eltorito.img、efi.img、297 个 .mod）；
  2. 0.7 的两套平台（`i386-pc-eltorito` 与 `x86_64-efi` 格式）；
  3. Lesson 01 的 [`Makefile`](../../lessons/lesson-01-stable/Makefile)（能读 target 依赖）。
- **本课交付**：三工具职责对比表 + Makefile 逐 target 解读 + prefix/模块目录实测证据。

---

## 2. 核心概念精讲

### 2.1 概念一：grub-mkimage——拼装 core image

`grub-mkimage`（`util/grub-mkimage.c`）把「GRUB 最小内核 + 指定模块」合成一个平台镜像：

```text
grub-mkimage -O <格式> -o <输出> -p <prefix> [模块...]
  格式：i386-pc（core.img）| i386-pc-eltorito（eltorito.img）| x86_64-efi（grub.efi）…
```

常用选项（本机 `grub-mkimage --help` 实测，GRUB 2.14）：

| 选项 | 含义 | 课程相关 |
|---|---|---|
| `-O, --format` | 输出格式（平台） | `i386-pc-eltorito`、`x86_64-efi` |
| `-o, --output` | 输出文件 | core image 文件名 |
| `-p, --prefix` | 运行时查找模块/配置的目录 | 默认 `/boot/grub` |
| `-c, --config` | 内嵌早期配置 | 一般不用于课程 |
| `-d, --directory` | 从哪读模块 | 默认 `/usr/lib/grub/<平台>` |
| `-C, --compression` | 压缩算法 | 影响 core image 大小 |

**为什么需要这个工具？** core image 必须「自举」：在还没有文件系统驱动时就能把自己和模块
从磁盘上拉起来。`grub-mkimage` 把启动段、内核与首批模块按平台布局焊成一个镜像。

### 2.2 概念二：prefix——GRUB 的「根目录」

`prefix`（默认 `/boot/grub`）告诉运行时 GRUB：去哪里找模块与配置文件。加载规则：
`(root)/boot/grub/<平台>/xxx.mod`。这正是 ISO 上 `/boot/grub/i386-pc/*.mod` 与
`/boot/grub/x86_64-efi/*.mod` 两套目录的来源——**模块目录 = prefix + 平台名**。
如果 prefix 与文件实际位置不符，GRUB 会陷入 rescue（0.9 课）。

### 2.3 概念三：grub-mkrescue——一站式造 ISO

`grub-mkrescue`（`util/grub-mkrescue.c`）是个「编排者」，内部依次完成：

1. 对 `i386-pc` 调 `grub-mkimage` 得到 eltorito 引导镜像；
2. 对 `x86_64-efi` 调 `grub-mkimage` 得到 grub.efi，并包进 FAT 格式的 `efi.img`；
3. 把全部模块复制进 ISO 的 `/boot/grub/<平台>/`；
4. 调用 xorriso 写盘：ISO9660 + Rock Ridge + El Torito 双记录 + 混合 MBR/GPT/APM。

本机实测的混合盘签名：`MBR protective-msdos-label grub2-mbr cyl-align-off GPT APM`——
同一文件既带传统 MBR（BIOS 可读）又带 GPT/APM（新固件与 Mac 可读）。

### 2.4 概念四：grub-install 与 grub-mkconfig（课程不直接使用）

- `grub-install`：把 GRUB 安装到磁盘/ESP：写 MBR 或 ESP 文件、复制模块、设置 prefix；
- `grub-mkconfig`：根据 `/etc/default/grub` 等生成 `grub.cfg`。

课程不用这两个工具（不装到真实设备），只用 `grub-mkrescue` 产出 ISO。理解它们是为了
对照「发行版装 GRUB」与「课程造启动盘」的差异。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（util/ 三工具）

```bash
cd "$GRUB_SRC"
ls util/grub-mkimage.c util/grub-mkrescue.c util/grub-install.c 2>/dev/null
grep -R "grub_mkimage_generate_core_image\|generate_core_image" util | head -10
grep -R "eltorito\|efi.img" util/grub-mkrescue.c | head -20
```

**预期输出解读**：`grub-mkimage.c` 里有 core image 生成函数；`grub-mkrescue.c` 里能看到
它如何分别处理 `eltorito` 与 `efi.img`。发行版可能改名，以 grep 为准。

### 3.2 观察一：Makefile 逐 target 解读

`lessons/lesson-01-stable/Makefile`（与构建相关的三组规则）：

```make
$(ISO_ROOT)/boot/grub/grub.cfg: grub.cfg
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $< $@

$(ISO_ROOT)/boot/kernel.elf: $(BUILD)/kernel.elf
	mkdir -p $(ISO_ROOT)/boot
	cp $< $@

$(BUILD)/kernel.iso: $(ISO_ROOT)/boot/kernel.elf $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_ROOT)
```

| target | 职责 |
|---|---|
| 前两条 | 把 `grub.cfg`（102 字节）与 `kernel.elf`（5352 字节）放进 ISO 根目录树 |
| `kernel.iso` | 依赖目录树就绪后调 `grub-mkrescue`，产出 13185024 字节的 ISO |

**解读**：TinyOS 只负责「文件到位」，GRUB 工具负责「按 prefix 布局、模块、El Torito 全部
搞定」。`make check` 里的 `grub-file` 是构建后协议检查（0.5 课）。

### 3.3 观察二：prefix 布局的实测证据

```bash
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/grub -maxdepth 2 -exec lsdl -- | grep -E 'i386-pc|grub.cfg|x86_64-efi'
```

实测（节选）：ISO 上存在 `/boot/grub/grub.cfg`、`/boot/grub/i386-pc/`（297 个 .mod）、
`/boot/grub/x86_64-efi/`、`/boot/grub/fonts/unicode.pf2`。**解读**：这正是默认
`prefix=/boot/grub` + 平台名的展开结果；GRUB 启动后按此路径 insmod 模块。

### 3.4 观察三：模块来自发行版目录

```bash
ls /usr/lib/grub/i386-pc/ | wc -l
ls /usr/lib/grub/x86_64-efi/ | wc -l
grub-mkimage --help | grep -E 'directory|默认=/usr/lib/grub'
```

实测：`/usr/lib/grub/` 同时含 `i386-pc` 与 `x86_64-efi` 平台目录（2026-08-06）；
`grub-mkimage --help` 显示 `-d, --directory=目录 使用 <目录> 中的镜像和模块
[默认=/usr/lib/grub/<平台>]`。**解读**：ISO 上的模块不是 GRUB 生成的，而是发行版打包的
现成 `.mod` 被复制进去——这解释了「数量因发行版而异」。

### 3.5 观察四：El Torito 双记录与 efi.img 大小

```bash
xorriso -indev "$ISO" -report_el_torito plain
xorriso -indev "$ISO" -find /efi.img -exec lsdl --
```

实测：BIOS 记录 4 扇区（eltorito.img，LBA 3040）；UEFI 记录 5760 扇区
（`/efi.img`，2949120 字节 = 5760×512）。**解读**：`efi.img` 是一整张 FAT 小镜像
（含 `EFI/BOOT/BOOTX64.EFI`），UEFI 固件直接以文件系统镜像方式挂载它。

---

## 4. 数据流与运行逻辑

```text
make（Lesson 01）
  ├─ gcc -m32 → boot.o / kernel.o
  ├─ ld -m elf_i386 → kernel.elf（入口 0x100020）
  ├─ mkdir + cp → iso/ 目录树（grub.cfg、kernel.elf）
  └─ grub-mkrescue -o kernel.iso iso/
        ├─ grub-mkimage（i386-pc）→ eltorito.img（4 扇区引导段）
        ├─ grub-mkimage（x86_64-efi）→ grub.efi → FAT efi.img
        ├─ 复制模块到 /boot/grub/{i386-pc,x86_64-efi}/
        └─ xorriso → ISO9660+RR+El Torito 双记录+混合 MBR/GPT/APM
```

`grub-install`/`grub-mkconfig` 是「装真机」路径，课程闭环不经过它们。

---

## 5. 观察与验证

### 5.1 依赖

`grub-common`、`grub-pc-bin`、`grub-efi-amd64-bin`（提供模块）、`xorriso`；`$GRUB_SRC`。

### 5.2 复现命令清单

```bash
grub-mkrescue --version                 # GRUB 2.14-2ubuntu2
grub-mkimage --help | grep -E 'format|directory|prefix'
ls /usr/lib/grub/
ISO="lessons/lesson-01-stable/build/kernel.iso"
xorriso -indev "$ISO" -find /boot/grub -maxdepth 2 -exec lsdl -- | grep -cE 'i386-pc/'
xorriso -indev "$ISO" -report_el_torito plain
```

### 5.3 实测记录（2026-08-06，全部只读）

`kernel.iso` 13185024 字节；`i386-pc` 297 个模块、`x86_64-efi` 同名模块集；`efi.img`
2949120 字节；El Torito 双记录；MBR 签名 `grub2-mbr`；`/usr/lib/grub/` 两套平台目录。

### 5.4 安全边界（本课红线）

不运行 `grub-mkimage`/`grub-mkrescue`/`grub-install`（会产生文件）；只用 `--help`/`--version`
与 `ls`/`xorriso -find` 只读查询；不安装任何包。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `make` 报 `grub-mkrescue: command not found` | 缺 `grub-common`/`grub-pc-bin` | `which grub-mkrescue`，装包 |
| ISO 上模块缺失、rescue 提示 | 模块集被裁剪或 prefix 不匹配 | 对比 `/usr/lib/grub/<平台>/` 与 ISO 内模块清单 |
| `xorriso` 找不到或版本旧 | 缺 xorriso | `xorriso --version`（课程实测 1.5.6） |
| ISO 无 UEFI 记录 | 缺 `grub-efi-amd64-bin` | 检查 `/usr/lib/grub/x86_64-efi/` 是否存在 |
| 想自定义模块集 | 需要改 `grub-mkrescue` 参数 | 只在个人副本实验，别动 stable |
| prefix 不符导致模块加载失败 | 目录结构被改动 | 确认 `/boot/grub/<平台>/` 存在且 `grub.cfg` 在位 |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/scripts/` 里的打包脚本与 `util/grub-mkrescue.c` 同属「构建期编排」工具，
  职责都是把分散的二进制装配成可启动镜像；
- `grub-install` 对应发行版的安装脚本（Debian `grub-installer`），课程以 ISO 替代真实安装。

**边界提醒**：构建工具细节以 GNU GRUB 源码为准；Linux 的构建脚本只是类比，不是实现来源。

---

## 8. 思考题与练习

1. 概念理解：`prefix=/boot/grub` 与平台名 `i386-pc` 是如何组合成模块路径的？如果 ISO 上
   目录名不是平台名，会发生什么？
2. 源码定位：在 `$GRUB_SRC/util/grub-mkrescue.c` 中找出处理 `eltorito` 与 `efi.img` 的两段
   代码，记录它们调用的 `grub-mkimage` 参数。
3. 动手观察：数一数 `/usr/lib/grub/i386-pc/` 与 ISO 上 `i386-pc/` 的 `.mod` 数量，判断
   `grub-mkrescue` 是全量复制还是有裁剪。
4. 实验（个人副本）：修改 Makefile 给 `grub-mkrescue` 加 `--compress=xz`，重建后比较
   `kernel.iso` 大小变化。
5. 综合：把 `make` 的产物链画成依赖图，标出每一条边使用的工具与输入输出文件。

---

## 9. 本课小结与下一课预告

**小结**：`grub-mkimage` 按平台拼装 core image（`i386-pc-eltorito` 出 eltorito.img、
`x86_64-efi` 出 grub.efi），`grub-mkrescue` 编排模块复制、efi.img 打包与 xorriso 写盘，
`grub-install` 面向真机安装；`prefix=/boot/grub` + 平台名决定模块路径；Lesson 01 的
`Makefile` 只需把 `grub.cfg` 与 `kernel.elf` 放进目录树，其余交给 GRUB 工具。

**下一课预告**：进入 [`lesson-0.9-stable`](../lesson-0.9-stable/README.md)，如果这些环节
有一环出错会怎样：缺 grub.cfg、路径错、模块缺失、坏 ELF、坏 header——用只读工具
模拟观察 GRUB 的报错与 rescue 提示。

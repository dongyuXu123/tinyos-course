# Lesson B15: El Torito 光盘引导 — 精讲文档

> **课号**：Lesson B15（Mini-GRUB 从零写 GRUB 课程第 15 课，阶段三收尾）
> **主题**：El Torito 启动记录、boot image + core image 模型、boot-info-table
> **课程位置**：阶段三「光盘与文件系统」第 3 课
> **前置课程**：[`b14-stable/README.md`](../b14-stable/README.md)（路径查找与文件抽象）
> **后续课程**：[`b16-stable/README.md`](../b16-stable/README.md)（命令注册表与命令行）
> **一句话目标**：做出一个真正能从 CD 启动的镜像：BIOS 按 El Torito 记录加载
> boot image（stage1），stage1 用 boot-info-table 从 CD 读入 core image
> （stage2 + loader），loader 按路径 `/BOOT/KERNEL.ELF` 从 ISO9660 读出并
> **装载、启动**一个 Multiboot2 测试内核。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能作为光盘引导镜像工作：QEMU
`-boot order=d` 从 CD 启动，loader 从 ISO9660 读出内核并装载交接。

- **在课程中的位置**：研读支线 0.1 观察到的 `eltorito.img`（BIOS 启动记录）
  是 grub-mkrescue 生成的；本课用 `xorriso` 手工打造自己的 El Torito 结构。
  B13/B14 用"整包镜像"（整个 loader 都是 boot image，`-boot-load-size`
  一次加载）；本课升级为 GRUB 的 **core image 模型**——boot image 只含
  stage1，stage1 用 boot-info-table 的参数从 CD 读入其余部分。
  阶段三完成：文件系统 + 光盘启动。
- **前置知识清单**：
  1. B13/B14：ISO9660 读取与路径解析（`file_open` 四层抽象）；
  2. El Torito：boot catalog、boot record descriptor、`-no-emul-boot` 与
     `-boot-load-size`、`-boot-info-table`；
  3. B08/B10/B12：ELF 装载与 Multiboot2 交接（`elf_load`/`mbi_build`/
     `mb2_boot`）。
- **本课交付**：`build/b15.img`（可引导 CD）；QEMU 从 CD 启动后 loader 从
  光盘读出 `/BOOT/KERNEL.ELF` 并装载启动（测试内核打印
  "B08 test-kernel: hello from Multiboot2"）。

---

## 2. 核心概念精讲

### 2.1 概念一：core image 模型（GRUB cdboot.S）

B13/B14 的"整包镜像"把整个 loader 塞进 boot image，靠大 `-boot-load-size`
一次加载。GRUB 真正的做法不同（`grub-core/boot/i386/pc/cdboot.S`）：

```text
CD 上的引导文件 boot.bin = stage1（2048 B，SeaBIOS 加载到 0x7C00）
                        + core（stage2 + loader，链接在 0x8400）
SeaBIOS 按 El Torito 记录只加载前 2048 字节（-boot-load-size 4）
stage1 读 boot-info-table 的 bi_file/bi_length
      -> core 起始扇区 = bi_file + 1（第 1 扇区已被 BIOS 加载）
      -> 用 INT 13 AH=42 读 (bi_length - 2048) 字节到 0x8400
      -> 远跳 0x8400
```

`bi_file + 1` 是关键：SeaBIOS 加载了 boot 文件的第一个 2048 字节（1 个 CD
扇区），core 紧随其后，从 `bi_file + 1` 开始。boot-info-table（BIF）由
xorriso 的 `-boot-info-table` 写在引导镜像偏移 8-63：

| BIF 偏移 | 字段 | 本课实测值 |
|---|---|---|
| 8 | bi_pvd（PVD 的 LBA） | 16 |
| 12 | bi_file（引导文件 LBA） | 34（= boot catalog 的 load RBA） |
| 16 | bi_length（引导文件字节数） | 6144（2048 + 4096 core） |
| 20 | bi_csum（前 512 字节校验和） | — |
| 24-63 | bi_reserved | — |

### 2.2 概念二：跳转结构必须跳过 BIF

xorriso 的 `-boot-info-table` 会**覆盖引导镜像偏移 8-63**（56 字节 BIF）。
因此跳转目标必须落在偏移 64 之后（GRUB cdboot.S 把 `1:` 放在 BIF 后）：
`jmp`（2 字节）+ 62 个 NOP 覆盖 0-63，`1:` 在 64。本课开发中踩过
`1:` 落在偏移 63 的坑——BIF 的最后一个字节把 `cli` 覆盖成 0x00。

### 2.3 概念三：栈上建 DAP（cdboot.S read_cdrom 手法）

stage1 在实模式，直接调 INT 13 AH=42。DAP 可以建在**栈上**（GRUB 做法），
按标准布局压入：`size/res`、`count`、`offset`、`segment`、`lba(8)`，然后
`movw %sp, %si`。栈在 0x7C00 下方，天然 < 64KB，不占用任何固定缓冲。

```asm
    pushl $0                    # lba 高 32 位
    pushl %eax                  # lba 低 32 位
    pushw $(CORE_OFF >> 4)      # 缓冲段 = 0x840
    pushw $(CORE_OFF & 0xF)     # 缓冲偏移 = 0
    pushw %cx                   # count（扇区数）
    pushw $0x10                 # size=0x10, reserved=0
    movw %sp, %si               # DS:SI = DAP
    movw $0x4200, %ax
    int $0x13
    addw $0x10, %sp             # 清理
```

### 2.4 概念四：-boot-info-table 与文件名大小写

本课实测（xorriso 1.5.6）：`-boot-info-table` 会让 xorriso 启用
**"保留大小写"**（relaxed filenames）——源文件名的写法就是 CD 上的
ISO9660 名字。B13/B14 的默认模式会把文件名转成大写（`test.txt` →
`TEST.TXT;1`）；B15 加上 `-boot-info-table` 后 `kernel.elf` 会保持小写
（`kernel.elf;1`）。为保持 loader 路径 `/BOOT/KERNEL.ELF` 与 CD 一致，
B15 把源文件命名为大写 `BOOT.BIN` / `BOOT/KERNEL.ELF`。

### 2.5 概念五：装载链的完成

loader_main 把 B14 的文件抽象与 B08–B11 的装载机制接起来：

```text
file_open("/BOOT/KERNEL.ELF") -> file_read(整个文件到 0x68000)
  -> elf_load(按 PT_LOAD 复制到 p_paddr，bss 清零)
  -> mbi_build(最小 MBI：boot loader name tag + end tag)
  -> mb2_boot(entry, mbi)   # EAX=0x36d76289, EBX=MBI，跳转内核
```

（B10 的 E820 mmap tag 是 B10 的内容；B15 的测试内核不消费 MBI，最小
MBI 即可——B12 的 TinyOS L05 需要 mmap tag 的完整版在 B12 已实现。）

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B14 的增量 |
|---|---|---|
| `stage1.S` | El Torito boot image：BIF + 栈上 DAP 读 core → 0x8400 | 重写（core image 模型） |
| `stage2.S` | 实模式入口 → 保护模式 → `bios_interrupt` + `mb2_boot` | 加回 `mb2_boot` |
| `loader.c` | 文件抽象 + `elf_load` + 最小 MBI + 交接 | 加装载/交接 |
| `Makefile` | `-boot-info-table`、`-boot-load-size 4`、大写源文件名 | 修改 |
| `test-kernel.S/.ld` | `/BOOT/KERNEL.ELF`（B08 复用） | 不变 |
| `build/b15.img` | 可引导 CD | 新增 |

### 3.2 `stage1.S` 精讲

```asm
_start:
    jmp 1f
    # -boot-info-table 覆盖偏移 8-63；跳转目标必须在 64 之后
    .fill 64 - 2, 1, 0x90
1:
    ...
    movl BIF_LENGTH, %ecx       # bi_length：整个引导文件字节数
    subl $STAGE1_SIZE, %ecx     # 减去已被 SeaBIOS 加载的 2048 字节
    jbe core_ok                 # 没有 core，跳过读取
    addl $2047, %ecx
    shrl $11, %ecx              # 扇区数 = ceil(剩余字节 / 2048)
    movl BIF_FILE, %eax
    addl $1, %eax               # core 起始 = bi_file + 1
    # 栈上建 DAP（见 2.3），AH=42 读 core 到 0x8400
    ...
core_ok:
    ljmp $0, $CORE_OFF
```

### 3.3 `loader.c` 装载链

`elf_load`/`elf_parse`/`mbi_build`/`mb2_boot` 从 B08/B10/B12 复用。B15 的
`loader_main` 比 B14 多三步：整文件读入 → ELF 装载 → 交接。

注意缓冲布局（B15 开发中踩坑）：`file_read` 的扇区暂存
`CD_BUF_FILE` 曾与 ELF 文件缓冲 `CD_BUF_ELF` 重叠——最后一次部分读把
暂存数据（最后一个扇区）盖掉了文件缓冲里已读好的多引导头。修正后暂存区
移到 0x6C000（ELF 缓冲 0x68000-0x6A2C0 之外）。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS: 读 boot catalog -> 按 load RBA=34 加载 2048 B 到 0x7C00, DL=0xE0
  |-> stage1: 读 BIF(bi_file=34, bi_length=6144)
  |     读 CD LBA 35..37 到 0x8400（core = stage2+loader）
  |-> stage2: 保护模式 -> loader_main
  |-> loader_main:
  |     iso9660_mount -> file_open("/BOOT/KERNEL.ELF") -> size=0x22c0
  |     file_read(整文件到 0x68000) -> elf_load(paddr=0x100000/0x101000)
  |     mbi_build -> mb2_boot(entry=0x100018, mbi)
  v
内核: "B08 test-kernel: hello from Multiboot2"（接管屏幕后停机）
```

期望输出（VGA 文本，验证脚本 marker 加粗）：

```
**B08 test-kernel: hello from Multiboot2**ge1, now in loader   ← 内核接管第 0 行
B15 eltorito: boot drive = e0
**B15 eltorito: open /BOOT/KERNEL.ELF size=000022c0**
B15 load: paddr=00100000 filesz=0000005d memsz=0000005d
B15 load: paddr=00101000 filesz=00000004 memsz=00000404
**B15 boot: jumping to KERNEL.ELF entry=00100018**
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make            # 构建 boot.bin(stage1+core) + kernel.elf + build/b15.img
make check      # BIF 字段校验 + El Torito 记录 + 符号 + grub-file
make run        # QEMU 从 CD 启动（图形窗口）
./scripts/validate-course.sh b15 check
./scripts/validate-course.sh b15 qemu    # QEMU + VGA 文本校验
```

### 5.2 成功判据

1. `make check` 全绿：BIF（bi_pvd=16、bi_length=boot.bin 大小）、
   `xorriso -report_el_torito` 显示 boot-info-table、`file_open`/`elf_load`/
   `mb2_boot` 符号存在、`grub-file` 验证 KERNEL.ELF；
2. QEMU 从 CD 启动，内核消息 "B08 test-kernel: hello from Multiboot2" 出现；
3. 验证脚本 grep 到 `B15 eltorito: open /BOOT/KERNEL.ELF`、
   `B15 boot: jumping`、`test-kernel`。

---

## 6. 调试地图

1. **`1:` 标签落在偏移 63**：`-boot-info-table` 覆盖 8-63，把 `cli` 覆盖成
   0x00。修正：`jmp(2) + 62 nop`，让代码从偏移 64 开始（GRUB cdboot.S
   同款布局）。
2. **`-boot-info-table` 改变文件名大小写**：xorriso 启用"保留大小写"，
   `kernel.elf` 不再转成 `KERNEL.ELF`。修正：源文件命名大写
   `BOOT.BIN`/`BOOT/KERNEL.ELF`（见 2.4）。
3. **file_read 暂存区与文件缓冲重叠**：最后一次部分读把暂存数据盖进文件
   缓冲，多引导头被最后一个扇区（.data）覆盖，内核执行垃圾 → double
   fault（trace 里 `check_exception old: 0x8 new 0xd`）。修正：暂存区移到
   ELF 缓冲之外。
4. **怀疑 ELF 装载错误时先查文件缓冲**：dump 0x68000 与 `build/kernel.elf`
   `cmp`——文件缓冲错则读盘/路径有问题，文件缓冲对则问题在装载/交接。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| stage1 读 BIF + core | `grub-core/boot/i386/pc/cdboot.S` | DAP 建栈手法一致；GRUB 还按块数折半重试 |
| BIF 字段 | cdboot.S 的 `bi_pvd/bi_file/bi_length/bi_csum` | 逐字段一致 |
| 跳转结构跳过 BIF | cdboot.S 的 `call next; jmp 1f; .org 8` | 本课用绝对地址读 BIF |
| file_open → elf_load → mb2_boot | `grub-core/loader/multiboot.c` + `commands/boot.c` | 装载链同构 |
| xorriso 打包 | `util/grub-mkrescue.c` | 本课手工单记录（BIOS），无 UEFI |

---

## 8. 思考题与练习

1. 如果 `-boot-load-size` 改成 8（加载 4096 字节），core 起始扇区还是
   `bi_file + 1` 吗？（提示：已加载字节数 / 2048）
2. GRUB 的 `read_cdrom` 在读取失败时会**把块数折半重试**。想想为什么需要
   这个逻辑（真实光驱与 QEMU 的差异），并实现一个最小重试。
3. `bi_csum` 是引导文件前 512 字节的校验和。写个脚本验证
   `-boot-info-table` 写的校验和是否正确。
4. 为什么 stage1 的 DAP 建在栈上比放在固定地址更稳？（提示：core 读到
   0x8400 起，可能覆盖任何低于镜像上沿的固定缓冲）
5. B15 的最小 MBI 只有 boot loader name + end tag；如果内核需要内存图
   （B12 的 TinyOS L05），需要加 type-6 mmap tag——回顾 B10 的
   `mmap_collect`，把它接进 B15 的 `mbi_build`。

---

## 9. 本课小结与下一课预告

**小结**：本课完成阶段三——loader 作为光盘引导镜像工作：El Torito boot
catalog + boot image + `-boot-info-table`（BIF），stage1 用栈上 DAP 从 CD
读入 core image，loader 按路径 `/BOOT/KERNEL.ELF` 从 ISO9660 读出内核、
装载并 Multiboot2 交接，测试内核在屏幕上打印欢迎消息。全程不依赖
`grub-mkrescue`，从零手工打造。

**下一课** [`b16-stable/README.md`](../b16-stable/README.md)：**阶段四**开始：
光盘上的 `/boot/grub/grub.cfg` 还只是文件——B16 实现命令注册表、`set`
环境变量与极简 `grub>` 命令行，对照 GRUB `include/grub/command.h` 与
`kern/env.c`。

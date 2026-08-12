# Lesson B13: ISO9660 基础读取 — 精讲文档

> **课号**：Lesson B13（Mini-GRUB 从零写 GRUB 课程第 13 课，阶段三第 1 课）
> **主题**：ISO9660 文件系统：PVD、目录记录、extent 读取
> **课程位置**：阶段三「光盘与文件系统」第 1 课
> **前置课程**：[`b12-stable/README.md`](../b12-stable/README.md)（综合 checkpoint）；
> [`b05-stable/README.md`](../b05-stable/README.md)（实模式回调磁盘读）
> **后续课程**：[`b14-stable/README.md`](../b14-stable/README.md)（路径查找与文件抽象）
> **一句话目标**：loader 从**光盘**上按块读取文件内容——从 ISO9660 的
> 主卷描述符（PVD）解析出根目录与文件的位置，读出 `TEST.TXT` 并与
> xorriso 抽取的原始字节对照一致。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能解析一张 ISO9660 光盘的卷结构，找到
根目录记录，遍历目录条目，并按 extent 读取一个文件的内容。

- **在课程中的位置**：阶段二一直在软盘固定扇区上读内核——真实 GRUB 从光盘
  ISO9660 文件系统**按路径找文件**。B13 引入文件系统层：CD 的读盘原语
  （INT 13 AH=42 EDD，2048 字节扇区）+ ISO9660 卷/目录解析。B14 把它抽象成
  路径查找（`device → disk → fs → file`），B15 完成 El Torito 光盘引导的
  "读 core image"部分。对照 GRUB `grub-core/fs/iso9660.c`。
- **前置知识清单**：
  1. B05：保护模式下调 BIOS 中断（`bios_interrupt`）——B13 的 CD 读取复用
     整个 B05 机制，只是把 INT 13 的调用方式从 CHS（AH=02）换成
     EDD（AH=42 + DAP）；
  2. El Torito：CD 上引导镜像如何被 BIOS 加载（B13 用整包镜像，B15 细讲）；
  3. ISO9660：逻辑扇区 2048 字节、PVD 在逻辑扇区 16、目录记录结构、
     both-endian 双字段。
- **本课交付**：`build/b13.img`（El Torito CD 镜像：整包 loader + `TEST.TXT`）；
  QEMU 从 CD 启动后，loader 打印引导盘号、PVD 卷标识、根目录 extent/size、
  每个目录条目，读出 `TEST.TXT` 内容并打印 "content match"。

---

## 2. 核心概念精讲

### 2.1 概念一：CD 的 BIOS 盘号与扇区大小

软盘是 512 字节/扇区，用 INT 13 AH=02 的 CHS 几何寻址；**CD 是 2048
字节/逻辑扇区**，BIOS 用 INT 13 AH=42（EDD，LBA 寻址）读取，地址包
DAP（Disk Address Packet）在 DS:SI：

```c
struct dap {          /* 长度 16 字节，按 1 对齐 */
    u8  size;         /* 0x10 */
    u8  reserved;
    u16 count;        /* 块数（每块 2048 字节） */
    u16 offset;       /* 缓冲偏移 */
    u16 segment;      /* 缓冲段 */
    u32 lba_lo;       /* 起始 LBA（小端） */
    u32 lba_hi;
};
```

关键经验（本课开发中实测验证，SeaBIOS 1.17.0 / QEMU 10.2.1）：

- **CD 盘号 = 0xE0**。SeaBIOS 从 `EXTSTART_CD = 0xE0` 开始给 CD 编盘号
  （`src/block.h`），El Torito no-emul 引导时把 `CDEmu.emulated_drive`
  （= 0xE0 + cdid）放进 DL 传给引导镜像（`src/boot.c` 的 `boot_cdrom`）。
  引导代码直接使用入口 DL 即可，或用 INT 13 AH=4B01 查询（结构偏移 2 的
  `emulated_drive` 字段，GRUB `biosdisk.c` 的做法）。
- **误用其他盘号会得到 AH=1**（invalid function）：例如对 0xE8 调 AH=42，
  SeaBIOS 的 `handle_legacy_disk` 在 `getDrive(EXTTYPE_CD, 8)` 找不到盘后
  返回 `DISK_RET_EPARAM`。
- **LBA 就是 ISO9660 卷空间的逻辑扇区号**：PVD 在 LBA 16，Boot Record 卷
  描述符在 LBA 17（引导目录的指针也在其中）；用 AH=42 直接读即可，无需
  偏移。

### 2.2 概念二：ISO9660 PVD 与根目录记录

ISO9660 卷描述符区从逻辑扇区 16 开始，一连串 2048 字节的描述符以 type
字段区分（1 = 主卷描述符 PVD，0xFF = 终止描述符）。PVD 关键字段：

| 偏移 | 长度 | 内容 |
|---|---|---|
| 0 | 1 | type = 1（主卷描述符） |
| 1 | 5 | `"CD001"` |
| 40 | 32 | 卷标识（volume id，空格填充） |
| 80 | 8 | 卷空间大小（both-endian u32） |
| 156 | 34 | **根目录记录** |

目录记录（ISO 9660 规范 9.1，最小 34 字节）：

| 偏移 | 长度 | 内容 |
|---|---|---|
| 0 | 1 | 记录长度（含文件名与偶数填充） |
| 2 | 8 | extent 起始逻辑扇区（LSB + MSB 双格式） |
| 10 | 8 | 数据长度（字节，LSB + MSB） |
| 25 | 1 | flags（bit1 = 目录） |
| 32 | 1 | 文件名长度 |
| 33 | 变长 | 文件名（无结尾 NUL，后面可跟 SUSP/Rock Ridge 扩展） |

**both-endian 约定**：关键字段同时以小端（LSB）与大端（MSB）各存一份，
规范要求两值一致；解析通常取 LSB（`le32`）。文件名是 level-1 短名
`NAME.EXT;版本`，`;` 后是版本号，比较/显示时截断到 `;`。

### 2.3 概念三：根目录的 "." 与 ".." 条目

根目录（和每个目录）的前两个条目是 "."（自身）与 ".."（父目录）。注意：
**ISO9660 里根目录记录的名字不是 `'.'`/`'..'`，而是单字节 `0x00`/`0x01`**
（ECMA-119 约定）。GRUB 在 `iso9660.c` 里显式映射：

```c
/* . and .. */
if (!ctx.filename && dirent.namelen == 1 && name[0] == 0)
    ctx.filename = (char *) ".";
if (!ctx.filename && dirent.namelen == 1 && name[0] == 1)
    ctx.filename = (char *) "..";
```

B13 的 `print_name` 复刻了这一映射，否则 0x00/0x01 打印出来是不可见字符。

### 2.4 概念四：El Torito 整包引导镜像

xorriso 用 `-b boot.bin -no-emul-boot -boot-load-size N` 写引导记录：
SeaBIOS 读引导目录（boot catalog），把 `boot.bin` 从起始 LBA 连续加载
`N × 512` 字节到 0x7C00（load segment 默认 0x07C0），并把 CD 盘号放进 DL。
B13 把**整个 loader**（stage1 2048 字节 + stage2 + loader.c）打包成
`boot.bin`，因此 stage1 无需从 CD 读任何东西，保存 DL 后直接跳 0x8400。
（真 GRUB 的 `cdboot.S` 用 64 字节头部的 boot-info-table 定位并读取
core image——那是 B15 的内容。）

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B12 的增量 |
|---|---|---|
| `stage1.S` | El Torito 引导镜像（2048 B）：保存 DL → 0x60000，跳 0x8400 | 新增（CD 版引导头） |
| `stage2.S` | 实模式入口 → 保护模式 → `bios_interrupt` 回调 | 去掉 `mb2_boot`（B13 不交接内核） |
| `loader.c` | `cd_read_lba`（AH=42+DAP）+ `iso9660_open`/`iso9660_read` + 目录遍历 | 新增 ISO9660 解析 |
| `linker.ld` | stage2 链接到 0x8400（= 0x7C00 + 2048） | 基址变化 |
| `Makefile` | `cat stage1+stage2` → xorriso 打 CD | 新增 |
| `test.txt` | 光盘上的测试文件（`TEST.TXT`） | 新增 |
| `build/b13.img` | El Torito CD 镜像 | 新增 |

### 3.2 `stage1.S` 精讲

```asm
1:
    cli
    ...
    # 注意：16 位模式的位移只有 16 位，0x60000 会被截断（汇编成 mov %dl,0x0），
    # 必须用段寄存器：ES=0x6000 -> ES:0 = 物理 0x60000。
    movw $0x6000, %ax
    movw %ax, %es
    movb %dl, %es:(0)         # 保存引导盘号（no-emul CD = 0xE0）
    ...
    ljmp $0, $STAGE2_OFF      # 远跳到 stage2 实模式入口（0x0000:0x8400）
```

教学要点：16 位实模式的 `mov` 内存位移只有 16 位，`0x60000` 会被**静默
截断**成 `0x0000`（本课开发中踩到：盘号被存进 IVT 而不是 0x60000）。超过
64KB 的绝对地址必须用 `ES`（或 DS）段寄存器组合寻址。

### 3.3 `loader.c` 精讲

CD 读取原语（对照 `cdboot.S read_cdrom` 与 `biosdisk.c` 的 DAP）：

```c
static struct dap cd_dap;    /* 静态：位于 loader 内（物理 < 64KB），DS:SI 可寻址 */

static int cd_read_lba(u8 drive, u32 lba, void *buf, u32 count)
{
    u32 phys = (u32)buf;
    struct bios_regs regs;

    cd_dap.size = 0x10;
    cd_dap.count = (u16)count;
    cd_dap.offset = (u16)(phys & 0xFu);
    cd_dap.segment = (u16)(phys >> 4);
    cd_dap.lba_lo = lba;

    regs.eax = 0x00004200u;
    regs.edx = drive;
    regs.ds  = 0;
    regs.esi = (u32)&cd_dap;   /* DS:SI = DAP（物理 < 64KB） */
    regs.flags = 0x0200u;
    bios_interrupt(0x13, &regs);
    return (regs.flags & 0x01u) ? -1 : 0;
}
```

注意 DAP 的放置：`bios_interrupt` 把 `regs.esi` 装入 16 位 SI（DS=0），所以
**DAP 物理地址必须 < 64KB**（静态变量在 0x8400 起的低内存正好满足）；缓冲
则用 DAP 内的段:偏移描述，可在 1MB 内任意位置（本课用 0x68000 的 GRUB
scratch 区）。

ISO9660 打开（读 PVD → 校验 → 记录根目录）：

```c
int iso9660_open(u8 drive)
{
    u8 *pvd = (u8 *)CD_BUF_PVD;          /* 0x68000 */
    ...
    if (cd_read_lba(drive, PVD_LBA, pvd, 1) < 0)
        return -1;
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1')
        return -2;
    ...
    iso_root_extent = le32(pvd + PVD_ROOT_OFF + 2);
    iso_root_size   = le32(pvd + PVD_ROOT_OFF + 10);
}
```

目录遍历：读根目录 extent（`(size+2047)/2048` 个扇区）→ 逐条解析记录
（`len == 0` 表示记录区结束；记录按 `len` 前进）→ 打印名字/extent/size →
用 `name_matches`（截断 `;` 版本号）找 `TEST.TXT` → 读文件 extent →
把前 19 字节与 `"Hello from ISO9660"` 逐字节比较。

### 3.4 构建管线（Makefile）

```text
stage1.o + stage2.o + loader.o
   -> stage1.bin（ld -Ttext 0x7C00，truncate 到 2048）
   -> stage2.bin（ld -T linker.ld = 0x8400）
   -> boot.bin（cat stage1 stage2，truncate 到 2048 的倍数）
   -> b13.img（xorriso -b boot.bin -no-emul-boot
               -boot-load-size = boot.bin/512）
```

`make check` 断言：stage1 = 2048 B、stage2 < 0x7000（保证 DAP < 64KB）、
boot.bin 2048 对齐、`cd_read_lba`/`iso9660_open`/`iso9660_read` 符号存在、
ISO 里有 El Torito 记录，且 `xorriso -extract` 出来的 `TEST.TXT` 与源文件
**字节级一致**（`cmp`）。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS (El Torito)                     loader
-------------                           ------
读 boot catalog -> 加载 boot.bin 到 0x7C00
DL = CD 盘号 (0xE0)
     |-> stage1: 保存 DL 到 0x60000, 跳 0x8400
     |-> stage2: 实模式提示 -> A20 -> GDT -> CR0.PE -> loader_main
     |-> loader_main:
     |     drive = *(u8*)0x60000
     |     iso9660_open(drive): 读 LBA 16 (PVD) -> 校验 "CD001"
     |        -> 卷标识, 根目录 extent/size
     |     walk_root_dir(): 读根目录 -> 逐条打印
     |        -> 找到 TEST.TXT (extent=0x25, size=0x37)
     |     iso9660_read(extent): 读文件 -> 打印 hex + 比对
     |        -> "content match"
     v
   停机（本课不交接内核；B14 起按路径读）
```

期望输出（VGA 文本，验证脚本 grep 的 marker 加粗）：

```
B13 iso9660: Mini-GRUB reads a CD
B13 iso9660: boot drive = e0
B13 iso9660: PVD ok, volume="B13TEST"
B13 iso9660: root dir extent=00000013 size=00000800
B13 iso9660: entry "." ext=00000013 size=00000800
B13 iso9660: entry ".." ext=00000013 size=00000800
B13 iso9660: entry "BOOT.BIN" ext=00000022 size=00001800
B13 iso9660: entry "BOOT.CAT" ext=00000021 size=00000800
B13 iso9660: entry "TEST.TXT" ext=00000025 size=00000037
B13 iso9660: reading TEST.TXT ext=00000025 size=00000037
B13 iso9660: content: 48656c6c6f2066726f6d2049534f39363630212042313320636f6e7465...
B13 iso9660: content match
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make            # 构建 build/b13.img（xorriso 打 CD）
make check      # 静态断言 + TEST.TXT 字节对照
make run        # QEMU 从 CD 启动（图形窗口）
./scripts/validate-course.sh b13 check   # 课程级 check
./scripts/validate-course.sh b13 qemu    # 课程级 QEMU + VGA 文本校验
```

### 5.2 成功判据

1. `make check` 全绿（符号、对齐、El Torito 记录、`TEST.TXT` 字节一致）；
2. QEMU 从 CD 启动（无 triple fault），VGA 显示上述输出；
3. `content: 48656c...` 与 `xxd test.txt` 的字节一致；
4. 验证脚本 grep 到 `B13 iso9660`、`root dir extent`、`TEST.TXT`、
   `content match` 四个 marker。

---

## 6. 调试地图

B13 的开发踩坑记录（按时间顺序），对排查 CD 相关问题有直接参考价值：

1. **CD 盘号不是 0xE8**：早期探测打印 "DL=248" 以为是 0xE8，实际是
   `sputs` 结束时 DX 停在 0x3F8、打印的是它的低字节。用 VGA 文本输出
   （不经串口）实测得到真正的 DL=0xE0。
2. **AH=42 对错误盘号返回 AH=1**：SeaBIOS 从 0xE0 起给 CD 编盘号，
   对 0xE8 找不到盘（`getDrive(EXTTYPE_CD, 8)` 为 NULL）→ EPARAM。
3. **裸 `objcopy` 不重定位符号**：`movw $msg_dl, %si` 在无链接器时汇编成
   段内偏移，puts 读到 IVT 里的垃圾字节。引导代码必须 `ld -Ttext` 链接。
4. **串口/UART 的陷阱**：sputc 用 CX 做等待计数器会冲掉调用方的 `loop`
   计数（死循环）；`serial_init` 会冲掉 DL。验证优先用 VGA 文本。
5. **16 位位移截断**：`movb %dl, (0x60000)` 被静默截断成 `(0x0000)`，
   必须用 ES 段寄存器（见 3.2）。
6. **根目录 "."/".." 的名字是 0x00/0x01**（ECMA-119），不是 `'.'`/`'..'`
   （见 2.3）。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `cd_read_lba`（AH=42 + DAP） | `grub-core/boot/i386/pc/cdboot.S` `read_cdrom`；`grub-core/disk/i386/pc/biosdisk.c` `grub_biosdisk_read` | GRUB 的 DAP 建在栈上并按块数折半重试；本课单块读 |
| `iso9660_open`（PVD/根目录） | `grub-core/fs/iso9660.c` `grub_iso9660_mount` | 结构体字段一致（`voldesc.rootdir`） |
| 目录记录遍历 | `grub_iso9660_iterate_dir` | GRUB 还解析 SUSP/Rock Ridge；本课只读 ISO9660 短名 |
| 0x00/0x01 → "."/".." | `iso9660.c` 的 `/* . and .. */` 分支 | 逐字复刻 |
| 引导盘号来源 | `biosdisk.c` `GRUB_MOD_INIT`：`cd_drive = cdrp->drive_no`（AH=4B01）或 `boot_drive` | 本课直接用 boot DL |

---

## 8. 思考题与练习

1. 为什么 DAP 必须放在 < 64KB 的地址，而数据缓冲可以放在 1MB 内任意位置？
   （提示：`bios_interrupt` 如何把 `regs.esi`/`regs.es+regs.ebx` 送进实模式）
2. 把 `cd_read_lba` 改成支持跨 65535 块（count 是 u16）的循环，读一个
   大文件需要几次调用？
3. ISO9660 的 both-endian 字段如果 LSB/MSB 不一致，取哪个？规范怎么要求的？
4. 如果光盘里还有子目录（flags bit1 = 1），`TEST.TXT` 在子目录里，
   `walk_root_dir` 该怎么扩展？（这正是 B14 路径查找的内容）
5. 用 `xorriso -indev build/b13.img -toc` 查看引导目录，对比 SeaBIOS 加载
   的 `-boot-load-size` 与镜像实际大小。

---

## 9. 本课小结与下一课预告

**小结**：本课把"按固定扇区读"升级为"按文件系统读"——CD 的 2048 字节扇区、
AH=42+DAP 读盘原语、ISO9660 PVD/目录记录/extent 解析、根目录 0x00/0x01
点条目约定，以及 El Torito 整包引导镜像的构建。loader 读出的文件字节与
xorriso 抽取的内容**逐字节一致**。

**下一课** [`b14-stable/README.md`](../b14-stable/README.md)：把"按块读"升级
为**按路径读**：`device → disk → fs → file` 四层抽象与逐组件路径解析，
让 loader 能用 `/boot/kernel.elf` 这样的路径打开文件——对照 GRUB 的
`kern/{device,disk,file}.c` 与 `include/grub/fs.h`。

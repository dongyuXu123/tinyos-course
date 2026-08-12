# Lesson B14: 路径查找与文件抽象 — 精讲文档

> **课号**：Lesson B14（Mini-GRUB 从零写 GRUB 课程第 14 课，阶段三第 2 课）
> **主题**：`device → disk → fs → file` 四层抽象与路径组件查找
> **课程位置**：阶段三「光盘与文件系统」第 2 课
> **前置课程**：[`b13-stable/README.md`](../b13-stable/README.md)（ISO9660 基础）
> **后续课程**：[`b15-stable/README.md`](../b15-stable/README.md)（El Torito 光盘引导）
> **一句话目标**：loader 能用 `/BOOT/KERNEL.ELF` 这样的路径打开文件，内部由
> "设备 → 磁盘 → 文件系统 → 文件"四层协作完成。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 有一个 `file_open(path)` 抽象接口，后续
B17（读 grub.cfg）、B12 化（读内核）都只跟路径打交道。

- **在课程中的位置**：B13 能读 ISO9660 的单文件（写死在代码里的根目录遍历）；
  本课把读文件组织成分层抽象，并让**路径**成为唯一入口。GRUB 的路径解析是
  `grub_file_open` → 文件系统驱动 → 分区 → 磁盘 → 设备的五层链（研读支线
  0.3）；本课精简为四层（无分区层，CD 单卷）。
- **前置知识清单**：
  1. B13：ISO9660 目录记录与 extent（`le32`/目录遍历/`;版本` 截断）；
  2. 路径语法：绝对路径 `/BOOT/KERNEL.ELF` 从根目录逐组件查找；
  3. 研读支线 0.3（`grub_file` 抽象与 `(cd0)` 设备命名）。
- **本课交付**：`build/b14.img`（El Torito CD：loader + `/BOOT/KERNEL.ELF`
  + `/TEST.TXT`）；loader 用 `file_open("/BOOT/KERNEL.ELF")` 打开内核并打印
  大小与 ELF entry，打开 `/TEST.TXT` 核对内容，并对不存在的路径打印
  `B14 error: file not found`。

---

## 2. 核心概念精讲

### 2.1 概念一：四层抽象（device → disk → fs → file）

GRUB 里"读一个文件"不是一个大函数，而是四层对象协作：

| 层 | GRUB 结构 | 本课结构 | 职责 |
|---|---|---|---|
| 设备 | `struct grub_device`（`kern/device.c`） | `cd_device{name,disk}` | 命名（`(cd0)`）、持有磁盘 |
| 磁盘 | `struct grub_disk`（`kern/disk.c`） | `cd_disk{drive,sector_size}` | LBA 读写原语（包 `cd_read_lba`） |
| 文件系统 | `struct grub_fs`（`include/grub/fs.h`） | `iso9660_mount/lookup` | 卷挂载、目录解析、extent → 字节流 |
| 文件 | `struct grub_file`（`kern/file.c`） | `file_open/read/close` | 面向路径的句柄（extent+size+pos） |

分层的好处：上层（路径、菜单、配置文件）只依赖 `file_open(path)` 的抽象，
不关心底下是软盘、硬盘还是 CD、是 ISO9660 还是 ext2——新增文件系统只需要
挂一个新的 `struct grub_fs`。本课固定 `(cd0)` 单设备，但层级划分与 GRUB
一一对应。

### 2.2 概念二：路径组件查找

`/BOOT/KERNEL.ELF` 的解析是一个逐组件下钻循环（对照 `grub_fshelp_find_file`
的 `path` 拆分）：

```text
path = /BOOT/KERNEL.ELF
  [0] 组件 "BOOT"      -> 在根目录（PVD 里的根目录记录）找 -> 是目录（flags bit1）
  [1] 组件 "KERNEL.ELF" -> 在 BOOT 的目录 extent 里找    -> 是文件 -> 返回其 extent/size
```

每次下钻把"当前目录"的 extent/size 换成找到的子目录的 extent/size。组件名
按 ISO9660 level-1 约定大写（`BOOT`、`KERNEL.ELF`），匹配时截断 `;版本`。

**错误语义**（为 B22 错误分类打基础）：

| 返回 | 含义 |
|---|---|
| -1 | 读盘失败 |
| -2 | 组件不存在（file not found） |
| -3 | 路径非法（试图穿过常规文件、或路径以目录结尾） |

### 2.3 概念三：file_read 的任意偏移读

`file_read(file, buf, len)` 从 `file->pos` 读 `len` 字节。文件数据可能从
扇区中间开始、也可能跨扇区，所以按"扇区内偏移 + 剩余长度"切片：整扇区
对齐的部分直接读入目标缓冲，非对齐部分先读入 scratch（0x69000）再拷贝。
这是 GRUB `grub_file_read` 的简化版（GRUB 还有 fs 层的 read hook 与
`GRUB_FILE_CACHE` 预读，本课省略）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B13 的增量 |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | El Torito 引导镜像 + BIOS 回调 | 消息文本变化 |
| `loader.c` | 四层抽象 + `file_open/read/close` + 路径演示 | 重写（fs/file 层） |
| `test-kernel.S`/`test-kernel.ld` | `/BOOT/KERNEL.ELF`（最小 Multiboot2 ELF，B08 复用） | 新增 |
| `Makefile` | CD 加 `BOOT/KERNEL.ELF` 子目录 | 新增 |
| `test.txt` | `/TEST.TXT` | 不变 |
| `build/b14.img` | El Torito CD 镜像 | 新增 |

### 3.2 四层结构的 C 定义

```c
/* 第 2 层：磁盘 */
struct grub_disk {
    u8  drive;           /* BIOS 盘号（0xE0） */
    u32 sector_size;     /* 2048 */
};

/* 第 1 层：设备（固定 (cd0)） */
struct grub_device {
    const char *name;
    struct grub_disk *disk;
};

/* 第 4 层：文件句柄 */
struct grub_file {
    struct grub_device *device;
    u32 extent;          /* 数据起始 LBA */
    u32 size;            /* 文件字节数 */
    u32 pos;             /* 当前读位置 */
};
```

文件系统层（第 3 层）暴露与 `struct grub_fs` 同构的
`open(path) / read(file) / close(file)` 三个函数，`file_open/file_read/
file_close` 只是把设备固定为 `(cd0)` 的薄封装——这就是"以后读内核、读
grub.cfg 只跟路径打交道"的接口。

### 3.3 路径查找实现

```c
static int iso9660_lookup(const char *path, struct grub_file *file)
{
    const char *p = path;
    u32 extent = iso_root_extent;      /* 从根目录出发 */
    u32 size = iso_root_size;
    int isdir = 1;

    if (*p == '/') p++;
    while (*p) {
        char comp[16];                 /* 组件名缓冲（ISO9660 短名上限） */
        u32 clen = 0;
        int r, child_is_dir;
        while (*p && *p != '/') {
            if (clen < sizeof(comp) - 1) comp[clen++] = *p;
            p++;
        }
        comp[clen] = 0;
        if (*p == '/') p++;
        if (clen == 0) continue;

        if (!isdir)
            return -3;                 /* 试图穿过常规文件 */
        r = find_in_dir(extent, size, comp, &extent, &size, &child_is_dir);
        if (r < 0)
            return r;                  /* -1 读失败 / -2 未找到 */
        if (*p == 0) {                 /* 最后一个组件 */
            if (child_is_dir) return -3;
            file->extent = extent;
            file->size = size;
            file->pos = 0;
            return 0;
        }
        isdir = child_is_dir;          /* 还有后续组件：继续下钻 */
    }
    return -3;
}
```

`find_in_dir` 复用 B13 的目录记录遍历（`name_matches` 截断 `;`），并在命中时
把 `flags bit1` 提取为 `child_is_dir`——这是"知道该继续下钻还是到达文件"
的关键。

### 3.4 测试内核与镜像布局

`test-kernel.S`（B08 复用）是一个最小 Multiboot2 ELF：`.multiboot` 段放
header（magic/arch/len/checksum + end tag），`.text` 打印 "B08 test-kernel"
后停机，链到物理 1 MiB。`grub-file --is-x86-multiboot2` 验证通过。CD 布局：

```text
/
├── BOOT/
│   └── KERNEL.ELF        # build/kernel.elf（Multiboot2 ELF）
├── TEST.TXT              # test.txt
└── BOOT.BIN              # 引导镜像（loader）
```

（xorriso 还会生成 BOOT.CAT 引导目录文件。）

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(存 DL, 跳 0x8400) -> stage2(保护模式) -> loader_main
  loader_main:
    cd_device.name = "cd0"; cd_disk.drive = 入口 DL (0xE0)
    iso9660_mount(&cd_device)          # 读 PVD -> 根目录 extent/size
    file_open("/BOOT/KERNEL.ELF", &f)  # 逐组件: BOOT(目录) -> KERNEL.ELF(文件)
        -> f.size = 0x22c0
    file_read(&f, buf, 32)             # ELF header
        -> "KERNEL.ELF magic ok, entry=00100018"
    file_close(&f)
    file_open("/TEST.TXT", &f)         # 根目录单组件
        -> 读内容 -> "TEST.TXT content match"
    file_open("/NOPE.TXT", &f)         # 未找到
        -> "B14 error: file not found (/NOPE.TXT)"
```

期望输出（VGA 文本，验证脚本 marker 加粗）：

```
B14 file: device (cd0) -> disk -> fs -> file
B14 file: boot drive = e0
B14 file: iso9660 mounted, root extent=00000013
**B14 file: open /BOOT/KERNEL.ELF size=000022c0**
**B14 file: KERNEL.ELF magic ok, entry=00100018**
B14 file: open /TEST.TXT size=00000037
B14 file: TEST.TXT content match
**B14 error: file not found (/NOPE.TXT)**
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make            # 构建 kernel.elf + CD 镜像 build/b14.img
make check      # 符号/大小断言 + grub-file + 抽取文件字节对照
make run        # QEMU 从 CD 启动
./scripts/validate-course.sh b14 check
./scripts/validate-course.sh b14 qemu    # QEMU + VGA 文本校验
```

### 5.2 成功判据

1. `make check` 全绿：`file_open/file_read/file_close` 符号存在、
   `grub-file` 验证 `KERNEL.ELF` 是合法 Multiboot2 内核、`xorriso -extract`
   出的 `KERNEL.ELF`/`TEST.TXT` 与源文件 `cmp` 一致；
2. QEMU 从 CD 启动，VGA 显示上述输出；
3. `entry=00100018` 与 `readelf -h build/kernel.elf` 的入口点一致；
4. 验证脚本 grep 到 `B14 file: open /BOOT/KERNEL.ELF`、
   `B14 file: KERNEL.ELF magic ok`、`B14 error: file not found`。

---

## 6. 调试地图

1. **ELF entry 读成 0**：演示代码只读了 16 字节却访问 `buf[24..27]`（e_entry
   在偏移 24）——栈上垃圾是 0。修正为读 32 字节。教训：读结构体字段前先
   确认已读入足够的字节。
2. **路径大小写**：ISO9660 文件名是大写（`KERNEL.ELF`），路径参数必须按
   CD 实际命名；Rock Ridge 才提供小写名（本课明确只支持 level-1 短名）。
3. **目录可能跨 extent**：B14 限制目录 ≤ 8 扇区（`find_in_dir` 里
   `sectors > 8` 截断）；真实 ISO 的大目录需要按 data_len 循环读（留作练习）。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `struct grub_disk/device/file` | `kern/disk.c`、`kern/device.c`、`kern/file.c` | 字段大幅精简；无分区/多设备 |
| `file_open(path)` | `grub_file_open` | GRUB 解析 `(cd0)/path` 设备前缀；本课固定设备 |
| `iso9660_mount/lookup` | `grub_iso9660_mount` + `grub_fshelp_find_file` | GRUB 走 `fshelp` 通用目录框架 + Rock Ridge |
| `file_read`（任意偏移） | `grub_file_read` | 无缓存/预读 |
| 错误码 -2/-3 | `GRUB_ERR_FILE_NOT_FOUND` 等 | 本课用整数返回码（无 errno 系统） |

---

## 8. 思考题与练习

1. 如果把 `file_open` 的路径参数改成 `"(cd0)/BOOT/KERNEL.ELF"`（GRUB 风格），
   需要在哪里加设备名解析？（提示：`grub_file_open` 先拆设备再拆路径）
2. `find_in_dir` 的 8 扇区上限去掉后，目录跨 extent 读取该怎么写？
   （提示：`dir_size` 是字节数，按扇区循环）
3. `file_read` 目前从 `pos` 读、不回卷；如果要支持 `file_seek`（随机访问），
   需要加什么字段和边界检查？
4. Rock Ridge 的 `NM` 记录可以让文件名是 `kernel.elf`（小写）。想想 ISO9660
   目录记录的 SUSP 区域在哪、`NM` 记录如何覆盖 ISO 短名——这正是 GRUB
   `iso9660.c` 里最复杂的部分。
5. 为什么四层抽象里"设备"在最外层、"文件"在最内层？如果有一天要支持两个
   光驱（`(cd0)`/`(cd1)`），哪些结构需要变成数组/链表？

---

## 9. 本课小结与下一课预告

**小结**：本课把 B13 的"读单文件"升级为"按路径读文件"——`device → disk →
fs → file` 四层抽象（对应 GRUB 的 `grub_device/grub_disk/grub_fs/grub_file`）、
逐组件路径查找、错误语义，以及面向任意偏移的 `file_read`。loader 现在只用
路径就能拿到 `/BOOT/KERNEL.ELF` 和 `/TEST.TXT` 的内容，为后续读 grub.cfg、
装载内核提供了统一入口。

**下一课** [`b15-stable/README.md`](../b15-stable/README.md)：CD 内容能读了，
还差"光盘如何启动"——B15 实现 El Torito：自建 boot image、boot catalog、
用 `xorriso` 打包，让 BIOS 直接从 CD 引导我们的 loader（stage1 用
boot-info-table 定位并读取 core image）。

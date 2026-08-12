# Lesson B02: 两段式引导 — 精讲文档

> **课号**：Lesson B02（Mini-GRUB 从零写 GRUB 课程第 2 课，可执行课）
> **主题**：两段式引导：512 字节 stage1 用 INT 13 读出 stage2，远跳执行
> **课程位置**：阶段一「实模式与保护模式引导链」第 2 课
> **前置课程**：[`b01-stable/README.md`](../b01-stable/README.md)（引导扇区 Hello）；
> 研读支线 0.7（`boot.S` → `diskboot.S` 的两段式结构）
> **后续课程**：[`b03-stable/README.md`](../b03-stable/README.md)（保护模式切换）
> **一句话目标**：让引导扇区真正读盘——用 `int $0x13` 把第二段代码从软盘读入
> `0x7E00` 并远跳过去，理解 CHS 换算与「boot.img → diskboot.S」的分工。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能写出一个用 CHS 读扇区的引导扇区，把 4096 字节的
stage2 从软盘 LBA 1–8 读到 `0x7E00`，远跳执行，并解释为什么 GRUB 也要这样分成两段。

- **在课程中的位置**：B01 的引导扇区只会打印——512 字节装不下真正的加载逻辑。
  本课加上「读盘 + 跳转」，stage2 成为后续所有课程（保护模式、ELF 装载、MBI）的
  载体。这正对应 GRUB 的 `boot.S`（stage1，定位）→ `diskboot.S`（读入 core image）
  分工。
- **前置知识清单**：
  1. B01：引导扇区初始化段寄存器、`INT 10` 打印、0x55AA 签名；
  2. 软盘几何：1.44 MB = 80 柱面 × 2 磁头 × 18 扇区 × 512 字节；
  3. 实模式段寻址：`ES:BX` = 物理地址，`int $0x13` 用 `ES:BX` 指定缓冲区。
- **本课交付**：`build/b02.img`；QEMU 上先见 stage1 的两行提示，再见 stage2 的
  `B02 stage2: loaded at 0x7E00, two-stage boot OK`。

---

## 2. 核心概念精讲

### 2.1 概念一：为什么必须两段式

**定义**：引导扇区只有 512 字节（还要留 2 字节签名），放不下"读文件、找内核"的
逻辑。标准做法是：stage1（512B）只负责把 stage2 读入内存，真正的加载逻辑放在
stage2。

**为什么需要**：GRUB 的 core image 有几十 KB；Linux 的 boot sector 之后还有
`setup` 段。用 512 字节的"信使"把大代码块搬运到内存，是 x86 引导链的通用模式。

**工作机制**：

```text
BIOS → stage1(0x7C00, 512B) → int 0x13 读 stage2 → 0x7E00 → 远跳 → stage2
```

`0x7E00` = `0x7C00 + 512`：紧挨引导扇区之后，不会覆盖正在执行的 stage1 代码
（stage1 的栈放在 0x7C00 向下，读盘缓冲区从 0x7E00 向上，互不冲突）。

### 2.2 概念二：INT 13 读盘与 CHS 几何

**定义**：`int $0x13` AH=02 从软盘/硬盘读扇区。旧式接口用 CHS 寻址：
`CH=柱面`、`DH=磁头`、`CL=扇区`（1 基），`AL=扇区数`，`ES:BX=缓冲区`，`DL=盘号`；
返回时 `CF=1` 表示失败。

**为什么需要**：BIOS 固件只认 CHS/LBA，不认文件系统。引导代码必须先会用最底层的
"按扇区读盘"，之后才能在其上搭文件系统（B13 起）。

**工作机制**：本课把 0 基 LBA 换算成 CHS：

```asm
    xorw %dx, %dx
    movw $SPC, %cx          # SPC = 每柱面扇区数 = 18 * 2 = 36
    divw %cx                # AX = LBA/SPC = 柱面, DX = LBA%SPC
    movb %al, %ch           # 柱面(低 8 位) -> CH
    movw %dx, %ax
    xorw %dx, %dx
    movw $SPT, %cx          # SPT = 每磁道扇区数 = 18
    divw %cx                # AX = 磁头, DX = 道内扇区(0 基)
    movb %al, %dh           # 磁头 -> DH
    movb %dl, %cl           # 扇区(0 基) -> CL
    incb %cl                # INT 13 的扇区号是 1 基
    andb $0x3F, %cl         # CL 高 2 位是柱面高 2 位；1.44MB 软盘柱面<1024 恒为 0
```

换算公式：`柱面 = LBA / 36`；`磁头 = (LBA % 36) / 18`；`扇区 = (LBA % 18) + 1`。

**边界说明**：1.44 MB 软盘最大柱面 79，`CH` 单字节放得下；柱面高 2 位走 `CL`
的 bit 6–7——`andb $0x3F` 是防御性写法，柱面超过 1023 才需要真正拼接高位。

### 2.3 概念三：INT 13 的 `ES:BX` 缓冲区约定

**定义**：`int $0x13` 读出的数据写入 `ES:BX` 指向的物理内存。引导代码必须保证
`ES` 正确（本课 `ES=0`，`BX=0x7E00` → 物理 `0x7E00`）。

**为什么需要**：BIOS 用段:偏移返回数据，调用方必须显式提供；忘记设 `ES` 是引导
代码最常见的 bug 之一（读到 `0x0000:0x7E00` 和 `0x1000:0x7E00` 完全不同的物理地址）。

**工作机制**：

```asm
    movw $STAGE2_OFF, %bx   # 缓冲区起点（ES=0，物理 0x7E00）
    ...
    movb $0x02, %ah         # INT 13 AH=02：读扇区
    movb $1, %al            # 每次读 1 个扇区
    int $0x13
    jc read_fail            # CF=1 表示读盘失败
```

### 2.4 概念四：远跳 `ljmp` 与 CS 刷新

**定义**：`ljmp $0x0000, $STAGE2_OFF` 是段间跳转：同时修改 `CS` 与 `IP`。
本课跳到 `CS=0, IP=0x7E00`。

**为什么需要**：stage2 是按"段 0 + 偏移 0x7E00"链接的（`-Ttext 0x7E00`），只有
`CS=0` 才能让 `call puts`、`lodsb` 等按正确地址取代码/数据。B03 用 `ljmp` 刷新
`CS` 进入保护模式，是同一指令的另一个关键用途。

**工作机制**：

```asm
    ljmp $0x0000, $STAGE2_OFF   # 远跳：CS=0, IP=0x7E00 -> stage2
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B01） |
|---|---|---|
| `stage1.S` | 512 字节引导扇区：初始化、打印、INT 13 读 stage2、远跳 | 新增 CHS 换算与读盘循环 |
| `stage2.S` | 第二段代码（0x7E00）：打印确认消息并挂起 | 新增（B02 引入） |
| `Makefile` | 构建两个二进制、拼软盘镜像、check/run | 新增双目标构建 |
| `build/b02.img` | 1.44 MB 软盘镜像（LBA0=stage1，LBA1–8=stage2） | 新增 |

### 3.2 `stage1.S` 精讲

**常量**：

```asm
.set BOOT_ADDR,   0x7C00     # BIOS 把本扇区加载到 0x7C00
.set STAGE2_OFF,  0x7E00     # stage2 目标地址 = 0x7C00 + 512
.set STAGE2_SECT, 8          # stage2 占 8 个扇区（4096 字节）
.set SPT,         18         # 1.44MB 软盘：每磁道 18 扇区
.set SPH,         2          # 每柱面 2 磁头
.set SPC,         36         # 每柱面扇区数 = SPT * SPH
```

`STAGE2_SECT = 8` 是 stage1 与 Makefile 的**契约**：Makefile 把 stage2 补齐到
8 扇区，stage1 恰好读 8 扇区。改大 stage2 时必须同步两处（B04 README 会再次强调）。

**启动与盘号保存**：

```asm
    movb %dl, boot_drive    # 保存 BIOS 给的启动盘号（软盘为 0）
```

BIOS 进入引导扇区时 `DL` = 启动盘号；后续所有 `int $0x13` 都要用同一个盘号。

**读盘循环**：

```asm
    movw $STAGE2_OFF, %bx   # 缓冲区起点（ES=0，物理 0x7E00）
    movw $1, %di            # 起始 LBA（0 基）
    movw $STAGE2_SECT, %cx  # 剩余扇区数（也是 loop 计数器）
read_loop:
    pushw %cx               # 保存计数（lba_to_chs 会破坏 CX）
    pushw %di               # 保存当前 LBA
    movw %di, %ax
    call lba_to_chs         # 输入 AX=LBA，输出 CH=柱面 DH=磁头 CL=扇区
    movb boot_drive, %dl
    movb $0x02, %ah         # INT 13 AH=02：读扇区
    movb $1, %al            # 每次读 1 个扇区
    int $0x13
    jc read_fail            # CF=1 表示读盘失败
    popw %di
    popw %cx
    addw $512, %bx          # 缓冲区前进一个扇区
    incw %di                # 下一个 LBA
    loop read_loop
```

- 循环不变式：`BX` 指向下一扇区写入位置，`DI` 是下一个 LBA，`CX` 是剩余扇区数；
- `pushw %cx` / `pushw %di` 保护循环状态：`lba_to_chs` 和 `int $0x13` 都会破坏
  这些寄存器；失败时 `jc` 直接跳出，不恢复栈（`read_fail` 不返回，无妨）。

**错误路径**：

```asm
read_fail:
    movw $msg_fail, %si
    call puts
1:  hlt
    jmp 1b
```

打印 `B02 stage1: disk read failed!` 后挂起——保留现场便于观察，这是引导代码的
标准错误处理（对照 GRUB 的 `grub_error` 精神，B22 会系统化）。

### 3.3 `stage2.S` 精讲

入口 `stage2_entry` 打印确认消息后挂起。本课它只是"被正确读入并跳转"的证据：

```asm
stage2_entry:
    # DL = 启动盘号（stage1 原样传过来；本课未用到，B05 起需要）
    movw $msg_stage2, %si
    call puts
1:  hlt
    jmp 1b
```

输出串：

```asm
msg_stage2:
    .asciz "B02 stage2: loaded at 0x7E00, two-stage boot OK"
```

### 3.4 构建管线（Makefile）

```makefile
STAGE2_SECT := 8           # stage2 占 8 个扇区
STAGE2_BYTES := $(shell echo $$(( $(STAGE2_SECT) * 512 )))
```

1. stage1 链到 `0x7C00`（与 B01 相同，`--oformat binary`）；
2. stage2 链到 `0x7E00`：

```makefile
$(BUILD)/stage2.bin: $(BUILD)/stage2.o
	$(LD) -m elf_i386 -N -e stage2_entry -Ttext 0x7E00 --build-id=none \
	  --oformat binary -o $@ $<
	truncate -s $(STAGE2_BYTES) $@   # 补齐到 8 个扇区（stage1 按 8 扇区读入）
```

3. 拼镜像：LBA 0 放 stage1，`seek=1` 让 stage2 从 LBA 1 写入：

```makefile
$(BUILD)/b02.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/stage1.bin of=$@ conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc status=none
```

4. `make check` 断言：stage1 = 512 字节 + 0x55AA；stage2 = 4096 字节；
   `stage2_entry` 符号存在。

### 3.5 主控制流

```text
BIOS → 0x7C00 (stage1)
  → 初始化段/栈，保存 DL
  → int 0x10 打印 "B02 stage1: reading stage2 from floppy"
  → 循环 8 次：lba_to_chs → int 0x13 AH=02 读 1 扇区到 0x7E00+i*512
  → 打印 "B02 stage1: jumping to stage2"
  → ljmp 0x0000:0x7E00
  → stage2: int 0x10 打印 "B02 stage2: loaded at 0x7E00, two-stage boot OK"
  → hlt
```

---

## 4. 数据流与运行逻辑

```text
软盘 LBA 1..8（stage2.bin, 4096B）--int 0x13--> 物理 0x7E00..0x8DFF
    ↑ CHS 换算（柱面/磁头/扇区）          ↑ ES:BX = 0x0000:0x7E00
软盘 LBA 0（stage1, 512B）--BIOS--> 物理 0x7C00
```

VGA 上的三行（第 0–2 行）：`B02 stage1: reading stage2 from floppy`、
`B02 stage1: jumping to stage2`、`B02 stage2: loaded at 0x7E00, two-stage boot OK`。
自动化验证以 `B02 stage2` 为关键 marker（证明 stage2 到达并执行）。

---

## 5. 构建、运行与验证

### 5.1 依赖

`as`、`ld`、`dd`、`od`、`truncate`、`objdump`、`qemu-system-x86_64`。

### 5.2 命令

```bash
cd bootloader-course/lessons/b02-stable
make clean && make -j"$(nproc)"
make check
make run    # QEMU 窗口，VGA 可见三行输出
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b02 check
bootloader-course/scripts/validate-course.sh b02 qemu
```

### 5.3 期望输出

- `make check`：`B02 check PASS: stage1=512+55aa stage2=4096B entry=stage2_entry`
- QEMU VGA：上面三行消息（第 2 行含 `B02 stage2`）。

### 5.4 成功判据

stage1 读盘无 `CF` 错误（不打印 `disk read failed!`）、stage2 消息完整可见、
QEMU trace 无 `triple fault`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 只显示 `reading stage2`，然后 `disk read failed!` | CHS 换算或盘号错误 | 单步核对 `lba_to_chs` 输出：LBA 1 → CH=0 DH=0 CL=2 |
| stage2 消息不出现，无失败提示 | 远跳目标或链接地址错 | 确认 `-Ttext 0x7E00` 与 `ljmp` 目标一致；`readelf -s` 看 `stage2_entry` 地址 |
| 读到的是乱码 | `ES` 不是 0 或缓冲区地址错 | 确认 `ES=0`、`BX` 每轮 +512 |
| 只读回同一个扇区 | `DI` 未递增或 `BX` 未 +512 | 检查循环末尾 `incw %di` / `addw $512, %bx` |
| 镜像里找不到 stage2 | `dd seek=1` 没生效或顺序颠倒 | `xxd`/`od` 看 LBA 1 处的字节是否等于 stage2.bin 开头 |
| stage2 跑飞（异常） | 栈被覆盖或 `SP` 无效 | 确认 `SP=0x7C00` 且在循环内 `pushw`/`popw` 配对 |

---

## 7. 与 GNU GRUB 源码对照

本课对应 GRUB 的 `$GRUB_SRC/grub-core/boot/i386/pc/boot.S`（stage1）与
`diskboot.S`（读入 core image 的第一段）。对照点：

- **相同**：512 字节入口、保存 `DL` 盘号、用 INT 13 把大代码块读入内存、远跳到
  下一段。
- **简化**：GRUB 的 `boot.S` 要按介质（MBR/ISO/网络）读取固定偏移处的 core
  image，`diskboot.S` 支持块列表（block list）加载；本课固定读 LBA 1–8，且用
  CHS 单扇区循环，把"读盘"本身讲透。
- **下一步**：GRUB 的 `diskboot.S` 之后是 `startup_raw.S`/`startup.S` 的
  实模式 → 保护模式切换——正是 B03 的主题。

Linux 对照：`arch/x86/boot/` 下 `bootsect.S` + `setup.S` 也是两段式（512B 引导 +
`setup` 段），`setup.S` 的 `setup_sig` 校验与 0xAA55 签名思想同源；仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么 `STAGE2_OFF` 必须等于 `0x7C00 + 512`？如果 stage2 放到
   `0x9000`，stage1 里的什么假设会失效？
2. 源码定位：在 `$GRUB_SRC` 运行
   `grep -R "int.*\$0x13\|int \$0x13" grub-core/boot/i386/pc`，找出 GRUB 的
   stage1/diskboot 调用读盘中断的位置，对比其使用的寻址方式。
3. 动手实验（临时副本）：把 `STAGE2_SECT` 改成 4，`truncate` 后不重建 stage1，
   观察 stage2 是否被截断执行；解释 stage1 与 Makefile 的契约关系。
4. 动手观察：用 `od -An -tx1 -j512 -N16 build/b02.img` 检查 LBA 1 开头字节是否
   等于 `stage2.bin` 开头。
5. 综合：画一张表，列出 LBA 1..8 每个扇区对应的 CHS（柱面/磁头/扇区）与你推测的
   缓冲区物理地址，与 `lba_to_chs` 的运算逐一对照。

---

## 9. 本课小结与下一课预告

**小结**：本课让引导扇区真正"干活"：保存盘号、按 CHS 用 `int $0x13` 逐扇区读出
stage2、远跳执行。关键收获：(1) 512 字节装不下加载逻辑，两段式是 x86 引导的通用
结构；(2) `int $0x13` 用 `ES:BX` 传缓冲区、`CH/DH/CL` 传 CHS 几何，`CF` 报错；
(3) LBA 与 CHS 的换算必须精确；(4) 循环中受破坏的寄存器要 `push`/`pop` 保护；
(5) 链接地址（0x7E00）与加载地址的契约决定跳转成败。

**下一课预告**：进入 [`b03-stable/README.md`](../b03-stable/README.md)。stage2
现在有了足够的空间，下一步是打开 A20、装载 GDT、置 CR0.PE，把 CPU 从实模式带入
32 位保护模式——对照 GRUB `startup.S` 的切换序列；保护模式之后，INT 10 不再可用，
输出将改为直接写 `0xB8000`。

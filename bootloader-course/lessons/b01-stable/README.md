# Lesson B01: 512 字节引导扇区 Hello — 精讲文档

> **课号**：Lesson B01（Mini-GRUB 从零写 GRUB 课程第 1 课，可执行课）
> **主题**：第一个 512 字节引导扇区：BIOS 如何找到它、如何在实模式用 INT 10 打印
> **课程位置**：阶段一「实模式与保护模式引导链」第 1 课；位于 GRUB 源码研读支线
> （0.1–0.10）之后
> **前置课程**：研读支线 0.1（GRUB 源码树与启动产物）、0.7（BIOS 平台分支）；
> 能看懂 AT&T 汇编（TinyOS Lesson 01 `boot.S` 同款语法）
> **后续课程**：[`b02-stable/README.md`](../b02-stable/README.md)（两段式引导：
> stage1 读入 stage2）
> **一句话目标**：写一个真正能被 BIOS 加载、在 QEMU 上可见打印的 512 字节引导扇区，
> 并理解「BIOS → 0x7C00 → INT 10 → VGA 文本」这条最小引导链。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能独立写出一个 512 字节、带 0xAA55 签名、在实模式用
BIOS 中断打印字符串的引导扇区，并用 `make check` 与 QEMU 验证它的每个字节。

- **在课程中的位置**：这是 Mini-GRUB 的第一课。研读支线告诉你 GRUB 的第一段是
  `boot.img`（512 字节引导扇区，`grub-core/boot/i386/pc/boot.S`）；本课把它真正
  写出来。后续 B02 会让它读入第二段（对应 GRUB 的 `diskboot.S`），B03 进入保护
  模式——本课的实模式基础是所有后续步骤的地基。
- **前置知识清单**：
  1. 研读支线 0.1：`boot.img`/`eltorito.img` 在启动链中的位置；
  2. 研读支线 0.7：BIOS 启动顺序与 El Torito 记录（本课用软盘路径简化）；
  3. AT&T 汇编基础：`.code16`、`movw`、`lodsb`、`int` 指令与 `$`/`%` 前缀约定；
  4. 实模式内存模型：段:偏移 = 物理地址 = 段<<4 + 偏移。
- **本课交付**：`build/b01.img`（1.44 MB 软盘镜像，LBA 0 是 512 字节引导扇区），
  QEMU 启动后在 VGA 上可见字符串 `B01 Mini-GRUB stage1: 512-byte boot sector OK`。

---

## 2. 核心概念精讲

### 2.1 概念一：BIOS 如何找到引导扇区（0x7C00 与 0xAA55）

**定义**：x86 加电后，BIOS 初始化硬件，然后按启动顺序（软盘/光盘/硬盘）读取
**第一个扇区**（512 字节）到物理地址 `0x7C00`，检查最后两个字节是否为
`0x55 0xAA`，是则跳转 `0x0000:0x7C00` 执行；否则认为介质不可引导。

**为什么需要**：CPU 没有"文件系统"概念，BIOS 也不知道什么是"可执行文件"。约定一个
固定地址 + 固定签名，是固件与引导代码之间唯一的握手协议。

**工作机制**（QEMU 的 SeaBIOS 实测路径）：

```text
QEMU -boot order=a → SeaBIOS → 读软盘 LBA 0 共 512 字节 → 物理 0x7C00
  → 检查偏移 510/511 == 0x55 0xAA → jmp 0x0000:0x7C00
```

**为什么是 0x7C00**：这是 IBM PC 时代的约定：地址必须避开中断向量表（0–0x400）、
BIOS 数据区（0x400–0x500），又必须低到能被 BIOS 的读盘代码（约 0x8000 起）覆盖不到
的地方。课程 `boot.S` 用常量记录这一约定：

```asm
.set BOOT_ADDR, 0x7C00      # BIOS 约定：引导扇区加载地址
```

### 2.2 概念二：实模式与段寄存器

**定义**：实模式（16 位）下，CPU 用 `段:偏移` 计算物理地址：`物理地址 = 段 << 4 + 偏移`。
段寄存器 `CS/DS/ES/SS` 各 16 位，偏移 16 位，因此可访问 1 MiB 空间（20 位地址）。

**为什么需要**：BIOS 启动引导扇区时，CPU 处于实模式，段寄存器的值不可假设——必须
自己初始化。本课把 `DS/ES/SS` 清零，让所有内存访问都按 `0x0000:offset` 解释：

```asm
    xorw %ax, %ax
    movw %ax, %ds           # DS = 0，所有地址按 0x0000:offset 解释
    movw %ax, %es           # ES = 0
    movw %ax, %ss           # SS = 0（栈段）
```

**工作机制**：`movw %ax, %ds` 之后，`lodsb`（从 `DS:SI` 取字节）访问的就是
`0x0000:SI` = 物理 `SI`，而 SI 由 `movw $msg_hello, %si` 载入为链接时的绝对地址
（见 3.2 的链接说明）。

### 2.3 概念三：BIOS 中断与 INT 10 视频服务

**定义**：实模式下，`int $0x10` 调用 BIOS 视频中断；通过寄存器传参，功能号在 `AH`。
`AH=0x0E` 是 teletype 输出：在光标处打印一个字符并推进光标。

**为什么需要**：引导扇区没有任何"库"可用，VGA 文本内存（`0xB8000`）的写法和 BIOS
服务是仅有的两种输出手段。B01 用 BIOS 服务（简单、自动处理光标）；B03 进入保护
模式后 BIOS 中断不可用，将改为直接写 `0xB8000`。

**工作机制**：一个字符 = 一次 `int $0x10`：

```asm
    movb $0x0E, %ah         # INT 10 功能号 0x0E：teletype 输出
    movb $0x00, %bh         # 页号 0
    int $0x10               # BIOS 视频服务
```

### 2.4 概念四：512 字节与扇区大小约束

**定义**：引导扇区必须恰好 512 字节，且第 510/511 字节是签名。超过即失败。

**为什么需要**：BIOS 只读一个扇区、只校验签名。如果代码超过 510 字节，签名位置就
被挤走，介质不可引导。

**工作机制**：课程用 `.fill` 补齐，若代码超长，`.fill` 的填充数为负，汇编器直接
报错——把"扇区大小检查"内置进了汇编阶段：

```asm
    .fill 510 - (. - _start), 1, 0    # 若代码超过 510 字节，此处会报错
    .word 0xAA55
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `boot.S` | 512 字节引导扇区：初始化段、INT 10 打印、挂起 | 全部（首课） |
| `Makefile` | 汇编→链接→转二进制→拼软盘镜像；`check`/`run` 目标 | 全部（首课） |
| `build/b01.img` | 1.44 MB 软盘镜像（LBA 0 = 引导扇区） | 全部（首课） |

### 3.2 `boot.S` 精讲

**结构/常量**：

```asm
.code16                 # 实模式代码（16 位）
.set BOOT_ADDR, 0x7C00  # BIOS 约定：引导扇区加载地址
.globl _start           # 导出入口，供链接脚本/链接器定位
```

**入口 `_start`**：

```asm
_start:
    cli                     # 关中断：段寄存器设置期间不允许被打断
    xorw %ax, %ax
    movw %ax, %ds           # DS = 0，所有地址按 0x0000:offset 解释
    movw %ax, %es           # ES = 0
    movw %ax, %ss           # SS = 0（栈段）
    movw $BOOT_ADDR, %sp    # 栈顶 = 0x7C00，向下增长，避开 IVT/BDA
    sti
```

- `cli`/`sti`：设置段寄存器期间 CPU 状态不完整，若有中断进来会用到未知的 `SS:SP`，
  因此先关中断。`SS` 与 `SP` 必须紧邻赋值（处理器会在 `MOV SS` 后自动屏蔽中断到
  下一条指令），这里 `cli` 更彻底。
- 栈顶放在 `0x7C00`：向下增长。`call puts`/`ret` 只压入几个返回地址，栈最深不过
  几十字节，落在 `0x500–0x7C00` 的自由常规内存区，安全。

**打印调用**：

```asm
    movw $msg_hello, %si    # DS:SI -> 待打印字符串
    call puts
```

`$msg_hello` 是链接器算出的绝对地址（`0x7C00 + 偏移`，见 3.3），运行时 DS=0，因此
`DS:SI` 恰好指向物理内存中加载引导扇区的位置。

**`puts` 函数**：

```asm
puts:
    lodsb                   # AL = DS:[SI]，SI += 1
    testb %al, %al
    jz 2f                   # 遇到 '\0' 结束
    movb $0x0E, %ah         # INT 10 功能号 0x0E：teletype 输出
    movb $0x00, %bh         # 页号 0
    int $0x10               # BIOS 视频服务
    jmp puts
2:  ret
```

- 循环不变式：`SI` 指向下一个待打印字符；`AL == 0` 时结束。
- `lodsb` 同时完成"取字节 + 指针前进"，是字符串处理的惯用指令。
- 输出串：

```asm
msg_hello:
    .asciz "B01 Mini-GRUB stage1: 512-byte boot sector OK"
```

**签名区**：

```asm
    .fill 510 - (. - _start), 1, 0    # 若代码超过 510 字节，此处会报错
    .word 0xAA55
```

- `. - _start` 是当前偏移（相对入口）；`510 - 偏移` 是要补的零字节数；代码超过
  510 字节时该数为负 → 汇编错误。
- `.word 0xAA55` 按小端写入 `55 AA`，正是 BIOS 检查的字节序。

**挂起循环**：

```asm
1:  hlt                     # 打印完成，挂起（没有操作系统可以"返回"）
    jmp 1b
```

引导代码"返回"无处可去（没有调用者），打印完成后挂起是标准做法；`1b` 引用最近的
前向标号 `1`。

### 3.3 构建管线（Makefile）

```makefile
AS := as
LD := ld
BUILD := build
SECTORS := 2880            # 1.44 MB 软盘 = 80 柱面 * 2 磁头 * 18 扇区
```

1. **汇编**：`as --32` 把 `boot.S` 汇编成 32 位 ELF 目标文件（`.code16` 只是编码
   模式，目标文件仍是 32 位 ELF 容器）：

```makefile
$(BUILD)/boot.o: boot.S | $(BUILD)
	$(AS) --32 -o $@ $<
```

2. **链接并直接输出二进制**：

```makefile
$(BUILD)/boot.bin: $(BUILD)/boot.o
	$(LD) -m elf_i386 -N -e _start -Ttext 0x7C00 --build-id=none \
	  --oformat binary -o $@ $<
```

   - `-Ttext 0x7C00`：代码段 VMA 从 0x7C00 开始，`$msg_hello` 等符号算成
     `0x7C00 + 偏移`——与 BIOS 实际加载地址一致；
   - `-N`（omagic）：不做页对齐、合并可读写段，适合平坦二进制；
   - `--oformat binary`：ld 直接吐二进制，跳过 objcopy。

3. **拼软盘镜像**：

```makefile
$(BUILD)/b01.img: $(BUILD)/boot.bin
	dd if=/dev/zero of=$@ bs=512 count=$(SECTORS) status=none
	dd if=$(BUILD)/boot.bin of=$@ conv=notrunc status=none
```

   先建 1.44 MB 全零镜像，再把引导扇区写进 LBA 0（`conv=notrunc` 不截断）。

4. **`make check`**：静态断言扇区大小与签名：

```makefile
	@test "$$(stat -c%s $(BUILD)/boot.bin)" -eq 512 \
	  || { printf 'FAIL: boot.bin size != 512\n' >&2; exit 1; }
	@test "$$(od -An -tx1 -j510 -N2 $(BUILD)/boot.bin | tr -d ' \n')" = "55aa" \
	  || { printf 'FAIL: missing 0xAA55 signature\n' >&2; exit 1; }
	@printf 'B01 check PASS: size=512 signature=55aa\n'
```

5. **`make run`**：QEMU 从软盘启动，打开窗口可见输出：

```makefile
run: $(BUILD)/b01.img
	qemu-system-x86_64 -accel tcg -boot order=a \
	  -drive file=$(BUILD)/b01.img,format=raw,if=floppy \
	  -no-reboot -no-shutdown
```

### 3.4 主控制流

```text
BIOS（SeaBIOS）读 LBA 0 到 0x7C00，验签名
  → jmp 0x0000:0x7C00
  → _start: cli；DS=ES=SS=0；SP=0x7C00；sti
  → mov si, msg_hello; call puts
  → puts: lodsb → INT 10 AH=0x0E 逐字符 → '\0' 时 ret
  → hlt 挂起
```

---

## 4. 数据流与运行逻辑

```text
磁盘(软盘 LBA 0, 512B) → BIOS 校验 0x55AA → 物理 0x7C00
  → 段寄存器初始化(DS=ES=SS=0) → DS:SI = 0x7C00+偏移
  → int $0x10 AH=0x0E, AL=字符 → VGA 文本内存 0xB8000 显示
  → 最终画面首行：B01 Mini-GRUB stage1: 512-byte boot sector OK
```

BIOS 中断的字符最终落到 VGA 文本缓冲区（`0xB8000` 起，每字符 2 字节：ASCII + 属性）。
自动化验证正是 `pmemsave 753664 4000`（`0xB8000` 起 4000 字节）后按偶数偏移读
ASCII 字符、grep `B01 Mini-GRUB stage1`。

---

## 5. 构建、运行与验证

### 5.1 依赖

`as`、`ld`、`dd`、`od`、`stat`、`qemu-system-x86_64`（GNU Binutils + QEMU，与
TinyOS 主线一致）。

### 5.2 命令

```bash
cd bootloader-course/lessons/b01-stable
make clean && make -j"$(nproc)"
make check
make run    # 打开 QEMU 窗口，可见 VGA 文本输出
```

自动化验证（脚本拷贝到临时目录，不改写提交的 build/）：

```bash
bootloader-course/scripts/validate-course.sh b01 check
bootloader-course/scripts/validate-course.sh b01 qemu
```

### 5.3 期望输出

- `make check` 输出：`B01 check PASS: size=512 signature=55aa`
- QEMU 画面（SeaBIOS 信息之后）：`B01 Mini-GRUB stage1: 512-byte boot sector OK`
- `make` 后 `build/boot.bin` 恰好 512 字节；`od -An -tx1 -j510 -N2` 输出 `55 aa`

### 5.4 成功判据

引导扇区被 BIOS 接受（不显示 "No bootable device"）、字符串完整出现在 VGA 文本区、
QEMU trace 无 `triple fault`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| QEMU 显示 "No bootable device" | 签名缺失或扇区不在 LBA 0 | `od -An -tx1 -j510 -N2 build/boot.bin` 必须是 `55 aa` |
| `make check` 报 size != 512 | `.fill` 未生效或链接地址错 | 确认 `-Ttext 0x7C00`；确认 `.fill` 表达式 `510 - (. - _start)` |
| 汇编报 "negative count" | 代码超过 510 字节 | 精简代码或检查是否有意外的大段数据 |
| 打印乱码 | 段寄存器未初始化或 SI 地址错 | 确认 `DS=0` 且链接 VMA 是 0x7C00（`readelf -h build/boot.o` 对照） |
| 画面空白但有 SeaBIOS | 代码执行了但 INT 10 无效 | `cli`/`sti` 配对检查；`AH=0x0E`、`BH=0` 传参是否齐全 |
| 打印了但立刻重启 | 返回地址被破坏 | 检查栈：`SP` 必须指向有效内存；`hlt` 循环不可被 `ret` 越过 |

---

## 7. 与 GNU GRUB 源码对照

本课的真正参照是 GRUB 的 `$GRUB_SRC/grub-core/boot/i386/pc/boot.S`（研读支线 0.1、
0.7 已定位）。对照点：

- **相同**：都是 512 字节、`0x55AA` 签名、实模式入口、链接到 0x7C00 附近、用
  BIOS 中断做第一件可见的事。
- **简化**：GRUB 的 `boot.S` 要定位并读出 core image（交给 `diskboot.S` 完成
  大部分读盘工作），还要兼容多种介质；本课只做打印。读盘是 B02 的主题。
- **为什么分步**：先建立「引导扇区 + 中断 + 链接地址」的最小闭环，再加读盘、加
  保护模式，符合研读支线 0.1 给出的「boot.img → core image → modules」分层。

Linux 侧工程对照：`arch/x86/boot/bootsect.S` 同样是从实模式开始的引导代码，可作
参照阅读，但不能当作 GRUB 的实现来源（证据边界见
[`docs/grub-implementation-guide.md`](../../docs/grub-implementation-guide.md)）。

---

## 8. 思考题与练习

1. 概念理解：为什么引导扇区必须放在 LBA 0？如果把 `dd` 的目标偏移改成 1 扇区，
   QEMU 会怎样？
2. 源码定位：在 `$GRUB_SRC` 运行
   `grep -R "ljmp" grub-core/boot/i386/pc/boot.S`，看看 GRUB 引导扇区跳转
   core image 的方式与 B02 有何异同。
3. 动手观察：`readelf -h build/boot.o` 与 `ld --oformat binary` 前后的符号地址，
   验证 `msg_hello` 确实落在 `0x7C00 + 偏移`。
4. 动手实验（临时副本）：把 `boot.S` 的 `.asciz` 字符串改长到 510 字节以上，
   观察 `.fill` 的报错信息；再改回。
5. 综合：画出「BIOS → 0x7C00 → INT 10 → 0xB8000」的完整数据流，标注每一步的
   地址与寄存器。

---

## 9. 本课小结与下一课预告

**小结**：本课写出了 Mini-GRUB 的第一块砖——512 字节引导扇区。关键收获：
(1) BIOS 通过固定地址 0x7C00 与固定签名 0x55AA 找到并执行引导代码；
(2) 实模式下必须自建段寄存器环境，中断调用通过寄存器传参；
(3) 链接地址（0x7C00）必须与加载地址一致，符号才能正确寻址；
(4) 512 字节约束可以靠 `.fill` 在汇编期强制。`make check` 的「大小 + 签名」断言
和 QEMU 的 VGA 文本校验构成了本课程每一课的验证基线。

**下一课预告**：进入 [`b02-stable/README.md`](../b02-stable/README.md)，引导扇区
只有 512 字节，装不下真正的加载逻辑——用 `int $0x13` 从软盘读出第二段代码到
`0x7E00` 并跳转过去，这就是 GRUB `boot.S` → `diskboot.S` 的两段式结构。

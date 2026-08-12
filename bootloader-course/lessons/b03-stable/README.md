# Lesson B03: 保护模式切换 — 精讲文档

> **课号**：Lesson B03（Mini-GRUB 从零写 GRUB 课程第 3 课，可执行课）
> **主题**：实模式 → 32 位保护模式：A20、GDT、CR0.PE，以及保护模式下直接写 VGA
> **课程位置**：阶段一「实模式与保护模式引导链」第 3 课
> **前置课程**：[`b02-stable/README.md`](../b02-stable/README.md)（两段式引导）；
> 研读支线 0.7（GRUB `startup.S` 的切换顺序）
> **后续课程**：[`b04-stable/README.md`](../b04-stable/README.md)（保护模式 C 运行时）
> **一句话目标**：把 CPU 从实模式安全地带入 32 位保护模式（A20 → GDT → CR0.PE →
> 远跳），并理解保护模式下为什么必须直接写 `0xB8000` 而不是调用 INT 10。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能写出 A20/GDT/CR0.PE 三步切换代码，在保护模式下用
平坦分段直接访问 `0xB8000` 输出，并解释每条 GDT 描述符字节的含义。

- **在课程中的位置**：B02 让 stage2 有了空间；本课让 stage2 摆脱实模式的 1 MiB
  限制和 BIOS 中断依赖，进入 32 位世界。这是后续一切（C 运行时、ELF 装载、MBI）
  的运行环境。对应 GRUB 的 `grub-core/kern/i386/pc/startup.S`。
- **前置知识清单**：
  1. B01/B02：实模式段寻址、`ljmp` 远跳、VGA 文本模式 0xB8000；
  2. 段描述符：GDT 条目 8 字节（base/limit/access/flags）的位布局；
  3. 保护模式概念：选择子、平坦分段、`CR0.PE` 位。
- **本课交付**：`build/b03.img`；QEMU 上第 0 行是实模式 INT 10 输出，第 1 行是
  保护模式直接写 VGA 的输出——同屏对照两种输出方式。

---

## 2. 核心概念精讲

### 2.1 概念一：A20 地址线

**定义**：80286+ 的地址线有 20+ 位，但实模式"段:偏移"算出的地址可能超过 1 MiB；
A20 地址线决定第 20 位是否生效。关闭时，`0x100000` 以上的地址会回卷到
`0x000000`（`0xFFFFF` 之后的环绕）。

**为什么需要**：保护模式用 32 位地址，必须让 A20 生效，否则高地址访问会错误回卷。

**工作机制**（QEMU/SeaBIOS 实测）：BIOS 的 INT 15 AX=2401 即可使能；再补
"fast A20"（端口 0x92 置 bit 1）作为兜底：

```asm
    movw $0x2401, %ax
    int $0x15                # INT 15 AX=2401：使能 A20
    inb $0x92, %al           # fallback：fast A20（端口 0x92 置位）
    orb $0x02, %al
    outb %al, $0x92
```

### 2.2 概念二：GDT 与平坦分段

**定义**：全局描述符表（GDT）是保护模式的分段依据：每个条目（描述符）8 字节，
定义一段内存的 base（32 位）、limit（20 位）、access 与 flags 位。

**为什么需要**：实模式的段寄存器只存 16 位段基址（隐含 <<4），保护模式下段寄存器
存的是 16 位**选择子**——指向 GDT 里的描述符。引导器最常用的布局是**平坦分段**
（base 0、limit 4G）：所有线性地址 == 物理地址，代码/数据段覆盖整个 4G。

**工作机制**（本课的 GDT 布局）：

```asm
gdt:
    .long 0, 0               # 0x00 null 描述符（必须全零）
gdt_code32:
    .word 0xFFFF             # 0x08 code32：limit 15:0
    .word 0x0000             # base 15:0
    .byte 0x00               # base 23:16
    .byte 0x9A               # P=1 DPL=0 S=1 type=0xA(exec/read)
    .byte 0xCF               # G=1 D=1 limit 19:16 = 0xF -> 4G
    .byte 0x00               # base 31:24
gdt_data32:
    .word 0xFFFF             # 0x10 data32：base 0 limit 4G
    .word 0x0000
    .byte 0x00
    .byte 0x92               # type=0x2(data read/write)
    .byte 0xCF
    .byte 0x00
gdt_code16:
    .word 0xFFFF             # 0x18 code16：base 0 limit 4G
    .word 0x0000
    .byte 0x00
    .byte 0x9A
    .byte 0x0F               # G=1 D=0（16 位）
    .byte 0x00
gdt_end:
```

三个描述符的 access 字节逐位含义（以 code32 的 `0x9A` = `1001 1010` 为例）：
`P=1`（存在）、`DPL=00`（ring 0）、`S=1`（代码/数据段）、`type=1010`
（`0xA` = 执行/读）；data32 的 `0x92` type=`0010`（读/写）。flags 字节 `0xCF` =
`1100 1111`：`G=1`（limit 单位 4 KiB）、`D=1`（32 位操作数）；code16 的
`0x0F` 使 `D=0`（16 位，B05 实模式回调用）。

`gdtr` 记录 GDT 大小与**线性基址**：

```asm
gdtr:
    .word gdt_end - gdt - 1  # GDT 大小 - 1
    .long gdt                # GDT 线性地址（链接在 0x7E00，基址 0）
```

`gdt` 符号 = `0x7E00 + 偏移`；切换后基址 0 的平坦段下，线性地址 = 物理地址，
GDTR 里的基址在实模式与保护模式下都正确。

### 2.3 概念三：CR0.PE 与远跳刷新

**定义**：`CR0` 的 bit 0（PE）置 1 开启保护模式。但指令流水线里还有按实模式
译码的指令，必须用**远跳**（`ljmp`）让 CPU 用新 `CS` 重新取指。

**为什么需要**：`movl %eax, %cr0` 之后 CPU 立即按保护模式解释，但 `CS` 还是实
模式的值（0x0000 不是合法选择子）。远跳到 code32 选择子（0x08）同时刷新 `CS`
并跳到 32 位代码段。

**工作机制**：

```asm
    movl %cr0, %eax
    orl $0x1, %eax           # PE = 1：进入保护模式
    movl %eax, %cr0
    ljmp $CODE32_SEL, $pm32 # 远跳：刷新 CS 为 code32，同时跳到 32 位代码
```

`$pm32` 是 `.code32` 代码段的标签；由于 stage2 链接在 0x7E00 且 code32 段基址
0，`pm32` 的线性地址 = 物理地址 = 代码实际所在位置，取指正确。

### 2.4 概念四：保护模式下直接写 VGA

**定义**：保护模式（以及后续长模式）下 BIOS 中断不可用，输出只能直接写 VGA 文本
内存：`0xB8000` 起，每字符 2 字节（低字节 ASCII，高字节属性）。

**为什么需要**：`int $0x10` 依赖 BIOS 中断门（实模式）；进入保护模式后没有 IDT、
BIOS 服务不可达。直接写内存是唯一可靠输出（TinyOS 主线 Lesson 01 之后的课程也是
这么做的）。

**工作机制**：

```asm
    movl $0xB8000 + 160, %edi   # 第 1 行：偏移 = 80 字符 * 2 字节
    movl $msg_pm, %esi
1:  lodsb
    testb %al, %al
    jz 2f
    movb %al, (%edi)            # 低字节 = ASCII
    movb $0x1F, 1(%edi)         # 高字节 = 属性（0x1F 白字蓝底）
    addl $2, %edi
    jmp 1b
2:  hlt
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B02） |
|---|---|---|
| `stage1.S` | 512 字节引导扇区（与 B02 相同，读 stage2） | 未变化 |
| `stage2.S` | A20、GDT、CR0.PE、保护模式直接 VGA 输出 | 全部重写（B03 核心） |
| `Makefile` | 与 B02 相同结构；check 增加 `pm32` 符号断言 | 微调 |
| `build/b03.img` | 软盘镜像 | 新增 |

### 3.2 `stage2.S` 精讲

**入口（实模式阶段）**：初始化段/栈、INT 10 打印实模式消息（`msg_real`），随后
依次执行 A20（见 2.1）、`lgdt gdtr`、`CR0.PE` + `ljmp`（见 2.3）。

**保护模式入口 `pm32`**：

```asm
.code32
pm32:
    movw $DATA32_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $STACK_TOP, %esp    # 常规内存高端的栈（向下增长）
```

- `ljmp` 之后 `CS` 已是 code32（0x08），其余段（DS/ES/FS/GS/SS）仍是实模式遗留
  值，必须全部重载为 data32（0x10）；`SS` 重载后 `ESP=0x00090000` 才生效；
- 栈顶选 `0x90000`：低于 EBDA（约 0x9FC00），处于常规内存自由区，且远离
  0x7C00/0x7E00 的代码区。

**消息常量**：

```asm
msg_real:
    .asciz "B03 stage2: real mode, enabling A20/GDT/CR0.PE"

msg_pm:
    .asciz "B03 Mini-GRUB in 32-bit protected mode"
```

### 3.3 构建管线

与 B02 完全同构：stage1 链 0x7C00、stage2 链 0x7E00（`--oformat binary`）、
`truncate` 补 8 扇区、`dd seek=1` 拼镜像。`make check` 新增 `pm32` 符号断言：

```makefile
	@objdump -t $(BUILD)/stage2.o | grep -q "pm32" \
	  || { printf 'FAIL: pm32 (protected-mode entry) symbol missing\n' >&2; exit 1; }
```

### 3.4 主控制流

```text
BIOS → stage1 → 0x7E00 (stage2, 实模式)
  → 打印 "B03 stage2: real mode, enabling A20/GDT/CR0.PE"
  → A20（INT 15 2401 + fast A20）
  → lgdt gdtr
  → CR0.PE = 1 → ljmp 0x08:pm32
  → 重载 DS/ES/FS/GS/SS = 0x10, ESP = 0x90000
  → 直接写 0xB8000 第 1 行："B03 Mini-GRUB in 32-bit protected mode"
  → hlt
```

---

## 4. 数据流与运行逻辑

```text
实模式: int 0x10 (BIOS) ──> 0xB8000 第 0 行（SeaBIOS/自己打印的提示）
        ↑ 中断服务（需要 IDT/BIOS）
保护模式: movb %al,(%edi) ──> 0xB8000 第 1 行（直接内存写）
        ↑ 平坦分段 DS=0x10, 线性地址 == 物理地址
```

`make check` 与自动化验证以 `B03 Mini-GRUB in 32-bit protected mode` 为关键
marker——它只可能来自保护模式的直接内存写，证明切换成功。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b03-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b03 check
bootloader-course/scripts/validate-course.sh b03 qemu
```

### 5.2 期望输出

- `make check`：`B03 check PASS: stage1=512+55aa stage2=4096B entries=stage2_entry/pm32`
- QEMU 第 0 行：`B03 stage2: real mode, enabling A20/GDT/CR0.PE`
- QEMU 第 1 行（白字蓝底）：`B03 Mini-GRUB in 32-bit protected mode`

### 5.3 成功判据

实模式消息出现（切换前正常）、保护模式消息出现（切换成功）、无 triple fault。
若出现 triple fault，多半是 GDT 描述符或选择子错位（见调试地图）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 实模式消息后 QEMU 重启/triple fault | 选择子错误或 GDT 描述符位错 | 检查 `ljmp $0x08` 与 code32 描述符位置；`0x9A/0x92/0xCF` 逐字节核对 |
| 画面保持实模式消息不动 | 远跳未执行或目标地址错 | 确认 `$pm32` 标签与链接地址；`readelf -s` 查 `pm32` 地址是否在 stage2 范围内 |
| 保护模式消息是乱码 | 直接写 VGA 时字节/属性写反 | `movb %al,(%edi)` 后必须 `movb $0x1F,1(%edi)`；确认 `(%edi)` 是 byte 写 |
| `movl %cr0,%eax` 汇编报错 | 代码还在 `.code16` 段 | 确认 `ljmp` 之后才是 `.code32`；`CR0` 访问必须在 32 位代码 |
| 栈操作异常 | SS/ESP 顺序或值错 | 先重载 SS 再设 ESP；`STACK_TOP` 避开 0x9FC00 EBDA |
| 消息在第一行但属性异常 | 属性字节写错 | 期望 `0x1F`（白字蓝底）；检查是否有字节序假设错误 |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/kern/i386/pc/startup.S`（GRUB core image 的
实模式 → 保护模式切换）以及 `startup_raw.S`（relocate 前的原始入口）。对照点：

- **相同**：A20 使能、GDT 装载、`CR0.PE` 置位、远跳刷新 CS、重载段寄存器；
- **简化**：GRUB 的 startup 会先把 core image 重定位到常规内存顶端、建立临时
  GDT/IDT、并保留实模式回调代码（`prot_to_real`/`real_to_prot`，B05 复刻）；
  本课只做最小切换，GDT 直接写在 stage2 内；
- **顺序一致**：先 A20、再 GDT、再 CR0.PE——切换顺序是经验教训的结晶，不要随意
  调换。

Linux 对照：`arch/x86/boot/pmjump.S` 与 `setup.S` 的 `go_to_protected_mode`
（`movl %cr0,%eax; orl $X86_CR0_PE,%eax; movl %eax,%cr0` + 远跳）与本课 CR0 序列
同构；仅作工程对照，不作为 GRUB 实现来源。

---

## 8. 思考题与练习

1. 概念理解：为什么 `ljmp` 是保护模式切换中不可省略的一步？不用远跳会发生什么？
2. 动手实验（临时副本）：把 `ljmp $CODE32_SEL` 改成 `ljmp $DATA32_SEL`，观察
   现象并解释（提示：DS 段不允许执行）。
3. 动手观察：用 `objdump -d -Mi386` 反汇编 `build/stage2.bin`，找到 `ljmp`
   指令的字节编码，确认选择子 0x08 与 `pm32` 偏移。
4. 源码定位：在 `$GRUB_SRC` 运行 `grep -R "cr0" grub-core/kern/i386/pc/startup.S`，
   对比 GRUB 置 `CR0.PE` 的写法与本课差异。
5. 综合：画出 GDT 三条描述符的 64 位布局（标出 base/limit/access/flags 各字段），
   并与本课 `.byte` 值逐一对应。

---

## 9. 本课小结与下一课预告

**小结**：本课完成了实模式 → 保护模式的完整切换。关键收获：(1) A20 保证 32 位
地址不回卷；(2) GDT 是保护模式的分段依据，平坦分段让线性地址 == 物理地址；
(3) `CR0.PE` 置位后必须远跳刷新 `CS`；(4) 进入保护模式后 BIOS 中断不可用，输出
改为直接写 `0xB8000`；(5) 切换顺序（A20 → GDT → PE）是工程经验，不可颠倒。
`make check` 对 `pm32` 符号与 VGA marker 的断言，验证了"真的在保护模式运行"。

**下一课预告**：进入 [`b04-stable/README.md`](../b04-stable/README.md)。保护模式
下有了平坦 4G 空间与直接内存写能力，是时候把 loader 主体从汇编迁到 C——编写 VGA
文本库（`vga_clear/vga_puts/vga_hex`），由 `loader_main()` 接管，对应 GRUB
`grub_main` 的初始化序。

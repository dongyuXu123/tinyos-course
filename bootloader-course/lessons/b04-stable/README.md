# Lesson B04: 保护模式 C 运行时与 VGA 文本库 — 精讲文档

> **课号**：Lesson B04（Mini-GRUB 从零写 GRUB 课程第 4 课，可执行课）
> **主题**：把 loader 主体迁到 C：无 libc 的 freestanding C 运行时 + 直接 VGA 文本库
> **课程位置**：阶段一「实模式与保护模式引导链」第 4 课
> **前置课程**：[`b03-stable/README.md`](../b03-stable/README.md)（保护模式切换）；
> 研读支线 0.1（`grub-core/kern/` 是"核心运行时"）
> **后续课程**：[`b05-stable/README.md`](../b05-stable/README.md)（实模式回调磁盘读）
> **一句话目标**：用 freestanding C 重写 loader 主体，提供 `vga_clear/vga_puts/
> vga_hex` 输出库，理解"无 libc、直接写内存"的引导代码 C 编程范式。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能把汇编写好的 loader 骨架与 C 写的 `loader_main()`
链接成一个平坦二进制，用自己写的 VGA 库输出多行文本与十六进制数值。

- **在课程中的位置**：B01–B03 的引导链全部是汇编。从本课起，loader 的主体用 C
  编写——后续的 ELF 解析、MBI 构建、文件系统都建立在"C 运行时 + VGA 库"之上。
  这对应 GRUB 中 `grub-core/kern/main.c`（`grub_main`）把控制交给 C 层初始化。
- **前置知识清单**：
  1. B03：保护模式、平坦分段、直接写 `0xB8000`；
  2. C 语言指针与类型转换（`volatile`、`u16`/`u32`）；
  3. 链接脚本基础：`SECTIONS`、`.` 位置计数器、`ENTRY`。
- **本课交付**：`build/b04.img`；QEMU 上四行输出：banner、内存布局常量、
  `vga text base = 000b8000`（hex 输出）、返回提示——全部由 C 打印。

---

## 2. 核心概念精讲

### 2.1 概念一：freestanding C 与无 libc 约束

**定义**：`-ffreestanding` 告诉编译器不假设宿主运行库存在：没有 `printf/malloc/
memcpy`，头文件只有 freestanding 子集。引导代码必须自给自足。

**为什么需要**：loader 运行在裸金属上，任何 libc 调用都会链接失败（或引入运行时
依赖）。TinyOS 主线同样以无 libc 为铁律（根 README："无 libc（无 printf/malloc/
memcpy/memset）"）。

**工作机制**（Makefile 编译参数，与 TinyOS 主线 flags 一致）：

```makefile
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector \
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Os
```

- `-fno-pie`：关闭 PIE，代码用绝对地址（裸机无重定位器）；
- `-fno-stack-protector`：栈保护需要 libc 支持；
- `-fno-asynchronous-unwind-tables`：去掉 `.eh_frame` 调试展开表；
- `-Werror`：把警告当错误，强制写出严格可移植的 freestanding 代码。

### 2.2 概念二：直接写 VGA 文本内存的 C 封装

**定义**：VGA 彩色文本缓冲区在物理 `0xB8000`，`80×25` 个单元，每单元 2 字节：
低字节 ASCII、高字节属性。C 里用一个 `volatile` 指针访问，避免编译器把写入优化掉。

**为什么需要**：`volatile` 告诉编译器"这段内存会被硬件消费，写入不可省略、不可重排"。
这是裸机/驱动编程的基本功。

**工作机制**：

```c
#define VGA_TEXT_BASE   0xB8000u   /* VGA 彩色文本缓冲区物理地址 */
#define VGA_COLS        80u
#define VGA_ROWS        25u
#define VGA_ATTR        0x1Fu      /* 属性：0x1F = 白字蓝底 */

static volatile u16 *vga_cell(u32 row, u32 col)
{
    return (volatile u16 *)(VGA_TEXT_BASE + 2u * (row * VGA_COLS + col));
}
```

`vga_clear` 整屏填空格（属性保留），`vga_putc` 处理 `'\n'` 与行末回卷：

```c
void vga_clear(void)
{
    u32 i;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
        ((volatile u16 *)VGA_TEXT_BASE)[i] = (u16)(VGA_ATTR << 8);
    vga_row = 0;
    vga_col = 0;
}

void vga_putc(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS)
            vga_row = 0;
        return;
    }
    *vga_cell(vga_row, vga_col) = (u16)((u16)VGA_ATTR << 8) | (u8)c;
    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_ROWS)
        vga_row = 0;
}
```

### 2.3 概念三：`vga_hex` —— 无 libc 的数值输出

**定义**：没有 `printf` 就没有 `%x`。`vga_hex` 用移位取 nibble，映射到
`'0'..'9'`/`'a'..'f'`，逐字符输出。

**为什么需要**：引导器到处要打印地址（ELF 段地址、MBI 地址、内存图），一个自写的
hex 输出函数是调试的生命线（TinyOS 主线的调试地图同样依赖 hex 输出）。

**工作机制**：

```c
void vga_hex(u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        u8 nib = (u8)((v >> (4 * i)) & 0xF);
        vga_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}
```

### 2.4 概念四：汇编与 C 的链接契约

**定义**：`stage2.S` 用 `call loader_main` 调用 C 符号；`loader_main` 是 C 里定义
的全局函数。链接脚本把汇编的 `.text` 与 C 的 `.text/.rodata/.data/.bss` 按顺序
铺在 `0x7E00` 起的内存。

**为什么需要**：两种语言要共享一份平坦二进制。汇编负责 CPU 相关（切换、段寄存器、
栈），C 负责逻辑；边界是 `loader_main` 这个符号。

**工作机制**：

```asm
    movl $STACK_TOP, %esp
    call loader_main         # loader.c 中的 C 入口（链接器解析符号）
    hlt                      # loader_main 返回后挂起
1:  jmp 1b
```

链接脚本（不额外对齐，避免二进制输出出现空洞被 `truncate` 截断）：

```ld
ENTRY(stage2_entry)
SECTIONS
{
    . = 0x7E00;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
```

实测布局（`readelf -S`）：`.text` @ `0x7E00`（486B）→ `.rodata` @ `0x7FE6`
（143B）→ `.bss` @ `0x8078`（8B），合计约 640 字节，8 扇区（4096B）余量充足。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B03） |
|---|---|---|
| `stage1.S` | 512 字节引导扇区（读 stage2） | 未变化 |
| `stage2.S` | 实模式切换 + pm32 入口，调用 `loader_main` | 精简：切换逻辑保留，输出移交 C |
| `loader.c` | VGA 文本库 + `loader_main` | 新增（本课核心） |
| `linker.ld` | 0x7E00 起铺 .text/.rodata/.data/.bss | 新增 |
| `Makefile` | 增加 gcc 编译与链接脚本 | 修改 |
| `build/b04.img` | 软盘镜像 | 新增 |

### 3.2 `stage2.S` 精讲

与 B03 相比的增量只有 pm32 入口：

```asm
pm32:
    movw $DATA32_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $STACK_TOP, %esp
    call loader_main         # loader.c 中的 C 入口（链接器解析符号）
    hlt                      # loader_main 返回后挂起
1:  jmp 1b
```

- 栈：`ESP = 0x00090000`（常规内存高端，EBDA 之下）——C 的函数调用帧从这里
  向下生长；
- `call loader_main` 是跨汇编/C 的调用：返回地址压栈，`loader_main` 的 `ret`
  回到这里的 `hlt`。

实模式阶段的 `msg_real` 改为提示将进入 C：

```asm
msg_real:
    .asciz "B04 stage2: real mode -> protected mode -> C"
```

### 3.3 `loader.c` 精讲

**类型与常量**：

```c
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define VGA_TEXT_BASE   0xB8000u
#define VGA_COLS        80u
#define VGA_ROWS        25u
#define VGA_ATTR        0x1Fu      /* 属性：0x1F = 白字蓝底 */

static u32 vga_row = 0;
static u32 vga_col = 0;
```

**VGA 库**：`vga_cell`/`vga_clear`/`vga_putc`/`vga_puts`/`vga_hex`
（见 2.2/2.3；`vga_puts` 是 `vga_putc` 的循环封装）。

**`loader_main`**（C 入口，演示全部库函数）：

```c
void loader_main(void)
{
    vga_clear();
    vga_puts("B04 Mini-GRUB stage2 C runtime: loader_main OK\n");
    vga_puts("mem layout: boot 0x7c00 stage2 0x7e00 stack 0x90000\n");
    vga_puts("vga text base = ");
    vga_hex(VGA_TEXT_BASE, 8);
    vga_puts("\n");
    vga_puts("return to assembly: hlt\n");
}
```

`vga_hex(VGA_TEXT_BASE, 8)` 输出 `000b8000`——"内存布局常量 + hex 打印"正是
引导器调试的基础。

### 3.4 构建管线（Makefile 增量）

```makefile
$(BUILD)/loader.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/stage2.bin: $(BUILD)/stage2.o $(BUILD)/loader.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib --build-id=none \
	  --oformat binary -o $@ $(BUILD)/stage2.o $(BUILD)/loader.o
	truncate -s $(STAGE2_BYTES) $@   # 补齐到 8 个扇区
```

- 与 B02/B03 的"`-Ttext` 单文件"不同，B04 用**链接脚本**把汇编与 C 的目标文件
  拼在一起；
- `make check` 新增两条断言：`loader_main` 符号存在、`B04 Mini-GRUB` 字符串
  出现在 `stage2.bin`（证明 .rodata 被正确编入镜像）。

### 3.5 主控制流

```text
stage1 → stage2(0x7E00, 实模式)
  → 打印 "B04 stage2: real mode -> protected mode -> C"
  → A20 → GDT → CR0.PE → ljmp pm32
  → 重载段/栈 → call loader_main(C)
      → vga_clear → vga_puts 四行 → vga_hex(0xB8000) → 返回
  → hlt
```

---

## 4. 数据流与运行逻辑

```text
loader.c 字符串常量(.rodata @0x7FE6) --vga_puts--> 0xB8000 文本内存
loader.c vga_row/vga_col(.bss @0x8078) --读写--> 全局状态
vga_hex(0xB8000u, 8) --移位/nibble--> "000b8000"
```

VGA 最终画面（`vga_clear` 后）：

```text
B04 Mini-GRUB stage2 C runtime: loader_main OK
mem layout: boot 0x7c00 stage2 0x7e00 stack 0x90000
vga text base = 000b8000
return to assembly: hlt
```

自动化验证 marker：`B04 Mini-GRUB`、`loader_main`、`vga text base`。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b04-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b04 check
bootloader-course/scripts/validate-course.sh b04 qemu
```

### 5.2 期望输出

- `make check`：`B04 check PASS: stage1=512+55aa stage2=4096B loader_main+banner present`
- QEMU 四行输出如上（4. 节）；`make` 后 `stage2.bin` 4096 字节，ELF 布局
  `.text@0x7E00 → .rodata@0x7FE6 → .bss@0x8078`。

### 5.3 成功判据

C 代码被编译、链接并执行（四行输出完整、hex 输出正确）、QEMU trace 无异常、
`stage2.bin` 未超出 8 扇区。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 链接失败 `undefined reference to loader_main` | C 符号未导出或目标文件未加入链接 | 确认 `loader.c` 的 `loader_main` 非 static；链接行包含 `$(BUILD)/loader.o` |
| 编译器警告被 -Werror 拦下 | 类型/符号问题（如 `u16` 未定义） | 先修警告再构建；本课曾因漏 `typedef unsigned short u16` 编译失败 |
| 只显示实模式消息，C 输出为空 | 远跳到 pm32 失败或栈无效 | 单步：`call loader_main` 前 `ESP` 必须有效；确认 `.code32` 标签在 `ljmp` 目标 |
| `vga_hex` 输出位数不对 | digits 参数与移位关系 | `v >> (4*i)`，i 从 digits-1 到 0；8 位数字输出 8 个字符 |
| 字符串乱码/缺失 | .rodata 被 `truncate` 截断 | 核对 `readelf -S` 布局总长 < 4096；加大 `STAGE2_SECT` 并同步 stage1.S |
| 输出闪烁/被清掉 | `vga_clear` 调用顺序 | 本课 `loader_main` 先 `vga_clear` 再输出，实模式消息被清除属预期 |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/kern/main.c`（`grub_main`：先最小运行时，再逐层
初始化设备/文件/模块）与 `grub-core/kern/i386/pc/startup.S` 尾部（`grub_main`
跳转点）。对照点：

- **相同**：汇编只做 CPU 准备（切换/段/栈），控制权交给 C 的入口函数；C 侧从
  最小输出能力开始自举；
- **简化**：GRUB 的 `grub_main` 会初始化内存管理（`grub_mm_init`）、设备枚举、
  终端与模块系统；本课只有 VGA 输出。B05 起逐步补齐内存/磁盘能力；
- **VGA 库思路**：GRUB 的 `grub_console_putchar`/`grub_vprintf` 是终端抽象；
  本课 `vga_puts/vga_hex` 是最小的自写等价物，后续课会扩展成 loader 自己的
  `printf`-like 输出。

Linux 对照：`arch/x86/boot/printf.c` 的 `putchar`/`printf` 同样是引导阶段自写的
极简输出（无 libc），与本课 `vga_putc/vga_hex` 思路一致；仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么 `VGA_TEXT_BASE` 的指针要 `volatile`？去掉后会有什么风险？
2. 动手实验（临时副本）：在 `loader_main` 里调用 `vga_hex(0x7e00, 4)` 打印
   stage2 地址，观察输出与链接脚本是否一致。
3. 动手观察：`readelf -s` 查看 `loader_main`/`pm32`/`stage2_entry` 的地址，验证
   它们都落在 `0x7E00–0x8080` 区间。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -R "grub_vprintf" grub-core/kern`，找出 GRUB 的格式化输出入口，对比
   本课 `vga_puts/vga_hex` 的能力边界。
5. 综合：把 `STAGE2_SECT` 改为 2 后重建，观察 `truncate` 截断导致的链接/运行
   现象，说明 stage1.S 与 Makefile 的扇区契约为什么必须同步。

---

## 9. 本课小结与下一课预告

**小结**：本课把 loader 主体迁移到 freestanding C。关键收获：(1) 无 libc 下
字符串/数值输出要自己写（`vga_puts`/`vga_hex`）；(2) `volatile` 指针是访问硬件
内存的纪律；(3) 汇编负责 CPU 准备、C 负责逻辑，边界是 `loader_main` 符号；
(4) 链接脚本按顺序铺段，避免对齐空洞；(5) C 的 `.rodata/.bss` 会真实出现在
平坦二进制里，扇区预算必须把 `truncate` 后的总长算进去。至此，引导链的"C 运行时
+VGA 库"地基完成。

**下一课预告**：进入 [`b05-stable/README.md`](../b05-stable/README.md)。C 有了
输出能力，但还没有输入能力——引导器必须读盘（后续要读 ELF、读文件系统）。B05
复刻 GRUB 的 `prot_to_real`/`real_to_prot`：从保护模式临时切回实模式调用
`int $0x13` 读盘，再切回来，把"磁盘读"封装成 C 可调用的函数。

# Lesson 0.5: Multiboot2 header 校验与 ABI — 精讲文档

> **课号**：Lesson 0.5（GRUB 源码研读支线第 5 课，文档观察课，不生成内核）
> **主题**：GRUB 凭什么认为一个镜像「是」Multiboot2 内核：header 四字段、8 字节对齐、
> 前 32768 字节搜索范围，以及交接时 CPU 寄存器的 ABI 约定
> **课程主线位置**：第 1 阶段支线；0.4 讲完装载算法，本课回答「装载前怎么验证」
> **前置课程**：[`lesson-0.4-stable/README.md`](../lesson-0.4-stable/README.md)
> （GRUB ELF 装载器路径）
> **后续课程**：[`lesson-0.6-stable/README.md`](../lesson-0.6-stable/README.md)
> （GRUB 生成 Multiboot information tags）
> **一句话目标**：把 `lesson-01-stable/boot.S` 里的 header 常量与 `readelf -x .multiboot`
> 的字节一一对上，并能手算校验和。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能回答三个问题：header 必须放在哪？四字段怎么校验？
跳进 `_start` 时 CPU 和寄存器处于什么状态？

- **在课程主线中的位置**：本课是「规范 → 二进制 → 源码」的正面交锋。TinyOS 侧
  [`boot.S`](../../lessons/lesson-01-stable/boot.S) 的 `.multiboot` 节就是按本课的规范写的；
  GRUB 侧 `grub-file`/装载器按同一规范验证。0.6 课在此基础上看 GRUB 怎么把 MBI 交过来。
- **前置知识清单**：
  1. Multiboot2 规范概念（Lesson 00 已给 magic/arch/length/checksum 表）；
  2. 0.1/0.4 的 `readelf -x`/`readelf -S` 用法与 `.multiboot` 节位置（1 MiB、对齐 8）；
  3. 会看汇编 `.set`/`.long`/`.short`（boot.S 只有十几行）。
- **本课交付**：header 逐字节对照表 + 校验和手工验算 + 交接 ABI 状态表。

---

## 2. 核心概念精讲

### 2.1 概念一：header 的放置约束

Multiboot2 规范要求：header 必须**完整落在镜像最初 32768 字节内**，且按 **8 字节对齐**
（即起始地址是 8 的倍数）。这两个约束是 GRUB 搜索算法的基础：

```text
for (addr = 0; addr < 32768; addr += 8)
    if 该位置是合法 Multiboot2 header：采用
```

TinyOS 侧用 `linker.ld` 保证：`.multiboot ALIGN(8)` + `KEEP()`，且 `. = 1M` 起排布，
`readelf` 实测节地址 `0x100000`、对齐 8、文件偏移 `0x1000`（< 0x8000）——三条件全满足。
若不用 `KEEP()`，链接器可能把孤儿节丢进 `.rodata` 合并，破坏「首 32 KiB + 对齐」假设。

### 2.2 概念二：四字段与校验和

header 前 16 字节是固定格式（小端）：

| 偏移 | 长度 | 字段 | 含义 |
|---|---|---|---|
| 0 | 4 | `magic` | `0xe85250d6`（Multiboot2 标识） |
| 4 | 4 | `architecture` | `0` = i386；`4` = MIPS |
| 8 | 4 | `length` | header 总字节数（含所有 tag） |
| 12 | 4 | `checksum` | 使 `magic+arch+length+checksum ≡ 0 (mod 2^32)` |

**校验规则**：四个 32 位字相加，低 32 位必须为 0。它是「低开销可自校验」设计：不用读完整
镜像，GRUB 扫到候选地址后一加便知真假。

### 2.3 概念三：header tags 与 end tag

第 16 字节起是 header tags，每个 tag 是 `u16 type, u16 flags, u32 size`，且整体 8 字节对齐；
`type=0` 的 end tag（`flags=0`、`size=8`）标志结束。TinyOS 的 header 只有 4×4 字段 + end tag，
共 0x18=24 字节。**为什么需要 tags？** 内核可以声明「我要 framebuffer」「地址可重定位」等
需求，GRUB 按需响应——0.6 课的 information tags 与之是两条方向相反的信息流。

### 2.4 概念四：交接 ABI（进入 _start 时）

Multiboot2 规范定义的 i386 交接状态：

| 项 | 状态 |
|---|---|
| 模式 | 32 位保护模式（不是实模式、不是 long mode） |
| EAX | `0x36d76289`（Multiboot2 boot magic） |
| EBX | MBI 物理地址（8 字节对齐） |
| 中断 | 关闭（GRUB 保证 `cli` 状态） |
| 分页/栈 | 分页关闭；栈未定义，内核必须自建 |

所以 `boot.S` 的 `_start` 第一件事就是 `cli` + 建栈，而**不需要**做保护模式切换——
那是 GRUB 已经完成的事（Lesson 00 已强调）。`MB2_BOOT_MAGIC 0x36d76289` 将出现在 TinyOS
Lesson 05–08 的 MBI walker 里（0.6 课详讲）。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（规范实现点）

```bash
cd "$GRUB_SRC"
grep -R "0xe85250d6\|MULTIBOOT2_HEADER_MAGIC" grub-core include util | head -20
grep -R "grub_util_check_x86_multiboot2\|grub_util_check_multiboot2" util | head -10
grep -R "32768\|0x8000" grub-core/loader/multiboot.c | head -10
```

**预期输出解读**：magic 常量出现在 `include/grub/multiboot.h` 与
`util/misc.c`（`grub_util_check_multiboot2` 系列）及装载器；`grub-file` 正是调用
`util/misc.c` 的检查函数完成 CLI 验证。**注意**：32768 字节范围与 8 字节对齐的实现
应能在 `util/misc.c` 的搜索循环里直接看到。

### 3.2 观察一：boot.S 源码与规范字段一一对应

`lessons/lesson-01-stable/boot.S`（第 8–23 行，完整引用）：

```asm
.set MB2_HEADER_MAGIC,       0xe85250d6
.set MB2_ARCHITECTURE_I386,  0
.set MB2_HEADER_LENGTH,      mb2_header_end - mb2_header
.set MB2_CHECKSUM,           -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_HEADER_MAGIC
    .long MB2_ARCHITECTURE_I386
    .long MB2_HEADER_LENGTH
    .long MB2_CHECKSUM
    .short 0
    .short 0
    .long 8
mb2_header_end:
```

逐行解读：

| 行 | 作用 |
|---|---|
| `.set ...` | 汇编期常量；`length` 用标签差 `mb2_header_end - mb2_header` 自动计算 |
| `.set MB2_CHECKSUM` | 汇编器对 `magic+arch+length` 取负（32 位回绕）——与规范公式完全一致 |
| `.section .multiboot, "a"` | 放进可分配节，等待 `linker.ld` 的 `KEEP` |
| `.align 8` | 满足 8 字节对齐约束 |
| `.long`×4 | 写四字段（16 字节） |
| `.short 0; .short 0; .long 8` | end tag：type=0、flags=0、size=8 |

### 3.3 观察二：readelf 转储 header 真实字节

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
readelf -x .multiboot "$K"
```

实测输出（2026-08-06）：

```text
0x00100000 d65052e8 00000000 18000000 12afad17 .PR.............
0x00100010 00000000 08000000                   ........
```

逐字节对照（小端）：

| 字节区间 | 32 位值 | 字段 |
|---|---|---|
| `d6 50 52 e8` | 0xe85250d6 | magic |
| `00 00 00 00` | 0x00000000 | architecture = i386 |
| `18 00 00 00` | 0x00000018 | length = 24 |
| `12 af ad 17` | 0x17adaf12 | checksum |
| `00 00 00 00` | type=0, flags=0 | end tag 前 4 字节 |
| `08 00 00 00` | size=8 | end tag 大小 |

**校验和验算**（关键一步，建议手算）：

```text
magic + arch + length = 0xe85250d6 + 0x00000000 + 0x00000018 = 0xe85250ee
checksum            = -0xe85250ee mod 2^32 = 0x17adaf12
四者之和            = 0xe85250ee + 0x17adaf12 = 0x100000000 ≡ 0 (mod 2^32) ✔
```

与 Lesson 00 相比，本课提供的是**真实字节**：`12 af ad 17`（注意不是 `e8` 开头）——这正是
「规范表 → 二进制」必须用实测校准的地方。

### 3.4 观察三：grub-file 的协议检查

```bash
grub-file --is-x86-multiboot2 "$K"
echo $?      # 期望 0
```

实测：退出码 0（2026-08-06）。**解读**：`grub-file`（`util/grub-file.c` → `util/misc.c`）
在镜像前 32768 字节内按 8 对齐扫描，找到 header 后验证四字段与 checksum。退出码 0 表示
「格式合法」，但**不等于能启动**（证据边界见
[`docs/grub-source-study.md`](../../docs/grub-source-study.md)）——运行时还要经过 0.4 的
装载与 0.6 的 MBI 生成。

### 3.5 观察四：位置约束的二进制证据

```bash
readelf -S -W "$K" | grep multiboot   # 节地址 0x100000、Al 8
readelf -l -W "$K" | grep LOAD        # header 落在第一个 R E 段内
```

实测：`.multiboot` 节地址 `0x00100000`、对齐 `8`、`Flg A`；文件偏移 `0x1000`（4 KiB < 32 KiB）。
**解读**：链接脚本保证的「首 32 KiB + 8 对齐」可以直接在节表里验证，不必启动 QEMU。

---

## 4. 数据流与运行逻辑

```text
grub-file / GRUB 装载器
  → 在 0..32767 按 8 步长扫描 → 命中 0x1000（文件偏移）
  → 读四字段 → 求和 ≡ 0 → 判定为 Multiboot2 镜像
  → 解析 header tags（end tag 收尾）
  → 0.4 课的 ELF 装载 → 生成 MBI（0.6 课）
  → EAX=0x36d76289, EBX=MBI 地址 → jmp 0x100020
  → boot.S：cli → 建栈 → kernel_main32
```

TinyOS 侧 `MB2_BOOT_MAGIC` 会在 Lesson 05–08 的 walker 中与 EAX 比对（0.6 课）。

---

## 5. 观察与验证

### 5.1 依赖

`binutils`（readelf）、`grub-common`（grub-file）。

### 5.2 复现命令清单

```bash
K="lessons/lesson-01-stable/build/kernel.elf"
readelf -x .multiboot "$K"                 # d65052e8 00000000 18000000 12afad17 ...
readelf -S -W "$K" | grep multiboot        # 0x100000, Al 8
grub-file --is-x86-multiboot2 "$K"; echo $? # 期望 0
# 手工验算：
python3 -c 'print(hex((0xe85250d6+0+0x18+0x17adaf12) & 0xffffffff))'   # 期望 0x0
```

### 5.3 实测记录（2026-08-06，全部只读）

`.multiboot` 十六进制：`d6 50 52 e8 | 00 00 00 00 | 18 00 00 00 | 12 af ad 17 |
00 00 00 00 | 08 00 00 00`；四字段求和为 0；`grub-file` 退出码 0；节对齐 8、位于 1 MiB。

### 5.4 安全边界（本课红线）

只读 ELF；`python3 -c` 只做整数计算不写文件；不执行第三方校验脚本；个人副本改 header
实验不影响 stable 产物。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `grub-file` 退出非 0 | magic/checksum 错、未对齐、不在前 32 KiB | `readelf -x .multiboot` 逐字节对照 3.3 表 |
| `readelf -x .multiboot` 输出异常 | 节被合并/丢弃 | 检查 `linker.ld` 的 `KEEP(*(.multiboot))` 与 `ALIGN(8)` |
| 修改 `MB2_HEADER_LENGTH` 后启动失败 | length 与实际标签不匹配 | 用 `mb2_header_end - mb2_header` 自动计算 |
| `grub-file` 通过但 QEMU 黑屏 | header 合法≠装载成功 | 检查 0.4 的 LOAD 段、0.6 的 MBI、0.9 的 rescue |
| 误把 Lesson 00 示例字节当实测 | 文档示例可能是理想值 | 以本课 3.3 实测 `12afad17` 为准 |
| 想验证「坏 checksum」行为 | 改动 header 需重建 | 只在个人副本做，重建后跑 `grub-file` 看非 0 |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/arch/x86/boot/header.S`：Linux 的 `setup_header` 有 `boot_flag 0xAA55` 等
  自校验字段，与 Multiboot2 的 magic/checksum 是同一类「早期自描述协议」；
- 差异：Linux 用 boot protocol（`header.S` + `setup.bin`），不是 Multiboot2；不能互换。

**权威边界**：header 字段与校验规则以 **Multiboot2 规范**为准，GRUB 是实现者，Linux 仅作
工程类比。

---

## 8. 思考题与练习

1. 概念理解：为什么 header 要放在前 32768 字节且 8 对齐？如果违反，GRUB 的搜索循环会怎样？
2. 手工验算：用十六进制加法重算 3.3 的 checksum，说明 `0x100000000` 为什么等价于 0。
3. 源码定位：在 `$GRUB_SRC/util/misc.c` 中找到 `grub_util_check_x86_multiboot2`，
   记录它搜索的起始地址与步长。
4. 动手实验（个人副本）：改 `boot.S` 里 `MB2_ARCHITECTURE_I386` 为 4，重建并跑
   `grub-file --is-x86-multiboot2`，观察结果变化并解释。
5. 综合：把 2.4 的 ABI 表抄一遍，标出哪几项由 GRUB 保证、哪几项由 `boot.S` 自己负责。

---

## 9. 本课小结与下一课预告

**小结**：Multiboot2 header 用「magic + architecture + length + checksum」四字段自证身份，
放在前 32768 字节且 8 对齐；`boot.S` 的 `.multiboot` 节与 `linker.ld` 的 `KEEP/ALIGN` 精确
满足约束；`readelf -x` 实测字节（`12 af ad 17`）通过手算校验和；交接时 EAX=0x36d76289、
EBX=MBI 地址、中断关闭、分页关闭。

**下一课预告**：进入 [`lesson-0.6-stable`](../lesson-0.6-stable/README.md)，header 验证通过后，
看 GRUB 在 `boot` 瞬间生成哪些 information tags（memory map、cmdline、boot loader name、
ELF sections、framebuffer…），以及 TinyOS Lesson 05–08 的 MBI walker 如何遍历它们。

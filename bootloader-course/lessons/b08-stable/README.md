# Lesson B08: PT_LOAD 装载与首个交接 — 精讲文档

> **课号**：Lesson B08（Mini-GRUB 从零写 GRUB 课程第 8 课，可执行课）
> **主题**：按 PT_LOAD 装载 ELF 段、清零 .bss、设置 EAX/EBX、跳转内核入口
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」第 3 课
> **前置课程**：[`b07-stable/README.md`](../b07-stable/README.md)（Multiboot2 header 校验）；
> 研读支线 0.4（ELF 装载器路径）、0.10（端到端时序）
> **后续课程**：[`b09-stable/README.md`](../b09-stable/README.md)（MBI 结构）
> **一句话目标**：把通过校验的内核真正装进内存并跳过去——自写引导器首次启动一个
> Multiboot2 内核，完成阶段二的第一个里程碑。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能把 test-kernel 的两个 PT_LOAD 段装载到
1 MiB 附近、清零 bss，然后以 `EAX=0x36d76289, EBX=MBI` 跳转到内核入口——内核
在 QEMU 上打印自己的欢迎消息。

- **在课程中的位置**：B06 读懂了 ELF、B07 确认了协议；本课把"看懂"变成"跑起来"。
  这是阶段二的核心一跃，对应 GRUB `multiboot_elfxx.c` 的 `grub_multiboot_load_elf`
  与 `commands/boot.c` 的交接。
- **前置知识清单**：
  1. B06：PT_LOAD 的 `p_paddr/p_filesz/p_memsz`；
  2. B07：header 校验通过是装载的前提；
  3. Multiboot2 交接 ABI：32 位保护模式、关中断、关分页、`EAX=0x36d76289`、
     `EBX=MBI`（8 对齐）。
- **本课交付**：`build/b08.img`；QEMU 上先见 loader 的装载日志与交接信息，再见
  内核自己的 `B08 test-kernel: hello from Multiboot2`。

---

## 2. 核心概念精讲

### 2.1 概念一：PT_LOAD 装载语义

**定义**：对每个 PT_LOAD 段：从文件偏移 `p_offset` 复制 `p_filesz` 字节到
`p_paddr`；若 `p_memsz > p_filesz`，剩余 `p_memsz - p_filesz` 字节清零（.bss）。

**为什么需要**：文件里只存已初始化的数据；`.bss` 是 NOBITS（不在文件里），装载
时必须补零。test-kernel 的 RW 段 `filesz=4, memsz=0x404` 正是这个语义。

**工作机制**（本课 `elf_load`，逐字节循环替代 libc memcpy/memset）：

```c
                const u8 *src = (const u8 *)buf + ph->p_offset;
                u8 *dst = (u8 *)ph->p_paddr;
                for (j = 0; j < ph->p_filesz; j++)
                    dst[j] = src[j];     /* 复制 p_filesz */
                for (; j < ph->p_memsz; j++)
                    dst[j] = 0;          /* bss 清零 */
```

### 2.2 概念二：装载的安全边界

**定义**：装载目标地址必须经过校验：不得覆盖 loader 自身（低内存代码/栈/缓冲区），
文件数据不得越界（`p_offset + p_filesz <= size`）。

**为什么需要**：内核段的 `p_paddr` 来自文件、不可信；若指向 loader 的代码区，
装载会把自己改坏（这是 GRUB 有 relocator 的原因）。本课用简单守卫
`p_paddr < 0x100000 -> 拒绝`（loader 全部在 1 MiB 以下）。

**工作机制**：

```c
            if (ph->p_paddr < 0x00100000u)
                return -2;               /* 拒绝装载到 loader 低内存区 */
            if (ph->p_offset + ph->p_filesz > size)
                return -3;               /* 文件数据越界 */
```

### 2.3 概念三：Multiboot2 交接 ABI

**定义**：跳转内核入口时的 CPU 状态是协议的一部分：32 位保护模式、平坦 GDT、
关中断、关分页；`EAX = 0x36d76289`（magic）、`EBX = MBI 物理地址`（8 对齐）。
栈不保证（内核自己建栈——TinyOS `_start` 第一句就是 `cli` + `movl $stack_top,%esp`）。

**为什么需要**：内核依赖这些状态做出第一决定（检查 EAX 判断 loader 是否符合
Multiboot2）。任何一项不符都可能导致内核误判或崩溃。

**工作机制**（本课 `mb2_boot`，stage2.S）：

```asm
.globl mb2_boot
.code32
mb2_boot:
    cli
    movl $0x36d76289, %eax   # Multiboot2 boot magic（规范固定值）
    movl 8(%esp), %ebx       # mbi_addr（cdecl：第二个参数）
    movl 4(%esp), %edx       # entry（cdecl：第一个参数）
    jmp *%edx                # 交接：跳转到内核入口，不再返回
```

关键点：**EAX/EBX 在跳转前的最后时刻才装载**，避免被 loader 尾部代码破坏。

### 2.4 概念四：最小 MBI 占位

**定义**：本课交接时 EBX 指向一个最小 MBI：`u32 total_size=16, u32 reserved=0,
end tag{type=0, size=8}`（共 16 字节，8 对齐）。

**为什么需要**：B08 的测试内核不读 MBI，但协议要求 EBX 指向合法结构；16 字节的
end-tag-only MBI 是最小合法形式（B09 起扩展为完整 tag 链）。

**工作机制**：

```c
struct mbi_min {
    u32 total_size;      /* +0 */
    u32 reserved;        /* +4 */
    struct mbi_end_tag { /* +8 */
        u32 type;
        u32 size;
    } end_tag;
} __attribute__((aligned(8)));

static struct mbi_min mbi = { 16u, 0u, { 0u, 8u } };
```

`&mbi` 在 .bss（约 0x86C8），低内存、8 对齐，交接后仍完整（内核不动它）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B07） |
|---|---|---|
| `stage1.S` | 引导扇区 | 未变化 |
| `stage2.S` | 切换 + BIOS 回调 + **`mb2_boot` 交接函数** | 新增 mb2_boot |
| `loader.c` | VGA/磁盘/ELF/MB2 校验（B05-B07）+ **`elf_load` + MBI 占位** | 新增装载与交接 |
| `test-kernel.S`/`.ld` | 测试内核（B06 复用） | 未变化 |
| `Makefile` | 去掉 bad.bin，检查 elf_load/mb2_boot 符号 | 修改 |
| `build/b08.img` | LBA0=stage1, 1-8=stage2, 9-26=kernel | 新增 |

### 3.2 `loader.c` 精讲

**`elf_load`**：`elf_parse` 校验 → 遍历 PT_LOAD → 复制 + 清零 + 守卫（见 2.1/2.2）。
装载日志：

```text
B08 load: paddr=00100000 filesz=0000005d memsz=0000005d
B08 load: paddr=00101000 filesz=00000004 memsz=00000404
```

**`loader_main`**：读盘 → `mb2_header_check` → `elf_load` → 打印交接信息 →
`mb2_boot(e_entry, &mbi)`。交接信息：

```text
B08 boot: entry=00100018 eax=36d76289 mbi=000086c8
B08 boot: jumping to kernel...
```

`entry=00100018` = `.multiboot`（0x18 字节）之后 `_start` 的地址，与
`readelf -h` 一致。

### 3.3 构建管线

与 B07 相同（无 bad.bin）；`make check` 新增 `elf_load`/`mb2_boot` 符号断言。

### 3.4 主控制流

```text
stage1 → stage2 → pm32 → loader_main(C)
  → disk_read_lba(0, 9, 0x68000, 18)      # 读 test-kernel.elf
  → mb2_header_check → elf_load           # 装载到 0x100000 / 0x101000
  → mb2_boot(0x100018, 0x86C8)            # EAX=36d76289 EBX=MBI
  → jmp *%edx → test-kernel _start
  → _start: cli → 直接写 0xB8000 打印 hello → hlt
```

---

## 4. 数据流与运行逻辑

```text
test-kernel.elf --int 0x13--> 0x68000
  → elf_load: LOAD1 0x100000（R E，0x5d 字节）LOAD2 0x101000（RW，4+0x400 清零）
  → mb2_boot: EAX=0x36d76289, EBX=&mbi, EIP=0x100018
  → _start 直接写 0xB8000：B08 test-kernel: hello from Multiboot2
  → hlt
```

自动化验证 marker：`B08 boot: jumping`（loader 交接日志）、`B08 test-kernel`
（内核自身的欢迎消息——证明跳转成功且内核在保护模式正常执行）。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b08-stable
make clean && make -j"$(nproc)"
make check
make run
```

自动化：

```bash
bootloader-course/scripts/validate-course.sh b08 check
bootloader-course/scripts/validate-course.sh b08 qemu
```

### 5.2 期望输出

- `make check`：`B08 check PASS: stage1=512+55aa stage2=4096B elf_load+mb2_boot present`
- QEMU：装载日志 + 交接信息（3.2）+ 内核消息 `B08 test-kernel: hello from
  Multiboot2`（第 0 行，覆盖 loader banner 之后）。

### 5.3 成功判据

内核欢迎消息出现（交接成功、内核在保护模式执行）；装载日志与 `readelf -l`
一致；QEMU trace 无 triple fault（说明切换/跳转后内核环境正确）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 只有装载日志，无内核消息 | 交接寄存器/入口错 | 检查 `mb2_boot` 的 cdecl 参数顺序（entry 在 4(%esp)）与 `jmp *%edx` |
| 内核消息乱码 | 装载地址错（段没到位）或内核写了错误属性 | 对照 readelf 的 p_paddr；检查 bss 清零是否覆盖了代码 |
| 跳转后 triple fault | EAX/EBX 被破坏、GDT 状态错 | 确认 `cli` + 平坦 GDT 仍在；EBX 指向 8 对齐 MBI |
| 装载时崩溃 | 内核段覆盖 loader 代码 | 检查 `p_paddr < 0x100000` 守卫；loader 栈 0x70FF0 与缓冲 0x68000 |
| 内核没打印但也没崩溃 | 内核入口地址错（跳到空白） | 核对 `e_entry`；`readelf -h` 的 Entry 与日志一致 |
| 交接后内核立刻重启 | 交接时中断未关或 IDT 错 | `mb2_boot` 先 `cli`；保护模式用空 IDT（prot_to_real 会换回实模式 IDT） |

---

## 7. 与 GNU GRUB 源码对照

本课对应 `$GRUB_SRC/grub-core/kern/elfXX.c` 的 `grub_elfXX_load`（PT_LOAD 循环、
`grub_file_seek/read` 读 `p_filesz`、`grub_memset` 补零）与
`$GRUB_SRC/grub-core/commands/boot.c`（`boot` → `grub_loader_boot` 的交接）。
对照点：

- **相同**：只装载 PT_LOAD；`p_filesz` 从文件读、`p_memsz-p_filesz` 清零；
  入口只信 `e_entry`；交接时关中断、EAX/EBX 就绪；
- **简化**：GRUB 用 relocator 动态分配装载区（可处理地址冲突），本课用固定
  地址 + `p_paddr < 1 MiB` 守卫；GRUB 用 `grub_file` 按段 seek/read，本课
  整文件读入后原地复制；MBI 先用 16 字节占位（B09 补全）；
- **下一步**：B09 把占位 MBI 升级为完整结构（total_size + tag 链），对照
  GRUB `multiboot_mbi2.c` 的 `make_mbi`。

Linux 对照：`fs/binfmt_elf.c` 的 `load_elf_binary` 同样按 `PT_LOAD` 装载段并
处理 bss 补零，思想同源；仅作工程对照。

---

## 8. 思考题与练习

1. 概念理解：为什么交接时 loader 不替内核建栈？TinyOS `_start` 第一句为什么是
   `cli` + `movl $stack_top,%esp`？
2. 动手实验（临时副本）：把 `mb2_boot` 里 EAX 改成 `0x36d76288`，观察内核是否
   拒绝（test-kernel 不检查 EAX，TinyOS L05 起会检查——B12 验证）。
3. 动手观察：`readelf -l -W build/test-kernel.elf` 对照装载日志；用 `-d in_asm`
   trace 找到 `jmp *%edx` 前后的寄存器现场。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "grub_loader_boot\|jmp" grub-core/commands/boot.c`，对照 GRUB 交接。
5. 综合：画出 test-kernel 从文件到内存的两段映射（含 bss 补零）与交接时刻的
   CPU 状态（寄存器/段/GDT/IDT/中断）。

---

## 9. 本课小结与下一课预告

**小结**：本课完成了阶段二的第一个里程碑——自写引导器启动了一个 Multiboot2
内核。关键收获：(1) PT_LOAD 装载 = 复制 `p_filesz` + 清零 `memsz-filesz`；
(2) 装载地址必须守卫（不覆盖 loader、文件不越界）；(3) 交接 ABI：EAX=magic、
EBX=MBI、关中断、平坦 GDT、不建栈；(4) EAX/EBX 在跳转前最后时刻装载；
(5) 最小 MBI（total_size=16 + end tag）是合法的占位。QEMU 上内核自己打印欢迎
消息，证明 loader 从"读文件"到"跑内核"的完整闭环打通。

**下一课预告**：进入 [`b09-stable/README.md`](../b09-stable/README.md)。交接时
EBX 指向的 MBI 目前是 16 字节占位——B09 实现正式的 MBI 结构：`total_size` +
tag 链（type/size/对齐）+ end tag，并用自写 walker 内核验证遍历，对照 GRUB
`multiboot_mbi2.c` 的 `make_mbi` 框架。

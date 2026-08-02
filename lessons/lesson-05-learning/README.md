# 第五课：读取并显示 Multiboot2 内存地图

> **课程状态：学习版（可编辑，尚未归档）**  
> 本课仍在 GRUB 交接的 32 位保护模式、未分页阶段运行。  
> 新增 `mmap` 命令：显示 GRUB 通过 Multiboot2 boot-information structure 交给 TinyOS 的原始物理内存范围。

## 第一部分：本课使命与源码索引（Mission & Source Index）

**一句话目标**：首次让 TinyOS 认识自己所处机器的物理内存布局，而不是凭空假设 RAM 大小或可用范围。

**责任边界与固定锚点：**

- **Multiboot2 官方规范 / GNU GRUB Multiboot2 manual**：i386 entry 的 `EAX = 0x36d76289`、`EBX = boot-information physical address`；tag `{type,size}`、8-byte 对齐、end tag、type-6 memory-map tag 和 type-1 information request tag。
- **GNU GRUB**：`multiboot2 /boot/kernel.elf` 实际装载内核并准备 MBI。
- **Linux v6.12** `arch/x86/include/uapi/asm/e820.h`：`E820_TYPE_RAM == 1` 的类型语义对照；`arch/x86/kernel/e820.c`：真实 Linux 会排序、合并、裁剪 firmware ranges。
- **Linux v6.12** `arch/x86/boot/header.S`、`arch/x86/kernel/head_64.S`：镜像/早期入口的工程对照；Linux boot protocol 不是 Multiboot2。
- 延续输入显示参考：`drivers/input/serio/i8042.c`、`drivers/input/keyboard/atkbd.c`、`drivers/tty/vt/keyboard.c`、`drivers/tty/vt/vt.c`、`drivers/video/console/vgacon.c`。

**简化边界**：本课只显示原始 map。type `1` 显示为 `available`，但绝不等于“现在即可分配”：内核 ELF、栈、MBI、GRUB/firmware、模块、framebuffer 和未来页表都尚未从范围中扣除。这是下一课物理分配器必须解决的保留问题。

## 第二部分：核心设计解剖（Design Anatomy）

```text
GRUB Multiboot2 i386 handoff
  EAX = bootloader magic
  EBX = MBI physical address
          │
          ▼
boot.S: 建栈后按 cdecl 把 (magic, mbi_address) 传给 C
          │
          ▼
show_memory_map()
  total_size → 8-byte aligned tag walker → type 6 mmap tag
          │                                   │ entry_size 作为步长
          ▼                                   ▼
  magic / size / end-tag / overflow 检查   u64 addr, u64 len, u32 type
          │                                   │
          └───────────────────────────────────┘
                          ▼
                    VGA `mmap` 输出
```

MBI 起始布局：

```text
u32 total_size
u32 reserved
随后每个 tag：u32 type, u32 size, payload, padding 到 8-byte 边界
最后必须有：type=0, size=8 的 end tag
```

memory-map tag（type 6）前 16 bytes 后跟可变数量 entry：

```text
u32 type = 6 | u32 size | u32 entry_size | u32 entry_version
u64 addr | u64 len | u32 type | u32 reserved
```

解析器永远按运行时的 `entry_size` 前进，而不是按 C struct 大小；只打印前六项但继续统计全量。地址与长度用 16 位十六进制显示，避免 32 位截断。

## 第三部分：增量代码交付（Incremental Code Delivery）

```text
lesson-05-learning/
├── boot.S       # 新增 information-request tag；传递 EAX/EBX
├── kernel.c     # 新增受边界约束的 MBI/tag parser 与 mmap command
├── grub.cfg     # 标题改为 TinyOS lesson 5
├── Makefile     # 不变
├── linker.ld    # 不变
└── README.md
```

`boot.S` 在 header end tag 前请求 type `6`：

```asm
.short 1          # information-request header tag
.short 0
.long 12
.long 6           # boot-information memory-map tag
.align 8
```

并以 i386 cdecl 交接：

```asm
pushl %ebx        # 第二参数：MBI physical address
pushl %eax        # 第一参数：Multiboot2 magic
call kernel_main32
addl $8, %esp
```

`mmap` 命令首先验证 magic、MBI 8-byte 对齐和 `total_size`，随后逐 tag 检查最小 header、声明 size、对齐后的安全步长与 end tag。错误路径显示 `mmap error: ...`，不会故意解引用边界外地址。

## 第四部分：编译与运行验证（Verification）

```bash
make clean && make -j$(nproc)
make check
readelf -x .multiboot build/kernel.elf
```

`make check` 必须输出：

```text
Multiboot2 header check passed.
```

`readelf` 的 `.multiboot` 应包含 type-1 request tag、请求 type `6`、以及对齐后的 end tag。

以固定 RAM 大小启动正常 VGA 图形窗口：

```bash
qemu-system-x86_64 -accel tcg -m 128M -boot order=d \
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

不要使用 `-display none`。点击窗口后输入：

```text
mmap<Enter>
help<Enter>
about<Enter>
```

预期 `mmap` 显示标题、至少一条 16-hex-digit base/length、至少一条 `available`、`shown ... entries` 和返回 prompt。具体地址、类型与条目数由 QEMU/GRUB 实际 MBI 决定，不能硬编码。

若 map 缺失，可用 QEMU `-S -s` 和 GDB 在 `_start` 检查 `$eax == 0x36d76289`、`$ebx` 8-byte 对齐，`x/4wx $ebx` 的第一个 word 为合理 `total_size`；不要用 QEMU `-kernel` 绕过 GRUB。

> **本次实际验证记录（2026-08-01）**：已执行 `make clean && make -j$(nproc)`；`gcc -m32`、`ld -m elf_i386` 和 `grub-mkrescue` 均无警告完成，`make check` 输出 `Multiboot2 header check passed.`。`readelf -x .multiboot` 显示 header length `0x28`，其后有 type-1 / size-12 / requested-type-6 information-request tag、4-byte padding 和 end tag。随后以 `-m 128M` 的正常 QEMU VGA 显示启动 ISO（未使用 `-display none`），通过 monitor `sendkey` 注入 `mmap`、`help`、`about` 并捕获 720×400 画面。按 80 列、每 cell 9×16 像素核验：`mmap` 解析出六条实际条目，首项为 `0000000000000000 +000000000009fc00 available`，随后显示 `shown 06 of 06 entries`、返回 prompt，且 help/about 回归路径均正确。具体 map 来自该次 QEMU/GRUB MBI，课程未将它作为跨平台常量。

## 第五部分：调试地图——对照源码排错（Debugging Map）

| 现象 | 对照来源 | 检查 |
|---|---|---|
| `mmap error: bad multiboot2 magic` | Multiboot2 i386 entry ABI | `_start` 必须在任何调用前保存 EAX 并按 cdecl 传给 C。 |
| MBI 地址无效 | Multiboot2 ABI | EBX 是 physical address；本课只因未分页 identity mapping 才可直接读。 |
| map missing | information-request tag / GRUB | 检查 header type-1 request 是否请求 `6`，运行必须经过 GRUB `multiboot2`。 |
| tag walker 卡死或越界 | Basic tags structure | 每 tag 验证 size、remaining bytes、8-byte round-up 与 end tag。 |
| 条目错位 | Memory-map tag | 用 runtime `entry_size` 步进，不能写死 struct stride。 |
| 高内存显示截断 | 64-bit map fields | base/length 用 `print_hex64()`，不能只输出 u32。 |
| 把 available 当 allocator | Linux `e820.c` | map 是原始报告；内核和 MBI 等保留范围下一课处理。 |
| 固定地址测试偶尔失败 | QEMU platform boundary | `-m`、设备、GRUB/firmware 都会改变 map；只验证结构。 |
| `-kernel` 启动后无 map | GRUB/Multiboot2 boundary | ISO + `multiboot2` 是本课 ABI 前提。 |
| 进入分页后读 MBI 崩溃 | `head_64.S` 早期映射原则 | long mode 前后必须显式映射该 physical address。 |

## 第六部分：课后源码阅读作业（Source Reading Homework）

1. 阅读 Multiboot2 manual 的 Boot information、Basic tags structure、Memory map 与 Information request header tag，画出 tag 的对齐过程。
2. 阅读 Linux v6.12 `arch/x86/kernel/e820.c`，列出 Linux 为何不能直接将 firmware RAM map 交给分配器。
3. 找出本课中 kernel image、临时 stack 与 MBI 可能占用的 range；说明它们为何必须在下一课标为 reserved。
4. 下一课：在这张原始 map 上建立最小早期物理页分配器。
# Lesson B12: 综合 checkpoint — 启动 TinyOS L01/L05 — 精讲文档

> **课号**：Lesson B12（Mini-GRUB 从零写 GRUB 课程第 12 课，阶段二 checkpoint）
> **主题**：用自写引导器启动 TinyOS 主线 Lesson 01 与 Lesson 05 的 `kernel.elf`
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」收尾
> **前置课程**：[`b11-stable/README.md`](../b11-stable/README.md)（装载框架）；
> 阶段一全部（B01–B05）
> **后续课程**：[`b13-stable/README.md`](../b13-stable/README.md)（ISO9660 基础）
> **一句话目标**：Mini-GRUB 首次替代真 GRUB——启动 TinyOS L01（VGA hello）与
> L05（内存图显示），完成"从零到能跑真实内核"的阶段性验收。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能用 B01–B11 的全部能力，在 QEMU 上用自写引导器
启动 TinyOS 主线的两个真实内核镜像，L05 的严格 MBI 校验全部通过。

- **在课程中的位置**：阶段一（引导链）+ 阶段二（装载）的综合验收点。此前验证
  都用自写测试内核；本课换成 TinyOS 真实产物（只读复用
  `lessons/lesson-01-stable` 与 `lessons/lesson-05-stable` 的 `build/kernel.elf`），
  证明 loader 满足协议而非只对自己写的内核有效。
- **前置知识清单**：
  1. B08：交接 ABI；B09：MBI；B10：mmap tag；B11：loader_set/loader_boot；
  2. TinyOS L01（无 MBI 依赖）与 L05（magic/对齐/end tag/mmap tag 全部
     fail-closed 校验）的启动要求；
  3. 研读支线 0.10（端到端时序）与
     [`docs/grub-implementation-guide.md`](../../docs/grub-implementation-guide.md)
     第 3 节（TinyOS 对引导器的硬性要求清单）。
- **本课交付**：`build/b12-l01.img` 与 `build/b12-l05.img`；QEMU 上 L01 显示
  "Hello from the VGA text console! / Multiboot2 boot succeeded."，L05 显示
  "shown 06 of 07 entries"（交互式 mmap 命令）。

---

## 2. 核心概念精讲

### 2.1 概念一：L01 vs L05 的交接差异

**定义**：L01 只要求"装载 + 跳转"（EBX 可指向占位 MBI）；L05 要求**完整 MBI**：
magic 正确、MBI 8 对齐、`total_size` 在 [16, 0x100000]、end tag `type=0 size=8`、
type-6 mmap tag 满足 `entry_version==0`、`entry_size>=24 && %8==0`、
`(size-16)%entry_size==0`。

**为什么需要**：TinyOS 从 L05 起把引导器当作"必须可信的协议实现者"——任何一项
不满足都会 fail-closed。loader 必须同时满足两个内核（子集）的要求。

**工作机制**（L05 的 `show_memory_map` 校验矩阵，逐条对照我们的 MBI）：

```c
if (multiboot_magic != MB2_BOOT_MAGIC) { printk("mmap error: bad multiboot2 magic\n"); return; }
if ((multiboot_address & 7) != 0) { printk("mmap error: unaligned mbi address\n"); return; }
... total_size 范围 / end tag size==8 / entry_version==0 / entry_size>=24&&%8==0 ...
```

### 2.2 概念二：只读复用 TinyOS 产物

**定义**：loader 启动的 `kernel.elf` 来自 TinyOS 主线的 `build/`（只读复制），
课程红线是"不修改 lessons/ 任何文件"。

**为什么需要**：验收的是"Mini-GRUB 与真 GRUB 的协议兼容性"，必须用 TinyOS 的
真实产物；复制到本课 `build/` 的副本可自由补齐扇区。

**工作机制**（Makefile）：

```makefile
TINYOS_ROOT ?= ../../../lessons      # 验证脚本会导出为仓库根 lessons/
L01_ELF := $(TINYOS_ROOT)/lesson-01-stable/build/kernel.elf
L05_ELF := $(TINYOS_ROOT)/lesson-05-stable/build/kernel.elf

$(BUILD)/l05-kernel.bin: $(L05_ELF)
	cp $< $@
	truncate -s $(shell echo $$(( $(KERNEL_SECT) * 512 ))) $@
```

### 2.3 概念三：双镜像与目标选择

**定义**：loader 通过 `-DTARGET_LBA` 编译期指定读哪个内核：L01（LBA 9）或
L05（LBA 27）；Makefile 生成两个镜像 `b12-l01.img` / `b12-l05.img`。

**为什么需要**：L01 与 L05 的内核文件大小不同（5352 vs 10144 字节），放不同
LBA 段；`KERNEL_SECT=20` 同时覆盖两者（多余扇区是零填充，解析边界用文件实际
内容）。真正的"选内核"是 B17/B18 配置脚本与菜单的事。

**工作机制**：

```makefile
$(BUILD)/loader-l01.o: loader.c | $(BUILD)
	$(CC) $(CFLAGS) -DTARGET_LBA=$(L01_LBA) -c $< -o $@
```

```c
#ifndef TARGET_LBA
#define TARGET_LBA   9u            /* 由 Makefile 通过 -DTARGET_LBA 指定 */
#endif
```

### 2.4 概念四：交互式内核的自动化验证

**定义**：L05 启动后停在 `tinyos> ` 提示符等待键盘输入；自动化验证通过 QEMU
monitor 的 `sendkey` 发送 "mmap" + 回车，再 dump VGA 文本检查 "available"。

**为什么需要**：这是课程验证手法的升级——从"只读画面"到"注入输入"。复用根
`qemu-vga-check.sh` 的 sendkey 思路（`monitor_cmd "sendkey m"`）。

**工作机制**（qemu-text-check.sh 新增 `SEND_KEYS` 支持）：

```sh
if [ -n "${SEND_KEYS:-}" ]; then
  for key in $SEND_KEYS; do
    monitor_cmd "sendkey $key"
    sleep .05
  done
  sleep 1
fi
```

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 B11） |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | 引导链与 BIOS 回调 | 未变化 |
| `loader.c` | loader_set/loader_boot（B11）+ `-DTARGET_LBA` | 常量改为编译期目标 |
| `Makefile` | 双镜像（l01/l05）、只读复制 TinyOS kernel | 重写 |
| `build/b12-l01.img` | L01 镜像（kernel @ LBA 9） | 新增 |
| `build/b12-l05.img` | L05 镜像（kernel @ LBA 27） | 新增 |

（B12 无 test-kernel——内核换成 TinyOS 真实产物。）

### 3.2 `loader.c` 精讲

`loader_set`/`loader_boot`/`mbi_build`/`mmap_collect` 全部沿用 B11；`loader_main`
精简为：

```c
void loader_main(void)
{
    vga_clear();
    vga_puts("B12 Mini-GRUB: booting TinyOS kernel (LBA ");
    vga_hex(TARGET_LBA, 2);
    vga_puts(")\n");

    if (loader_set(TARGET_LBA) < 0) {
        vga_puts("B12 error: load failed\n");
        return;
    }
    loader_boot();                     /* 不返回；TinyOS 接管屏幕并清屏 */
}
```

注意：TinyOS 内核启动第一件事是 `vga_clear()`（清屏）——loader 的日志会被覆盖，
这是"内核接管"的正常表现。

### 3.3 构建管线与主控制流

```text
TinyOS kernel.elf --cp+truncate--> l0X-kernel.bin --dd--> b12-l0X.img
  （KERNEL_SECT=20 覆盖 5352/10144 两种大小）

BIOS → stage1 → stage2 → pm32 → loader_main
  → loader_set(TARGET_LBA)：mmap_collect → 读内核 → mb2_header_check
      → elf_load → mbi_build（type-2 + type-6 mmap）
  → loader_boot：EAX=36d76289 EBX=MBI → 跳转 TinyOS _start
  → L01: 打印 hello → L05: 提示符 → 交互式 mmap → 显示 7 条区间
```

---

## 4. 数据流与运行逻辑

```text
loader_set(L05_LBA)：E820 → 7 条条目 → MBI(total_size=0xE0, type-2, type-6)
  → mb2_boot → TinyOS L05 _start → kernel_main32(magic, mbi)
  → vga_clear → 打印 banner → tinyos> 提示符
  → sendkey "mmap" + ret → show_memory_map
  → magic/对齐/total_size/end tag/mmap tag 全部通过
  → 打印 7 条（显示前 6 条）：00 available / 01-02 reserved / 03 available ...
  → "shown 06 of 07 entries"
```

自动化验证 marker（L05 镜像）：`TinyOS lesson 5`（内核 banner）、`available`
（mmap 显示成功）。

---

## 5. 构建、运行与验证

### 5.1 命令

```bash
cd bootloader-course/lessons/b12-stable
make clean && make -j"$(nproc)"
make check
make run-l01    # QEMU 启动 TinyOS Lesson 01
make run-l05    # QEMU 启动 TinyOS Lesson 05（提示符下手动输入 mmap）
```

自动化（脚本拷贝到临时目录，导出 `TINYOS_ROOT` 指向仓库根 lessons）：

```bash
bootloader-course/scripts/validate-course.sh b12 check
bootloader-course/scripts/validate-course.sh b12 qemu    # L05 + sendkey mmap
```

### 5.2 期望输出

- `make check`：`B12 check PASS: TinyOS L01+L05 kernels valid, loader framework present`
- L01 QEMU：`TinyOS lesson 1`、`Hello from the VGA text console!`、
  `Multiboot2 boot succeeded.`
- L05 QEMU：`TinyOS lesson 5: Multiboot2 memory map`、`tinyos> mmap`、
  `00 0000000000000000 +000000000009fc00 available` … `shown 06 of 07 entries`。

### 5.3 成功判据

L01 显示 hello（最小交接成功）；L05 的 `show_memory_map` 不打印任何
`mmap error:`（全部 fail-closed 校验通过）、显示 7 条中的 6 条（L05 显示上限）；
QEMU trace 无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| L01 能启动但 L05 卡住 | L05 需要完整 MBI（L01 可容忍占位） | 检查 `mbi_build` 是否含 type-6；`total_size` 是否包含 mmap tag |
| L05 打印 `mmap error: unaligned mbi address` | MBI 未 8 对齐 | 确认 `mbi_buf` 有 `__attribute__((aligned(8)))` |
| L05 打印 `mmap error: bad tag size / unsupported map tag` | tag 布局错 | 对照 entry_size=24、version=0、`(size-16)%24==0` |
| L05 打印 `mmap error: memory map missing` | 没有 type-6 tag | 检查 `mmap_count > 0` 分支与 E820 收集 |
| L05 提示符无响应（输入无效） | sendkey 时机或按键名错 | 检查 `SEND_KEYS` 与 monitor `sendkey` 名称（m/a/p/ret） |
| `make` 找不到 kernel.elf | `TINYOS_ROOT` 相对路径在副本中失效 | 导出 `TINYOS_ROOT`（验证脚本已处理）；本目录内用默认相对路径 |
| L05 显示条目少/乱 | E820 条目数或 entry_size 错 | 对照 L05 的 `shown N of M` 与 loader 的 `mmap entries=N` |

---

## 7. 与 GNU GRUB 源码对照

本课没有新增 GRUB 源码逻辑，而是**验收**：loader 产出的 MBI 必须与真 GRUB
（`multiboot_mbi2.c` 的 `make_mbi` + `grub_fill_multiboot_mmap`）在 TinyOS L05
的消费端完全等价。对照点：

- **等价性证明**：TinyOS L05 的 `show_memory_map`（`lessons/lesson-05-stable/
  kernel.c`）对 loader 的 MBI 全部校验通过，并显示与真 GRUB 启动时一致的内存图
  ——这是"同构"的最强证据；
- **差异说明**：真 GRUB 的 MBI 还含 cmdline、boot device、elf-sections 等 tag
  （L05 的 walker 跳过未知类型）；Mini-GRUB 只生成 type-2 + type-6，够用即可；
- **只读边界**：本课只复制 `build/kernel.elf`，不修改 TinyOS 主线任何源码
  （依据 [`docs/grub-implementation-guide.md`](../../docs/grub-implementation-guide.md)
  的证据边界）。

---

## 8. 思考题与练习

1. 概念理解：L05 的 walker 对 mmap tag 有 4 项硬性校验（entry_version/entry_size/
   对齐/整除），为什么这些约束能防止越界读？
2. 动手实验（临时副本）：把 `mbi_build` 的 `entry_size` 改成 20，观察 L05 的
   `mmap error:` 输出，再改回来。
3. 动手观察：`make run-l05` 后手动输入 `help`/`mmap`，对照真 GRUB 启动 TinyOS
   的画面差异（应只有 boot-loader-name 等 tag 不同）。
4. 源码定位：在 `$GRUB_SRC` 运行
   `grep -n "grub_fill_multiboot_mmap\|entry_size" grub-core/loader/multiboot_mbi2.c`，
   对照本课 `mbi_build` 的 mmap 段。
5. 综合：画出"Mini-GRUB loader → TinyOS L05"的完整交接时序（含 MBI 内容、
   校验点、显示输出），标注每步对应的课程（B05–B11）。

---

## 9. 本课小结与下一课预告

**小结**：本课是阶段二的收官验收。关键收获：(1) TinyOS L01 与 L05 对引导器的
要求不同，loader 必须满足并集；(2) L05 的 mmap 显示"shown 06 of 07 entries"
证明 Mini-GRUB 生成的 MBI 与真 GRUB 在消费端完全等价——七条 E820 区间、全部
fail-closed 校验通过；(3) 只读复用 TinyOS 产物、编译期 `-DTARGET_LBA` 选择
目标、`sendkey` 交互验证，都是课程基础设施的进化；(4) "从零写 GRUB"在阶段二
达到了第一个真正的里程碑：**自写引导器替代真 GRUB 启动 TinyOS 主线**。

**下一课预告**：进入 [`b13-stable/README.md`](../b13-stable/README.md)。**阶段三**
开始：TinyOS 内核目前是靠固定扇区塞进软盘的；真实 GRUB 从光盘文件系统读内核。
B13 实现 ISO9660 文件系统读取，对照 GRUB `fs/iso9660.c`。

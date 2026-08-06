# Lesson 0.6: GRUB 生成 Multiboot information tags — 精讲文档

> **课号**：Lesson 0.6（GRUB 源码研读支线第 6 课，文档观察课，不生成内核）
> **主题**：`boot` 交接前，GRUB 在内存里拼出一份 boot-information structure（MBI），
> 里面是若干 information tags——memory map、cmdline、boot loader name、ELF sections 等
> **课程主线位置**：第 1 阶段支线；0.5 讲完 header 校验，本课讲「GRUB 怎么回答内核的问题」
> **前置课程**：[`lesson-0.5-stable/README.md`](../lesson-0.5-stable/README.md)
> （Multiboot2 header 校验与 ABI）
> **后续课程**：[`lesson-0.7-stable/README.md`](../lesson-0.7-stable/README.md)
> （BIOS/legacy 与 UEFI 平台分支）
> **一句话目标**：讲清 MBI 的布局规则、GRUB 生成哪些 tag、以及 TinyOS Lesson 05–08 的
> MBI walker 如何消费这些 tag。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你能画出 MBI 的字节布局，列出 GRUB 默认生成的 tag 类型，
并解释「内核请求 tag（header 侧）」与「GRUB 提供 tag（MBI 侧）」两条信息流的关系。

- **在课程主线中的位置**：本课是「GRUB 视角」的最后一课之一：0.1–0.4 讲装载，0.5 讲验证，
  本课讲交接数据；之后 0.7–0.10 转向平台与整体闭环。TinyOS 侧，Lesson 05–08 的 MBI walker
  是直接消费者（本课第 4 节）。
- **前置知识清单**：
  1. 0.5 的交接 ABI：EAX=0x36d76289、EBX=MBI 物理地址；
  2. 0.4 的 ELF 装载（MBI 是 GRUB 装载完内核后临时分配的）；
  3. Lesson 05 的 [`kernel.c`](../../lessons/lesson-05-stable/kernel.c) 结构（可只读翻阅）。
- **本课交付**：MBI 布局图 + tag 类型表 + 两条 header 实测（Lesson 01 无请求 / Lesson 05
  请求 type 6）+ Lesson 05 walker 的逐段解读。

---

## 2. 核心概念精讲

### 2.1 概念一：MBI 的字节布局

Multiboot2 规范定义 boot-information structure（MBI）：

```text
偏移  内容
0     u32 total_size          （整个 MBI 的字节数，含所有 tag 与填充）
4     u32 reserved            （恒为 0，保留）
8     tag[0]: u32 type | u32 size | payload | padding
...   下一个 tag（8 字节对齐：size 按 8 向上取整后是下一个起点）
...   end tag: type=0, size=8
```

**关键规则**：每个 tag 的前 8 字节是 `{u32 type, u32 size}`，`size` 包含这 8 字节头；
下一个 tag 从 `(size + 7) & ~7` 处开始。遍历必须按运行时读到的 `size` 步进，不能假设固定
间隔——这正是 TinyOS walker 用 `(tag->size + 7) & ~7U` 的原因。

### 2.2 概念二：GRUB 生成哪些 tag

GRUB 在 `grub-core/loader/multiboot_mbi2.c` 的 MBI 构造函数里，按平台与内核请求生成一组
tag（以 grep 结果为准，2.14 实现在该目录）：

| type | 内容 | 说明 |
|---|---|---|
| 1 | command line | 内核命令行（本课程为空） |
| 2 | boot loader name | 形如 `GNU GRUB <版本>` 的字符串 |
| 3 | modules | 装载的模块列表（本课无 module 命令） |
| 4 | basic meminfo | 低位/高位内存大小 |
| 5 | boot device | 启动设备描述 |
| 6 | memory map | 物理内存图（TinyOS 最关心） |
| 8 | framebuffer | 视频帧缓冲信息（若启用了 GRUB 视频） |
| 9 | elf sections | 内核 ELF 节表信息 |
| 11/12… | EFI 系统表等 | 仅 UEFI 平台生成（0.7 课） |
| 0 | end | 收尾，size=8 |

**为什么需要 tag 而不固定结构体？** 内核只需知道自己要的信息，其余类型可以跳过；规范可以
扩展新类型而不破坏老内核。

### 2.3 概念三：type-6 memory-map tag（TinyOS 的核心）

type 6 tag 的 payload：

```text
u32 entry_size          （每条 entry 的字节数，运行时读取）
u32 entry_version       （= 0）
entry[0]: u64 addr | u64 len | u32 type | u32 zero
entry[1]: ...
```

entry 的 `type`：`1` = available（可用 RAM）、`2` = reserved、`3` = ACPI 可回收、
`4` = NVS、`5` = bad。**addr/len 都是 64 位**，必须用 64 位打印避免截断（Lesson 05 的
`print_hex64` 就是为了这个）。

### 2.4 概念四：两条信息流方向相反

| 方向 | 载体 | 例子 |
|---|---|---|
| 内核 → GRUB | header tags（在镜像里，0.5 课） | Lesson 05 的 information-request tag：type=1、size=12、请求 type 6 |
| GRUB → 内核 | MBI tags（运行时生成，本课） | type-6 mmap、type-2 boot loader name |

Lesson 05 在 header 里加了一个 information-request tag（`.short 1 .short 0 .long 12 .long 6`），
对 GRUB 说「请务必提供 type-6 内存图」。GRUB 读到请求后把 mmap tag 加进 MBI。

---

## 3. 机制精讲与观察方法

### 3.1 源码定位（MBI 构造函数）

```bash
cd "$GRUB_SRC"
grep -R "make_mbi\|multiboot_make_mbi" grub-core/loader | head -10
grep -R "grub_multiboot_make_mbi2\|MULTIBOOT_TAG_TYPE_MMAP\|MULTIBOOT_TAG_TYPE_CMDLINE" \
     grub-core/loader grub-core/../include/grub | head -30
grep -R "grub_machine_mmap_iterate" grub-core/kern grub-core/loader | head -10
```

**预期输出解读**：第一条/第二条定位 MBI 构造入口（`loader/multiboot_mbi2.c`）与 tag 类型常量
（`include/grub/multiboot.h`）；第三条是内存图数据来源——i386-pc 上通过 BIOS 中断收集
（类似 E820），这正是「GRUB 问 BIOS，再把答案转成 tag 交给内核」的链条。

### 3.2 观察一：Lesson 01 的 header——无请求

```bash
readelf -x .multiboot lessons/lesson-01-stable/build/kernel.elf
```

实测（节选，2026-08-06）：

```text
0x00100000 d65052e8 00000000 18000000 12afad17 .PR.............
0x00100010 00000000 08000000                   ........
```

**解读**：length=0x18=24，只有四字段 + end tag，**没有** information-request tag。于是 GRUB
按默认集生成 MBI（仍含 memory map 等基础 tag），TinyOS 用 walker 自己找 type 6。

### 3.3 观察二：Lesson 05 的 header——显式请求 type 6

```bash
readelf -x .multiboot lessons/lesson-05-stable/build/kernel.elf
```

实测（2026-08-06）：

```text
0x00100000 d65052e8 00000000 28000000 02afad17 .PR.....(.......
0x00100010 01000000 0c000000 06000000 00000000 ................
0x00100020 00000000 08000000                   ........
```

逐段解读（小端）：

| 字节 | 值 | 含义 |
|---|---|---|
| `d6 50 52 e8` | 0xe85250d6 | magic |
| `00 00 00 00` | 0 | architecture = i386 |
| `28 00 00 00` | 0x28 = 40 | length（16 头 + 12 请求 + 4 填充 + 8 end） |
| `02 af ad 17` | 0x17adaf02 | checksum（`-(magic+arch+0x28)`，可验算） |
| `01 00 00 00 0c 00 00 00` | type=1, size=12 | information-request tag 头 |
| `06 00 00 00` | 请求类型 6 | 请求 memory map |
| `00 00 00 00` | — | 8 字节对齐填充 |
| `00 00 00 00 08 00 00 00` | type=0, size=8 | end tag |

这是「header tag → GRUB → MBI tag」双向流的请求侧实物证据。

### 3.4 观察三：TinyOS 的 MBI walker（Lesson 05 kernel.c）

`lessons/lesson-05-stable/kernel.c` 的关键常量与结构（只读摘录）：

```c
#define MB2_TAG_END 0
#define MB2_TAG_MMAP 6
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
```

walker 算法步骤（对应 `kernel.c` 的遍历函数）：
1. 校验 `magic == 0x36d76289` 且 `mbi_address & 7 == 0`（EBX 应 8 对齐）；
2. 读 `total_size`（MBI 首 4 字节），`pos = mbi + 8` 指向第一个 tag；
3. 循环：`tag->type == 6` 时解析 `entry_size`/`entry_version`，再按 `entry_size` 步进遍历
   entry（`print_mmap_entry` 以 `NN aaaa... +llll... type` 格式输出）；
4. `type == 0` 且 `size == 8` 时结束；否则 `pos += (tag->size + 7) & ~7`。

`__attribute__((packed))` 是必须的：否则 `u64 addr` 会因对齐填充把字段读错位。

### 3.5 观察四：Lesson 06–08 如何复用同一份 MBI

```bash
grep -rn "MB2_TAG_MMAP\|mbi_address\|struct mb2_tag" \
     lessons/lesson-06-stable/kernel.c lessons/lesson-07-stable/kernel.c \
     lessons/lesson-08-stable/kernel64.c | head -15
```

**预期输出解读**（2026-08-06 只读）：Lesson 06/07 在分页分配器里用同一 walker 找出可用
entry 并避开与 `mbi_address..mbi_address+total_size` 重叠的页；Lesson 08 的 `kernel64.c`
在 64 位侧再次遍历 MBI 找 type 6，用 `alloc64` 从可用区域取页。**结论**：一份 MBI 从
GRUB 出生后，被 Lesson 05（显示）、06/07（分页避让）、08（64 位分配）反复消费。

---

## 4. 数据流与运行逻辑

```text
boot 命令
  → grub_loader_boot()
  → 生成 MBI：
      ├─ tag type 2 boot loader name（"GNU GRUB ..."）
      ├─ tag type 1 cmdline（若有）
      ├─ tag type 6 memory map（BIOS E820 数据转写）
      ├─ tag type 9 elf sections（0.4 装载时的 ELF 信息）
      ├─ （按请求/平台）type 4/8/11/12 ...
      └─ tag type 0 end
  → EAX = 0x36d76289，EBX = MBI 地址
  → jmp _start（0x100020）
  → boot.S 压栈传参 → kernel_main32(u32 magic, u32 mbi_address)
  → Lesson 05 walker：从 EBX 出发遍历 tag 链，找到 type 6 打印内存图
```

注意：MBI 是**运行时产物**，`kernel.elf`/`kernel.iso` 里没有它——观察它的唯一方式就是让
TinyOS walker 在 QEMU 里跑起来（Lesson 05 的 `mmap` 命令，画面见
[`lesson-05-stable/README.md`](../../lessons/lesson-05-stable/README.md)）。

---

## 5. 观察与验证

### 5.1 依赖

`binutils`（readelf）；`$GRUB_SRC` 源码阅读；只读翻阅 Lesson 05–08 的 `.c` 文件。

### 5.2 复现命令清单

```bash
readelf -x .multiboot lessons/lesson-01-stable/build/kernel.elf  # 无请求（0x18）
readelf -x .multiboot lessons/lesson-05-stable/build/kernel.elf  # 请求 type 6（0x28）
grep -rn "MB2_TAG_MMAP" lessons/lesson-05-stable/kernel.c        # walker 常量
cd "$GRUB_SRC" && grep -R "multiboot_make_mbi" grub-core/loader  # MBI 构造入口
```

### 5.3 实测记录（2026-08-06，全部只读）

Lesson 01 header 24 字节无请求 tag；Lesson 05 header 40 字节含 information-request tag
（type=1, size=12, 请求 6）；两 header 的 checksum 均自洽；Lesson 05–08 源码共享
`MB2_TAG_END=0`、`MB2_TAG_MMAP=6`、`struct mb2_tag{type,size}` 结构。

### 5.4 安全边界（本课红线）

只读 ELF 与课程源码；MBI 无法静态导出，运行观察须按 Lesson 05 的流程在个人副本/QEMU 中
进行；不执行修改工具；stable 产物不覆盖。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| walker 卡死或越界 | 未按 `(size+7)&~7` 对齐步进 | 检查 `rounded` 计算与 `end` 边界（Lesson 05 kernel.c） |
| mmap 打印出乱值 | 结构体未 `packed`，字段错位 | 检查 `struct mb2_mmap_entry` 是否有 `__attribute__((packed))` |
| 找不到 type 6 tag | header 未请求而 GRUB 未生成，或 walker 没遍历到 | 用 `readelf -x .multiboot` 核对 Lesson 05 header；检查 end tag |
| `magic != 0x36d76289` | 传参顺序错（cdecl 右到左） | 查 `boot.S` 的 `pushl %ebx; pushl %eax` |
| `entry_size` 与 C 结构体步长不一致 | 写死了步长 | 必须运行时读 `entry_size` 步进 |
| 分页后 MBI 被页表覆盖 | 分配页与 MBI 重叠 | 参考 Lesson 06/07 的 `ranges_overlap` 避让 |

---

## 7. 与 Linux 源码对照

- `linux-v6.12/arch/x86/kernel/e820.c`：Linux 的 e820 内存图与 GRUB 的 type-6 mmap 同源——
  都来自 BIOS 的 E820/内存探测，只是 struct 不同；
- `linux-v6.12/drivers/firmware/efi/`：UEFI 平台下 GRUB 生成的 EFI tags 与 Linux 的 EFI
  内存映射对应。

**边界提醒**：Multiboot2 规范定义 tag 格式，GRUB 是实现者，Linux 不生成/不消费 MBI；
对照仅限「内存图数据同源」。

---

## 8. 思考题与练习

1. 概念理解：为什么 tag 遍历必须用运行时读到的 `size` 步进而不能固定间隔？
2. 源码定位：在 `$GRUB_SRC` 的 `include/grub/multiboot.h` 中列出所有
   `MULTIBOOT_TAG_TYPE_*` 常量，找出 Lesson 05 请求的 type 6 与 end type 0。
3. 动手观察：用 `readelf -x .multiboot` 对比 Lesson 01 与 Lesson 05 的 header，
   找出多出的 16 字节逐字段解释。
4. 实验（个人副本）：给 Lesson 05 的 header 去掉 information-request tag，重建后运行
   `mmap`，观察 GRUB 是否仍提供 type 6（体会「请求」与「默认」的差别）。
5. 综合：画出「Lesson 05 walker → GRUB MBI → BIOS E820」的数据来源链，标出每一跳的
   权威来源（规范/GRUB/BIOS）。

---

## 9. 本课小结与下一课预告

**小结**：MBI 是 GRUB 在 `boot` 瞬间拼出的 tag 列表：`{u32 total_size, u32 reserved, tags…}`
以 type=0/size=8 的 end tag 收尾；GRUB 按平台与内核请求生成 cmdline、boot loader name、
memory map、ELF sections 等 tag；type-6 mmap 是 TinyOS 的核心；Lesson 05–08 的 walker
共享同一套 `mb2_tag` 结构与对齐步进规则。

**下一课预告**：进入 [`lesson-0.7-stable`](../lesson-0.7-stable/README.md)，回答「这些 tag
为什么在不同机器上不一样」：BIOS/legacy（i386-pc，BIOS INT 13/15）与 UEFI（x86_64-efi，
EFI 服务）两条平台分支如何影响 core image、模块与 MBI 的生成。

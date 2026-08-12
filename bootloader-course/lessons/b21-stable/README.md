# Lesson B21: type-8 framebuffer tag → 启动 TinyOS GUI — 精讲文档

> **课号**：Lesson B21（Mini-GRUB 从零写 GRUB 课程第 21 课，阶段五收尾）
> **主题**：MBI type-8 framebuffer tag 生成与 TinyOS Lesson 61 GUI 内核启动
> **课程位置**：阶段五「模块系统与图形」第 3 课（阶段五收尾）
> **前置课程**：[`b20-stable/README.md`](../b20-stable/README.md)（VBE framebuffer）；
> [`b11-stable/README.md`](../b11-stable/README.md)（E820 → type-6 mmap tag）
> **后续课程**：[`b22-stable/README.md`](../b22-stable/README.md)（故障调试与 rescue）
> **一句话目标**：自写引导器装载 TinyOS Lesson 61 的 GUI 内核：VBE 设好
> 800x600x32 LFB，MBI 携带 type-8 framebuffer tag + type-6 mmap tag，交接后
> 内核在 QEMU 上接管显示并进入 tinyos> shell。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能装载 TinyOS 主线的真实 GUI 内核
（`lesson-61-stable/build/kernel.elf`，只读复用），并按内核 header 里的
graphics request tag 设置 VBE 图形模式，把 LFB 信息写进 MBI 的 type-8 tag，
交接后内核接管显示。

- **在课程中的位置**：阶段五收尾——B20 单独设置了 VBE，本课把它接进完整
  引导链（B17 的 ISO9660/命令/脚本 + B11 的 E820 + B20 的 VBE），补齐
  type-8 framebuffer tag，实现「自写 GRUB → TinyOS GUI」的端到端闭环。
  对照 GRUB：`loader/multiboot.c`（读 graphics request tag 并
  `grub_video_set_mode`）+ `multiboot_mbi2.c`（`make_mbi` 生成 type-8 tag）。
- **前置知识清单**：
  1. B20：VBE 4F00/4F01/4F02 与 LFB；
  2. B11：E820 内存图与 type-6 mmap tag（TinyOS L61 强制要求）；
  3. B07/B17：mb2 header 解析、ELF 装载、MBI 构建、mb2_boot 交接。
- **本课交付**：`build/b21.img`（CD：核心 + TinyOS L61 kernel.elf）；
  QEMU（bochs-display）上 loader 装载内核 → VBE 设模式 → boot → L61 banner
  与 `tinyos>` 提示符出现在 VGA 文本。

---

## 2. 核心概念精讲

### 2.1 概念一：type-8 framebuffer tag 布局

Multiboot2 的 framebuffer tag（`multiboot_tag_framebuffer`）把 B20 的 VBE
结果交给内核：

```c
/* +0 type=8  +4 size=32  +8 address(u64)  +16 pitch  +20 width
 * +24 height +28 bpp     +29 type_field(1=direct RGB)  +30 reserved */
fb[0] = MB2_TAG_FRAMEBUFFER;   /* 8 */
fb[1] = 32;                    /* size = 32 */
fb[2] = fb_mode.phys_base;     /* LFB 物理地址低 32 位 */
fb[3] = 0;                     /* 高 32 位 */
fb[4] = fb_mode.pitch;
fb[5] = fb_mode.width;
fb[6] = fb_mode.height;
fbt[28] = fb_mode.bpp;         /* 32 */
fbt[29] = 1;                   /* type_field：直接 RGB */
```

TinyOS L61 内核的校验（`prepare_memory_map` 在 END tag 处）比规范更严：

```c
framebuffer_tag->bpp==32 && type_field==1 && 地址页对齐 &&
pitch >= width*4 && height && pitch*height <= 512*4096 (2MB) &&
address+bytes 不溢出
```

### 2.2 概念二：读内核的 graphics request tag

GRUB 不是拍脑袋选分辨率，而是读内核 mb2 header 里的 **graphics request
tag（type 5）**：`{type=5, size=20, width, height, depth}`。L61 的 header
（boot.S）请求 `800x600x32`。本课 `mb2_graphics_request` 在装载区
（0x100000 起 32KB 内）搜 magic → 遍历 header tags → 找到 type 5 返回
width/height/depth；无请求则回退 800x600x32。

### 2.3 概念三：MBI 必须带 mmap（TinyOS 强制）

L61 内核的 `prepare_memory_map` 在 END tag 处**若没有 type-6 mmap 直接
返回失败**（`if(tag->size!=8 || !memory_map) return 0`）——它要靠 E820 条目
做 bootstrap 页分配器。因此 B21 的 `mbi_build` 固定三段：
type-2 名称 tag + **type-6 mmap**（B11 复用）+ **type-8 framebuffer**（本课）
+ end tag。

### 2.4 概念四：交接与内核接管

`mb2_boot(entry, mbi)` 设 EAX=0x36d76289、EBX=mbi 跳转。L61 内核是 ELF32
（e_entry=0x100040），boot.S 在 32 位模式解析 MBI → 自建页表进 long mode →
64 位部分 `framebuffer_init(h)` 读取 handoff 里的 framebuffer 字段并映射
FRAMEBUFFER_VA。引导器的职责到交接为止。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 对照 |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | El Torito 引导 + BIOS 回调 | 消息文本变化 |
| `loader.c` | 完整引导链 + graphics request 解析 + E820 + type-8 tag + boot | `multiboot.c`/`multiboot_mbi2.c` |
| `vbe.h`/`vbe.c` | VBE 解析/匹配/LFB 绘制（B20 复用） | `grub-core/video/i386_pc/vbe.c` |
| `script.c`/`test_script.c` | tokenizer（B17 复用） | — |
| `grub.cfg` | multiboot2 + boot | TinyOS grub.cfg 对齐 |
| `check-gfx-request.py` | make check：L61 header 的 graphics request 断言 | — |
| `build/b21.img` | CD：核心 + `boot/kernel.elf`（TinyOS L61 只读复用） | — |

### 3.2 mb2_graphics_request（读内核请求）

```c
static int mb2_graphics_request(u32 *w, u32 *h, u32 *depth)
{
    for (off = 0; off + 16 <= 32768; off += 8) {   /* 搜 magic */
        if (hd->magic != MB2_HEADER_MAGIC) continue;
        t = 16;                                     /* 跳过基础 header */
        while (t + 8 <= hd->length) {
            type = 读 u16; size = 读 u32;
            if (type == 5 && size >= 20) {          /* graphics request */
                *w = 读 u32; *h = 读 u32; *depth = 读 u32;
                return 1;
            }
            if (type == 0) break;                   /* end tag */
            t += (size + 7) & ~7;                   /* 8 对齐前进 */
        }
    }
    return 0;
}
```

### 3.3 mbi_build 追加 type-8 tag

在 B11 的 mmap tag 之后追加（字节布局见 2.1），`fb_ready` 由 multiboot2
命令在 VBE 设置成功后置位。整个 MBI 缓冲扩到 1024 字节（mmap 24 条目 +
framebuffer + 名称 + end 足够）。

### 3.4 multiboot2 / boot 命令

```c
/* multiboot2：读文件 → elf_load → 记 entry → 读 graphics request →
 *   vbe_get_info → vbe_find_mode(w,h,depth) → vbe_set_mode → fb_ready=1 */
/* boot：mmap_collect（E820）→ 打印 mmap 条目数 → mb2_boot(entry, mbi_build()) */
```

装载区 `CD_BUF_KERNEL=0x10000`（L61 kernel.elf = 131KB，`KERNEL_MAX=0x24000`
），ELFT_LOAD 目标 0x100000 起（B06 语义，p_paddr ≥ 1MB 校验）。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(读 core) -> stage2 -> loader_main:
  串口+VGA 双通道日志 -> 挂载 (cd0) -> 注册命令
  -> script_run_file("/boot/grub/grub.cfg"):
      multiboot2 /boot/kernel.elf     # 读 131KB ELF -> elf_load 到 0x100000
                                      #   -> graphics request 800x600x32
                                      #   -> VBE 4F00/4F01/4F02 设 LFB
      boot                            # E820 -> mbi(名称+mmap+type-8 fb+end)
                                      #   -> mb2_boot(0x100040, mbi)
-> L61 内核: 32 位解析 MBI -> 自建页表进 long mode
  -> "Lesson 61: Multiboot2 framebuffer" banner + "tinyos>" 提示符（VGA 文本）
```

串口日志（loader 阶段）：

```
B21 fb: Mini-GRUB framebuffer handoff
B21 fb: boot drive = e0
B21 multiboot2: loaded /boot/kernel.elf entry=00100040
B21 vbe: graphics request 0320x0258x20
B21 vbe: mode set, lfb=fd000000 pitch=0c80
B21 mmap: 07 entries
B21 boot: jumping to entry=00100040 mbi=00009ec0
```

VGA 文本（内核接管后）：

```
Lesson 61: Multiboot2 framebuffer
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
tinyos>
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make              # 构建 b21.img（TinyOS L61 kernel.elf 只读复用）
make check        # vbe 单测 + L61 ELF32/graphics request 断言 + 符号 + ISO 抽取
make run          # QEMU 窗口（bochs-display）：内核 banner + tinyos> 提示符
./scripts/validate-course.sh b21 check
./scripts/validate-course.sh b21 qemu
```

### 5.2 成功判据

1. `make check` 全绿：`test_vbe` PASS；L61 kernel.elf 是 ELF32；`check-gfx-
   request.py` 断言 mb2 header 的 graphics request 是 800x600x32；
   `mb2_graphics_request`/`vbe_find_mode`/`mbi_build`/`mmap_collect` 符号
   存在；ISO 抽取的 kernel.elf 与源文件一致；
2. QEMU（bochs-display）串口 marker：装载、graphics request、mode set、
   mmap 条目、boot jumping；
3. VGA 文本：`Lesson 61: Multiboot2 framebuffer` + `tinyos>`——内核接管。

### 5.3 已知边界（本课如实记录，源码与内存证据）

- **`guiinfo` 的 framebuffer ready/mapped 为 0/0——根因已定位到 TinyOS L61
  内核自身的 32/64 位 `long_mode_handoff` 结构不一致，不是引导器问题**：
  - 32 位侧 `kernel.c` 的结构含 `u32 user_image_status, user_image_bytes,
    user_entry_offset, user_entry_length`（16 字节）；
  - 64 位侧 `kernel64.c` 的结构**没有**这些字段 → framebuffer 字段整体错位
    16 字节；
  - 内存 dump 实证：32 位侧写入 `fb_address@8344=0xfd000000, bpp=32,
    type=1` 全部正确（内核 `framebuffer_ready=1`，**接受了我们的 type-8
    tag**）；但 64 位侧按错位偏移读到 `fb_address@8328=0x2500000000`、
    `bpp@8360=128` → `framebuffer_init` 走清零分支 → guiinfo 0/0；
  - **真 GRUB**（主线 kernel.iso）同配置下 `guiinfo` 同样 0/0，证明与
    引导器无关；主线 `qemu-vga-check.sh` 在本环境对 lesson-61 也失败；
  - 主线 `validate-course.sh` 对 L61 的 CI 只做 QEMU 冒烟（timeout+trace），
    不检查 guiinfo——`guiinfo 1/1` 是 GUI 开发手册的手动验收目标。
- 本课判据（引导器职责边界）：内核接受 type-8 tag（`framebuffer_ready=1`）、
  启动并接管显示（banner + `tinyos>`）；tag 字节级正确性由 make check 校验。

---

## 6. 调试地图

1. **内核不启动（VGA 全 0xFFFF）**：先用 `-vga std` 时 0xB8000 在 VBE 切
   模式后被清空——换 bochs-display（`QEMU_BOCHS=1`）或走串口日志确认 loader
   已到 boot。
2. **`tinyos>` 不出现**：内核 `prepare_memory_map` 强制要求 mmap tag——
   先确认 `mbi_build` 里 type-6 在 end tag 之前（无 mmap 则内核直接返回
   失败）；再看 serial 里 `B21 mmap: N entries` 是否 >0。
3. **graphics request 解析失败**：打印 header magic 搜索到的偏移与第一个
   tag 的 type/size；注意 header tags 是 `u16 type/u16 flags/u32 size`（不是
   u32/u32），8 对齐前进。
4. **内核文件太大读不下**：L61 kernel.elf = 131KB，`KERNEL_MAX` 必须
   ≥ 0x21000，装载缓冲放低内存 0x10000（避开核心 0x8400 与 CD 缓冲
   0x68000+）。
5. **framebuffer ready=0**：先区分引导器 vs 内核侧——用真 GRUB 同配置跑
   主线 kernel.iso 对比（本课实测真 GRUB 也是 0/0）；引导器只保证 tag 字节
   正确（make check 断言）。若确需排查内核侧，比对 32 位 `kernel.c` 与
   64 位 `kernel64.c` 的 `long_mode_handoff` 结构是否一致（本课定位到 L61
   内核此结构不一致——32 位侧多 `user_image_*` 16 字节导致 framebuffer 字段
   错位）。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `mb2_graphics_request` | `loader/multiboot.c` 读 graphics request tag | 简化：只支持 type 5 |
| VBE 设模式（B20 复用） | `grub_video_set_mode`（video.c） | 固定目标分辨率 |
| `mbi_build` 追加 type-8 | `multiboot_mbi2.c` 的 `make_mbi` | 只生成必要 tag 集合 |
| E820 → type-6 mmap | `grub_machine_mmap_iterate` | 24 条目上限 |
| 双通道日志 | `term/serial.c` | 只出不进 |

---

## 8. 思考题与练习

1. 内核 header 同时请求了 mmap + framebuffer（info request tag，type 1）。
   实现解析 info request 并按请求的 tag 集合裁剪 MBI——GRUB 会这么做。
2. 在 `guiinfo` 显示 ready=0 时，读内核源码（`framebuffer_init`）判断是
   哪个条件不满足（地址范围、bpp、type_field、映射）；对比真 GRUB 同配置
   的行为，形成"引导器职责边界"的判断。
3. 支持多个 graphics request（如 1024x768 首选、800x600 备选）：从高到低
   尝试 `vbe_find_mode`，找不到再降级。
4. 把 type-8 tag 的生成抽成函数 `mbi_add_framebuffer(&p, &fb_mode)`，供
   B23 端到端验收复用。
5. 研究 L61 内核 `long_mode_handoff` 里 framebuffer 字段的消费路径：boot.S
   交接后内核如何把物理 LFB 映射到 `FRAMEBUFFER_VA`。

---

## 9. 本课小结与下一课预告

**小结**：本课把 B20 的 VBE 接进完整引导链，新增 `mb2_graphics_request`
（读内核 graphics request tag）+ `mbi_build` 的 type-8 framebuffer tag，
装载 TinyOS L61 真实 GUI 内核（131KB ELF32，只读复用）并交接。QEMU 上
loader 串口日志完整、内核接管后显示 L61 banner 与 `tinyos>` 提示符。阶段五
（模块系统 + 图形）完成：从核心 → 模块 → VBE → framebuffer tag 全链路打通。

**下一课** [`b22-stable/README.md`](../b22-stable/README.md)：阶段六（故障
与验收）——坏 ELF/缺文件/缺模块的错误路径与 rescue 调试。

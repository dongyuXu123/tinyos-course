# Lesson B20: VBE framebuffer — 精讲文档

> **课号**：Lesson B20（Mini-GRUB 从零写 GRUB 课程第 20 课，阶段五第 2 课）
> **主题**：VBE 显卡 BIOS 扩展：4F00/4F01/4F02、线性帧缓冲（LFB）
> **课程位置**：阶段五「模块系统与图形」第 2 课
> **前置课程**：[`b19-stable/README.md`](../b19-stable/README.md)（模块系统）；
> [`b05-stable/README.md`](../b05-stable/README.md)（实模式回调）
> **后续课程**：[`b21-stable/README.md`](../b21-stable/README.md)（type-8 framebuffer
> tag → 启动 TinyOS GUI）
> **一句话目标**：loader 查询并设置一个 800x600x32 的 VBE 线性帧缓冲，往 LFB
> 里画测试图案——为 TinyOS Lesson 61 的 framebuffer 交接做准备。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 能调 VBE BIOS 扩展（INT 10 4F00/4F01/
4F02）查询显卡能力、协商并设置 800x600x32 图形模式，拿到 LFB 物理地址与
pitch，直接往帧缓冲写像素画出色块/渐变。

- **在课程中的位置**：研读支线 0.6 提到 GRUB 会按内核请求生成 type-8
  framebuffer tag；前置是 GRUB 自己先用 VBE 设置图形模式。B20 做设置，B21
  做 tag 交接。对照 `grub-core/video/i386_pc/vbe.c`（`grub_vbe_get_mode_info`
  /`grub_vbe_set_mode`）。
- **前置知识清单**：
  1. B05：实模式回调（VBE 的 INT 10 必须在实模式执行）；
  2. VBE 3.0 调用约定：INT 10 `AX=4F00`（控制器信息）、`4F01`（模式信息）、
     `4F02`（设置模式）；`AL=0x4F` 返回表示"支持"；
  3. ModeInfoBlock 布局（字段偏移）与直接色模型。
- **本课交付**：`build/b20.img`（CD）；QEMU 上 loader 设置 800x600x32 后画
  渐变 + 红/绿/蓝三色块，screendump 像素探针逐点校验颜色。

---

## 2. 核心概念精讲

### 2.1 概念一：VBE 调用约定（INT 10 AX=4Fxx）

VBE（VESA BIOS Extension）是显卡 BIOS 在 INT 10 上的一套扩展，三件套：

| 调用 | 输入 | 输出 |
|---|---|---|
| AX=4F00 | ES:DI → 256B 缓冲 | VbeInfoBlock：签名 "VESA"、版本、`video_mode_ptr`（模式列表 far pointer） |
| AX=4F01 | CX=模式号，ES:DI → 256B 缓冲 | ModeInfoBlock：分辨率/色深/pitch/LFB 地址 |
| AX=4F02 | BX=模式号（bit14=LFB），ES:DI → ModeInfoBlock | 进入图形模式 |

统一封装（本课 `vbe_call`）：`AX=0x4Fxx`，返回 `AL=0x4F` 表示 VBE 功能被
支持；失败时 `AL!=0x4F`。**注意返回判据是 AL（低字节）而不是 AX**——
`AX=0x014F` 时低字节才是 0x4F。

### 2.2 概念二：ModeInfoBlock 关键字段（对照 vbe.h）

4F01 返回的 256 字节块里，本课只关心这些偏移：

| 偏移 | 字段 | 说明 |
|---|---|---|
| 0x00 | mode_attributes (u16) | bit7 = LFB 可用 |
| 0x10 | bytes_per_scanline (u16) | **pitch**（一行字节数） |
| 0x12 | x_resolution (u16) | 宽 |
| 0x14 | y_resolution (u16) | 高 |
| 0x19 | bits_per_pixel (u8) | 色深 |
| 0x1B | memory_model (u8) | 6 = 直接色 |
| 0x28 | phys_base_addr (u32) | **LFB 物理地址** |

`vbe.c` 的 `vbe_parse_mode_info` 按这些偏移解析，`vbe_mode_matches` 判定
是否满足目标（800x600x32 + LFB + 直接色）——纯数据操作，无硬件依赖，可
主机单测。

### 2.3 概念三：模式协商（遍历模式列表）

不硬编码模式号，而是走 GRUB 的做法：

1. 4F00 拿 VbeInfoBlock，读偏移 0x0E 的 `video_mode_ptr`（far pointer，
   seg:off）；
2. far pointer 转物理地址 `(seg<<4)+off`，指向 u16 数组（模式列表），
   `0xFFFF` 结束；
3. 逐个 4F01 查询，解析 ModeInfoBlock，`vbe_mode_matches` 找到第一个
   800x600x32 且带 LFB 的模式。

```c
static int vbe_find_mode(u16 *out_mode, struct vbe_mode_info *out)
{
    u32 mode_ptr = 读取 VbeInfoBlock 偏移 0x0E;         /* seg:off */
    const u16 *list = (const u16 *)((mode_ptr >> 16 << 4) + (mode_ptr & 0xFFFF));
    for (i = 0; list[i] != 0xFFFF; i++) {
        vbe_get_mode_info(list[i]);                    /* 4F01 */
        vbe_parse_mode_info(buf, out);
        if (vbe_mode_matches(out, 800, 600, 32)) { *out_mode = list[i]; return 0; }
    }
    return -1;
}
```

### 2.4 概念四：LFB 写入与输出模式切换

- `phys_base_addr` 是 LFB 物理地址，32bpp 直接色像素 = `0x00RRGGBB`；
  保护模式 1:1 映射下 `fb_putpixel` 直接写：
  ```c
  *(volatile u32 *)(fb_base + y * fb_pitch + x * 4) = color;
  ```
- **切模式副作用**：4F02 设置 VBE 后 0xB8000 文本模式失效，先前写入的文本
  被清空。因此验证日志要双通道：VGA 文本（切模式前）+ 串口 COM1
  （切模式后依然可读，对照 GRUB 的串口终端 `grub-core/term/serial.c`）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 对照 |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | El Torito 引导 + BIOS 回调 | 消息文本变化 |
| `vbe.h` | 共享头：`struct vbe_mode_info` 与函数声明 | `include/grub/i386/pc/vbe.h` |
| `vbe.c` | ModeInfoBlock 解析 + 匹配判定 + LFB 绘制（无硬件） | `grub_vbe_get_mode_info` 的数据部分 |
| `loader.c` | 4F00/4F01/4F02 封装 + 模式协商 + 双通道日志 | `grub_vbe_set_mode` + 串口终端 |
| `test_vbe.c` | 主机单测（合成 ModeInfoBlock + 越界绘制） | 课程单测约定 |
| `build/b20.img` | CD 镜像 | — |

### 3.2 vbe.c：解析与匹配（可单测）

```c
int vbe_parse_mode_info(const u8 *block, struct vbe_mode_info *out)
{
    out->mode_attributes = block[0] | (block[1] << 8);
    out->pitch           = block[0x10] | (block[0x11] << 8);
    out->width           = block[0x12] | (block[0x13] << 8);
    out->height          = block[0x14] | (block[0x15] << 8);
    out->bpp             = block[0x19];
    out->memory_model    = block[0x1B];
    out->phys_base       = 小端读 block[0x28..0x2B];
    return 0;
}
int vbe_mode_matches(const struct vbe_mode_info *m, u16 w, u16 h, u8 bpp)
{
    return (m->mode_attributes & (1u << 7)) &&      /* LFB 可用 */
           m->width == w && m->height == h && m->bpp == bpp &&
           m->memory_model == 6;                    /* 直接色 */
}
```

### 3.3 loader.c：VBE 三件套封装

```c
static int vbe_call(u16 ax, u16 cx, u16 bx, u16 buf)
{
    regs.eax = ax; regs.ebx = bx; regs.ecx = cx;
    regs.es = buf >> 4; regs.edi = buf & 0xF;       /* ES:DI → 低内存缓冲 */
    bios_interrupt(0x10, &regs);
    return ((regs.eax & 0xFF) == 0x4F) ? 0 : -1;
}
static int vbe_get_info(void)      { vbe_call(0x4F00, ...); 校验 "VESA"; }
static int vbe_get_mode_info(u16 m) { vbe_call(0x4F01, m, ...); }
static int vbe_set_mode(u16 m)     { vbe_call(0x4F02, 0, m | 0x4000, ...); }
```

`VBE_INFO_BUF=0x7000`/`VBE_MODE_BUF=0x7200` 是低内存缓冲（<1 MiB），实模式
ES:DI 可直接寻址，回调填充后保护模式读回。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(读 core) -> stage2 -> loader_main:
  serial_init() + vga_clear()
  vbe_get_info()  4F00 -> "VESA" 签名 OK
  vbe_find_mode() 遍历模式列表 4F01 -> 800x600x32 + LFB
  vbe_set_mode()  4F02 进入图形模式
  fb_setup(phys_base, pitch, 800, 600)
  fb_draw_pattern() 渐变 + 红/绿/蓝三色块
  halt
```

串口日志（QEMU `-serial file:` 捕获）：

```
B20 vbe: VBE framebuffer demo
B20 vbe: VBE controller OK (VESA signature)
B20 vbe: mode=0114 800x600x32 lfb=00fd0000 pitch=0320
B20 vbe: mode set, drawing test pattern to LFB
B20 vbe: pattern drawn, halting
```

screendump 像素探针（800x600 PPM）：

```
(125,125) = ff0000  红块锚点
(425,325) = 00ff00  绿块锚点
(675,475) = 0000ff  蓝块锚点
```

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make              # 构建 b20.img
make check        # vbe.c 主机单测 + 符号断言 + El Torito 记录
make run          # QEMU 窗口：图形模式 + 测试图案
./scripts/validate-course.sh b20 check
./scripts/validate-course.sh b20 qemu
```

### 5.2 成功判据

1. `make check` 全绿：`test_vbe` 解析/匹配/越界绘制 14 项 PASS，
   `vbe_get_info`/`vbe_find_mode`/`vbe_set_mode`/`vbe_parse_mode_info` 符号
   存在（`noinline` 保护）；
2. QEMU 串口 marker：VBE 控制器 OK、mode= 0xNNNN 800x600x32、lfb=/pitch=、
   mode set、pattern drawn；
3. screendump 像素探针：三色块锚点颜色精确匹配（红 ff0000/绿 00ff00/
   蓝 0000ff），LFB 物理地址非 0、pitch ≥ width×4。

---

## 6. 调试地图

1. **VBE 调用总失败**：`vbe_call` 的返回判据写成 `(regs.eax & 0xFFFF) ==
   0x4F` 会误判（AX=0x014F 时高字节是功能号）——必须判低字节 `(regs.eax &
   0xFF) == 0x4F`。
2. **文本 marker 全丢**：4F02 切到图形模式后 0xB8000 内容被清空，VGA 文本
   校验必然失败——验证日志改走串口（QEMU_SERIAL=1），或检查在切模式前把
   关键信息打出来。
3. **模式协商找不到目标**：先打印 `video_mode_ptr` 与模式列表前几个值确认
   far pointer 转换正确（`(seg<<4)+off`），再确认 QEMU 的 std VGA 是否支持
   800x600x32（`-vga std` 支持 VBE 3.0）。
4. **LFB 写不进去/花屏**：确认 `phys_base` 来自 ModeInfoBlock 偏移 0x28
   （不是 0x24），pitch 用 `bytes_per_scanline`（不是 width×4，可能有填充）；
   直接色像素是 `0x00RRGGBB`。
5. **`-Os` 内联吞符号**：`vbe_call`/`vbe_find_mode` 等静态函数被内联后
   `make check` 符号断言失败——`__attribute__((noinline))` 保护（B19 同款）。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `vbe_call`（4F00/4F01/4F02） | `grub_vbe_get_mode_info`/`grub_vbe_set_mode` | 简化参数面 |
| `vbe_parse_mode_info` | `vbe.h` 的 `struct grub_vbe_mode_info_block` | 用偏移读取 vs 结构体 |
| 模式协商（遍历列表） | `video.c` 的 `grub_video_vbe_setup` | 固定目标分辨率 |
| LFB 绘制 | `video_fb.c` 的 framebuffer 抽象 | 无字体/合成层 |
| 串口日志 | `term/serial.c` 串口终端 | 只出不进 |

---

## 8. 思考题与练习

1. 把 `vbe_find_mode` 改成优先选"最高分辨率且色深≥32"的模式（GRUB 的
   `grub_vbe_bpp_to_mask`/分辨率选择逻辑），而不是固定 800x600。
2. 实现 VBE 保护模式接口（INT 10 的 PM 入口，`pm_entry` 字段）绕过实模式
   回调，比较两种路径的差异。
3. 在 LFB 上画一个 8x8 点阵字符表，把 `vga_puts` 移植成 `fb_puts`——B21
   交接后内核要自己画字（对照 TinyOS L61 的 framebuffer 字体）。
4. 处理 `pitch > width*4`（行填充）的情况：验证当前 `fb_putpixel` 用 pitch
   定位是否正确，并加一个 pitch=width*4+16 的单测。
5. 模式切换后把 VBE 信息（地址/pitch/尺寸/色深）打成一个"图形环境快照"
   结构，B21 直接填进 type-8 framebuffer tag。

---

## 9. 本课小结与下一课预告

**小结**：本课实现 VBE BIOS 扩展三件套（4F00 控制器信息 / 4F01 模式信息 /
4F02 设置模式），通过遍历模式列表协商出 800x600x32 + LFB，按
ModeInfoBlock 的 phys_base 直接写帧缓冲画出测试图案。`vbe.c` 解析/匹配/绘制
无硬件依赖可主机单测；验证用串口日志（切模式后 0xB8000 失效）+ screendump
像素探针双保险。LFB 就绪——下一课把它交给内核。

**下一课** [`b21-stable/README.md`](../b21-stable/README.md)：把
`address/pitch/width/height/bpp/type` 填进 MBI 的 type-8 framebuffer tag，
启动 TinyOS Lesson 61——自写引导器点亮 TinyOS GUI 桌面。

/* Lesson B20: VBE 模式信息解析与帧缓冲绘制（无硬件依赖，可主机单元测试）
 *
 * 本文件不含 BIOS 调用（loader.c 负责 INT 10），只做两件事：
 *   1. 解析 VBE ModeInfoBlock（INT 10 AX=4F01 返回的 256 字节块）字段；
 *   2. 往线性帧缓冲（LFB）写像素/色块/测试图案。
 * 因此既能链进 loader（freestanding），也能在主机上用 test_vbe.c 单测。
 * 对照：include/grub/i386/pc/vbe.h（struct grub_vbe_mode_info_block）
 *       grub-core/video/i386/pc/vbe.c（grub_vbe_get_mode_info/set_mode）
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#include "vbe.h"

/* ---- ModeInfoBlock 关键字段偏移（VBE 3.0 规范）----------------------------
 *   0x00 u16 mode_attributes      0x10 u16 bytes_per_scanline (pitch)
 *   0x12 u16 x_resolution         0x14 u16 y_resolution
 *   0x19 u8  bits_per_pixel       0x1B u8  memory_model
 *   0x28 u32 phys_base_addr (LFB)
 * 详见 include/grub/i386/pc/vbe.h 的 struct grub_vbe_mode_info_block。
 */

/* vbe_parse_mode_info: 从 4F01 返回的 256 字节块解析出模式信息。 */
int vbe_parse_mode_info(const u8 *block, struct vbe_mode_info *out)
{
    out->mode_attributes = (u16)(block[0] | ((u16)block[1] << 8));
    out->pitch           = (u16)(block[0x10] | ((u16)block[0x11] << 8));
    out->width           = (u16)(block[0x12] | ((u16)block[0x13] << 8));
    out->height          = (u16)(block[0x14] | ((u16)block[0x15] << 8));
    out->bpp             = block[0x19];
    out->memory_model    = block[0x1B];
    out->phys_base       = (u32)block[0x28] | ((u32)block[0x29] << 8) |
                           ((u32)block[0x2A] << 16) | ((u32)block[0x2B] << 24);
    return 0;
}

/* vbe_mode_matches: 模式是否满足目标分辨率/色深（LFB 可用 + 直接色模型）。 */
int vbe_mode_matches(const struct vbe_mode_info *m, u16 w, u16 h, u8 bpp)
{
    return (m->mode_attributes & VBE_ATTR_LFB_AVAIL) &&
           m->width == w && m->height == h && m->bpp == bpp &&
           m->memory_model == VBE_MEMORY_MODEL_DIRECT;
}

/* ---- 帧缓冲（32bpp 直接色：0x00RRGGBB）------------------------------------ */
static u32 fb_base, fb_pitch, fb_width, fb_height;

void fb_setup(u32 base, u32 pitch, u32 width, u32 height)
{
    fb_base = base;
    fb_pitch = pitch;
    fb_width = width;
    fb_height = height;
}

void fb_putpixel(u32 x, u32 y, u32 color)
{
    if (x >= fb_width || y >= fb_height)
        return;
    *(volatile u32 *)(fb_base + y * fb_pitch + x * 4u) = color;
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    u32 i, j;
    for (j = y; j < y + h && j < fb_height; j++)
        for (i = x; i < x + w && i < fb_width; i++)
            fb_putpixel(i, j, color);
}

/* fb_draw_pattern: 画测试图案——渐变背景 + 三块纯色矩形（QEMU screendump
 * 像素探针的锚点：红(125,125) 绿(425,325) 蓝(675,475)）。 */
void fb_draw_pattern(void)
{
    u32 x, y;
    for (y = 0; y < fb_height; y++)
        for (x = 0; x < fb_width; x++) {
            u8 r = (u8)(x * 255u / fb_width);
            u8 g = (u8)(y * 255u / fb_height);
            fb_putpixel(x, y, ((u32)r << 16) | ((u32)g << 8) | 0x80u);
        }
    fb_fill_rect(100, 100, 50, 50, 0x00FF0000u);   /* 红 */
    fb_fill_rect(400, 300, 50, 50, 0x0000FF00u);   /* 绿 */
    fb_fill_rect(650, 450, 50, 50, 0x000000FFu);   /* 蓝 */
}

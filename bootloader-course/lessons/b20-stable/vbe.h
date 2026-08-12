/* Lesson B20: vbe.h — 共享头文件（核心与主机单测共用，对照 include/grub/i386/pc/vbe.h）
 *
 * GRUB 用共享头文件保证核心与模块/工具的结构体布局一致；本课照做：
 * vbe.c（解析/绘制）、loader.c（BIOS 调用）、test_vbe.c（主机单测）
 * 全部 include 本头，避免重复声明漂移。
 */
#ifndef MINI_GRUB_VBE_H
#define MINI_GRUB_VBE_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define VBE_ATTR_LFB_AVAIL   (1u << 7)   /* mode_attributes bit7：LFB 可用 */
#define VBE_MEMORY_MODEL_DIRECT 6        /* 直接色（RGB 掩码描述） */

struct vbe_mode_info {
    u16 mode_attributes;
    u16 pitch;          /* bytes_per_scanline */
    u16 width;          /* x_resolution */
    u16 height;         /* y_resolution */
    u8  bpp;            /* bits_per_pixel */
    u8  memory_model;
    u32 phys_base;      /* LFB 物理地址 */
};

int vbe_parse_mode_info(const u8 *block, struct vbe_mode_info *out);
int vbe_mode_matches(const struct vbe_mode_info *m, u16 w, u16 h, u8 bpp);
void fb_setup(u32 base, u32 pitch, u32 width, u32 height);
void fb_putpixel(u32 x, u32 y, u32 color);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_draw_pattern(void);

#endif

/* Lesson B20: vbe.c 主机单元测试（无硬件，构造合成 ModeInfoBlock 验证解析） */
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "vbe.h"

static int failures = 0;

static void check(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    } else {
        printf("ok: %s\n", msg);
    }
}

/* 构造一个 800x600x32 的合成 ModeInfoBlock（VBE 3.0 字段偏移） */
static void make_mode_block(unsigned char *b)
{
    memset(b, 0, 256);
    b[0x00] = 0x81;                 /* mode_attributes bit0 + bit7 (LFB) */
    b[0x01] = 0x00;
    b[0x10] = 3200 & 0xFF;          /* bytes_per_scanline = 800*4 */
    b[0x11] = 3200 >> 8;
    b[0x12] = 800 & 0xFF;           /* x_resolution = 800 */
    b[0x13] = 800 >> 8;
    b[0x14] = 600 & 0xFF;           /* y_resolution = 600 */
    b[0x15] = 600 >> 8;
    b[0x19] = 32;                   /* bits_per_pixel */
    b[0x1B] = 6;                    /* memory_model = direct color */
    b[0x28] = 0x00;                 /* phys_base = 0xFD000000 */
    b[0x29] = 0x00;
    b[0x2A] = 0x00;
    b[0x2B] = 0xFD;
}

int main(void)
{
    unsigned char block[256];
    struct vbe_mode_info m;
    int ok;

    /* 1. 解析 800x600x32 模式块 */
    make_mode_block(block);
    ok = vbe_parse_mode_info(block, &m);
    check(ok == 0, "parse 800x600x32 block returns 0");
    check(m.mode_attributes == 0x0081, "mode_attributes parsed (0x81)");
    check(m.pitch == 3200, "pitch parsed (3200)");
    check(m.width == 800 && m.height == 600, "resolution parsed (800x600)");
    check(m.bpp == 32, "bpp parsed (32)");
    check(m.memory_model == 6, "memory_model parsed (direct color)");
    check(m.phys_base == 0xFD000000u, "phys_base parsed (0xFD000000)");

    /* 2. 匹配判定 */
    check(vbe_mode_matches(&m, 800, 600, 32) == 1, "800x600x32 matches");
    check(vbe_mode_matches(&m, 1024, 768, 32) == 0, "1024x768x32 does not match");
    check(vbe_mode_matches(&m, 800, 600, 16) == 0, "800x600x16 does not match");

    /* 3. 无 LFB 位（bit7 清零）不匹配 */
    block[0x00] = 0x01;
    vbe_parse_mode_info(block, &m);
    check(vbe_mode_matches(&m, 800, 600, 32) == 0, "windowed mode (no LFB) rejected");

    /* 4. 非直接色模型不匹配 */
    make_mode_block(block);
    block[0x1B] = 4;                /* packed pixel */
    vbe_parse_mode_info(block, &m);
    check(vbe_mode_matches(&m, 800, 600, 32) == 0, "non-direct color rejected");

    /* 5. fb_setup/putpixel 越界保护
     * vbe.c 的 fb_base 是 u32（32 位目标）；宿主用 mmap 固定 32 位可寻址
     * 地址（0x20000000）映射一块 LFB，保证 u32 截断后仍是有效指针。 */
    {
        void *map = mmap((void *)0x20000000u, 600u * 3200u,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        unsigned int base;
        if (map == MAP_FAILED) {
            printf("FAIL: cannot mmap fixed LFB\n");
            failures++;
        } else {
            fb_setup(0x20000000u, 3200u, 800u, 600u);
            fb_putpixel(0, 0, 0x00FF0000u);
            fb_putpixel(799, 599, 0x00FF0000u);
            fb_putpixel(800, 0, 0x0000FF00u);   /* 越界，应被忽略 */
            fb_putpixel(0, 600, 0x0000FF00u);   /* 越界，应被忽略 */
            base = *(volatile unsigned int *)0x20000000u;
            check(base == 0x00FF0000u, "pixel written at LFB origin");
            base = *(volatile unsigned int *)(0x20000000u + 599u * 3200u + 799u * 4u);
            check(base == 0x00FF0000u, "pixel written at LFB corner (799,599)");
            /* 越界写入没有破坏缓冲之外/被忽略位置 */
            base = *(volatile unsigned int *)(0x20000000u + 600u * 3200u);
            check(base == 0, "out-of-bounds row not written");
            munmap(map, 600u * 3200u);
        }
    }

    if (failures == 0) {
        printf("test_vbe: all PASS\n");
        return 0;
    }
    printf("test_vbe: %d FAILURES\n", failures);
    return 1;
}

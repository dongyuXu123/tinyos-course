/* Lesson B20: Mini-GRUB stage2 — VBE framebuffer
 *
 * 通过实模式回调调 INT 10 VBE BIOS 扩展（对照 grub-core/video/i386_pc/vbe.c）：
 *   1. AX=4F00：获取 VBE 控制器信息（VbeInfoBlock，校验 "VESA" 签名）；
 *   2. AX=4F01：逐个查询模式（ModeInfoBlock → vbe.c 解析字段）；
 *   3. AX=4F02：设置目标模式（BX = mode | 1<<14 LFB 标志）；
 * 然后按 ModeInfoBlock 的 phys_base 直接往 LFB 写像素（保护模式 1:1 映射）。
 * 简化边界：固定 800x600x32（TinyOS L61 请求的模式）；只走实模式回调，
 * 不实现 VBE 保护模式接口；无调色板路径（只用直接色模型）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---- VGA 文本库（B04；图形模式设置前输出）--------------------------------- */
#define VGA_TEXT_BASE   0xB8000u
#define VGA_COLS        80u
#define VGA_ROWS        25u
#define VGA_ATTR        0x1Fu

static u32 vga_row = 0;
static u32 vga_col = 0;

static volatile u16 *vga_cell(u32 row, u32 col)
{
    return (volatile u16 *)(VGA_TEXT_BASE + 2u * (row * VGA_COLS + col));
}

void vga_clear(void)
{
    u32 i;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
        ((volatile u16 *)VGA_TEXT_BASE)[i] = (u16)(VGA_ATTR << 8);
    vga_row = 0;
    vga_col = 0;
}

void vga_putc(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS)
            vga_row = 0;
        return;
    }
    *vga_cell(vga_row, vga_col) = (u16)((u16)VGA_ATTR << 8) | (u8)c;
    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_ROWS)
        vga_row = 0;
}

void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}

void vga_hex(u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        u8 nib = (u8)((v >> (4 * i)) & 0xF);
        vga_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}

/* ---- BIOS 回调（B05）------------------------------------------------------ */
struct bios_regs {
    u32 eax;
    u16 es;
    u16 ds;
    u16 flags;
    u32 ebx;
    u32 ecx;
    u32 edi;
    u32 esi;
    u32 edx;
};

void bios_interrupt(u8 intno, struct bios_regs *regs);

/* ---- 串口日志（COM1 115200 8N1；VBE 模式切换后 0xB8000 文本失效，
 *        验证与调试走串口——对照 grub-core/term/serial.c）------------------- */
#define COM1 0x3F8u

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);           /* 关中断 */
    outb(COM1 + 3, 0x80);           /* DLAB=1 锁存波特率 */
    outb(COM1 + 0, 0x01);           /* 115200 分频 = 1 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);           /* 8N1 */
    outb(COM1 + 2, 0xC7);           /* FIFO 使能 */
}

static void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20u)) /* THR 空 */
        ;
    outb(COM1 + 0, (u8)c);
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void serial_hex(u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        u8 nib = (u8)((v >> (4 * i)) & 0xF);
        serial_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}

/* ---- VBE 低内存缓冲（INT 10 在实模式填充，须 < 1 MiB）--------------------- */
#define VBE_INFO_BUF   0x7000u      /* VbeInfoBlock（256 字节） */
#define VBE_MODE_BUF   0x7200u      /* ModeInfoBlock（256 字节） */
#define BOOT_DRIVE_ADDR 0x60000u
#define WANT_W         800u
#define WANT_H         600u
#define WANT_BPP       32u

#include "vbe.h"

/* 4F00/4F01/4F02 通用封装：AX=0x4Fxx 返回 AL=0x4F 表示 VBE 功能支持。
 * regs.es/edi 指向低内存缓冲（VBE 规范要求 ES:DI 提供）。 */
static __attribute__((noinline)) int vbe_call(u16 ax, u16 cx, u16 bx, u16 buf)
{
    struct bios_regs regs;

    regs.eax = ax;
    regs.ebx = bx;
    regs.ecx = cx;
    regs.edx = 0;
    regs.es = (u16)(buf >> 4);
    regs.ds = 0;
    regs.edi = (u16)(buf & 0xF);
    regs.esi = 0;
    regs.flags = 0x0200u;

    bios_interrupt(0x10, &regs);
    return ((regs.eax & 0xFF) == 0x4F) ? 0 : -1;
}

/* 4F00：读控制器信息并校验 "VESA" 签名 */
static __attribute__((noinline)) int vbe_get_info(void)
{
    u8 *b = (u8 *)VBE_INFO_BUF;
    if (vbe_call(0x4F00u, 0, 0, VBE_INFO_BUF) < 0)
        return -1;
    if (b[0] != 'V' || b[1] != 'E' || b[2] != 'S' || b[3] != 'A')
        return -2;
    return 0;
}

/* 4F01：查询单个模式，结果由调用方解析 */
static __attribute__((noinline)) int vbe_get_mode_info(u16 mode)
{
    return vbe_call(0x4F01u, mode, 0, VBE_MODE_BUF);
}

/* 4F02：设置模式；bx = mode | 0x4000（LFB 标志，VBE 3.0） */
static __attribute__((noinline)) int vbe_set_mode(u16 mode)
{
    return vbe_call(0x4F02u, 0, (u16)(mode | 0x4000), VBE_MODE_BUF);
}

/* ---- VBE 模式协商（vbe.c 提供解析/匹配，本文件提供查询回调）-------------- */
/* 遍历模式列表（VbeInfoBlock 偏移 0x0E 的 video_mode_ptr，far pointer 指向
 * u16 数组，0xFFFF 结束），找到满足 800x600x32 的第一个模式。 */
static __attribute__((noinline)) int vbe_find_mode(u16 *out_mode, struct vbe_mode_info *out)
{
    const u8 *info = (const u8 *)VBE_INFO_BUF;
    u32 mode_ptr = (u32)info[0x0E] | ((u32)info[0x0F] << 8) |
                   ((u32)info[0x10] << 16) | ((u32)info[0x11] << 24);
    const u16 *list = (const u16 *)((u32)((mode_ptr >> 16) << 4) + (mode_ptr & 0xFFFF));
    u32 i;

    for (i = 0; list[i] != 0xFFFF; i++) {
        if (vbe_get_mode_info(list[i]) < 0)
            continue;
        if (vbe_parse_mode_info((const u8 *)VBE_MODE_BUF, out) < 0)
            continue;
        if (vbe_mode_matches(out, WANT_W, WANT_H, WANT_BPP)) {
            *out_mode = list[i];
            return 0;
        }
    }
    return -1;
}

/* ---- loader_main ---------------------------------------------------------- */
void loader_main(void)
{
    u16 mode = 0;
    struct vbe_mode_info mi;

    serial_init();
    vga_clear();
    vga_puts("B20 vbe: VBE framebuffer demo\n");
    serial_puts("B20 vbe: VBE framebuffer demo\n");

    if (vbe_get_info() < 0) {
        vga_puts("B20 vbe: ERROR: VBE not available\n");
        serial_puts("B20 vbe: ERROR: VBE not available\n");
        for (;;)
            ;
    }
    vga_puts("B20 vbe: VBE controller OK (VESA signature)\n");
    serial_puts("B20 vbe: VBE controller OK (VESA signature)\n");

    if (vbe_find_mode(&mode, &mi) < 0) {
        vga_puts("B20 vbe: ERROR: no 800x600x32 mode found\n");
        serial_puts("B20 vbe: ERROR: no 800x600x32 mode found\n");
        for (;;)
            ;
    }
    vga_puts("B20 vbe: mode=");
    vga_hex(mode, 4);
    vga_puts(" 800x600x32 lfb=");
    vga_hex(mi.phys_base, 8);
    vga_puts(" pitch=");
    vga_hex(mi.pitch, 4);
    vga_putc('\n');
    serial_puts("B20 vbe: mode=");
    serial_hex(mode, 4);
    serial_puts(" 800x600x32 lfb=");
    serial_hex(mi.phys_base, 8);
    serial_puts(" pitch=");
    serial_hex(mi.pitch, 4);
    serial_putc('\n');

    if (vbe_set_mode(mode) < 0) {
        vga_puts("B20 vbe: ERROR: set mode failed\n");
        serial_puts("B20 vbe: ERROR: set mode failed\n");
        for (;;)
            ;
    }
    vga_puts("B20 vbe: mode set, drawing test pattern to LFB\n");
    serial_puts("B20 vbe: mode set, drawing test pattern to LFB\n");

    fb_setup(mi.phys_base, mi.pitch, mi.width, mi.height);
    fb_draw_pattern();
    vga_puts("B20 vbe: pattern drawn, halting\n");
    serial_puts("B20 vbe: pattern drawn, halting\n");
    for (;;)
        __asm__ volatile ("hlt");
}

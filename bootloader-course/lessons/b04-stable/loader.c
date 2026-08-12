/* Lesson B04: Mini-GRUB stage2 的保护模式 C 运行时与 VGA 文本库
 *
 * stage2.S 完成实模式 -> 保护模式切换后，调用本文件的 loader_main()。
 * 本课演示：
 *   1. 无 libc 的 freestanding C 代码如何被链接进引导链；
 *   2. 保护模式下直接写 VGA 文本内存 0xB8000（每字符 2 字节：ASCII+属性）；
 *   3. loader 的内存布局常量（boot 0x7C00 / stage2 0x7E00 / stack 0x90000）。
 * 参照：grub-core/kern/main.c 的初始化顺序（先最小运行时，再逐层向上）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define VGA_TEXT_BASE   0xB8000u   /* VGA 彩色文本缓冲区物理地址 */
#define VGA_COLS        80u
#define VGA_ROWS        25u
#define VGA_ATTR        0x1Fu      /* 属性：0x1F = 白字蓝底 */

static u32 vga_row = 0;
static u32 vga_col = 0;

/* vga_cell: 返回第 row 行第 col 列的 16 位字符单元指针（低字节 ASCII，高字节属性） */
static volatile u16 *vga_cell(u32 row, u32 col)
{
    return (volatile u16 *)(VGA_TEXT_BASE + 2u * (row * VGA_COLS + col));
}

/* vga_clear: 整屏填空格（属性保留），光标回 0,0 */
void vga_clear(void)
{
    u32 i;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
        ((volatile u16 *)VGA_TEXT_BASE)[i] = (u16)(VGA_ATTR << 8);
    vga_row = 0;
    vga_col = 0;
}

/* vga_putc: 输出一个字符；'\n' 回车换行，到行尾/屏底简单回卷 */
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

/* vga_puts: 输出 C 字符串 */
void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}

/* vga_hex: 以十六进制输出 v 的低 digits 位（digits <= 8），小写 */
void vga_hex(u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        u8 nib = (u8)((v >> (4 * i)) & 0xF);
        vga_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}

/* loader_main: stage2.S 在保护模式下调用。 */
void loader_main(void)
{
    vga_clear();
    vga_puts("B04 Mini-GRUB stage2 C runtime: loader_main OK\n");
    vga_puts("mem layout: boot 0x7c00 stage2 0x7e00 stack 0x90000\n");
    vga_puts("vga text base = ");
    vga_hex(VGA_TEXT_BASE, 8);
    vga_puts("\n");
    vga_puts("return to assembly: hlt\n");
}

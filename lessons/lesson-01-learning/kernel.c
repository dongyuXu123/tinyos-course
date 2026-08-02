/* 第一课：最小 VGA 文本 printk，不依赖 libc。
 * Linux v6.12 的显示栈会处理 framebuffer/DRM/console；本课故意直接使用
 * legacy VGA text buffer，作为最小可观察输出设备，而不是复刻现代 Linux 控制台。
 */

#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xb8000)
#define VGA_COLUMNS     80
#define VGA_ATTRIBUTE   0x0f

static unsigned short cursor;

static void vga_putc(char c)
{
    if (c == '\n') {
        cursor += VGA_COLUMNS - cursor % VGA_COLUMNS;
        return;
    }

    VGA_TEXT_BUFFER[cursor++] = ((unsigned short)VGA_ATTRIBUTE << 8) |
                              (unsigned char)c;
}

/* 教学级 printk：后续课程会增加定位、清屏和滚屏。 */
static void printk(const char *text)
{
    while (*text != '\0')
        vga_putc(*text++);
}

void kernel_main32(void)
{
    printk("TinyOS lesson 1\n");
    printk("Hello from the VGA text console!\n");
    printk("Multiboot2 boot succeeded.\n");
}

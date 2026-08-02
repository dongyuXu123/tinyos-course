/* 第二课：80×25 VGA 文本控制台，不依赖 libc。
 * 参考 Linux v6.12:
 *   drivers/video/console/vgacon.c 的 vga_con_putc()/vga_con_clear()/vga_con_scroll();
 *   drivers/tty/vt/vt.c 的 gotoxy()/lf()/con_scroll()。
 * 本课直接管理 legacy VGA text buffer，只提取字符、清屏、定位和滚屏语义。
 */

#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xb8000)
#define VGA_COLUMNS     80
#define VGA_ROWS        25
#define VGA_CELLS       (VGA_COLUMNS * VGA_ROWS)
#define VGA_ATTRIBUTE   0x0f

static unsigned short cursor;

static unsigned short vga_make_cell(char c)
{
    return ((unsigned short)VGA_ATTRIBUTE << 8) | (unsigned char)c;
}

static void vga_clear_row(unsigned short row)
{
    unsigned short column;
    unsigned short start = row * VGA_COLUMNS;

    for (column = 0; column < VGA_COLUMNS; column++)
        VGA_TEXT_BUFFER[start + column] = vga_make_cell(' ');
}

static void vga_scroll_one_line(void)
{
    unsigned short cell;

    for (cell = 0; cell < VGA_CELLS - VGA_COLUMNS; cell++)
        VGA_TEXT_BUFFER[cell] = VGA_TEXT_BUFFER[cell + VGA_COLUMNS];

    vga_clear_row(VGA_ROWS - 1);
    cursor = (VGA_ROWS - 1) * VGA_COLUMNS;
}

/* 教学级软件 cursor；硬件 CRTC cursor 留给后续课程。 */
static void vga_set_cursor(unsigned short row, unsigned short column)
{
    if (row >= VGA_ROWS)
        row = VGA_ROWS - 1;
    if (column >= VGA_COLUMNS)
        column = VGA_COLUMNS - 1;

    cursor = row * VGA_COLUMNS + column;
}

static void vga_clear(void)
{
    unsigned short row;

    for (row = 0; row < VGA_ROWS; row++)
        vga_clear_row(row);

    cursor = 0;
}

static void vga_newline(void)
{
    cursor += VGA_COLUMNS - cursor % VGA_COLUMNS;
    if (cursor >= VGA_CELLS)
        vga_scroll_one_line();
}

static void vga_putc(char c)
{
    if (c == '\n') {
        vga_newline();
        return;
    }

    VGA_TEXT_BUFFER[cursor++] = vga_make_cell(c);
    if (cursor >= VGA_CELLS)
        vga_scroll_one_line();
}

/* 教学级 printk：逐字符写入，不调用 printf 或 libc。 */
static void printk(const char *text)
{
    while (*text != '\0')
        vga_putc(*text++);
}

void kernel_main32(void)
{
    vga_clear();

    vga_set_cursor(0, 0);
    printk("TinyOS lesson 2: VGA printk\n");

    vga_set_cursor(23, 0);
    printk("scroll line A\n");
    printk("scroll line B\n");
    printk("scroll line C");

    vga_set_cursor(2, 10);
    printk("positioned text");
}

/* 第三课：PS/2 键盘轮询和 VGA 回显，不依赖 libc。
 * 参考 Linux v6.12:
 *   drivers/input/serio/i8042.c 的 i8042_read_status()/i8042_read_data()
 *   与 i8042_interrupt() 对 I8042_STR_OBF 的检查；
 *   drivers/input/keyboard/atkbd.c 的 atkbd_receive_byte()。
 * 本课只取 QEMU 默认环境中的 Set-1 make code 小子集，未复刻 Linux 输入栈。
 */

#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xb8000)
#define VGA_COLUMNS     80
#define VGA_ROWS        25
#define VGA_CELLS       (VGA_COLUMNS * VGA_ROWS)
#define VGA_ATTRIBUTE   0x0f

#define I8042_DATA_PORT   0x60
#define I8042_STATUS_PORT 0x64
#define I8042_STATUS_OBF  0x01

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

/* x86 I/O 端口访问；0x60/0x64 不是内存映射地址。 */
static unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

/* 对应 Linux i8042_interrupt()：仅在 OBF 表示有数据时读取数据端口。 */
static unsigned char keyboard_poll_scancode(void)
{
    if ((inb(I8042_STATUS_PORT) & I8042_STATUS_OBF) == 0)
        return 0;

    return inb(I8042_DATA_PORT);
}

/* QEMU 默认 translated Set-1 make code 的教学子集。 */
static char scancode_set1_to_ascii(unsigned char scancode)
{
    switch (scancode) {
    case 0x02: return '1';
    case 0x03: return '2';
    case 0x04: return '3';
    case 0x05: return '4';
    case 0x06: return '5';
    case 0x07: return '6';
    case 0x08: return '7';
    case 0x09: return '8';
    case 0x0a: return '9';
    case 0x0b: return '0';
    case 0x10: return 'q';
    case 0x11: return 'w';
    case 0x12: return 'e';
    case 0x13: return 'r';
    case 0x14: return 't';
    case 0x15: return 'y';
    case 0x16: return 'u';
    case 0x17: return 'i';
    case 0x18: return 'o';
    case 0x19: return 'p';
    case 0x1c: return '\n';
    case 0x1e: return 'a';
    case 0x1f: return 's';
    case 0x20: return 'd';
    case 0x21: return 'f';
    case 0x22: return 'g';
    case 0x23: return 'h';
    case 0x24: return 'j';
    case 0x25: return 'k';
    case 0x26: return 'l';
    case 0x2c: return 'z';
    case 0x2d: return 'x';
    case 0x2e: return 'c';
    case 0x2f: return 'v';
    case 0x30: return 'b';
    case 0x31: return 'n';
    case 0x32: return 'm';
    case 0x39: return ' ';
    default:   return 0;
    }
}

void kernel_main32(void)
{
    unsigned char scancode;
    char character;

    vga_clear();
    vga_set_cursor(0, 0);
    printk("TinyOS lesson 3: PS/2 keyboard polling\n");
    printk("Click this QEMU window, then type a-z, 0-9, space, or Enter.\n");
    printk("Unsupported keys are ignored; input starts below:\n\n");

    for (;;) {
        scancode = keyboard_poll_scancode();
        if (scancode == 0 || (scancode & 0x80) != 0)
            continue;

        character = scancode_set1_to_ascii(scancode);
        if (character != 0)
            vga_putc(character);
    }
}

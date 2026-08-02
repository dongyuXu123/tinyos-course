/* 第四课：最小命令循环，不依赖 libc。
 * 参考 Linux v6.12: i8042.c 的 i8042_interrupt()、atkbd.c 的
 * atkbd_receive_byte()、keyboard.c 的 kbd_event()/fn_enter()/put_queue()。
 * 本课只将 QEMU Set-1 键盘子集填入固定命令缓冲区，不复刻 Linux TTY。
 */

#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xb8000)
#define VGA_COLUMNS     80
#define VGA_ROWS        25
#define VGA_CELLS       (VGA_COLUMNS * VGA_ROWS)
#define VGA_ATTRIBUTE   0x0f

#define I8042_DATA_PORT   0x60
#define I8042_STATUS_PORT 0x64
#define I8042_STATUS_OBF  0x01
#define COMMAND_MAX       64

static unsigned short cursor;
static char command[COMMAND_MAX];
static unsigned short command_length;

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

/* 仅删除当前命令刚刚回显的最后一个字符。 */
static void vga_backspace(void)
{
    cursor--;
    VGA_TEXT_BUFFER[cursor] = vga_make_cell(' ');
}

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
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
    case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
    case 0x08: return '7'; case 0x09: return '8'; case 0x0a: return '9';
    case 0x0b: return '0'; case 0x0e: return '\b';
    case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e';
    case 0x13: return 'r'; case 0x14: return 't'; case 0x15: return 'y';
    case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o';
    case 0x19: return 'p'; case 0x1c: return '\n';
    case 0x1e: return 'a'; case 0x1f: return 's'; case 0x20: return 'd';
    case 0x21: return 'f'; case 0x22: return 'g'; case 0x23: return 'h';
    case 0x24: return 'j'; case 0x25: return 'k'; case 0x26: return 'l';
    case 0x2c: return 'z'; case 0x2d: return 'x'; case 0x2e: return 'c';
    case 0x2f: return 'v'; case 0x30: return 'b'; case 0x31: return 'n';
    case 0x32: return 'm'; case 0x39: return ' ';
    default: return 0;
    }
}

static void print_prompt(void)
{
    printk("tinyos> ");
}

static void reset_command(void)
{
    command_length = 0;
    command[0] = '\0';
}

static int command_equals(const char *expected)
{
    unsigned short index;

    for (index = 0; command[index] != '\0' && expected[index] != '\0'; index++) {
        if (command[index] != expected[index])
            return 0;
    }

    return command[index] == expected[index];
}

/* 返回 1 表示 clear 已显示了下一条 prompt。 */
static int execute_command(void)
{
    if (command_length == 0)
        return 0;
    if (command_equals("help")) {
        printk("commands: help about clear\n");
        return 0;
    }
    if (command_equals("about")) {
        printk("TinyOS lesson 4: minimal command loop\n");
        return 0;
    }
    if (command_equals("clear")) {
        vga_clear();
        print_prompt();
        return 1;
    }

    printk("unknown command: ");
    printk(command);
    vga_putc('\n');
    return 0;
}

static void handle_input_character(char character)
{
    int prompt_printed;

    if (character == '\n') {
        vga_putc('\n');
        prompt_printed = execute_command();
        reset_command();
        if (!prompt_printed)
            print_prompt();
        return;
    }
    if (character == '\b') {
        if (command_length != 0) {
            command_length--;
            command[command_length] = '\0';
            vga_backspace();
        }
        return;
    }
    if (command_length < COMMAND_MAX - 1) {
        command[command_length++] = character;
        command[command_length] = '\0';
        vga_putc(character);
    }
}

void kernel_main32(void)
{
    unsigned char scancode;
    char character;

    vga_clear();
    vga_set_cursor(0, 0);
    printk("TinyOS lesson 4: minimal command loop\n");
    printk("Commands: help about clear. Type lowercase a-z, 0-9, space, Enter.\n");
    printk("Backspace removes the last input character.\n\n");
    reset_command();
    print_prompt();

    for (;;) {
        scancode = keyboard_poll_scancode();
        if (scancode == 0 || (scancode & 0x80) != 0)
            continue;
        character = scancode_set1_to_ascii(scancode);
        if (character != 0)
            handle_input_character(character);
    }
}

/* 第五课：显示 GRUB Multiboot2 memory map，不依赖 libc。
 * Multiboot2 规范定义 EAX/EBX、tag 对齐和 type-6 memory-map layout。
 * Linux v6.12 e820.h/e820.c 是 RAM type 与后续范围规范化的工程对照。
 */
#define VGA_TEXT_BUFFER ((volatile unsigned short *)0xb8000)
#define VGA_COLUMNS 80
#define VGA_ROWS 25
#define VGA_CELLS (VGA_COLUMNS * VGA_ROWS)
#define VGA_ATTRIBUTE 0x0f
#define I8042_DATA_PORT 0x60
#define I8042_STATUS_PORT 0x64
#define I8042_STATUS_OBF 0x01
#define COMMAND_MAX 64
#define MB2_BOOT_MAGIC 0x36d76289
#define MB2_TAG_END 0
#define MB2_TAG_MMAP 6
#define MB2_MMAP_DISPLAY_MAX 6

typedef unsigned int u32;
typedef unsigned long long u64;
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));

static unsigned short cursor, command_length;
static char command[COMMAND_MAX];
static u32 multiboot_magic, multiboot_address;

static unsigned short vga_make_cell(char c) { return ((unsigned short)VGA_ATTRIBUTE << 8) | (unsigned char)c; }
static void vga_clear_row(unsigned short row) { unsigned short column, start = row * VGA_COLUMNS; for (column = 0; column < VGA_COLUMNS; column++) VGA_TEXT_BUFFER[start + column] = vga_make_cell(' '); }
static void vga_scroll_one_line(void) { unsigned short cell; for (cell = 0; cell < VGA_CELLS - VGA_COLUMNS; cell++) VGA_TEXT_BUFFER[cell] = VGA_TEXT_BUFFER[cell + VGA_COLUMNS]; vga_clear_row(VGA_ROWS - 1); cursor = (VGA_ROWS - 1) * VGA_COLUMNS; }
static void vga_set_cursor(unsigned short row, unsigned short column) { if (row >= VGA_ROWS) row = VGA_ROWS - 1; if (column >= VGA_COLUMNS) column = VGA_COLUMNS - 1; cursor = row * VGA_COLUMNS + column; }
static void vga_clear(void) { unsigned short row; for (row = 0; row < VGA_ROWS; row++) vga_clear_row(row); cursor = 0; }
static void vga_newline(void) { cursor += VGA_COLUMNS - cursor % VGA_COLUMNS; if (cursor >= VGA_CELLS) vga_scroll_one_line(); }
static void vga_putc(char c) { if (c == '\n') { vga_newline(); return; } VGA_TEXT_BUFFER[cursor++] = vga_make_cell(c); if (cursor >= VGA_CELLS) vga_scroll_one_line(); }
static void vga_backspace(void) { cursor--; VGA_TEXT_BUFFER[cursor] = vga_make_cell(' '); }
static void printk(const char *text) { while (*text != '\0') vga_putc(*text++); }

static void print_hex32(u32 value) { static const char hex[] = "0123456789abcdef"; int shift; for (shift = 28; shift >= 0; shift -= 4) vga_putc(hex[(value >> shift) & 0xf]); }
static void print_hex64(u64 value) { print_hex32((u32)(value >> 32)); print_hex32((u32)value); }
static void print_two_digits(u32 value) { vga_putc('0' + (value / 10) % 10); vga_putc('0' + value % 10); }

static unsigned char inb(unsigned short port) { unsigned char value; __asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port)); return value; }
static unsigned char keyboard_poll_scancode(void) { if ((inb(I8042_STATUS_PORT) & I8042_STATUS_OBF) == 0) return 0; return inb(I8042_DATA_PORT); }
static char scancode_set1_to_ascii(unsigned char s)
{
    switch (s) {
    case 0x02:return '1'; case 0x03:return '2'; case 0x04:return '3'; case 0x05:return '4'; case 0x06:return '5'; case 0x07:return '6'; case 0x08:return '7'; case 0x09:return '8'; case 0x0a:return '9'; case 0x0b:return '0'; case 0x0e:return '\b';
    case 0x10:return 'q'; case 0x11:return 'w'; case 0x12:return 'e'; case 0x13:return 'r'; case 0x14:return 't'; case 0x15:return 'y'; case 0x16:return 'u'; case 0x17:return 'i'; case 0x18:return 'o'; case 0x19:return 'p'; case 0x1c:return '\n';
    case 0x1e:return 'a'; case 0x1f:return 's'; case 0x20:return 'd'; case 0x21:return 'f'; case 0x22:return 'g'; case 0x23:return 'h'; case 0x24:return 'j'; case 0x25:return 'k'; case 0x26:return 'l'; case 0x2c:return 'z'; case 0x2d:return 'x'; case 0x2e:return 'c'; case 0x2f:return 'v'; case 0x30:return 'b'; case 0x31:return 'n'; case 0x32:return 'm'; case 0x39:return ' ';
    default:return 0;
    }
}

static void print_prompt(void) { printk("tinyos> "); }
static void reset_command(void) { command_length = 0; command[0] = '\0'; }
static int command_equals(const char *expected) { unsigned short i; for (i = 0; command[i] != '\0' && expected[i] != '\0'; i++) if (command[i] != expected[i]) return 0; return command[i] == expected[i]; }
static const char *mmap_type_name(u32 type) { if (type == 1) return "available"; if (type == 3) return "acpi"; if (type == 4) return "hibernation"; if (type == 5) return "bad"; return "reserved"; }
static void print_mmap_entry(u32 index, const struct mb2_mmap_entry *entry) { print_two_digits(index); vga_putc(' '); print_hex64(entry->addr); printk(" +"); print_hex64(entry->len); vga_putc(' '); printk(mmap_type_name(entry->type)); vga_putc('\n'); }

/* 未分页教学阶段：MBI physical address 以 identity mapping 直接解引用。 */
static void show_memory_map(void)
{
    u32 total_size, pos, end, displayed = 0, entries = 0;
    int found = 0, ended = 0;
    if (multiboot_magic != MB2_BOOT_MAGIC) { printk("mmap error: bad multiboot2 magic\n"); return; }
    if ((multiboot_address & 7) != 0) { printk("mmap error: unaligned mbi address\n"); return; }
    total_size = *(const u32 *)(unsigned long)multiboot_address;
    if (total_size < 16 || total_size > 0x100000 || multiboot_address + total_size < multiboot_address) { printk("mmap error: bad mbi size\n"); return; }
    pos = multiboot_address + 8; end = multiboot_address + total_size;
    printk("Multiboot2 memory map:\n");
    while (pos < end) {
        const struct mb2_tag *tag;
        u32 rounded;
        if (end - pos < 8) { printk("mmap error: short tag\n"); return; }
        tag = (const struct mb2_tag *)(unsigned long)pos;
        if (tag->size < 8 || tag->size > end - pos) { printk("mmap error: bad tag size\n"); return; }
        if (tag->type == MB2_TAG_END) { if (tag->size != 8) { printk("mmap error: bad end tag\n"); return; } ended = 1; break; }
        if (tag->type == MB2_TAG_MMAP && !found) {
            const struct mb2_mmap_tag *map = (const struct mb2_mmap_tag *)tag;
            u32 entry_pos, map_end;
            if (tag->size < 16 || map->entry_version != 0 || map->entry_size < 24 || (map->entry_size & 7) != 0 || ((tag->size - 16) % map->entry_size) != 0) { printk("mmap error: unsupported map tag\n"); return; }
            found = 1; entry_pos = pos + 16; map_end = pos + tag->size;
            while (entry_pos < map_end) {
                const struct mb2_mmap_entry *entry = (const struct mb2_mmap_entry *)(unsigned long)entry_pos;
                if (displayed < MB2_MMAP_DISPLAY_MAX) { print_mmap_entry(entries, entry); displayed++; }
                entries++; entry_pos += map->entry_size;
            }
        }
        rounded = (tag->size + 7) & ~7U;
        if (rounded < tag->size || rounded > end - pos) { printk("mmap error: bad tag alignment\n"); return; }
        pos += rounded;
    }
    if (!ended) { printk("mmap error: missing end tag\n"); return; }
    if (!found) { printk("mmap error: memory map missing\n"); return; }
    printk("shown "); print_two_digits(displayed); printk(" of "); print_two_digits(entries); printk(" entries\n");
}

static int execute_command(void)
{
    if (command_length == 0) return 0;
    if (command_equals("help")) { printk("commands: help about clear mmap\n"); return 0; }
    if (command_equals("about")) { printk("TinyOS lesson 5: Multiboot2 memory map\n"); return 0; }
    if (command_equals("clear")) { vga_clear(); print_prompt(); return 1; }
    if (command_equals("mmap")) { show_memory_map(); return 0; }
    printk("unknown command: "); printk(command); vga_putc('\n'); return 0;
}
static void handle_input_character(char c)
{
    int prompt_printed;
    if (c == '\n') { vga_putc('\n'); prompt_printed = execute_command(); reset_command(); if (!prompt_printed) print_prompt(); return; }
    if (c == '\b') { if (command_length != 0) { command_length--; command[command_length] = '\0'; vga_backspace(); } return; }
    if (command_length < COMMAND_MAX - 1) { command[command_length++] = c; command[command_length] = '\0'; vga_putc(c); }
}
void kernel_main32(u32 magic, u32 mbi_address)
{
    unsigned char scancode; char character;
    multiboot_magic = magic; multiboot_address = mbi_address;
    vga_clear(); vga_set_cursor(0, 0);
    printk("TinyOS lesson 5: Multiboot2 memory map\n");
    printk("Commands: help about clear mmap. Type mmap to inspect GRUB memory.\n\n");
    reset_command(); print_prompt();
    for (;;) { scancode = keyboard_poll_scancode(); if (scancode == 0 || (scancode & 0x80) != 0) continue; character = scancode_set1_to_ascii(scancode); if (character != 0) handle_input_character(character); }
}

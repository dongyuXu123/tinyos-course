/* 第七课：用已保留的物理页建立最小 32 位 identity paging。
 * Intel SDM 定义 CR3、CR0.PG 和 PDE/PTE；Multiboot2 定义 memory map。
 * Linux v6.12 head_64.S/e820.c/memblock.c 是早期转换与内存管理工程对照。 */
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
#define PAGE_SIZE 0x1000ULL
#define LOW_MEMORY_END 0x00100000ULL
#define ALLOCATION_HISTORY_MAX 64
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_PRESENT_WRITABLE 0x003U
#define CR0_PG 0x80000000U
#define IDENTITY_MAP_END 0x00400000U

typedef unsigned int u32;
typedef unsigned long long u64;
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));

extern char _kernel_start[], _kernel_end[], stack_bottom[], stack_top[];

static unsigned short cursor, command_length;
static char command[COMMAND_MAX];
static u32 multiboot_magic, multiboot_address, multiboot_total_size;
static const struct mb2_mmap_tag *memory_map;
static int memory_map_ready;
static u64 allocation_cursor, allocation_end, allocation_history[ALLOCATION_HISTORY_MAX];
static u64 page_directory_page, page_table_page;
static u32 allocated_pages, usable_pages;
static int paging_enabled;

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

/* 验证 MBI 一次；map 的 entry_size 仍是所有后续访问的实际步长。 */
static int prepare_memory_map(void)
{
    u32 pos, end;
    if (memory_map_ready) return 1;
    if (multiboot_magic != MB2_BOOT_MAGIC || (multiboot_address & 7) != 0) return 0;
    multiboot_total_size = *(const u32 *)(unsigned long)multiboot_address;
    if (multiboot_total_size < 16 || multiboot_total_size > 0x100000 || multiboot_address + multiboot_total_size < multiboot_address) return 0;
    pos = multiboot_address + 8; end = multiboot_address + multiboot_total_size;
    while (pos < end) {
        const struct mb2_tag *tag; u32 rounded;
        if (end - pos < 8) return 0;
        tag = (const struct mb2_tag *)(unsigned long)pos;
        if (tag->size < 8 || tag->size > end - pos) return 0;
        if (tag->type == MB2_TAG_END) { if (tag->size != 8 || memory_map == 0) return 0; memory_map_ready = 1; return 1; }
        if (tag->type == MB2_TAG_MMAP && memory_map == 0) {
            const struct mb2_mmap_tag *map = (const struct mb2_mmap_tag *)tag;
            if (tag->size < 16 || map->entry_version != 0 || map->entry_size < 24 || (map->entry_size & 7) != 0 || ((tag->size - 16) % map->entry_size) != 0) return 0;
            memory_map = map;
        }
        rounded = (tag->size + 7) & ~7U;
        if (rounded < tag->size || rounded > end - pos) return 0;
        pos += rounded;
    }
    return 0;
}

static u64 align_up_page(u64 value) { if (value > ~0ULL - (PAGE_SIZE - 1)) return 0; return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }
static u64 align_down_page(u64 value) { return value & ~(PAGE_SIZE - 1); }
static int ranges_overlap(u64 first_start, u64 first_end, u64 second_start, u64 second_end) { return first_start < second_end && second_start < first_end; }
static int page_was_allocated(u64 page) { u32 index; for (index = 0; index < allocated_pages; index++) if (allocation_history[index] == page) return 1; return 0; }
static int page_is_reserved(u64 page)
{
    u64 page_end = page + PAGE_SIZE;
    u64 kernel_start = (u64)(u32)(unsigned long)_kernel_start, kernel_end = (u64)(u32)(unsigned long)_kernel_end;
    u64 stack_start = (u64)(u32)(unsigned long)stack_bottom, stack_end = (u64)(u32)(unsigned long)stack_top;
    u64 mbi_start = multiboot_address, mbi_end = mbi_start + multiboot_total_size;
    if (page_end < page || ranges_overlap(page, page_end, 0, LOW_MEMORY_END)) return 1;
    if (ranges_overlap(page, page_end, kernel_start, kernel_end)) return 1;
    if (ranges_overlap(page, page_end, stack_start, stack_end)) return 1;
    if (ranges_overlap(page, page_end, mbi_start, mbi_end)) return 1;
    if (allocated_pages != 0 && page <= allocation_history[allocated_pages - 1]) return 1;
    return page_was_allocated(page);
}

static const struct mb2_mmap_entry *map_entry_at(u32 offset) { return (const struct mb2_mmap_entry *)((const unsigned char *)memory_map + 16 + offset); }
static void phys_allocator_init(void)
{
    u32 offset;
    if (!prepare_memory_map()) return;
    for (offset = 0; offset < memory_map->size - 16; offset += memory_map->entry_size) {
        const struct mb2_mmap_entry *entry = map_entry_at(offset); u64 page, end;
        if (entry->type != 1 || entry->addr + entry->len < entry->addr) continue;
        page = align_up_page(entry->addr); end = align_down_page(entry->addr + entry->len);
        while (page != 0 && page < end) { if (!page_is_reserved(page)) usable_pages++; page += PAGE_SIZE; }
    }
}

static u64 phys_alloc_page(void)
{
    u32 offset;
    if (!prepare_memory_map() || allocated_pages == ALLOCATION_HISTORY_MAX) return 0;
    if (allocation_cursor != 0) {
        while (allocation_cursor < allocation_end) {
            u64 page = allocation_cursor; allocation_cursor += PAGE_SIZE;
            if (!page_is_reserved(page)) { allocation_history[allocated_pages++] = page; return page; }
        }
    }
    for (offset = 0; offset < memory_map->size - 16; offset += memory_map->entry_size) {
        const struct mb2_mmap_entry *entry = map_entry_at(offset); u64 page, end;
        if (entry->type != 1 || entry->addr + entry->len < entry->addr) continue;
        page = align_up_page(entry->addr); end = align_down_page(entry->addr + entry->len);
        while (page != 0 && page < end) {
            if (!page_is_reserved(page)) { allocation_cursor = page + PAGE_SIZE; allocation_end = end; allocation_history[allocated_pages++] = page; return page; }
            page += PAGE_SIZE;
        }
    }
    return 0;
}

/* 此处 physical == virtual 仅在启用前、以及本课的前 4 MiB identity map 内成立。 */
static void zero_page(u64 physical_page)
{
    u32 index;
    volatile u32 *words = (volatile u32 *)(unsigned long)(u32)physical_page;
    for (index = 0; index < PAGE_TABLE_ENTRIES; index++) words[index] = 0;
}
static void write_cr3(u32 value) { __asm__ volatile ("movl %0, %%cr3" : : "r" (value) : "memory"); }
static u32 read_cr0(void) { u32 value; __asm__ volatile ("movl %%cr0, %0" : "=r" (value)); return value; }
static void write_cr0(u32 value) { __asm__ volatile ("movl %0, %%cr0" : : "r" (value) : "memory"); }
static int page_is_32bit(u64 page) { return page != 0 && (page & (PAGE_SIZE - 1)) == 0 && page <= 0xfffff000ULL; }
static int enable_identity_paging(void)
{
    volatile u32 *directory, *table;
    u32 index;
    page_directory_page = phys_alloc_page();
    page_table_page = phys_alloc_page();
    if (!page_is_32bit(page_directory_page) || !page_is_32bit(page_table_page)) { printk("paging error: cannot allocate tables\n"); return 0; }
    zero_page(page_directory_page);
    zero_page(page_table_page);
    directory = (volatile u32 *)(unsigned long)(u32)page_directory_page;
    table = (volatile u32 *)(unsigned long)(u32)page_table_page;
    for (index = 0; index < PAGE_TABLE_ENTRIES; index++) table[index] = (index * (u32)PAGE_SIZE) | PAGE_PRESENT_WRITABLE;
    directory[0] = (u32)page_table_page | PAGE_PRESENT_WRITABLE;
    write_cr3((u32)page_directory_page);
    write_cr0(read_cr0() | CR0_PG);
    paging_enabled = 1;
    return 1;
}

static void show_memory_map(void)
{
    u32 offset, displayed = 0, entries = 0;
    if (!prepare_memory_map()) { printk("mmap error: invalid multiboot2 map\n"); return; }
    printk("Multiboot2 memory map:\n");
    for (offset = 0; offset < memory_map->size - 16; offset += memory_map->entry_size) {
        const struct mb2_mmap_entry *entry = map_entry_at(offset);
        if (displayed < MB2_MMAP_DISPLAY_MAX) { print_mmap_entry(entries, entry); displayed++; }
        entries++;
    }
    printk("shown "); print_two_digits(displayed); printk(" of "); print_two_digits(entries); printk(" entries\n");
}
static void show_page_info(void)
{
    if (!prepare_memory_map()) { printk("pinfo error: invalid multiboot2 map\n"); return; }
    printk("page size: "); print_hex32((u32)PAGE_SIZE); vga_putc('\n');
    printk("kernel: "); print_hex64((u64)(u32)(unsigned long)_kernel_start); printk(" - "); print_hex64((u64)(u32)(unsigned long)_kernel_end); vga_putc('\n');
    printk("stack:  "); print_hex64((u64)(u32)(unsigned long)stack_bottom); printk(" - "); print_hex64((u64)(u32)(unsigned long)stack_top); vga_putc('\n');
    printk("mbi:    "); print_hex64(multiboot_address); printk(" - "); print_hex64((u64)multiboot_address + multiboot_total_size); vga_putc('\n');
    printk("usable pages: "); print_hex32(usable_pages); vga_putc('\n');
    printk("allocated pages: "); print_hex32(allocated_pages); vga_putc('\n');
}
static void show_paging_info(void)
{
    printk("paging: "); printk(paging_enabled ? "on" : "off"); vga_putc('\n');
    printk("directory: "); print_hex64(page_directory_page); vga_putc('\n');
    printk("table:     "); print_hex64(page_table_page); vga_putc('\n');
    printk("identity:  "); print_hex32(0); printk(" - "); print_hex32(IDENTITY_MAP_END); vga_putc('\n');
}

static int execute_command(void)
{
    u64 page;
    if (command_length == 0) return 0;
    if (command_equals("help")) { printk("commands: help about clear mmap pinfo palloc pginfo\n"); return 0; }
    if (command_equals("about")) { printk("TinyOS lesson 7: minimal 32-bit identity paging\n"); return 0; }
    if (command_equals("clear")) { vga_clear(); print_prompt(); return 1; }
    if (command_equals("mmap")) { show_memory_map(); return 0; }
    if (command_equals("pinfo")) { show_page_info(); return 0; }
    if (command_equals("pginfo")) { show_paging_info(); return 0; }
    if (command_equals("palloc")) { page = phys_alloc_page(); if (page == 0) printk("palloc: out of pages\n"); else { printk("palloc: "); print_hex64(page); vga_putc('\n'); } return 0; }
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
    printk("TinyOS lesson 7: minimal 32-bit identity paging\n");
    printk("Commands: help about clear mmap pinfo palloc pginfo.\n\n");
    if (prepare_memory_map()) { phys_allocator_init(); if (!enable_identity_paging()) printk("warning: paging remains disabled\n"); } else printk("warning: Multiboot2 map is invalid\n");
    reset_command(); print_prompt();
    for (;;) { scancode = keyboard_poll_scancode(); if (scancode == 0 || (scancode & 0x80) != 0) continue; character = scancode_set1_to_ascii(scancode); if (character != 0) handle_input_character(character); }
}

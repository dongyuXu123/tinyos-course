/* Lesson B10: test-kernel — MBI walker 与内存图（内核侧）
 *
 * kernel_main(magic, mbi) 检查交接 magic，然后按 MBI 规范遍历 tag 链：
 *   u32 total_size @0、u32 reserved @4、tag 从 @8 开始；
 *   tag = {u32 type, u32 size}，步长 (size+7)&~7，end tag type=0。
 * 与 TinyOS Lesson 05 的 MBI walker 结构一致（fail-closed：magic 不符即报错）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

#define MB2_BOOT_MAGIC   0x36d76289u
#define MB2_TAG_END      0u
#define VGA_TEXT_BASE    0xB8000u
#define VGA_COLS         80u
#define VGA_ATTR         0x1Fu

static u32 vga_row = 0;
static u32 vga_col = 0;

static void vga_putc(char c)
{
    u16 *cell;
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= 25u)
            vga_row = 0;
        return;
    }
    cell = (u16 *)(VGA_TEXT_BASE + 2u * (vga_row * VGA_COLS + vga_col));
    *cell = (u16)((u16)VGA_ATTR << 8) | (u8)c;
    vga_col++;
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= 25u)
        vga_row = 0;
}

static void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}

static void vga_hex(u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        u8 nib = (u8)((v >> (4 * i)) & 0xF);
        vga_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}

/* vga_hex64: 打印 64 位值（高 32 位 + 低 32 位） */
static void vga_hex64(u64 v)
{
    vga_hex((u32)(v >> 32), 8);
    vga_hex((u32)v, 8);
}

#define MB2_TAG_MMAP  6u
#define MB2_MMAP_AVAILABLE 1u

/* MBI tag：{u32 type, u32 size} */
struct mb2_tag {
    u32 type;
    u32 size;
};

void kernel_main(u32 magic, u32 mbi)
{
    u32 total;
    const u8 *p;

    if (magic != MB2_BOOT_MAGIC) {
        vga_puts("B10 test-kernel: BAD magic=");
        vga_hex(magic, 8);
        vga_puts("\n");
        return;
    }
    vga_puts("B10 test-kernel: magic ok\n");

    total = *(const u32 *)mbi;
    vga_puts("mbi total_size=");
    vga_hex(total, 8);
    vga_puts("\n");

    /* 从 mbi+8 起遍历 tag，直到 end tag（type=0, size=8） */
    p = (const u8 *)mbi + 8u;
    for (;;) {
        const struct mb2_tag *t = (const struct mb2_tag *)p;

        vga_puts("tag type=");
        vga_hex(t->type, 4);
        vga_puts(" size=");
        vga_hex(t->size, 4);
        vga_puts("\n");

        if (t->type == MB2_TAG_MMAP) {
            /* type-6 mmap tag：{type,size,entry_size,version,entries} */
            const u8 *ep = p + 16u;
            u32 i, n = (t->size - 16u) / 24u;
            vga_puts("  mmap entries=");
            vga_hex(n, 2);
            vga_puts("\n");
            for (i = 0; i < n; i++) {
                u64 addr = *(const u64 *)ep;
                u64 len  = *(const u64 *)(ep + 8u);
                u32 type = *(const u32 *)(ep + 16u);
                vga_puts("  addr=");
                vga_hex64(addr);
                vga_puts(" len=");
                vga_hex64(len);
                vga_puts(" type=");
                vga_hex(type, 2);
                vga_puts("\n");
                ep += 24u;
            }
        }

        if (t->type == MB2_TAG_END)
            break;
        p += (t->size + 7u) & ~7u;      /* 8 字节对齐步进 */
    }
    vga_puts("B10 walker done: end tag reached\n");
}

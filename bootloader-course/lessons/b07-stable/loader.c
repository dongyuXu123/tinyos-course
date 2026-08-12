/* Lesson B07: Mini-GRUB stage2 — Multiboot2 header 校验
 *
 * B06 能解析 ELF；本课校验内核是否声明自己是 Multiboot2 客户：
 *   mb2_header_check(buf, size, &off) —— 在前 32768 字节、8 字节对齐处搜索
 *   Multiboot2 header（magic/arch/length/checksum），并遍历 header tags。
 * 参照：grub-core/loader/multiboot_mbi2.c 的 find_header（32768/8 对齐/四字段
 *       求和为 0）与研读支线 0.5（Multiboot2 header 校验与 ABI）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---- VGA 文本库（B04，保持不变）------------------------------------------ */
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

/* ---- BIOS 回调与磁盘读（B05，保持不变）----------------------------------- */
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

#define SPT  18u
#define SPC  36u

static void lba_to_chs(u32 lba, u8 *cyl, u8 *head, u8 *sect)
{
    *cyl  = (u8)(lba / SPC);
    *head = (u8)((lba % SPC) / SPT);
    *sect = (u8)((lba % SPT) + 1);
}

static int disk_read_sector(u8 drive, u32 lba, void *buf)
{
    struct bios_regs regs;
    u8 cyl, head, sect;

    lba_to_chs(lba, &cyl, &head, &sect);

    regs.eax = 0x00000201u;
    regs.ecx = ((u32)cyl << 8) | sect;
    regs.edx = ((u32)head << 8) | drive;
    regs.es  = (u16)((u32)buf >> 4);
    regs.ebx = (u32)buf & 0xFu;
    regs.ds  = 0;
    regs.flags = 0x0200u;
    regs.edi = 0;
    regs.esi = 0;

    bios_interrupt(0x13, &regs);
    return (regs.flags & 0x01u) ? -1 : 0;
}

int disk_read_lba(u8 drive, u32 lba, void *buf, u32 count)
{
    u32 i;
    for (i = 0; i < count; i++) {
        if (disk_read_sector(drive, lba + i, (u8 *)buf + i * 512) < 0)
            return -1;
    }
    return 0;
}

/* ---- ELF32 解析（B06，保持不变）------------------------------------------ */
#define EI_MAG0  0
#define EI_MAG1  1
#define EI_MAG2  2
#define EI_MAG3  3
#define EI_CLASS 4
#define EI_DATA  5
#define EI_VERSION 6

#define ELFMAG0   0x7Fu
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define EM_386    3
#define PT_LOAD   1

struct elf32_ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

struct elf32_phdr {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
};

int elf_parse(const void *buf, u32 size)
{
    const struct elf32_ehdr *eh = (const struct elf32_ehdr *)buf;
    const struct elf32_phdr *ph;
    u32 i;

    if (eh->e_ident[EI_MAG0] != ELFMAG0 || eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 || eh->e_ident[EI_MAG3] != ELFMAG3)
        return -1;
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -2;
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return -3;
    if (eh->e_ident[EI_VERSION] != EV_CURRENT)
        return -4;
    if (eh->e_machine != EM_386)
        return -5;
    if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(struct elf32_phdr))
        return -6;
    if (eh->e_phoff + (u32)eh->e_phnum * eh->e_phentsize > size)
        return -7;

    vga_puts("B07 elf: entry=");
    vga_hex(eh->e_entry, 8);
    vga_puts(" phnum=");
    vga_hex(eh->e_phnum, 2);
    vga_puts("\n");

    ph = (const struct elf32_phdr *)((const u8 *)buf + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph->p_type == PT_LOAD) {
            vga_puts("  LOAD paddr=");
            vga_hex(ph->p_paddr, 8);
            vga_puts(" filesz=");
            vga_hex(ph->p_filesz, 8);
            vga_puts(" memsz=");
            vga_hex(ph->p_memsz, 8);
            vga_puts("\n");
        }
        ph = (const struct elf32_phdr *)((const u8 *)ph + eh->e_phentsize);
    }
    return 0;
}

/* ---- Multiboot2 header 校验（本课核心）----------------------------------- */
#define MB2_HEADER_MAGIC       0xe85250d6u
#define MB2_ARCHITECTURE_I386  0u
#define MB2_HEADER_ALIGN       8u
#define MB2_SEARCH_LIMIT       32768u

struct mb2_header {
    u32 magic;        /* +0  */
    u32 architecture; /* +4  */
    u32 length;       /* +8  */
    u32 checksum;     /* +12 */
};

/* header tags：u16 type, u16 flags, u32 size，8 字节对齐 */
struct mb2_header_tag {
    u16 type;
    u16 flags;
    u32 size;
};

/* mb2_header_check: 在前 32768 字节、8 对齐处搜索并校验 Multiboot2 header。
 * 返回 0 成功；负值为错误码；hdr_off 输出 header 在 buf 中的偏移。
 * 对照 GRUB find_header：magic 命中 + 四字段求和为 0 + arch 匹配。 */
int mb2_header_check(const void *buf, u32 size, u32 *hdr_off)
{
    const u8 *base = (const u8 *)buf;
    u32 limit = size < MB2_SEARCH_LIMIT ? size : MB2_SEARCH_LIMIT;
    u32 off;

    for (off = 0; off + 16u <= limit; off += MB2_HEADER_ALIGN) {
        const struct mb2_header *h =
            (const struct mb2_header *)(base + off);

        if (h->magic != MB2_HEADER_MAGIC)
            continue;                    /* 不是 header，继续搜 */
        if (h->architecture != MB2_ARCHITECTURE_I386)
            return -2;                   /* arch 不匹配 */
        if (h->length < 16u || off + h->length > size)
            return -3;                   /* length 越界/过小 */
        if (h->magic + h->architecture + h->length + h->checksum != 0u)
            return -4;                   /* checksum 不匹配 */

        /* 遍历 header tags 到 end tag（type=0, size=8） */
        {
            u32 t = 16u;
            while (t < h->length) {
                const struct mb2_header_tag *tag =
                    (const struct mb2_header_tag *)((const u8 *)h + t);
                if (tag->size < 8u || t + tag->size > h->length)
                    return -5;           /* tag 越界 */
                if (tag->type == 0)
                    break;               /* end tag */
                t += (tag->size + 7u) & ~7u;
            }
        }
        if (hdr_off)
            *hdr_off = off;
        return 0;
    }
    return -1;                           /* 未找到 header */
}

/* ---- loader_main: 校验合法与非法镜像 ------------------------------------- */
#define KERNEL_BUF   0x00068000u   /* GRUB scratch 区起点，作内核暂存 */
#define KERNEL_LBA   9u            /* ok.elf（test-kernel.elf） */
#define KERNEL_SECT  18u           /* 18 扇区 = 9216 字节 ≥ 文件 8896 字节 */
#define BAD_LBA      27u           /* bad.elf（破坏 checksum 的副本） */

static int check_one(const char *label, u32 lba)
{
    u32 hdr_off = 0;
    int r;

    if (disk_read_lba(0, lba, (void *)KERNEL_BUF, KERNEL_SECT) < 0) {
        vga_puts(label);
        vga_puts(": disk read failed\n");
        return -1;
    }
    if (elf_parse((const void *)KERNEL_BUF, KERNEL_SECT * 512u) < 0) {
        vga_puts(label);
        vga_puts(": invalid ELF\n");
        return -1;
    }
    r = mb2_header_check((const void *)KERNEL_BUF, KERNEL_SECT * 512u, &hdr_off);
    if (r < 0) {
        vga_puts(label);
        vga_puts(": mb2 header rejected (code=");
        vga_hex((u32)(-r), 2);
        vga_puts(")\n");
        return r;
    }
    vga_puts(label);
    vga_puts(": mb2 header ok @");
    vga_hex(hdr_off, 8);
    vga_puts(" magic=ok arch=ok checksum=ok\n");
    return 0;
}

void loader_main(void)
{
    vga_clear();
    vga_puts("B07 Mini-GRUB: Multiboot2 header check\n");

    vga_puts("== ok.elf (test-kernel) ==\n");
    check_one("ok", KERNEL_LBA);

    vga_puts("== bad.elf (corrupted checksum) ==\n");
    check_one("bad", BAD_LBA);

    vga_puts("B07 done: multiboot2 header check OK\n");
}

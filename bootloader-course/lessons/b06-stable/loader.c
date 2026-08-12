/* Lesson B06: Mini-GRUB stage2 — ELF32 解析
 *
 * B05 提供了 disk_read_lba；本课把读入的字节解读为 ELF32：
 *   elf_parse(buf, size) —— 校验 ELF header，解析 program headers，
 *                          打印 e_entry 与每个 PT_LOAD 段（地址/大小）。
 * 参照：grub-core/kern/elf.c（grub_elf_check_header）与
 *       grub-core/kern/elfXX.c（grub_elfXX_load_phdrs、FOR_ELFXX_PHDRS）。
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

/* ---- ELF32 结构（字段布局与 ELF 规范一致，自然对齐无填充）----------------- */
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
    u8  e_ident[16];   /* +0   */
    u16 e_type;        /* +16  */
    u16 e_machine;     /* +18  */
    u32 e_version;     /* +20  */
    u32 e_entry;       /* +24  */
    u32 e_phoff;       /* +28  */
    u32 e_shoff;       /* +32  */
    u32 e_flags;       /* +36  */
    u16 e_ehsize;      /* +40  */
    u16 e_phentsize;   /* +42  */
    u16 e_phnum;       /* +44  */
    u16 e_shentsize;   /* +46  */
    u16 e_shnum;       /* +48  */
    u16 e_shstrndx;    /* +50  */
};

struct elf32_phdr {
    u32 p_type;        /* +0 */
    u32 p_offset;      /* +4 */
    u32 p_vaddr;       /* +8 */
    u32 p_paddr;       /* +12 */
    u32 p_filesz;      /* +16 */
    u32 p_memsz;       /* +20 */
    u32 p_flags;       /* +24 */
    u32 p_align;       /* +28 */
};

/* elf_parse: 校验并打印 ELF32 的 e_entry 与各 PT_LOAD 段。
 * 返回 0 成功；负值为错误码（fail-closed：任一校验不过即拒绝）。 */
int elf_parse(const void *buf, u32 size)
{
    const struct elf32_ehdr *eh = (const struct elf32_ehdr *)buf;
    const struct elf32_phdr *ph;
    u32 i;

    /* GRUB grub_elf_check_header：magic + class + data + version */
    if (eh->e_ident[EI_MAG0] != ELFMAG0 || eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 || eh->e_ident[EI_MAG3] != ELFMAG3)
        return -1;                          /* not ELF */
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -2;                          /* 只支持 32 位 */
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return -3;                          /* 只支持小端 */
    if (eh->e_ident[EI_VERSION] != EV_CURRENT)
        return -4;
    if (eh->e_machine != EM_386)
        return -5;                          /* 只支持 i386 */
    if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(struct elf32_phdr))
        return -6;                          /* 没有可用的程序头 */
    if (eh->e_phoff + (u32)eh->e_phnum * eh->e_phentsize > size)
        return -7;                          /* 程序头表越界 */

    vga_puts("B06 elf: entry=");
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

/* ---- loader_main: 演示 ELF 解析 ------------------------------------------ */
#define KERNEL_BUF   0x00068000u   /* GRUB scratch 区起点，作内核暂存 */
#define KERNEL_LBA   9u            /* 软盘 LBA 9 起放 test-kernel.elf */
#define KERNEL_SECT  8u            /* 占 8 个扇区 */

void loader_main(void)
{
    vga_clear();
    vga_puts("B06 Mini-GRUB: ELF32 parser\n");
    vga_puts("reading test-kernel.elf from LBA ");
    vga_hex(KERNEL_LBA, 2);
    vga_puts(" x ");
    vga_hex(KERNEL_SECT, 2);
    vga_puts(" sectors\n");

    if (disk_read_lba(0, KERNEL_LBA, (void *)KERNEL_BUF, KERNEL_SECT) < 0) {
        vga_puts("B06 error: disk read failed\n");
        return;
    }
    if (elf_parse((const void *)KERNEL_BUF, KERNEL_SECT * 512u) < 0) {
        vga_puts("B06 error: invalid ELF\n");
        return;
    }
    vga_puts("B06 done: ELF32 parse OK\n");
}

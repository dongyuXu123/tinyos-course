/* Lesson B10: Mini-GRUB stage2 — E820 内存图与 type-6 mmap tag
 *
 * B06/B07 能解析 ELF 并校验 Multiboot2 header；本课把它们用起来：
 *   elf_load(buf, size) —— 按 PT_LOAD 把段复制到 p_paddr，bss 尾部清零；
 *   mb2_boot(entry, mbi) —— stage2.S 中的交接跳转（EAX=magic, EBX=MBI）。
 * 参照：grub-core/kern/elfXX.c（grub_elfXX_load 的 PT_LOAD 循环与 bss 清零）、
 *       grub-core/commands/boot.c（boot 命令执行交接）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

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

/* ---- ELF32 结构（B06，保持不变）------------------------------------------ */
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
    return 0;
}

/* ---- Multiboot2 header 校验（B07，保持不变）------------------------------- */
#define MB2_HEADER_MAGIC       0xe85250d6u
#define MB2_ARCHITECTURE_I386  0u
#define MB2_HEADER_ALIGN       8u
#define MB2_SEARCH_LIMIT       32768u

struct mb2_header {
    u32 magic;
    u32 architecture;
    u32 length;
    u32 checksum;
};

struct mb2_header_tag {
    u16 type;
    u16 flags;
    u32 size;
};

int mb2_header_check(const void *buf, u32 size, u32 *hdr_off)
{
    const u8 *base = (const u8 *)buf;
    u32 limit = size < MB2_SEARCH_LIMIT ? size : MB2_SEARCH_LIMIT;
    u32 off;

    for (off = 0; off + 16u <= limit; off += MB2_HEADER_ALIGN) {
        const struct mb2_header *h =
            (const struct mb2_header *)(base + off);

        if (h->magic != MB2_HEADER_MAGIC)
            continue;
        if (h->architecture != MB2_ARCHITECTURE_I386)
            return -2;
        if (h->length < 16u || off + h->length > size)
            return -3;
        if (h->magic + h->architecture + h->length + h->checksum != 0u)
            return -4;
        {
            u32 t = 16u;
            while (t < h->length) {
                const struct mb2_header_tag *tag =
                    (const struct mb2_header_tag *)((const u8 *)h + t);
                if (tag->size < 8u || t + tag->size > h->length)
                    return -5;
                if (tag->type == 0)
                    break;
                t += (tag->size + 7u) & ~7u;
            }
        }
        if (hdr_off)
            *hdr_off = off;
        return 0;
    }
    return -1;
}

/* ---- ELF 装载（本课核心）-------------------------------------------------- */
/* elf_load: 按 PT_LOAD 段把文件字节复制到 p_paddr，bss 尾部（memsz>filesz）
 * 清零。返回 0 成功；负值失败（fail-closed）。
 * 对照 GRUB grub_elfXX_load：p_filesz 从文件读，p_memsz-p_filesz 清零。 */
int elf_load(const void *buf, u32 size)
{
    const struct elf32_ehdr *eh = (const struct elf32_ehdr *)buf;
    const struct elf32_phdr *ph;
    u32 i, j;

    if (elf_parse(buf, size) < 0)
        return -1;

    ph = (const struct elf32_phdr *)((const u8 *)buf + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph->p_type == PT_LOAD) {
            if (ph->p_paddr < 0x00100000u)
                return -2;               /* 拒绝装载到 loader 低内存区 */
            if (ph->p_offset + ph->p_filesz > size)
                return -3;               /* 文件数据越界 */
            {
                const u8 *src = (const u8 *)buf + ph->p_offset;
                u8 *dst = (u8 *)ph->p_paddr;
                for (j = 0; j < ph->p_filesz; j++)
                    dst[j] = src[j];     /* 复制 p_filesz */
                for (; j < ph->p_memsz; j++)
                    dst[j] = 0;          /* bss 清零 */
            }
            vga_puts("B10 load: paddr=");
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

/* ---- 交接（本课核心）------------------------------------------------------ */
#define MB2_BOOT_MAGIC  0x36d76289u   /* Multiboot2 交接时 EAX 的 magic */

/* stage2.S 实现：设置 EAX/EBX 并跳转内核入口（不返回） */
void mb2_boot(u32 entry, u32 mbi_addr);

/* ---- MBI 构建（本课核心）------------------------------------------------- */
#define MB2_TAG_END              0u
#define MB2_TAG_BOOT_LOADER_NAME 2u

struct mb2_tag {
    u32 type;
    u32 size;
};

/* 固定缓冲（.bss，8 对齐，低内存）；构建结果 = boot loader name tag + end tag */
static u8 mbi_buf[512] __attribute__((aligned(8)));
static const char boot_loader_name[] = "Mini-GRUB 0.1";

/* ---- E820 内存图收集（本课核心）---------------------------------------- */
#define MB2_TAG_MMAP        6u
#define MB2_MMAP_AVAILABLE  1u

/* 24 字节条目，与 multiboot2.h 的 struct multiboot_mmap_entry 一致 */
struct mmap_entry {
    u64 addr;   /* +0  */
    u64 len;    /* +8  */
    u32 type;   /* +16 */
    u32 zero;   /* +20 */
};

/* E820 收集缓冲（.bss，低内存，实模式 ES:DI 可访问） */
static struct mmap_entry mmap_entries[24] __attribute__((aligned(8)));
static u32 mmap_count = 0;

/* mmap_collect: 用 INT 15 E820 循环收集内存图。返回条目数；0 表示失败。
 * 对照 GRUB grub_get_mmap_entry / grub_machine_mmap_iterate：
 *   EAX=0xE820, EDX='SMAP', EBX=续传值, ECX=缓冲大小, ES:DI=缓冲；
 *   返回 CF 或 EAX!='SMAP' 或 ECX 越界即结束。 */
static int __attribute__((noinline)) mmap_collect(void)
{
    u32 cont = 0;
    u32 n = 0;

    while (n < 24u) {
        struct bios_regs regs;
        struct mmap_entry *e = &mmap_entries[n];

        e->addr = 0;                    /* 先清零，BIOS 可能只回填 20 字节 */
        e->len = 0;
        e->type = 0;
        e->zero = 0;

        regs.eax = 0x0000e820u;
        regs.edx = 0x534d4150u;         /* 'SMAP' */
        regs.ebx = cont;                /* 续传值（0 开始，非 0 结束） */
        regs.ecx = 24u;                 /* 请求 24 字节条目 */
        regs.es  = (u16)((u32)e >> 4);  /* ES:DI = 条目缓冲 */
        regs.edi = (u32)e & 0xFu;
        regs.ds  = 0;
        regs.flags = 0x0200u;
        regs.esi = 0;

        bios_interrupt(0x15, &regs);

        if ((regs.flags & 0x01u) || regs.eax != 0x534d4150u ||
            regs.ecx < 20u || regs.ecx > 0x400u)
            break;                      /* 出错结束 */
        cont = regs.ebx;
        n++;
        if (cont == 0)
            break;                      /* 正常结束 */
    }
    mmap_count = n;
    return (int)n;
}

/* mbi_build: 在 mbi_buf 中构建 MBI（total_size + type-2 tag + type-6 mmap
 * tag + end tag），返回 MBI 物理地址。结构对照 GRUB make_mbi：逐个 append
 * tag、8 对齐、最后写 total_size。 */

static u32 mbi_build(void)
{
    u8 *p = mbi_buf + 8u;              /* 跳过 total_size + reserved */
    struct mb2_tag *t;
    u32 namelen = sizeof(boot_loader_name) - 1u;
    u32 i;

    /* type-2 boot loader name tag：{type, size=8+len, name} */
    t = (struct mb2_tag *)p;
    t->type = MB2_TAG_BOOT_LOADER_NAME;
    t->size = 8u + namelen;
    p += 8u;
    for (i = 0; i < namelen; i++)
        p[i] = (u8)boot_loader_name[i];
    p += namelen;
    while ((u32)p & 7u)                /* 8 字节对齐 */
        *p++ = 0;

    /* type-6 mmap tag：{type, size, entry_size=24, entry_version=0, entries} */
    if (mmap_count > 0) {
        u32 *mm = (u32 *)p;
        mm[0] = MB2_TAG_MMAP;
        mm[1] = 16u + 24u * mmap_count;
        mm[2] = 24u;                   /* entry_size（TinyOS 要求 >=24 且 %8==0） */
        mm[3] = 0u;                    /* entry_version */
        p += 16u;
        for (i = 0; i < mmap_count; i++) {
            u64 *e = (u64 *)p;
            e[0] = mmap_entries[i].addr;
            e[1] = mmap_entries[i].len;
            ((u32 *)p)[4] = mmap_entries[i].type;
            ((u32 *)p)[5] = 0;         /* 规范第 4 字段 */
            p += 24u;
        }
    }

    /* end tag：type=0, size=8 */
    t = (struct mb2_tag *)p;
    t->type = MB2_TAG_END;
    t->size = 8u;
    p += 8u;

    *(u32 *)mbi_buf = (u32)(p - mbi_buf);   /* total_size */
    *(u32 *)(mbi_buf + 4u) = 0;             /* reserved */
    return (u32)(u32 *)mbi_buf;
}

/* ---- loader_main ---------------------------------------------------------- */
#define KERNEL_BUF   0x00068000u
#define KERNEL_LBA   9u
#define KERNEL_SECT  18u

void loader_main(void)
{
    const struct elf32_ehdr *eh;

    vga_clear();
    vga_puts("B10 Mini-GRUB: E820 -> type-6 mmap tag\n");

    vga_puts("collecting E820 memory map...\n");
    vga_puts("B10 mmap: entries=");
    vga_hex((u32)mmap_collect(), 2);
    vga_puts("\n");

    if (disk_read_lba(0, KERNEL_LBA, (void *)KERNEL_BUF, KERNEL_SECT) < 0) {
        vga_puts("B10 error: disk read failed\n");
        return;
    }
    if (mb2_header_check((const void *)KERNEL_BUF, KERNEL_SECT * 512u, 0) < 0) {
        vga_puts("B10 error: multiboot2 header rejected\n");
        return;
    }
    if (elf_load((const void *)KERNEL_BUF, KERNEL_SECT * 512u) < 0) {
        vga_puts("B10 error: ELF load failed\n");
        return;
    }

    eh = (const struct elf32_ehdr *)KERNEL_BUF;
    vga_puts("B10 boot: entry=");
    vga_hex(eh->e_entry, 8);
    vga_puts(" eax=36d76289 mbi=");
    vga_hex(mbi_build(), 8);
    vga_puts("\n");
    vga_puts("B10 boot: jumping to kernel...\n");

    mb2_boot(eh->e_entry, mbi_build());        /* 不返回 */
    /* 内核启动后接管屏幕，walk MBI tag 链 */
}

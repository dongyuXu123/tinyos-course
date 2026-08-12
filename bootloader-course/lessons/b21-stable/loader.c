/* Lesson B21: Mini-GRUB stage2 — type-8 framebuffer tag → 启动 TinyOS GUI
 *
 * 综合课：把 B17 的完整引导链（ISO9660 + 命令 + 脚本 + multiboot2/boot）、
 * B11 的 E820 内存图（type-6 mmap tag）、B20 的 VBE（设置 800x600x32 LFB）
 * 组合起来，向内核 MBI 追加 type-8 framebuffer tag 后交接。
 * 对照 GRUB：loader/multiboot.c 读内核 header 的 graphics request tag 并调
 * grub_video_set_mode；multiboot_mbi2.c 的 make_mbi 生成 type-8 tag。
 * TinyOS L61 内核要求：magic 0x36d76289、type-6 mmap（entry_size≥24）、
 * type-8 framebuffer（bpp=32、type_field=1、地址页对齐、pitch≥width*4）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

/* ---- VGA 文本库（B04）----------------------------------------------------- */
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

/* ---- 串口（B20；VBE 切换后 0xB8000 失效，验证日志走串口）------------------ */
#define COM1 0x3F8u

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
}

static void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20u))
        ;
    outb(COM1 + 0, (u8)c);
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void serial_hex(u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        u8 nib = (u8)((v >> (4 * i)) & 0xF);
        serial_putc(nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10));
    }
}

/* 双通道日志：VGA 文本 + 串口 */
static void log_puts(const char *s)
{
    vga_puts(s);
    serial_puts(s);
}

static void log_hex(u32 v, int digits)
{
    vga_hex(v, digits);
    serial_hex(v, digits);
}

static void log_putc(char c)
{
    vga_putc(c);
    serial_putc(c);
}

/* ---- BIOS 回调（B05）------------------------------------------------------ */
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

/* ---- CD 读盘原语（B13）---------------------------------------------------- */
#define CD_SECTOR_SIZE  2048u
#define CD_BUF_PVD      0x68000u
#define CD_BUF_CFG      0x68000u
#define CD_BUF_DIR      0x6A000u
#define CD_BUF_FILE     0x6A800u
#define CD_BUF_KERNEL   0x10000u    /* 内核文件暂存（L61 kernel.elf = 131KB） */
#define BOOT_DRIVE_ADDR 0x60000u
#define CFG_MAX         8192u
#define KERNEL_MAX      0x24000u    /* 147456 ≥ 131652 */

struct dap {
    u8  size;
    u8  reserved;
    u16 count;
    u16 offset;
    u16 segment;
    u32 lba_lo;
    u32 lba_hi;
} __attribute__((packed));

static struct dap cd_dap;

static int cd_read_lba(u8 drive, u32 lba, void *buf, u32 count)
{
    u32 phys = (u32)buf;
    struct bios_regs regs;

    cd_dap.size = 0x10;
    cd_dap.reserved = 0;
    cd_dap.count = (u16)count;
    cd_dap.offset = (u16)(phys & 0xFu);
    cd_dap.segment = (u16)(phys >> 4);
    cd_dap.lba_lo = lba;
    cd_dap.lba_hi = 0;

    regs.eax = 0x00004200u;
    regs.edx = drive;
    regs.es  = 0;
    regs.ds  = 0;
    regs.esi = (u32)&cd_dap;
    regs.flags = 0x0200u;
    regs.ebx = 0;
    regs.ecx = 0;
    regs.edi = 0;

    bios_interrupt(0x13, &regs);
    return (regs.flags & 0x01u) ? -1 : 0;
}

/* ---- 磁盘 / 设备 / 文件系统（B14/B15）------------------------------------- */
struct grub_disk {
    u8  drive;
    u32 sector_size;
};

static struct grub_disk cd_disk;

static int grub_disk_read(struct grub_disk *disk, u32 lba, u32 count, void *buf)
{
    return cd_read_lba(disk->drive, lba, buf, count);
}

#define PVD_LBA        16u
#define PVD_ROOT_OFF   156u
#define DIR_FLAG_DIR   0x02u

struct iso_dir_record {
    u8 len;
    u8 ext_attr_len;
    u8 ext_lba[4];
    u8 ext_lba_msb[4];
    u8 data_len[4];
    u8 data_len_msb[4];
    u8 date[7];
    u8 flags;
    u8 unit_size;
    u8 gap;
    u8 vol_seq[2];
    u8 vol_seq_msb[2];
    u8 name_len;
    u8 name[1];
};

static u32 iso_root_extent;
static u32 iso_root_size;

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int iso9660_mount(void)
{
    u8 *pvd = (u8 *)CD_BUF_PVD;

    if (grub_disk_read(&cd_disk, PVD_LBA, 1, pvd) < 0)
        return -1;
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1')
        return -2;
    iso_root_extent = le32(pvd + PVD_ROOT_OFF + 2);
    iso_root_size   = le32(pvd + PVD_ROOT_OFF + 10);
    return 0;
}

static int name_matches(const u8 *name, u8 nlen, const char *target)
{
    u8 i;
    for (i = 0; i < nlen; i++) {
        if (name[i] == ';')
            break;
        if (target[i] == 0 || name[i] != (u8)target[i])
            return 0;
    }
    return target[i] == 0;
}

static int find_in_dir(u32 dir_extent, u32 dir_size, const char *name,
                       u32 *out_extent, u32 *out_size, int *out_isdir)
{
    u8 *dir = (u8 *)CD_BUF_DIR;
    u32 sectors = (dir_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
    u32 off = 0;

    if (sectors > 8)
        sectors = 8;
    if (grub_disk_read(&cd_disk, dir_extent, sectors, dir) < 0)
        return -1;
    while (off < dir_size && off < sectors * CD_SECTOR_SIZE) {
        struct iso_dir_record *r = (struct iso_dir_record *)(dir + off);
        if (r->len == 0)
            break;
        if (r->len < 34) {
            off += 1;
            continue;
        }
        if (name_matches(r->name, r->name_len, name)) {
            *out_extent = le32(r->ext_lba);
            *out_size   = le32(r->data_len);
            *out_isdir  = (r->flags & DIR_FLAG_DIR) ? 1 : 0;
            return 0;
        }
        off += r->len;
    }
    return -2;
}

struct grub_file {
    u32 extent;
    u32 size;
    u32 pos;
};

static int file_open(const char *path, struct grub_file *file)
{
    const char *p = path;
    u32 extent = iso_root_extent;
    u32 size = iso_root_size;
    int isdir = 1;

    if (*p == '/')
        p++;
    while (*p) {
        char comp[16];
        u32 clen = 0;
        int r, child_is_dir;

        while (*p && *p != '/') {
            if (clen < sizeof(comp) - 1)
                comp[clen++] = *p;
            p++;
        }
        comp[clen] = 0;
        if (*p == '/')
            p++;
        if (clen == 0)
            continue;

        if (!isdir)
            return -3;
        r = find_in_dir(extent, size, comp, &extent, &size, &child_is_dir);
        if (r < 0)
            return r;
        if (*p == 0) {
            if (child_is_dir)
                return -3;
            file->extent = extent;
            file->size = size;
            file->pos = 0;
            return 0;
        }
        isdir = child_is_dir;
    }
    return -3;
}

static int file_read(struct grub_file *file, void *buf, u32 len)
{
    u8 *out = buf;
    u32 left = len;
    u32 pos = file->pos;

    while (left) {
        u32 sector = file->extent + pos / CD_SECTOR_SIZE;
        u32 in = pos % CD_SECTOR_SIZE;
        u32 chunk = CD_SECTOR_SIZE - in;
        if (chunk > left)
            chunk = left;
        if (in == 0 && chunk == CD_SECTOR_SIZE) {
            if (grub_disk_read(&cd_disk, sector, 1, out) < 0)
                return -1;
        } else {
            u8 *scratch = (u8 *)CD_BUF_FILE;
            u32 i;
            if (grub_disk_read(&cd_disk, sector, 1, scratch) < 0)
                return -1;
            for (i = 0; i < chunk; i++)
                out[i] = scratch[in + i];
        }
        out += chunk;
        pos += chunk;
        left -= chunk;
    }
    file->pos = pos;
    return 0;
}

/* ---- 环境变量与 tokenizer（B17，script.c）---------------------------------- */
int env_set(const char *name, const char *value);
const char *env_get(const char *name);
int script_tokenize(const char *line, char **argv, int max);

/* ---- VBE（B20，vbe.c/vbe.h）----------------------------------------------- */
#include "vbe.h"

#define VBE_INFO_BUF   0x7000u
#define VBE_MODE_BUF   0x7200u

static __attribute__((noinline)) int vbe_call(u16 ax, u16 cx, u16 bx, u16 buf)
{
    struct bios_regs regs;

    regs.eax = ax;
    regs.ebx = bx;
    regs.ecx = cx;
    regs.edx = 0;
    regs.es = (u16)(buf >> 4);
    regs.ds = 0;
    regs.edi = (u16)(buf & 0xF);
    regs.esi = 0;
    regs.flags = 0x0200u;

    bios_interrupt(0x10, &regs);
    return ((regs.eax & 0xFF) == 0x4F) ? 0 : -1;
}

static __attribute__((noinline)) int vbe_get_info(void)
{
    u8 *b = (u8 *)VBE_INFO_BUF;
    if (vbe_call(0x4F00u, 0, 0, VBE_INFO_BUF) < 0)
        return -1;
    if (b[0] != 'V' || b[1] != 'E' || b[2] != 'S' || b[3] != 'A')
        return -2;
    return 0;
}

static __attribute__((noinline)) int vbe_get_mode_info(u16 mode)
{
    return vbe_call(0x4F01u, mode, 0, VBE_MODE_BUF);
}

static __attribute__((noinline)) int vbe_set_mode(u16 mode)
{
    return vbe_call(0x4F02u, 0, (u16)(mode | 0x4000), VBE_MODE_BUF);
}

static __attribute__((noinline)) int vbe_find_mode(u16 want_w, u16 want_h,
                                                   u8 want_bpp, u16 *out_mode,
                                                   struct vbe_mode_info *out)
{
    const u8 *info = (const u8 *)VBE_INFO_BUF;
    u32 mode_ptr = (u32)info[0x0E] | ((u32)info[0x0F] << 8) |
                   ((u32)info[0x10] << 16) | ((u32)info[0x11] << 24);
    const u16 *list = (const u16 *)((u32)((mode_ptr >> 16) << 4) + (mode_ptr & 0xFFFF));
    u32 i;

    for (i = 0; list[i] != 0xFFFF; i++) {
        if (vbe_get_mode_info(list[i]) < 0)
            continue;
        if (vbe_parse_mode_info((const u8 *)VBE_MODE_BUF, out) < 0)
            continue;
        if (vbe_mode_matches(out, want_w, want_h, want_bpp)) {
            *out_mode = list[i];
            return 0;
        }
    }
    return -1;
}

/* ---- ELF32 解析与装载（B06/B17）------------------------------------------- */
#define EI_CLASS  4
#define EI_DATA   5
#define ELFCLASS32 1
#define ELFDATA2LSB 1
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

static int elf_parse(const void *buf, u32 size)
{
    const struct elf32_ehdr *eh = (const struct elf32_ehdr *)buf;

    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -2;
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return -3;
    if (eh->e_machine != EM_386)
        return -4;
    if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(struct elf32_phdr))
        return -5;
    if (eh->e_phoff + (u32)eh->e_phnum * eh->e_phentsize > size)
        return -6;
    return 0;
}

static int elf_load(const void *buf, u32 size)
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
                return -2;
            if (ph->p_offset + ph->p_filesz > size)
                return -3;
            {
                const u8 *src = (const u8 *)buf + ph->p_offset;
                u8 *dst = (u8 *)ph->p_paddr;
                for (j = 0; j < ph->p_filesz; j++)
                    dst[j] = src[j];
                for (; j < ph->p_memsz; j++)
                    dst[j] = 0;
            }
        }
        ph = (const struct elf32_phdr *)((const u8 *)ph + eh->e_phentsize);
    }
    return 0;
}

/* ---- Multiboot2 header：graphics request tag（type 5）解析（B07 扩展）------ */
#define MB2_HEADER_MAGIC       0xe85250d6u
#define MB2_HEADER_TAG_GRAPHICS 5u
#define MB2_SEARCH_LIMIT       32768u

struct mb2_header {
    u32 magic;
    u32 architecture;
    u32 length;
    u32 checksum;
};

/* 在内核装载区（0x100000 起）搜索 mb2 header，返回 graphics request 的
 * width/height/depth；无请求 tag 时返回 0（调用方用默认 800x600x32）。 */
static __attribute__((noinline)) int mb2_graphics_request(u32 *w, u32 *h, u32 *depth)
{
    const u8 *base = (const u8 *)0x100000u;
    u32 off;

    for (off = 0; off + 16u <= MB2_SEARCH_LIMIT; off += 8u) {
        const struct mb2_header *hd = (const struct mb2_header *)(base + off);
        u32 t;

        if (hd->magic != MB2_HEADER_MAGIC)
            continue;
        if (hd->length < 16u || off + hd->length > MB2_SEARCH_LIMIT)
            continue;
        t = 16u;
        while (t + 8u <= hd->length) {
            u16 type = (u16)((const u8 *)hd)[t] | ((u16)((const u8 *)hd)[t + 1] << 8);
            u32 size = (u32)((const u8 *)hd)[t + 4] |
                       ((u32)((const u8 *)hd)[t + 5] << 8) |
                       ((u32)((const u8 *)hd)[t + 6] << 16) |
                       ((u32)((const u8 *)hd)[t + 7] << 24);
            if (size < 8u || t + size > hd->length)
                break;
            if (type == MB2_HEADER_TAG_GRAPHICS && size >= 20u) {
                const u8 *gt = (const u8 *)hd + t + 8u;
                *w = (u32)gt[0] | ((u32)gt[1] << 8) |
                     ((u32)gt[2] << 16) | ((u32)gt[3] << 24);
                *h = (u32)gt[4] | ((u32)gt[5] << 8) |
                     ((u32)gt[6] << 16) | ((u32)gt[7] << 24);
                *depth = (u32)gt[8] | ((u32)gt[9] << 8) |
                         ((u32)gt[10] << 16) | ((u32)gt[11] << 24);
                return 1;
            }
            if (type == 0)
                break;              /* end tag */
            t += (size + 7u) & ~7u;
        }
        return 0;
    }
    return 0;
}

/* ---- E820 内存图收集（B11）→ type-6 mmap tag ------------------------------- */
#define MB2_TAG_MMAP        6u
#define MB2_MMAP_AVAILABLE  1u

struct mmap_entry {
    u64 addr;
    u64 len;
    u32 type;
    u32 zero;
};

static struct mmap_entry mmap_entries[24] __attribute__((aligned(8)));
static u32 mmap_count = 0;

static int __attribute__((noinline)) mmap_collect(void)
{
    u32 cont = 0;
    u32 n = 0;

    while (n < 24u) {
        struct bios_regs regs;
        struct mmap_entry *e = &mmap_entries[n];

        e->addr = 0;
        e->len = 0;
        e->type = 0;
        e->zero = 0;

        regs.eax = 0x0000e820u;
        regs.edx = 0x534d4150u;
        regs.ebx = cont;
        regs.ecx = 24u;
        regs.es  = (u16)((u32)e >> 4);
        regs.edi = (u32)e & 0xFu;
        regs.ds  = 0;
        regs.flags = 0x0200u;
        regs.esi = 0;

        bios_interrupt(0x15, &regs);

        if ((regs.flags & 0x01u) || regs.eax != 0x534d4150u ||
            regs.ecx < 20u || regs.ecx > 0x400u)
            break;
        cont = regs.ebx;
        n++;
        if (cont == 0)
            break;
    }
    mmap_count = n;
    return (int)n;
}

/* ---- MBI 构建（B11 + type-8 framebuffer tag）------------------------------- */
#define MB2_TAG_END              0u
#define MB2_TAG_BOOT_LOADER_NAME 2u
#define MB2_TAG_FRAMEBUFFER      8u

struct mb2_tag {
    u32 type;
    u32 size;
};

static u8 mbi_buf[1024] __attribute__((aligned(8)));
static const char boot_loader_name[] = "Mini-GRUB 0.2";

/* 全局 framebuffer 状态（vbe 协商结果，boot 时填进 type-8 tag） */
static struct vbe_mode_info fb_mode;
static int fb_ready = 0;

static __attribute__((noinline)) u32 mbi_build(void)
{
    u8 *p = mbi_buf + 8u;
    struct mb2_tag *t;
    u32 namelen = sizeof(boot_loader_name) - 1u;
    u32 i;

    /* type-2 boot loader name tag */
    t = (struct mb2_tag *)p;
    t->type = MB2_TAG_BOOT_LOADER_NAME;
    t->size = 8u + namelen;
    p += 8u;
    for (i = 0; i < namelen; i++)
        p[i] = (u8)boot_loader_name[i];
    p += namelen;
    while ((u32)p & 7u)
        *p++ = 0;

    /* type-6 mmap tag（TinyOS L61 强制要求：无 mmap 则 prepare_memory_map 失败）*/
    if (mmap_count > 0) {
        u32 *mm = (u32 *)p;
        mm[0] = MB2_TAG_MMAP;
        mm[1] = 16u + 24u * mmap_count;
        mm[2] = 24u;
        mm[3] = 0u;
        p += 16u;
        for (i = 0; i < mmap_count; i++) {
            u64 *e = (u64 *)p;
            e[0] = mmap_entries[i].addr;
            e[1] = mmap_entries[i].len;
            ((u32 *)p)[4] = mmap_entries[i].type;
            ((u32 *)p)[5] = 0;
            p += 24u;
        }
        while ((u32)p & 7u)
            *p++ = 0;
    }

    /* type-8 framebuffer tag（本课核心；对照 multiboot2.h 的
     * struct multiboot_tag_framebuffer：type/size + address(u64) +
     * pitch/width/height + bpp + type_field(1=direct RGB) + reserved） */
    if (fb_ready) {
        u8 *fbt = p;
        u32 *fb = (u32 *)fbt;
        fb[0] = MB2_TAG_FRAMEBUFFER;
        fb[1] = 32u;                    /* size = 32 */
        fb[2] = fb_mode.phys_base;      /* address 低 32 位 */
        fb[3] = 0;                      /* address 高 32 位 */
        fb[4] = fb_mode.pitch;
        fb[5] = fb_mode.width;
        fb[6] = fb_mode.height;
        fbt[28] = fb_mode.bpp;          /* bpp */
        fbt[29] = 1;                    /* type_field = 1 (direct RGB) */
        fbt[30] = 0;                    /* reserved */
        fbt[31] = 0;
        p += 32u;
    }

    /* end tag */
    t = (struct mb2_tag *)p;
    t->type = MB2_TAG_END;
    t->size = 8u;
    p += 8u;

    *(u32 *)mbi_buf = (u32)(p - mbi_buf);
    *(u32 *)(mbi_buf + 4u) = 0;
    return (u32)mbi_buf;
}

/* ---- 命令注册表（B16）----------------------------------------------------- */
static int name_eq(const char *a, const char *b);

struct cmd {
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *help;
    struct cmd *next;
};

static struct cmd *cmd_list;

int cmd_register(struct cmd *c)
{
    c->next = cmd_list;
    cmd_list = c;
    return 0;
}

static struct cmd *cmd_find(const char *name)
{
    struct cmd *c;
    for (c = cmd_list; c; c = c->next)
        if (name_eq(c->name, name))
            return c;
    return 0;
}

int cmd_execute(int argc, char **argv)
{
    struct cmd *c = cmd_find(argv[0]);
    if (!c) {
        vga_puts("B21 error: command not found: ");
        vga_puts(argv[0]);
        vga_putc('\n');
        return -1;
    }
    return c->fn(argc, argv);
}

void mb2_boot(u32 entry, u32 mbi_addr);

/* ---- 命令实现 -------------------------------------------------------------- */
static __attribute__((noinline)) int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int cmd_echo_fn(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1)
            vga_putc(' ');
        vga_puts(argv[i]);
    }
    vga_putc('\n');
    return 0;
}

static int cmd_set_fn(int argc, char **argv)
{
    if (argc < 2)
        return 0;
    {
        const char *arg = argv[1];
        const char *eq = arg;
        while (*eq && *eq != '=')
            eq++;
        if (*eq == '=') {
            char name[16];
            u32 nlen = (u32)(eq - arg);
            u32 i;
            if (nlen >= 16)
                nlen = 15;
            for (i = 0; i < nlen; i++)
                name[i] = arg[i];
            name[nlen] = 0;
            env_set(name, eq + 1);
        }
    }
    return 0;
}

/* 已装载内核状态（multiboot2 命令设置，boot 命令使用） */
static u32 loaded_entry = 0;
static int loaded = 0;

static int cmd_multiboot2_fn(int argc, char **argv)
{
    struct grub_file f;
    u8 *kbuf = (u8 *)CD_BUF_KERNEL;
    const struct elf32_ehdr *eh;
    u32 gw = 800, gh = 600, gdepth = 32;   /* 无请求时默认 800x600x32 */
    u16 mode = 0;

    if (argc < 2) {
        vga_puts("B21 error: multiboot2 requires a file\n");
        return -1;
    }
    if (file_open(argv[1], &f) < 0) {
        vga_puts("B21 error: no such file: ");
        vga_puts(argv[1]);
        vga_putc('\n');
        return -1;
    }
    if (f.size > KERNEL_MAX) {
        vga_puts("B21 error: kernel too big\n");
        return -1;
    }
    if (file_read(&f, kbuf, f.size) < 0) {
        vga_puts("B21 error: read failed\n");
        return -1;
    }
    if (elf_load(kbuf, f.size) < 0) {
        vga_puts("B21 error: bad ELF\n");
        return -1;
    }
    eh = (const struct elf32_ehdr *)kbuf;
    loaded_entry = eh->e_entry;
    loaded = 1;
    log_puts("B21 multiboot2: loaded ");
    log_puts(argv[1]);
    log_puts(" entry=");
    log_hex(loaded_entry, 8);
    log_putc('\n');

    /* 读内核 graphics request tag，设置 VBE 图形模式（GRUB 同款行为） */
    if (mb2_graphics_request(&gw, &gh, &gdepth)) {
        log_puts("B21 vbe: graphics request ");
        log_hex(gw, 4);
        log_puts("x");
        log_hex(gh, 4);
        log_puts("x");
        log_hex(gdepth, 2);
        log_putc('\n');
    } else {
        log_puts("B21 vbe: no graphics request, defaulting 800x600x32\n");
    }

    if (vbe_get_info() < 0) {
        log_puts("B21 vbe: ERROR: VBE not available\n");
        return -1;
    }
    if (vbe_find_mode((u16)gw, (u16)gh, (u8)gdepth, &mode, &fb_mode) < 0) {
        log_puts("B21 vbe: ERROR: requested mode unavailable\n");
        return -1;
    }
    if (vbe_set_mode(mode) < 0) {
        log_puts("B21 vbe: ERROR: set mode failed\n");
        return -1;
    }
    fb_ready = 1;
    log_puts("B21 vbe: mode set, lfb=");
    log_hex(fb_mode.phys_base, 8);
    log_puts(" pitch=");
    log_hex(fb_mode.pitch, 4);
    log_putc('\n');
    return 0;
}

static int cmd_boot_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!loaded) {
        vga_puts("B21 error: no kernel loaded\n");
        return -1;
    }
    if (!fb_ready) {
        vga_puts("B21 error: framebuffer not ready\n");
        return -1;
    }
    /* E820 内存图（内核 bootstrap 分配器要用） */
    mmap_collect();
    log_puts("B21 mmap: ");
    log_hex(mmap_count, 2);
    log_puts(" entries\n");
    log_puts("B21 boot: jumping to entry=");
    log_hex(loaded_entry, 8);
    log_puts(" mbi=");
    log_hex(mbi_build(), 8);
    log_putc('\n');
    mb2_boot(loaded_entry, mbi_build());   /* 不返回 */
    return 0;
}

static struct cmd cmd_echo       = { "echo",       cmd_echo_fn,       "print its arguments", 0 };
static struct cmd cmd_set        = { "set",        cmd_set_fn,        "get/set environment variables", 0 };
static struct cmd cmd_multiboot2 = { "multiboot2", cmd_multiboot2_fn, "load a Multiboot2 kernel", 0 };
static struct cmd cmd_boot       = { "boot",       cmd_boot_fn,       "boot the loaded kernel", 0 };

static void cmd_register_all(void)
{
    cmd_register(&cmd_echo);
    cmd_register(&cmd_set);
    cmd_register(&cmd_multiboot2);
    cmd_register(&cmd_boot);
}

/* ---- 脚本执行器（B17）----------------------------------------------------- */
#define LINE_MAX 256
#define ARGV_MAX 16

static char *argv_list[ARGV_MAX];

static void script_execute_line(const char *l)
{
    int argc;
    argc = script_tokenize(l, argv_list, ARGV_MAX);
    if (argc == 0)
        return;
    cmd_execute(argc, argv_list);
}

static int __attribute__((noinline)) script_run_file(const char *path)
{
    struct grub_file f;
    char *cfg = (char *)CD_BUF_CFG;
    char *p, *end;

    if (file_open(path, &f) < 0)
        return -1;
    if (f.size > CFG_MAX)
        return -2;
    if (file_read(&f, cfg, f.size) < 0)
        return -3;

    p = cfg;
    end = cfg + f.size;
    while (p < end) {
        char *nl = p;
        while (nl < end && *nl != '\n')
            nl++;
        *nl = 0;
        script_execute_line(p);
        p = nl + 1;
    }
    return 0;
}

/* ---- loader_main ---------------------------------------------------------- */
void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;

    serial_init();
    vga_clear();
    log_puts("B21 fb: Mini-GRUB framebuffer handoff\n");

    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    log_puts("B21 fb: boot drive = ");
    log_hex(drive, 2);
    log_putc('\n');

    if (iso9660_mount() < 0) {
        log_puts("B21 error: ISO9660 mount failed\n");
        for (;;)
            ;
    }

    cmd_register_all();

    if (script_run_file("/boot/grub/grub.cfg") < 0) {
        log_puts("B21 error: grub.cfg not found; halting\n");
        for (;;)
            ;
    }
    for (;;)
        ;
}

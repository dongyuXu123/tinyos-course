/* Lesson B15: Mini-GRUB stage2 — El Torito 光盘引导 + 从 CD 装载内核
 *
 * stage1 已用 boot-info-table 从 CD 读入 core image（本文件 + stage2.S，
 * 链接在 0x8400）。本课把 B14 的四层文件抽象与 B08–B11 的装载机制接起来：
 *   file_open("/BOOT/KERNEL.ELF") -> file_read -> elf_load -> mbi_build
 *   -> mb2_boot
 * 即：内核不再是软盘固定 LBA，而是 CD 文件系统里按路径找的文件。
 * 参照：grub-core/loader/multiboot.c（装载链）、kern/file.c、
 *       kern/disk/i386/pc/biosdisk.c（CD 读盘）。
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

/* ---- BIOS 回调（B05）----------------------------------------------------- */
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

/* ---- CD 读盘原语（B13）：INT 13 AH=42 + DAP，2048 字节扇区 ---------------- */
#define CD_SECTOR_SIZE  2048u
#define CD_BUF_PVD      0x68000u
#define CD_BUF_DIR      0x68800u
#define CD_BUF_ELF      0x68000u        /* 内核文件缓冲（~9KB，0x68000-0x6A2C0） */
#define CD_BUF_FILE     0x6C000u        /* file_read 的扇区暂存（避开 ELF 缓冲！
                                         * 曾与 ELF 缓冲重叠：最后一次部分读把
                                         * 暂存数据盖掉了已读好的文件内容） */
#define BOOT_DRIVE_ADDR 0x60000u

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

/* ---- 四层抽象（B14）：device -> disk -> fs -> file ------------------------ */
struct grub_disk {
    u8  drive;
    u32 sector_size;
};

static struct grub_disk cd_disk;

static int grub_disk_read(struct grub_disk *disk, u32 lba, u32 count, void *buf)
{
    return cd_read_lba(disk->drive, lba, buf, count);
}

struct grub_device {
    const char *name;
    struct grub_disk *disk;
};

static struct grub_device cd_device;

struct grub_file {
    struct grub_device *device;
    u32 extent;
    u32 size;
    u32 pos;
};

/* ---- ISO9660（B13/B14）--------------------------------------------------- */
#define PVD_LBA        16u
#define PVD_VOLID_OFF  40u
#define PVD_VOLID_LEN  32u
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

static int iso9660_mount(struct grub_device *dev)
{
    u8 *pvd = (u8 *)CD_BUF_PVD;

    if (grub_disk_read(dev->disk, PVD_LBA, 1, pvd) < 0)
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

static int iso9660_lookup(const char *path, struct grub_file *file)
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

static int iso9660_open(struct grub_device *dev, const char *path,
                        struct grub_file *file)
{
    file->device = dev;
    return iso9660_lookup(path, file);
}

static int iso9660_read(struct grub_file *file, void *buf, u32 len)
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
            if (grub_disk_read(file->device->disk, sector, 1, out) < 0)
                return -1;
        } else {
            u8 *scratch = (u8 *)CD_BUF_FILE;
            u32 i;
            if (grub_disk_read(file->device->disk, sector, 1, scratch) < 0)
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

int file_open(const char *path, struct grub_file *file)
{
    return iso9660_open(&cd_device, path, file);
}

int file_read(struct grub_file *file, void *buf, u32 len)
{
    return iso9660_read(file, buf, len);
}

/* ---- ELF32 解析与装载（B06/B08/B12，保持不变）---------------------------- */
#define EI_MAG0  0
#define EI_CLASS 4
#define EI_DATA  5
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

/* elf_load: 按 PT_LOAD 段把文件字节复制到 p_paddr，memsz>filesz 清零。
 * 对照 grub_elfXX_load（B08/B12 原样）。 */
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
            vga_puts("B15 load: paddr=");
            vga_hex(ph->p_paddr, 8);
            vga_puts(" filesz=");
            vga_hex(ph->p_filesz, 8);
            vga_puts(" memsz=");
            vga_hex(ph->p_memsz, 8);
            vga_putc('\n');
        }
        ph = (const struct elf32_phdr *)((const u8 *)ph + eh->e_phentsize);
    }
    return 0;
}

/* ---- Multiboot2 交接与最小 MBI（B08/B10/B12 复用）------------------------- */
#define MB2_BOOT_MAGIC  0x36d76289u
#define MB2_TAG_END              0u
#define MB2_TAG_BOOT_LOADER_NAME 2u

void mb2_boot(u32 entry, u32 mbi_addr);

struct mb2_tag {
    u32 type;
    u32 size;
};

static u8 mbi_buf[256] __attribute__((aligned(8)));
static const char boot_loader_name[] = "Mini-GRUB 0.1";

/* mbi_build: 构建最小 MBI = total_size + type-2 boot loader name tag +
 * end tag（对照 grub make_mbi 的 append + 8 对齐 + total_size）。 */
static u32 mbi_build(void)
{
    u8 *p = mbi_buf + 8u;
    struct mb2_tag *t;
    u32 namelen = sizeof(boot_loader_name) - 1u;
    u32 i;

    t = (struct mb2_tag *)p;
    t->type = MB2_TAG_BOOT_LOADER_NAME;
    t->size = 8u + namelen;
    p += 8u;
    for (i = 0; i < namelen; i++)
        p[i] = (u8)boot_loader_name[i];
    p += namelen;
    while ((u32)p & 7u)
        *p++ = 0;
    t = (struct mb2_tag *)p;
    t->type = MB2_TAG_END;
    t->size = 8u;
    p += 8u;
    *((u32 *)mbi_buf) = (u32)p - (u32)mbi_buf;   /* total_size */
    return (u32)mbi_buf;
}

/* ---- loader_main ---------------------------------------------------------- */
void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;
    struct grub_file f;
    u8 *kbuf = (u8 *)CD_BUF_ELF;
    const struct elf32_ehdr *eh;
    u32 mbi;

    vga_clear();
    vga_puts("B15 eltorito: core image loaded by stage1, now in loader\n");

    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    cd_device.name = "cd0";
    cd_device.disk = &cd_disk;
    vga_puts("B15 eltorito: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_mount(&cd_device) < 0) {
        vga_puts("B15 error: ISO9660 mount failed\n");
        for (;;)
            ;
    }

    if (file_open("/BOOT/KERNEL.ELF", &f) < 0) {
        vga_puts("B15 error: open /BOOT/KERNEL.ELF failed\n");
        for (;;)
            ;
    }
    vga_puts("B15 eltorito: open /BOOT/KERNEL.ELF size=");
    vga_hex(f.size, 8);
    vga_putc('\n');

    if (file_read(&f, kbuf, f.size) < 0) {
        vga_puts("B15 error: read KERNEL.ELF failed\n");
        for (;;)
            ;
    }
    if (elf_load(kbuf, f.size) < 0) {
        vga_puts("B15 error: bad ELF\n");
        for (;;)
            ;
    }

    eh = (const struct elf32_ehdr *)kbuf;
    mbi = mbi_build();

    vga_puts("B15 boot: jumping to KERNEL.ELF entry=");
    vga_hex(eh->e_entry, 8);
    vga_putc('\n');

    mb2_boot(eh->e_entry, mbi);     /* 不返回；内核接管屏幕 */
}

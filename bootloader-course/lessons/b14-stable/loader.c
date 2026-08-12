/* Lesson B14: Mini-GRUB stage2 — 路径查找与文件抽象
 *
 * B13 能读 ISO9660 的单文件；本课把"读文件"组织成 GRUB 的四层抽象：
 *   设备（device，固定 (cd0)）→ 磁盘（disk，LBA 读写）→
 *   文件系统（fs，ISO9660 目录/路径解析）→ 文件（file，open/read/close）。
 * 对照：grub-core/kern/device.c（grub_device）、kern/disk.c（grub_disk）、
 *       include/grub/fs.h（struct grub_fs 的 open/read/close）、
 *       kern/file.c（grub_file_open 的路径解析与组件拆分）。
 * B14 简化边界：固定 (cd0) 单设备、无分区层、单 extent 文件、目录最多
 * 8 扇区、ISO9660 level-1 短名（路径按 CD 实际命名大写）。
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
#define CD_BUF_FILE     0x69000u
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

/* ---- 四层抽象 -------------------------------------------------------------
 * 对照 GRUB：struct grub_disk { u32 id; grub_uint64_t total_sectors; ... }
 *            struct grub_device { const char *name; struct grub_disk *disk; ... }
 *            struct grub_file { struct grub_device *device; struct grub_fs *fs;
 *                               grub_off_t offset; grub_off_t size; ... } */

/* 第 2 层：磁盘 —— 记录 BIOS 盘号与扇区大小，提供 LBA 读写 */
struct grub_disk {
    u8  drive;           /* BIOS 盘号（0xE0） */
    u32 sector_size;     /* 2048 */
};

static struct grub_disk cd_disk;

static int grub_disk_read(struct grub_disk *disk, u32 lba, u32 count, void *buf)
{
    return cd_read_lba(disk->drive, lba, buf, count);
}

/* 第 1 层：设备 —— 命名 + 绑定磁盘（本课固定 (cd0)，无分区层） */
struct grub_device {
    const char *name;
    struct grub_disk *disk;
};

static struct grub_device cd_device;

/* 第 4 层：文件句柄 —— 路径打开后的 extent 流 */
struct grub_file {
    struct grub_device *device;
    u32 extent;          /* 数据起始 LBA */
    u32 size;            /* 文件字节数 */
    u32 pos;             /* 当前读位置 */
};

/* ---- ISO9660（第 3 层：文件系统）----------------------------------------- */
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

/* iso9660 卷状态（mount 时填充；GRUB 对应 voldesc 存于 fs data） */
static u32 iso_root_extent;
static u32 iso_root_size;
static char iso_volume_id[PVD_VOLID_LEN + 1];

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* iso9660_mount: 读 PVD 校验并记录根目录（对照 grub_iso9660_mount） */
static int iso9660_mount(struct grub_device *dev)
{
    u8 *pvd = (u8 *)CD_BUF_PVD;
    u32 i;

    if (grub_disk_read(dev->disk, PVD_LBA, 1, pvd) < 0)
        return -1;
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1')
        return -2;

    for (i = 0; i < PVD_VOLID_LEN; i++)
        iso_volume_id[i] = (char)pvd[PVD_VOLID_OFF + i];
    iso_volume_id[PVD_VOLID_LEN] = 0;

    iso_root_extent = le32(pvd + PVD_ROOT_OFF + 2);
    iso_root_size   = le32(pvd + PVD_ROOT_OFF + 10);
    return 0;
}

/* 组件名比较：ISO9660 名 "NAME.EXT;版本"，比较到 ';' 为止 */
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

/* find_in_dir: 在 (extent,size) 目录里找名为 name 的条目，返回其
 * extent/size/目录标志。返回 0 找到，-1 读失败，-2 未找到。 */
static int find_in_dir(u32 dir_extent, u32 dir_size, const char *name,
                       u32 *out_extent, u32 *out_size, int *out_isdir)
{
    u8 *dir = (u8 *)CD_BUF_DIR;
    u32 sectors = (dir_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
    u32 off = 0;

    if (sectors > 8)
        sectors = 8;                /* B14 简化：目录最多 8 扇区 */
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

/* iso9660 路径查找：绝对路径从根目录按 '/' 拆分组件逐层下钻，
 * 最后组件必须是常规文件（对照 grub_fshelp_find_file 的逐组件循环）。
 * 返回 0 并填 file；-2 未找到，-3 路径非法（穿过文件/以目录结尾）。 */
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
            return -3;              /* 试图穿过常规文件 */
        r = find_in_dir(extent, size, comp, &extent, &size, &child_is_dir);
        if (r < 0)
            return r;               /* -1 读失败 / -2 未找到 */
        if (*p == 0) {              /* 最后一个组件 */
            if (child_is_dir)
                return -3;          /* 以目录结尾，不是常规文件 */
            file->extent = extent;
            file->size = size;
            file->pos = 0;
            return 0;
        }
        isdir = child_is_dir;       /* 还有后续组件：必须继续下钻 */
    }
    return -3;                      /* 空路径或以目录结尾 */
}

/* ---- 第 3 层接口：文件系统的 open/read/close（对照 struct grub_fs）-------- */
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
            /* 整扇区对齐：直接读入目标缓冲 */
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

static void iso9660_close(struct grub_file *file)
{
    file->pos = 0;                  /* B14 无资源释放；pos 归零 */
}

/* ---- 第 4 层接口：面向路径的 file_open / file_read / file_close ------------
 * 对照 grub_file_open：设备固定 (cd0)（B14 简化，无设备名解析）。 */
int file_open(const char *path, struct grub_file *file)
{
    return iso9660_open(&cd_device, path, file);
}

int file_read(struct grub_file *file, void *buf, u32 len)
{
    return iso9660_read(file, buf, len);
}

void file_close(struct grub_file *file)
{
    iso9660_close(file);
}

/* ---- loader_main ---------------------------------------------------------- */
void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;
    struct grub_file f;
    u8 buf[64];
    u32 i;

    vga_clear();
    vga_puts("B14 file: device (cd0) -> disk -> fs -> file\n");

    /* 设备层初始化：(cd0) = BIOS 盘号 0xE0，2048 字节扇区 */
    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    cd_device.name = "cd0";
    cd_device.disk = &cd_disk;
    vga_puts("B14 file: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_mount(&cd_device) < 0) {
        vga_puts("B14 error: ISO9660 mount failed\n");
        for (;;)
            ;
    }
    vga_puts("B14 file: iso9660 mounted, root extent=");
    vga_hex(iso_root_extent, 8);
    vga_putc('\n');

    /* 路径查找：/BOOT/KERNEL.ELF（两段组件：目录 BOOT -> 文件 KERNEL.ELF） */
    if (file_open("/BOOT/KERNEL.ELF", &f) < 0) {
        vga_puts("B14 error: open /BOOT/KERNEL.ELF failed\n");
    } else {
        vga_puts("B14 file: open /BOOT/KERNEL.ELF size=");
        vga_hex(f.size, 8);
        vga_putc('\n');
        /* ELF header 32 字节：e_ident(16) + e_type(2) + e_machine(2) +
         * e_version(4) + e_entry(4, 偏移 24) */
        if (file_read(&f, buf, 32) < 0) {
            vga_puts("B14 error: read failed\n");
        } else if (buf[0] == 0x7F && buf[1] == 'E' &&
                   buf[2] == 'L' && buf[3] == 'F') {
            u32 entry = (u32)buf[24] | ((u32)buf[25] << 8) |
                        ((u32)buf[26] << 16) | ((u32)buf[27] << 24);
            vga_puts("B14 file: KERNEL.ELF magic ok, entry=");
            vga_hex(entry, 8);
            vga_putc('\n');
        } else {
            vga_puts("B14 file: KERNEL.ELF not ELF\n");
        }
        file_close(&f);
    }

    /* 路径查找：/TEST.TXT（根目录单组件），读内容核对字节 */
    if (file_open("/TEST.TXT", &f) == 0) {
        static const char expect[] = "Hello from ISO9660";
        int ok = 1;
        vga_puts("B14 file: open /TEST.TXT size=");
        vga_hex(f.size, 8);
        vga_putc('\n');
        if (file_read(&f, buf, sizeof(expect) - 1) < 0) {
            vga_puts("B14 error: read failed\n");
        } else {
            for (i = 0; i < sizeof(expect) - 1; i++)
                if (buf[i] != (u8)expect[i]) {
                    ok = 0;
                    break;
                }
            vga_puts(ok ? "B14 file: TEST.TXT content match\n"
                        : "B14 file: TEST.TXT MISMATCH\n");
        }
        file_close(&f);
    }

    /* 错误路径：不存在的文件返回错误（对齐 GRUB 的 file not found） */
    if (file_open("/NOPE.TXT", &f) < 0)
        vga_puts("B14 error: file not found (/NOPE.TXT)\n");

    for (;;)
        ;
}

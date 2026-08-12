/* Lesson B13: Mini-GRUB stage2 — ISO9660 基础读取
 *
 * 目标：loader 在光盘上按块读取 ISO9660 内容——从主卷描述符（PVD，
 * 逻辑扇区 16）解析出根目录记录，遍历目录条目，按 extent 读取文件，
 * 并与 xorriso 抽取的内容对照。
 * 参照：grub-core/fs/iso9660.c（grub_iso9660_read、目录记录解析）、
 *       grub-core/boot/i386/pc/cdboot.S（read_cdrom：INT 13 AH=42 + DAP）、
 *       grub-core/disk/i386/pc/biosdisk.c（DAP 结构与 LBA 读盘）。
 *
 * B13 简化边界：单 extent 文件、ISO9660 level-1 短文件名（无 Rock Ridge
 * 长名解析）；多 extent / Rock Ridge 留待 B14 及以后。
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

/* ---- BIOS 回调（B05，保持不变）------------------------------------------- */
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

/* ---- CD 读取：INT 13 AH=42（EDD），2048 字节扇区 --------------------------
 * CD 的 BIOS 盘号 = 引导盘号（El Torito no-emul：DL 直接传给引导镜像，
 * 本环境 SeaBIOS 1.17 为 0xE0）；扇区号是 2048 字节逻辑扇区（ISO9660
 * 卷空间的扇区），与软盘的 512 字节 CHS 读取完全不同。
 * DAP（Disk Address Packet）在 DS:SI，缓冲在 DAP 内的 段:偏移 描述。
 * 对照 cdboot.S read_cdrom 的 DAP 与 biosdisk.c grub_biosdisk_read。 */
#define CD_SECTOR_SIZE  2048u
#define CD_SCRATCH      0x68000u    /* GRUB 的 scratch 区：CD 数据缓冲 */
#define CD_BUF_PVD      0x68000u
#define CD_BUF_DIR      0x68800u
#define CD_BUF_FILE     0x69000u
#define BOOT_DRIVE_ADDR 0x60000u    /* stage1 保存的引导盘号 */

struct dap {
    u8  size;               /* 0x10 */
    u8  reserved;
    u16 count;              /* 块数（2048 字节/块） */
    u16 offset;             /* 缓冲偏移 */
    u16 segment;            /* 缓冲段 */
    u32 lba_lo;             /* 起始 LBA（小端） */
    u32 lba_hi;
} __attribute__((packed));

/* 静态 DAP：位于 loader 内（链接在 0x8400 起的低内存，物理地址 < 64KB），
 * 实模式 DS=0 时 DS:SI 可直接寻址。 */
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
    regs.esi = (u32)&cd_dap;    /* DS:SI = DAP（物理 < 64KB） */
    regs.flags = 0x0200u;
    regs.ebx = 0;
    regs.ecx = 0;
    regs.edi = 0;

    bios_interrupt(0x13, &regs);
    return (regs.flags & 0x01u) ? -1 : 0;
}

/* ---- ISO9660 结构 ---------------------------------------------------------
 * PVD（主卷描述符，逻辑扇区 16）：type=1，"CD001" 在偏移 1，卷标识在偏移
 * 40（32 字节），根目录记录在偏移 156。
 * 目录记录（ISO 9660 规范 9.1）：关键字段同时以小端（LSB）与大端（MSB）
 * 各存一份，规范要求双格式一致，通常取 LSB。 */
#define PVD_LBA        16u
#define PVD_VOLID_OFF  40u
#define PVD_VOLID_LEN  32u
#define PVD_ROOT_OFF   156u
#define DIR_FLAG_DIR   0x02u

struct iso_dir_record {
    u8 len;                  /* 0 记录长度（含名字与偶数填充） */
    u8 ext_attr_len;         /* 1 */
    u8 ext_lba[4];           /* 2 extent 起始逻辑扇区（LSB 在前） */
    u8 ext_lba_msb[4];       /* 6 （MSB 在前，双格式） */
    u8 data_len[4];          /* 10 数据长度（字节，LSB 在前） */
    u8 data_len_msb[4];      /* 14 */
    u8 date[7];              /* 18 记录日期/时间 */
    u8 flags;                /* 25 bit1=目录 */
    u8 unit_size;            /* 26 */
    u8 gap;                  /* 27 */
    u8 vol_seq[2];           /* 28 卷序列号（LSB） */
    u8 vol_seq_msb[2];       /* 30 */
    u8 name_len;             /* 32 文件名长度 */
    u8 name[1];              /* 33.. 文件名（无结尾 NUL；后面可跟 Rock Ridge） */
};

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* ---- ISO9660 打开：读 PVD，校验 magic，记录根目录位置 --------------------- */
static u8  iso_drive;
static u32 iso_root_extent;
static u32 iso_root_size;
static char iso_volume_id[PVD_VOLID_LEN + 1];

int iso9660_open(u8 drive)
{
    u8 *pvd = (u8 *)CD_BUF_PVD;
    u32 i;

    iso_drive = drive;
    if (cd_read_lba(drive, PVD_LBA, pvd, 1) < 0)
        return -1;
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1')
        return -2;                      /* 不是 ISO9660 主卷描述符 */

    for (i = 0; i < PVD_VOLID_LEN; i++)
        iso_volume_id[i] = (char)pvd[PVD_VOLID_OFF + i];
    iso_volume_id[PVD_VOLID_LEN] = 0;

    iso_root_extent = le32(pvd + PVD_ROOT_OFF + 2);
    iso_root_size   = le32(pvd + PVD_ROOT_OFF + 10);
    return 0;
}

/* iso9660_read: 读 extent 起的 sectors 个 CD 扇区到 buf（B14 起作为
 * 文件读取的底层原语）。 */
int iso9660_read(u32 extent, u32 sectors, void *buf)
{
    return cd_read_lba(iso_drive, extent, buf, sectors);
}

/* ---- 目录条目处理 --------------------------------------------------------- */
/* ISO9660 level-1 文件名为 "NAME.EXT;版本"，比较到 ';' 为止 */
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

static void print_name(const u8 *name, u8 nlen)
{
    u8 i;
    /* ISO9660 根目录的 "." 与 ".." 条目名字是单字节 0x00 与 0x01
     * （ECMA-119 约定），GRUB 在 iterate 时映射回 "." 和 ".."
     * （对照 grub-core/fs/iso9660.c 中处理 . 与 .. 的分支）。 */
    if (nlen == 1 && name[0] == 0) {
        vga_puts(".");
        return;
    }
    if (nlen == 1 && name[0] == 1) {
        vga_puts("..");
        return;
    }
    for (i = 0; i < nlen; i++) {
        if (name[i] == ';')
            break;
        vga_putc((char)name[i]);
    }
}

/* walk_root_dir: 读根目录，逐个打印条目；找到 TEST.TXT 后按 extent 读取
 * 并核对内容（对照 xorriso 抽取的原始字节）。 */
static void walk_root_dir(void)
{
    u8 *dir = (u8 *)CD_BUF_DIR;
    u32 dir_sectors = (iso_root_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
    u32 off = 0;
    u32 found_extent = 0;
    u32 found_size = 0;

    if (dir_sectors > 4)
        dir_sectors = 4;                /* B13 简化：小目录，最多 4 扇区 */
    if (iso9660_read(iso_root_extent, dir_sectors, dir) < 0) {
        vga_puts("B13 iso9660: root dir read failed\n");
        return;
    }

    while (off < iso_root_size && off < dir_sectors * CD_SECTOR_SIZE) {
        struct iso_dir_record *r = (struct iso_dir_record *)(dir + off);
        u32 ext, sz;

        if (r->len == 0)
            break;                      /* 记录区结束（后续为 0 填充） */
        if (r->len < 34) {              /* 畸形记录：最小记录 = 33+1 */
            off += 1;
            continue;
        }
        ext = le32(r->ext_lba);
        sz  = le32(r->data_len);

        vga_puts("B13 iso9660: entry \"");
        print_name(r->name, r->name_len);
        vga_puts("\" ext=");
        vga_hex(ext, 8);
        vga_puts(" size=");
        vga_hex(sz, 8);
        vga_putc('\n');

        if (name_matches(r->name, r->name_len, "TEST.TXT")) {
            found_extent = ext;
            found_size = sz;
        }
        off += r->len;
    }

    if (!found_extent) {
        vga_puts("B13 iso9660: TEST.TXT not found\n");
        return;
    }
    {
        static const char expect[] = "Hello from ISO9660";
        u8 *fbuf = (u8 *)CD_BUF_FILE;
        u32 fsectors = (found_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
        u32 i;
        int ok = 1;

        vga_puts("B13 iso9660: reading TEST.TXT ext=");
        vga_hex(found_extent, 8);
        vga_puts(" size=");
        vga_hex(found_size, 8);
        vga_putc('\n');

        if (iso9660_read(found_extent, fsectors, fbuf) < 0) {
            vga_puts("B13 iso9660: file read failed\n");
            return;
        }
        vga_puts("B13 iso9660: content: ");
        for (i = 0; i < 48 && i < found_size; i++)
            vga_hex(fbuf[i], 2);
        vga_putc('\n');

        for (i = 0; i < sizeof(expect) - 1; i++)
            if (i >= found_size || fbuf[i] != (u8)expect[i]) {
                ok = 0;
                break;
            }
        vga_puts(ok ? "B13 iso9660: content match\n"
                    : "B13 iso9660: content MISMATCH\n");
    }
}

/* ---- loader_main ---------------------------------------------------------- */
void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;

    vga_clear();
    vga_puts("B13 iso9660: Mini-GRUB reads a CD\n");

    vga_puts("B13 iso9660: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_open(drive) < 0) {
        vga_puts("B13 iso9660: PVD invalid\n");
        for (;;)
            ;
    }
    vga_puts("B13 iso9660: PVD ok, volume=\"");
    vga_puts(iso_volume_id);
    vga_puts("\"\n");

    vga_puts("B13 iso9660: root dir extent=");
    vga_hex(iso_root_extent, 8);
    vga_puts(" size=");
    vga_hex(iso_root_size, 8);
    vga_putc('\n');

    walk_root_dir();

    for (;;)
        ;
}

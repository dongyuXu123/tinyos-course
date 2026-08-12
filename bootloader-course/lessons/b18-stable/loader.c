/* Lesson B18: Mini-GRUB stage2 — menuentry 菜单与 timeout/default 选择
 *
 * B17 能逐行执行 grub.cfg；本课给 menuentry 块真实语义：
 *   1. 解析 cfg 时把每个 menuentry { title, 块体 } 注册进菜单列表；
 *   2. cfg 执行完后进入菜单：渲染条目、上下键选择、回车执行；
 *   3. timeout=N：倒计时（PIT 100Hz），超时自动执行 default；timeout=0
 *      不显示菜单立即启动 default（对齐 TinyOS 主线 grub.cfg）。
 * 对照：grub-core/commands/menuentry.c（菜单项注册）、
 *       grub-core/normal/menu.c（菜单绘制与选择循环）、
 *       grub-core/normal/main.c（timeout/default 处理）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

/* ---- VGA 文本库（B04 + vga_goto）------------------------------------------ */
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
    if (c == '\b') {
        if (vga_col > 0)
            vga_col--;
        *vga_cell(vga_row, vga_col) = (u16)((u16)VGA_ATTR << 8) | (u8)' ';
        return;
    }
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

void vga_goto(u32 row)
{
    if (row < VGA_ROWS)
        vga_row = row;
    vga_col = 0;
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

/* ---- CD 读盘原语（B13）--------------------------------------------------- */
#define CD_SECTOR_SIZE  2048u
#define CD_BUF_PVD      0x68000u
#define CD_BUF_CFG      0x68000u
#define CD_BUF_DIR      0x6A000u
#define CD_BUF_FILE     0x6A800u
#define CD_BUF_KERNEL   0x6B000u
#define BOOT_DRIVE_ADDR 0x60000u
#define CFG_MAX         8192u
#define KERNEL_MAX      12288u

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

/* ---- 环境变量与 tokenizer（script.c，无硬件依赖）--------------------------- */
int env_set(const char *name, const char *value);
const char *env_get(const char *name);
int script_tokenize(const char *line, char **argv, int max);

/* ---- PS/2 键盘（B16 + 方向键）-------------------------------------------- */
#define KBD_STATUS  0x64u
#define KBD_DATA    0x60u

#define KEY_UP      1
#define KEY_DOWN    2

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

static u8 kbd_shift = 0;

static char scancode_to_char(u8 sc)
{
    static const char map[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '-', '=', '\b', 0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h',
        'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
    };
    static const char shift_map[] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
        '_', '+', '\b', 0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
        'O', 'P', '{', '}', '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H',
        'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0
    };
    if (sc < sizeof(map))
        return kbd_shift ? shift_map[sc] : map[sc];
    return 0;
}

static u8 kbd_read(void)
{
    while (!(inb(KBD_STATUS) & 0x01u))
        ;
    return inb(KBD_DATA);
}

static int kbd_has_data(void)
{
    return (inb(KBD_STATUS) & 0x01u) != 0;
}

/* kbd_getc: 返回可见字符，或 KEY_UP/KEY_DOWN（扩展键方向键） */
static char kbd_getc(void)
{
    for (;;) {
        u8 sc = kbd_read();
        if (sc == 0xE0) {           /* 扩展键：方向键 make 码 */
            u8 k = kbd_read();
            if (k == 0x48)
                return KEY_UP;
            if (k == 0x50)
                return KEY_DOWN;
            continue;
        }
        if (sc & 0x80u) {
            if ((sc & 0x7Fu) == 0x2A || (sc & 0x7Fu) == 0x36)
                kbd_shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) {
            kbd_shift = 1;
            continue;
        }
        {
            char c = scancode_to_char(sc);
            if (c != 0)
                return c;
        }
    }
}

/* ---- PIT 计时（8254，通道 0，100 Hz；对照 TinyOS Lesson 11）--------------- */
#define PIT_DATA0  0x40u
#define PIT_CMD    0x43u

static void pit_init_100hz(void)
{
    outb(PIT_CMD, 0x34);            /* ch0, mode 2, 先低后高字节 */
    outb(PIT_DATA0, 11932 & 0xFF);  /* 1193182/100 ≈ 11932 -> 100 Hz */
    outb(PIT_DATA0, 11932 >> 8);
}

/* 读通道 0 当前计数值（先 latch） */
static u16 pit_count_read(void)
{
    u8 lo, hi;
    outb(PIT_CMD, 0x00);
    lo = inb(PIT_DATA0);
    hi = inb(PIT_DATA0);
    return (u16)(lo | (hi << 8));
}

/* 忙等 n 个 10ms tick（利用 PIT 回绕检测，精度不依赖 CPU 速度） */
static void delay_ticks(u32 n)
{
    u16 prev = pit_count_read();
    u32 waited = 0;
    while (waited < n) {
        u16 cur = pit_count_read();
        if (cur > prev)             /* 计数回绕：一个 10ms tick */
            waited++;
        prev = cur;
    }
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
        vga_puts("B18 error: command not found: ");
        vga_puts(argv[0]);
        vga_putc('\n');
        return -1;
    }
    return c->fn(argc, argv);
}

/* ---- 装载链（B15 复用）----------------------------------------------------- */
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

#define MB2_TAG_END              0u
#define MB2_TAG_BOOT_LOADER_NAME 2u

void mb2_boot(u32 entry, u32 mbi_addr);

struct mb2_tag {
    u32 type;
    u32 size;
};

static u8 mbi_buf[256] __attribute__((aligned(8)));
static const char boot_loader_name[] = "Mini-GRUB 0.1";

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
    *((u32 *)mbi_buf) = (u32)p - (u32)mbi_buf;
    return (u32)mbi_buf;
}

/* ---- 命令实现（B17）------------------------------------------------------- */
static int name_eq(const char *a, const char *b)
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

static int cmd_ls_fn(int argc, char **argv)
{
    u8 *dir = (u8 *)CD_BUF_DIR;
    u32 sectors = (iso_root_size + CD_SECTOR_SIZE - 1) / CD_SECTOR_SIZE;
    u32 off = 0;
    (void)argc;
    (void)argv;

    if (sectors > 8)
        sectors = 8;
    if (grub_disk_read(&cd_disk, iso_root_extent, sectors, dir) < 0)
        return -1;
    while (off < iso_root_size && off < sectors * CD_SECTOR_SIZE) {
        struct iso_dir_record *r = (struct iso_dir_record *)(dir + off);
        if (r->len == 0)
            break;
        if (r->len < 34) {
            off += 1;
            continue;
        }
        {
            u8 i;
            for (i = 0; i < r->name_len; i++) {
                if (r->name[i] == ';')
                    break;
                vga_putc((char)r->name[i]);
            }
        }
        if (r->flags & DIR_FLAG_DIR)
            vga_putc('/');
        vga_puts("  ");
        vga_hex(le32(r->data_len), 8);
        vga_putc('\n');
        off += r->len;
    }
    return 0;
}

static u32 loaded_entry = 0;
static int loaded = 0;

static int cmd_multiboot2_fn(int argc, char **argv)
{
    struct grub_file f;
    u8 *kbuf = (u8 *)CD_BUF_KERNEL;
    const struct elf32_ehdr *eh;

    if (argc < 2)
        return -1;
    if (file_open(argv[1], &f) < 0) {
        vga_puts("B18 error: no such file: ");
        vga_puts(argv[1]);
        vga_putc('\n');
        return -1;
    }
    if (f.size > KERNEL_MAX)
        return -1;
    if (file_read(&f, kbuf, f.size) < 0)
        return -1;
    if (elf_load(kbuf, f.size) < 0) {
        vga_puts("B18 error: bad ELF\n");
        return -1;
    }
    eh = (const struct elf32_ehdr *)kbuf;
    loaded_entry = eh->e_entry;
    loaded = 1;
    vga_puts("B18 multiboot2: loaded ");
    vga_puts(argv[1]);
    vga_puts(" entry=");
    vga_hex(loaded_entry, 8);
    vga_putc('\n');
    return 0;
}

static int cmd_boot_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!loaded) {
        vga_puts("B18 error: no kernel loaded\n");
        return -1;
    }
    vga_puts("B18 boot: jumping to entry=");
    vga_hex(loaded_entry, 8);
    vga_putc('\n');
    mb2_boot(loaded_entry, mbi_build());
    return 0;
}

static struct cmd cmd_echo       = { "echo",       cmd_echo_fn,       "print its arguments", 0 };
static struct cmd cmd_set        = { "set",        cmd_set_fn,        "get/set environment variables", 0 };
static struct cmd cmd_ls         = { "ls",         cmd_ls_fn,         "list files on (cd0)", 0 };
static struct cmd cmd_multiboot2 = { "multiboot2", cmd_multiboot2_fn, "load a Multiboot2 kernel", 0 };
static struct cmd cmd_boot       = { "boot",       cmd_boot_fn,       "boot the loaded kernel", 0 };

static void cmd_register_all(void)
{
    cmd_register(&cmd_echo);
    cmd_register(&cmd_set);
    cmd_register(&cmd_ls);
    cmd_register(&cmd_multiboot2);
    cmd_register(&cmd_boot);
}

/* ---- 菜单数据（对照 menuentry.c：menuentry 注册 {title, 块体}）------------- */
#define MENU_MAX   8
#define TITLE_MAX  64
#define BODY_MAX   512

struct menu_entry {
    char title[TITLE_MAX];
    char body[BODY_MAX];
    u32 body_len;
};

static struct menu_entry menu_entries[MENU_MAX];
static u32 menu_count = 0;

int menu_add(const char *title)
{
    u32 i;
    struct menu_entry *e;
    if (menu_count >= MENU_MAX)
        return -1;
    e = &menu_entries[menu_count++];
    for (i = 0; title[i] && i < TITLE_MAX - 1; i++)
        e->title[i] = title[i];
    e->title[i] = 0;
    e->body_len = 0;
    return 0;
}

/* 收集块体：把一行追加到最后一个菜单项（菜单收集状态由执行器管理） */
static void menu_collect_line(const char *l)
{
    struct menu_entry *e = &menu_entries[menu_count - 1];
    u32 i = 0;
    while (l[i] && e->body_len + i < BODY_MAX - 1) {
        e->body[e->body_len + i] = l[i];
        i++;
    }
    e->body_len += i;
    if (e->body_len < BODY_MAX - 1)
        e->body[e->body_len++] = '\n';
    e->body[e->body_len < BODY_MAX ? e->body_len : BODY_MAX - 1] = 0;
}

/* ---- 脚本执行器（B17 + 菜单块收集）--------------------------------------- */
#define LINE_MAX 256
#define ARGV_MAX 16

static char *argv_list[ARGV_MAX];
static int collecting_menu = 0;

static void script_execute_line(const char *l)
{
    int argc;
    const char *p = l;

    if (collecting_menu) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '}') {
            collecting_menu = 0;        /* 块结束 */
            return;
        }
        menu_collect_line(l);           /* 块体行存入菜单项 */
        return;
    }

    argc = script_tokenize(l, argv_list, ARGV_MAX);
    if (argc == 0)
        return;

    if (name_eq(argv_list[0], "menuentry")) {
        if (argc > 1)
            menu_add(argv_list[1]);
        collecting_menu = 1;
        return;
    }
    cmd_execute(argc, argv_list);
}

static int script_run_file(const char *path)
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

/* ---- 菜单运行（对照 normal/menu.c + normal/main.c）------------------------- */
static u32 atoi_u32(const char *s)
{
    u32 v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (u32)(*s - '0');
        s++;
    }
    return v;
}

/* 渲染菜单：标题 + 高亮 + 提示 */
static void menu_render(u32 sel, u32 timeout)
{
    u32 i;
    vga_clear();
    vga_puts("B18 menu: Mini-GRUB boot menu\n");
    vga_puts("B18 menu: use up/down + enter, or wait for timeout\n");
    vga_puts("B18 menu: --------------------------------\n");
    for (i = 0; i < menu_count; i++) {
        vga_puts(i == sel ? "B18 menu: > " : "B18 menu:   ");
        vga_puts(menu_entries[i].title);
        vga_putc('\n');
    }
    vga_goto(4 + menu_count);
    vga_puts("B18 menu: boot in ");
    vga_hex(timeout, 2);
    vga_puts("s ...\n");
}

/* 执行第 idx 个菜单项的块体（GRUB 行为：清屏后执行，块体输出覆盖菜单） */
static void menu_execute(u32 idx)
{
    const char *p;
    if (idx >= menu_count)
        return;
    vga_clear();
    vga_puts("B18 menu: executing entry ");
    vga_puts(menu_entries[idx].title);
    vga_putc('\n');
    p = menu_entries[idx].body;
    while (*p) {
        const char *nl = p;
        char lcopy[LINE_MAX];
        u32 i = 0;
        while (*nl && *nl != '\n')
            nl++;
        for (i = 0; p + i < nl && i < LINE_MAX - 1; i++)
            lcopy[i] = p[i];
        lcopy[i] = 0;
        script_execute_line(lcopy);
        p = nl;
        if (*p == '\n')
            p++;
    }
}

void menu_run(void)
{
    const char *v;
    u32 timeout = 0;
    u32 default_idx = 0;
    u32 sel;
    u32 remaining;

    v = env_get("timeout");
    if (v)
        timeout = atoi_u32(v);
    v = env_get("default");
    if (v)
        default_idx = atoi_u32(v);
    if (default_idx >= menu_count)
        default_idx = 0;

    if (timeout == 0) {
        /* GRUB 语义：timeout=0 不显示菜单，立即启动 default */
        vga_puts("B18 menu: timeout=0 -> boot default\n");
        menu_execute(default_idx);
        return;
    }

    pit_init_100hz();
    sel = default_idx;
    remaining = timeout;
    menu_render(sel, remaining);

    for (;;) {
        if (kbd_has_data()) {
            char c = kbd_getc();
            if (c == KEY_UP) {
                sel = (sel + menu_count - 1) % menu_count;
                menu_render(sel, remaining);
            } else if (c == KEY_DOWN) {
                sel = (sel + 1) % menu_count;
                menu_render(sel, remaining);
            } else if (c == '\n') {
                menu_execute(sel);
                return;
            } else {
                /* 任意其他键暂停倒计时（GRUB 行为） */
            }
        } else {
            delay_ticks(100);           /* 100 wraps = 1 秒（10ms/wrap） */
            if (remaining > 0)
                remaining--;
            menu_render(sel, remaining);
            if (remaining == 0) {
                menu_execute(default_idx);
                return;
            }
        }
    }
}

/* ---- loader_main ---------------------------------------------------------- */
void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;

    vga_clear();
    vga_puts("B18 menu: Mini-GRUB boots from grub.cfg\n");

    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    vga_puts("B18 menu: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_mount() < 0) {
        vga_puts("B18 error: ISO9660 mount failed\n");
        for (;;)
            ;
    }

    cmd_register_all();

    if (script_run_file("/boot/grub/grub.cfg") < 0) {
        vga_puts("B18 error: grub.cfg not found\n");
        for (;;)
            ;
    }

    if (menu_count > 0)
        menu_run();
    else {
        vga_puts("B18 error: no menu entries\n");
        for (;;)
            ;
    }
    for (;;)
        ;
}

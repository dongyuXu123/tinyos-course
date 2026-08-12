/* Lesson B17: Mini-GRUB stage2 — grub.cfg 解析执行
 *
 * loader 启动时自动读取光盘上的 /boot/grub/grub.cfg（B14 文件抽象），逐行
 * tokenize（引号/$var 展开，见 script.c）并分发到命令表（B16），其中
 * multiboot2 /boot/kernel.elf + boot 命令把 B15 的装载链变成可配置的命令流。
 * 对照：grub-core/normal/main.c（grub_normal_execute：读 cfg 并执行）、
 *       grub-core/script/（tokenizer/parser）、commands/multiboot2.c、
 *       commands/boot.c。
 * B17 简化边界：无 if/for/函数定义；menuentry 块只识别与跳过（B18 实现菜单）。
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
#define CD_BUF_CFG      0x68000u    /* grub.cfg（挂载后 PVD 不再需要） */
#define CD_BUF_DIR      0x6A000u
#define CD_BUF_FILE     0x6A800u
#define CD_BUF_KERNEL   0x6B000u    /* 内核文件（≤12KB） */
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
void env_foreach(void (*fn)(const char *name, const char *value, void *data),
                 void *data);
int script_tokenize(const char *line, char **argv, int max);

/* ---- PS/2 键盘（B16）----------------------------------------------------- */
#define KBD_STATUS  0x64u
#define KBD_DATA    0x60u

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
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

static char kbd_getc(void)
{
    for (;;) {
        u8 sc = kbd_read();
        if (sc == 0xE0) {
            kbd_read();
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

static int kbd_getline(char *buf, int max)
{
    int n = 0;
    for (;;) {
        char c = kbd_getc();
        if (c == '\n') {
            buf[n] = 0;
            vga_putc('\n');
            return n;
        }
        if (c == '\b') {
            if (n > 0) {
                n--;
                vga_putc('\b');
            }
            continue;
        }
        if (n < max - 1) {
            buf[n++] = c;
            vga_putc(c);
        }
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
        vga_puts("B17 error: command not found: ");
        vga_puts(argv[0]);
        vga_putc('\n');
        return -1;
    }
    return c->fn(argc, argv);
}

/* ---- 装载链（B15 复用）：ELF + MBI + 交接 ---------------------------------- */
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

/* ---- 命令实现 -------------------------------------------------------------- */
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

static int cmd_help_fn(int argc, char **argv)
{
    struct cmd *c;
    (void)argc;
    (void)argv;
    vga_puts("B17 help: available commands\n");
    for (c = cmd_list; c; c = c->next) {
        vga_puts("  ");
        vga_puts(c->name);
        vga_puts(" - ");
        vga_puts(c->help);
        vga_putc('\n');
    }
    return 0;
}

static void env_print_cb(const char *name, const char *value, void *data)
{
    (void)data;
    vga_puts(name);
    vga_puts("=");
    vga_puts(value);
    vga_putc('\n');
}

static int cmd_set_fn(int argc, char **argv)
{
    if (argc == 1) {
        env_foreach(env_print_cb, 0);
        return 0;
    }
    {
        const char *arg = argv[1];
        const char *eq = arg;
        while (*eq && *eq != '=')
            eq++;
        if (*eq == '=') {
            char name[16];
            u32 nlen = (u32)(eq - arg);
            if (nlen >= 16)
                nlen = 15;
            {
                u32 i;
                for (i = 0; i < nlen; i++)
                    name[i] = arg[i];
                name[nlen] = 0;
            }
            env_set(name, eq + 1);
        } else {
            const char *v = env_get(arg);
            if (v) {
                vga_puts(arg);
                vga_puts("=");
                vga_puts(v);
                vga_putc('\n');
            }
        }
    }
    return 0;
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
            if (r->name_len == 1 && r->name[0] == 0) {
                vga_puts(".");
            } else if (r->name_len == 1 && r->name[0] == 1) {
                vga_puts("..");
            } else {
                for (i = 0; i < r->name_len; i++) {
                    if (r->name[i] == ';')
                        break;
                    vga_putc((char)r->name[i]);
                }
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

/* 已装载内核状态（multiboot2 命令设置，boot 命令使用） */
static u32 loaded_entry = 0;
static int loaded = 0;

static int cmd_multiboot2_fn(int argc, char **argv)
{
    struct grub_file f;
    u8 *kbuf = (u8 *)CD_BUF_KERNEL;
    const struct elf32_ehdr *eh;

    if (argc < 2) {
        vga_puts("B17 error: multiboot2 requires a file\n");
        return -1;
    }
    if (file_open(argv[1], &f) < 0) {
        vga_puts("B17 error: no such file: ");
        vga_puts(argv[1]);
        vga_putc('\n');
        return -1;
    }
    if (f.size > KERNEL_MAX) {
        vga_puts("B17 error: kernel too big\n");
        return -1;
    }
    if (file_read(&f, kbuf, f.size) < 0) {
        vga_puts("B17 error: read failed\n");
        return -1;
    }
    if (elf_load(kbuf, f.size) < 0) {
        vga_puts("B17 error: bad ELF\n");
        return -1;
    }
    eh = (const struct elf32_ehdr *)kbuf;
    loaded_entry = eh->e_entry;
    loaded = 1;
    vga_puts("B17 multiboot2: loaded ");
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
        vga_puts("B17 error: no kernel loaded\n");
        return -1;
    }
    vga_puts("B17 boot: jumping to entry=");
    vga_hex(loaded_entry, 8);
    vga_putc('\n');
    mb2_boot(loaded_entry, mbi_build());   /* 不返回 */
    return 0;
}

static struct cmd cmd_help      = { "help",       cmd_help_fn,      "list available commands", 0 };
static struct cmd cmd_set       = { "set",        cmd_set_fn,       "get/set environment variables", 0 };
static struct cmd cmd_echo      = { "echo",       cmd_echo_fn,      "print its arguments", 0 };
static struct cmd cmd_ls        = { "ls",         cmd_ls_fn,        "list files on (cd0)", 0 };
static struct cmd cmd_multiboot2 = { "multiboot2", cmd_multiboot2_fn, "load a Multiboot2 kernel", 0 };
static struct cmd cmd_boot      = { "boot",       cmd_boot_fn,      "boot the loaded kernel", 0 };

static void cmd_register_all(void)
{
    cmd_register(&cmd_help);
    cmd_register(&cmd_set);
    cmd_register(&cmd_echo);
    cmd_register(&cmd_ls);
    cmd_register(&cmd_multiboot2);
    cmd_register(&cmd_boot);
}

/* ---- 脚本执行器（对照 normal/main.c 的 grub_normal_execute）--------------- */
#define LINE_MAX 256
#define ARGV_MAX 16

static char line[LINE_MAX];
static char *argv_list[ARGV_MAX];
static int in_menuentry_skip = 0;

/* script_execute_line: 展开并切词一行，分发执行；menuentry 块识别与跳过 */
static void script_execute_line(const char *l)
{
    int argc;

    if (in_menuentry_skip) {
        /* 块体：跳过直到 '}'（简化：单独一行的 '}' 结束块） */
        const char *p = l;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '}')
            in_menuentry_skip = 0;
        return;
    }

    argc = script_tokenize(l, argv_list, ARGV_MAX);
    if (argc == 0)
        return;

    if (name_eq(argv_list[0], "menuentry")) {
        vga_puts("B17 script: menuentry '");
        if (argc > 1) {
            vga_puts(argv_list[1]);
        }
        vga_puts("' body skipped (B18 will implement the menu)\n");
        in_menuentry_skip = 1;
        return;
    }
    cmd_execute(argc, argv_list);
}

/* script_run_file: 读入 cfg 文件并逐行执行（8KiB 上限） */
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
        *nl = 0;                        /* 行终止（cfg 缓冲可写） */
        script_execute_line(p);
        p = nl + 1;
    }
    return 0;
}

/* ---- loader_main：自动执行 grub.cfg，失败则回退交互提示符 ------------------ */
static void interactive_prompt(void)
{
    for (;;) {
        int argc;
        vga_puts("grub> ");
        kbd_getline(line, LINE_MAX);
        argc = script_tokenize(line, argv_list, ARGV_MAX);
        if (argc == 0)
            continue;
        cmd_execute(argc, argv_list);
    }
}

void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;

    vga_clear();
    vga_puts("B17 cfg: Mini-GRUB executes grub.cfg\n");

    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    vga_puts("B17 cfg: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_mount() < 0) {
        vga_puts("B17 error: ISO9660 mount failed\n");
        for (;;)
            ;
    }

    cmd_register_all();

    if (script_run_file("/boot/grub/grub.cfg") < 0) {
        vga_puts("B17 error: grub.cfg not found; entering prompt\n");
        interactive_prompt();
    }
    for (;;)
        ;
}

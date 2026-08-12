/* Lesson B19: Mini-GRUB stage2 — 模块系统
 *
 * 核心镜像只带最小命令集，其余功能以 .mod 模块（ELF32 可重定位目标文件，
 * ET_REL）从磁盘加载注册——复刻 GRUB "核心 + 模块" 架构。
 * 对照：grub-core/kern/dl.c（grub_dl_load：节搬移、重定位、符号解析）、
 *       include/grub/dl.h（struct grub_dl 与导出符号）。
 * B19 简化边界：无压缩/依赖解析；重定位只支持 R_386_32 与 R_386_PC32；
 * 模块加载区用固定地址 0x200000（2MB，避开核心 0x8400 与内核 1MB）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

/* ---- VGA 文本库（B04；vga_putc/puts/hex 导出供模块使用）------------------- */
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
#define CD_BUF_CFG      0x68000u
#define CD_BUF_DIR      0x6A000u
#define CD_BUF_FILE     0x6A800u
#define CD_BUF_KERNEL   0x6B000u
#define CD_BUF_MOD      0x6C000u    /* 模块文件缓冲（≤4KB） */
#define BOOT_DRIVE_ADDR 0x60000u
#define CFG_MAX         8192u
#define KERNEL_MAX      12288u
#define MOD_FILE_MAX    4096u
#define MODULE_BASE     0x200000u   /* 模块加载区（避开核心/内核） */
#define MODULE_MAX      65536u

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

/* ---- 磁盘 / 设备 / 文件系统（B14/B15；file_open/read 导出供模块）----------- */
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

int file_open(const char *path, struct grub_file *file)
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

int file_read(struct grub_file *file, void *buf, u32 len)
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
        if (c < 0x20)
            continue;
        if (n < max - 1) {
            buf[n++] = c;
            vga_putc(c);
        }
    }
}

/* ---- 命令注册表（B16；cmd_register 导出供模块）----------------------------- */
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
        vga_puts("B19 error: command not found: ");
        vga_puts(argv[0]);
        vga_putc('\n');
        return -1;
    }
    return c->fn(argc, argv);
}

/* ---- 核心导出符号表（供模块重定位解析）------------------------------------- */
struct core_symbol {
    const char *name;
    void *addr;
};

/* noinline：核心导出表是本课教学点，防止 -Os 内联掉符号 */
static __attribute__((noinline)) void *core_sym_lookup(const char *name)
{
    static const struct core_symbol core_syms[] = {
        { "cmd_register", (void *)(u32)cmd_register },
        { "cmd_execute",  (void *)(u32)cmd_execute },
        { "vga_puts",     (void *)(u32)vga_puts },
        { "vga_putc",     (void *)(u32)vga_putc },
        { "vga_hex",      (void *)(u32)vga_hex },
        { "file_open",    (void *)(u32)file_open },
        { "file_read",    (void *)(u32)file_read },
        { "env_set",      (void *)(u32)env_set },
        { "env_get",      (void *)(u32)env_get },
    };
    u32 i;
    for (i = 0; i < sizeof(core_syms) / sizeof(core_syms[0]); i++)
        if (name_eq(core_syms[i].name, name))
            return core_syms[i].addr;
    return 0;
}

/* ---- 模块加载（对照 kern/dl.c：节搬移 + 重定位 + 符号解析）----------------- */
#define ET_REL      1
#define SHT_NOBITS  8
#define SHT_SYMTAB  2
#define SHT_REL     9
#define SHF_ALLOC   0x02u
#define SHN_UNDEF   0
#define SHN_ABS     0xFFF1u
#define R_386_32    1
#define R_386_PC32  2
#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)

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

struct elf32_shdr {
    u32 sh_name, sh_type, sh_flags, sh_addr, sh_offset;
    u32 sh_size, sh_link, sh_info, sh_addralign, sh_entsize;
};

struct elf32_sym {
    u32 st_name, st_value, st_size;
    u8  st_info, st_other;
    u16 st_shndx;
};

struct elf32_rel {
    u32 r_offset, r_info;
};

/* 已加载模块列表（lsmod 用） */
struct loaded_mod {
    char name[32];
    u32 base;
    struct loaded_mod *next;
};

static struct loaded_mod loaded_mods[8];
static u32 loaded_count = 0;
static struct loaded_mod *loaded_head = 0;

/* mod_load: 读取 ET_REL 模块，把 SHF_ALLOC 节铺到 MODULE_BASE 起的内存，
 * 应用重定位（模块内符号 + 核心导出符号），最后调用 grub_mod_init。
 * 返回模块基址；负值失败。
 * noinline：-Os 会把本函数内联进 cmd_insmod_fn，符号消失（make check 依赖）。 */
static __attribute__((noinline)) int mod_load(const char *path)
{
    struct grub_file f;
    u8 *buf = (u8 *)CD_BUF_MOD;
    struct elf32_shdr *shdr;
    u32 shnum, i;
    u32 sec_addr[16];
    u32 base = MODULE_BASE;
    const char *strtab = 0;
    struct elf32_sym *symtab = 0;
    u32 sym_count = 0;

    if (file_open(path, &f) < 0)
        return -1;
    if (f.size > MOD_FILE_MAX)
        return -2;
    if (file_read(&f, buf, f.size) < 0)
        return -3;

    {
        const struct elf32_ehdr *h = (const struct elf32_ehdr *)buf;
        if (h->e_ident[0] != 0x7F || h->e_ident[1] != 'E' ||
            h->e_ident[2] != 'L' || h->e_ident[3] != 'F')
            return -4;
        if (h->e_type != ET_REL)
            return -5;
        shnum = h->e_shnum;
        if (shnum > 16)
            return -6;
        shdr = (struct elf32_shdr *)(buf + h->e_shoff);
    }

    /* 1. SHF_ALLOC 节顺序铺放 */
    for (i = 0; i < shnum; i++) {
        if (shdr[i].sh_flags & SHF_ALLOC) {
            sec_addr[i] = base;
            if (shdr[i].sh_type != SHT_NOBITS) {
                u32 j;
                for (j = 0; j < shdr[i].sh_size; j++)
                    ((u8 *)base)[j] = buf[shdr[i].sh_offset + j];
            } else {
                u32 j;
                for (j = 0; j < shdr[i].sh_size; j++)
                    ((u8 *)base)[j] = 0;
            }
            base += (shdr[i].sh_size + 15u) & ~15u;
        } else {
            sec_addr[i] = 0;
        }
    }

    /* 2. 找符号表与字符串表 */
    for (i = 0; i < shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = (struct elf32_sym *)(buf + shdr[i].sh_offset);
            sym_count = shdr[i].sh_size / sizeof(struct elf32_sym);
            strtab = (const char *)(buf + shdr[shdr[i].sh_link].sh_offset);
            break;
        }
    }
    if (!symtab)
        return -7;

    /* 3. 应用重定位 */
    for (i = 0; i < shnum; i++) {
        if (shdr[i].sh_type == SHT_REL) {
            u32 target = shdr[i].sh_info;
            const struct elf32_rel *rel =
                (const struct elf32_rel *)(buf + shdr[i].sh_offset);
            u32 n = shdr[i].sh_size / sizeof(struct elf32_rel);
            u32 j;
            if (target >= 16 || !(sec_addr[target]))
                continue;
            for (j = 0; j < n; j++) {
                u32 sym_idx = ELF32_R_SYM(rel[j].r_info);
                u32 type = ELF32_R_TYPE(rel[j].r_info);
                struct elf32_sym *sym = &symtab[sym_idx];
                u32 sym_addr = 0;
                u32 *loc = (u32 *)(sec_addr[target] + rel[j].r_offset);

                if (sym_idx >= sym_count)
                    return -8;
                if (sym->st_shndx == SHN_UNDEF) {
                    const char *nm = strtab + sym->st_name;
                    sym_addr = (u32)core_sym_lookup(nm);
                    if (!sym_addr) {
                        vga_puts("B19 mod: undefined symbol: ");
                        vga_puts(nm);
                        vga_putc('\n');
                        return -9;
                    }
                } else if (sym->st_shndx == SHN_ABS) {
                    sym_addr = sym->st_value;
                } else if (sym->st_shndx < 16) {
                    sym_addr = sec_addr[sym->st_shndx] + sym->st_value;
                } else {
                    return -10;
                }
                if (type == R_386_32) {
                    *loc += sym_addr;
                } else if (type == R_386_PC32) {
                    *loc += sym_addr - (u32)loc;
                } else {
                    vga_puts("B19 mod: unsupported relocation type ");
                    vga_hex(type, 2);
                    vga_putc('\n');
                    return -11;
                }
            }
        }
    }

    /* 4. 记录模块并调用 grub_mod_init */
    if (loaded_count < 8) {
        struct loaded_mod *m = &loaded_mods[loaded_count++];
        u32 i2 = 0;
        while (path[i2] && i2 < 31) {
            m->name[i2] = path[i2];
            i2++;
        }
        m->name[i2] = 0;
        m->base = MODULE_BASE;
        m->next = loaded_head;
        loaded_head = m;
    }
    {
        int (*mod_init)(void) = 0;
        for (i = 0; i < sym_count; i++) {
            if (symtab[i].st_shndx != SHN_UNDEF &&
                strtab && name_eq(strtab + symtab[i].st_name, "grub_mod_init")) {
                mod_init = (int (*)(void))(u32)(sec_addr[symtab[i].st_shndx] +
                                                symtab[i].st_value);
                break;
            }
        }
        if (!mod_init)
            return -12;
        mod_init();
    }
    return (int)MODULE_BASE;
}

/* ---- 命令实现（B17 + insmod/lsmod）--------------------------------------- */
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

static int cmd_insmod_fn(int argc, char **argv)
{
    int r;
    if (argc < 2) {
        vga_puts("B19 error: insmod requires a module file\n");
        return -1;
    }
    r = mod_load(argv[1]);
    if (r < 0) {
        vga_puts("B19 error: insmod failed: ");
        vga_puts(argv[1]);
        vga_putc('\n');
        return -1;
    }
    vga_puts("B19 mod: insmod ");
    vga_puts(argv[1]);
    vga_puts(" -> base=");
    vga_hex((u32)r, 8);
    vga_putc('\n');
    return 0;
}

static int cmd_lsmod_fn(int argc, char **argv)
{
    struct loaded_mod *m;
    (void)argc;
    (void)argv;
    for (m = loaded_head; m; m = m->next) {
        vga_puts("B19 lsmod: ");
        vga_puts(m->name);
        vga_puts(" base=");
        vga_hex(m->base, 8);
        vga_putc('\n');
    }
    return 0;
}

static int cmd_halt_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_puts("B19: halted\n");
    for (;;)
        __asm__ volatile ("hlt");
    return 0;
}

static struct cmd cmd_echo   = { "echo",   cmd_echo_fn,   "print its arguments", 0 };
static struct cmd cmd_set    = { "set",    cmd_set_fn,    "get/set environment variables", 0 };
static struct cmd cmd_insmod = { "insmod", cmd_insmod_fn, "load a .mod module", 0 };
static struct cmd cmd_lsmod  = { "lsmod",  cmd_lsmod_fn,  "list loaded modules", 0 };
static struct cmd cmd_halt   = { "halt",   cmd_halt_fn,   "halt the CPU", 0 };

static void cmd_register_all(void)
{
    cmd_register(&cmd_echo);
    cmd_register(&cmd_set);
    cmd_register(&cmd_insmod);
    cmd_register(&cmd_lsmod);
    cmd_register(&cmd_halt);
}

/* ---- 脚本执行器（B17）----------------------------------------------------- */
#define LINE_MAX 256
#define ARGV_MAX 16

static char line[LINE_MAX];
static char *argv_list[ARGV_MAX];

static void script_execute_line(const char *l)
{
    int argc;
    argc = script_tokenize(l, argv_list, ARGV_MAX);
    if (argc == 0)
        return;
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

/* ---- loader_main ---------------------------------------------------------- */
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
    vga_puts("B19 mod: Mini-GRUB module system\n");

    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    vga_puts("B19 mod: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_mount() < 0) {
        vga_puts("B19 error: ISO9660 mount failed\n");
        for (;;)
            ;
    }

    cmd_register_all();

    if (script_run_file("/boot/grub/grub.cfg") < 0) {
        vga_puts("B19 error: grub.cfg not found; entering prompt\n");
        interactive_prompt();
    }
    for (;;)
        ;
}

/* Lesson B16: Mini-GRUB stage2 — 命令注册表与极简 grub> 命令行
 *
 * 本课把 B14 的文件抽象接到一个命令驱动的交互界面：loader 启动后进入
 * "grub> " 提示符，PS/2 键盘输入命令，命令表（名字 → 函数指针）分发执行。
 * 对照：include/grub/command.h（grub_command_register/find/execute）、
 *       grub-core/kern/env.c（环境变量）、commands/{help,set,ls}.c。
 * B16 简化边界：命令签名 (int argc, char **argv)；环境变量固定槽位链表；
 * 无补全/历史；boot 命令为占位（B17/B18 接 grub.cfg 后实现真装载）。
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
    if (c == '\b') {               /* 退格：光标左移一格并清字符 */
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

/* ---- 工具（无 libc）------------------------------------------------------- */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
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
#define CD_BUF_FILE     0x6C000u
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

/* 打印目录条目名：根目录 "."/".." 条目名字是 0x00/0x01（ECMA-119），
 * 映射回 "." 和 ".."（对照 GRUB iso9660.c）。 */
static void print_name(const u8 *name, u8 nlen)
{
    u8 i;
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

/* ---- PS/2 键盘（对照 TinyOS Lesson 03 轮询思路）--------------------------- */
#define KBD_STATUS  0x64u
#define KBD_DATA    0x60u

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static u8 kbd_shift = 0;

/* set-1 扫描码 -> 字符（US 布局；shift 处理数字行符号与大写） */
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

/* 读一个键盘字节（等待 PS/2 输出缓冲非空） */
static u8 kbd_read(void)
{
    while (!(inb(KBD_STATUS) & 0x01u))
        ;
    return inb(KBD_DATA);
}

/* kbd_getc: 等待并返回一个可见字符（跳过按下/释放状态、扩展键） */
static char kbd_getc(void)
{
    for (;;) {
        u8 sc = kbd_read();
        if (sc == 0xE0) {           /* 扩展键前缀：吞掉后续字节 */
            kbd_read();
            continue;
        }
        if (sc & 0x80u) {           /* 释放码 */
            if ((sc & 0x7Fu) == 0x2A || (sc & 0x7Fu) == 0x36)
                kbd_shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) {   /* Shift 按下 */
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

/* kbd_getline: 读一行到 buf（回显 + 退格），返回字符数 */
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

/* ---- 环境变量（对照 kern/env.c，固定槽位简化）----------------------------- */
#define ENV_MAX   16
#define ENV_NAME  16
#define ENV_VALUE 48

struct env {
    char name[ENV_NAME];
    char value[ENV_VALUE];
    struct env *next;
};

static struct env env_slots[ENV_MAX];
static u32 env_count = 0;
static struct env *env_head = 0;

static struct env *env_find(const char *name)
{
    struct env *e;
    for (e = env_head; e; e = e->next)
        if (str_eq(e->name, name))
            return e;
    return 0;
}

static const char *env_get(const char *name)
{
    struct env *e = env_find(name);
    return e ? e->value : 0;
}

static int __attribute__((noinline)) env_set(const char *name, const char *value)
{
    struct env *e = env_find(name);
    if (e) {
        str_copy(e->value, value, ENV_VALUE);
        return 0;
    }
    if (env_count >= ENV_MAX)
        return -1;
    e = &env_slots[env_count++];
    str_copy(e->name, name, ENV_NAME);
    str_copy(e->value, value, ENV_VALUE);
    e->next = env_head;
    env_head = e;
    return 0;
}

/* ---- 命令注册表（对照 include/grub/command.h）----------------------------- */
struct cmd {
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *help;
    struct cmd *next;
};

static struct cmd *cmd_list;

int cmd_register(struct cmd *c)
{
    c->next = cmd_list;             /* 头插 */
    cmd_list = c;
    return 0;
}

static struct cmd *cmd_find(const char *name)
{
    struct cmd *c;
    for (c = cmd_list; c; c = c->next)
        if (str_eq(c->name, name))
            return c;
    return 0;
}

int cmd_execute(int argc, char **argv)
{
    struct cmd *c = cmd_find(argv[0]);
    if (!c) {
        vga_puts("B16 error: command not found: ");
        vga_puts(argv[0]);
        vga_putc('\n');
        return -1;
    }
    return c->fn(argc, argv);
}

/* ---- 命令实现（对照 commands/{help,set,ls}.c）----------------------------- */
static int cmd_help_fn(int argc, char **argv)
{
    struct cmd *c;
    (void)argc;
    (void)argv;
    vga_puts("B16 help: available commands\n");
    for (c = cmd_list; c; c = c->next) {
        vga_puts("  ");
        vga_puts(c->name);
        vga_puts(" - ");
        vga_puts(c->help);
        vga_putc('\n');
    }
    return 0;
}

static int cmd_set_fn(int argc, char **argv)
{
    struct env *e;
    if (argc == 1) {                /* set：列出全部 */
        for (e = env_head; e; e = e->next) {
            vga_puts(e->name);
            vga_puts("=");
            vga_puts(e->value);
            vga_putc('\n');
        }
        return 0;
    }
    {
        const char *arg = argv[1];
        const char *eq = arg;
        while (*eq && *eq != '=')
            eq++;
        if (*eq == '=') {           /* set name=value */
            char name[ENV_NAME];
            u32 nlen = (u32)(eq - arg);
            if (nlen >= ENV_NAME)
                nlen = ENV_NAME - 1;
            str_copy(name, arg, (int)nlen + 1);
            if (env_set(name, eq + 1) < 0) {
                vga_puts("B16 error: env full\n");
                return -1;
            }
            vga_puts(name);
            vga_puts("=");
            vga_puts(eq + 1);
            vga_putc('\n');
        } else {                    /* set name：回显单个 */
            const char *v = env_get(arg);
            if (v) {
                vga_puts(arg);
                vga_puts("=");
                vga_puts(v);
                vga_putc('\n');
            } else {
                vga_puts("B16 error: variable not set: ");
                vga_puts(arg);
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
    if (grub_disk_read(&cd_disk, iso_root_extent, sectors, dir) < 0) {
        vga_puts("B16 error: ls failed\n");
        return -1;
    }
    while (off < iso_root_size && off < sectors * CD_SECTOR_SIZE) {
        struct iso_dir_record *r = (struct iso_dir_record *)(dir + off);
        if (r->len == 0)
            break;
        if (r->len < 34) {
            off += 1;
            continue;
        }
        print_name(r->name, r->name_len);
        if (r->flags & DIR_FLAG_DIR)
            vga_putc('/');
        vga_puts("  ");
        vga_hex(le32(r->data_len), 8);
        vga_putc('\n');
        off += r->len;
    }
    return 0;
}

static int cmd_halt_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_puts("B16: halted\n");
    for (;;)
        __asm__ volatile ("hlt");
    return 0;                       /* 不可达；满足 -Werror=return-type */
}

static int cmd_boot_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* B16 占位：真装载在 B17/B18 由 grub.cfg/menuentry 驱动 */
    vga_puts("B16 error: nothing to boot\n");
    return -1;
}

static struct cmd cmd_help = { "help", cmd_help_fn, "list available commands", 0 };
static struct cmd cmd_set  = { "set",  cmd_set_fn,  "get/set environment variables", 0 };
static struct cmd cmd_echo = { "echo", cmd_echo_fn, "print its arguments", 0 };
static struct cmd cmd_ls   = { "ls",   cmd_ls_fn,   "list files on (cd0)", 0 };
static struct cmd cmd_halt = { "halt", cmd_halt_fn, "halt the CPU", 0 };
static struct cmd cmd_boot = { "boot", cmd_boot_fn, "boot the loaded kernel (not yet)", 0 };

static void cmd_register_all(void)
{
    cmd_register(&cmd_help);
    cmd_register(&cmd_set);
    cmd_register(&cmd_echo);
    cmd_register(&cmd_ls);
    cmd_register(&cmd_halt);
    cmd_register(&cmd_boot);
}

/* ---- 命令行：拆词 + 执行 --------------------------------------------------- */
#define LINE_MAX 256
#define ARGV_MAX 16

static char line[LINE_MAX];
static char *argv_list[ARGV_MAX];

static int split_args(char *l, char **argv, int max)
{
    int argc = 0;
    while (*l) {
        while (*l == ' ')
            l++;
        if (!*l)
            break;
        if (argc >= max)
            break;
        argv[argc++] = l;
        while (*l && *l != ' ')
            l++;
        if (*l)
            *l++ = 0;
    }
    return argc;
}

/* ---- loader_main：进入交互提示符 ------------------------------------------ */
void loader_main(void)
{
    u8 drive = *(volatile u8 *)BOOT_DRIVE_ADDR;

    vga_clear();
    vga_puts("B16 cmd: Mini-GRUB interactive prompt\n");

    cd_disk.drive = drive;
    cd_disk.sector_size = CD_SECTOR_SIZE;
    vga_puts("B16 cmd: boot drive = ");
    vga_hex(drive, 2);
    vga_putc('\n');

    if (iso9660_mount() < 0)
        vga_puts("B16 error: ISO9660 mount failed\n");
    else
        vga_puts("B16 cmd: (cd0) mounted\n");

    cmd_register_all();

    for (;;) {
        int argc;
        vga_puts("grub> ");
        kbd_getline(line, LINE_MAX);
        argc = split_args(line, argv_list, ARGV_MAX);
        if (argc == 0)
            continue;
        cmd_execute(argc, argv_list);
    }
}

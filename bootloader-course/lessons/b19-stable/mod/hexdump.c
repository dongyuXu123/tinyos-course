/* Lesson B19: 示例模块 — hexdump 命令
 *
 * 编译为 ET_REL 可重定位 ELF（.mod），由核心的 insmod 命令加载：
 * 重定位解析核心导出的符号（cmd_register/vga_puts/vga_hex/file_open/
 * file_read），加载完成后核心调用 grub_mod_init 注册本模块的命令。
 * 结构与 GRUB 模块一致：grub_mod_init/grub_mod_fini 两个入口。
 *
 * 注意：本文件声明的核心函数与结构体布局必须与核心（loader.c）一致——
 * GRUB 用共享头文件保证，本课为教学简化在模块里重复声明。
 */

typedef unsigned char  u8;
typedef unsigned int   u32;

/* ---- 核心导出符号的声明（与 loader.c 布局一致）--------------------------- */
void vga_puts(const char *s);
void vga_putc(char c);
void vga_hex(u32 v, int digits);

struct grub_file {
    u32 extent;
    u32 size;
    u32 pos;
};

int file_open(const char *path, struct grub_file *file);
int file_read(struct grub_file *file, void *buf, u32 len);

struct cmd {
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *help;
    struct cmd *next;
};

int cmd_register(struct cmd *c);

/* ---- hexdump 命令：dump 一个文件的前 48 字节 --------------------------------- */
static int hexdump_fn(int argc, char **argv)
{
    struct grub_file f;
    u8 buf[48];
    u32 n, i;

    if (argc < 2) {
        vga_puts("hexdump: usage: hexdump FILE\n");
        return -1;
    }
    if (file_open(argv[1], &f) < 0) {
        vga_puts("hexdump: no such file: ");
        vga_puts(argv[1]);
        vga_putc('\n');
        return -1;
    }
    n = f.size < 48 ? f.size : 48;
    if (file_read(&f, buf, n) < 0) {
        vga_puts("hexdump: read failed\n");
        return -1;
    }
    vga_puts("B19 hexdump: ");
    vga_puts(argv[1]);
    vga_puts(":\n");
    for (i = 0; i < n; i++) {
        vga_hex(buf[i], 2);
        vga_putc(' ');
        if ((i & 7) == 7)
            vga_putc('\n');
    }
    vga_putc('\n');
    return 0;
}

static struct cmd cmd_hexdump = {
    "hexdump",
    hexdump_fn,
    "dump the first bytes of a file (module demo)",
    0
};

/* ---- 模块入口（GRUB 约定）------------------------------------------------- */
int grub_mod_init(void)
{
    cmd_register(&cmd_hexdump);
    return 0;
}

void grub_mod_fini(void)
{
}

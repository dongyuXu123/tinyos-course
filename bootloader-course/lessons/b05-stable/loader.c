/* Lesson B05: Mini-GRUB stage2 — 保护模式下的 BIOS 回调磁盘读
 *
 * B04 的 VGA 库保留；本课新增"保护模式调 BIOS"能力：
 *   bios_interrupt(intno, regs)  —— stage2.S 中的实模式回调封装
 *   disk_read_lba(drive, lba, buf, count) —— 用 INT 13 AH=02 (CHS) 读盘
 * 参照：grub-core/kern/i386/int.S（grub_bios_interrupt 的寄存器结构）、
 *       grub-core/disk/i386/pc/biosdisk.c（INT 13 参数组装）。
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

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

/* ---- BIOS 寄存器结构与中断封装 ------------------------------------------- */
/* 布局与 GRUB 的 struct grub_bios_int_registers 完全一致（int.S 的偏移） */
struct bios_regs {
    u32 eax;   /* +0  */
    u16 es;    /* +4  */
    u16 ds;    /* +6  */
    u16 flags; /* +8  */
    u32 ebx;   /* +12 */
    u32 ecx;   /* +16 */
    u32 edi;   /* +20 */
    u32 esi;   /* +24 */
    u32 edx;   /* +28 */
};

/* stage2.S 实现：保护模式调 BIOS 中断（prot_to_real -> int -> real_to_prot） */
void bios_interrupt(u8 intno, struct bios_regs *regs);

/* ---- 软盘几何（与 B02 一致）---------------------------------------------- */
#define SPT  18u   /* 每磁道扇区数 */
#define SPC  36u   /* 每柱面扇区数 = SPT * 2 磁头 */

/* lba_to_chs: 0 基 LBA -> CHS（与 B02 汇编的换算公式一致） */
static void lba_to_chs(u32 lba, u8 *cyl, u8 *head, u8 *sect)
{
    *cyl  = (u8)(lba / SPC);
    *head = (u8)((lba % SPC) / SPT);
    *sect = (u8)((lba % SPT) + 1);
}

/* disk_read_sector: 用 INT 13 AH=02 读 1 个 512 字节扇区。返回 0 成功 / -1 失败
 * 对照 GRUB biosdisk：EAX=0x0201，CH/DH/CL=CHS，ES:BX=缓冲区，DL=盘号。 */
static int disk_read_sector(u8 drive, u32 lba, void *buf)
{
    struct bios_regs regs;
    u8 cyl, head, sect;

    lba_to_chs(lba, &cyl, &head, &sect);

    regs.eax = 0x00000201u;            /* AH=02 读扇区，AL=1 */
    regs.ecx = ((u32)cyl << 8) | sect; /* CH=柱面，CL=扇区(1 基) */
    regs.edx = ((u32)head << 8) | drive; /* DH=磁头，DL=盘号 */
    regs.es  = (u16)((u32)buf >> 4);   /* ES:BX = 缓冲区线性地址 */
    regs.ebx = (u32)buf & 0xFu;
    regs.ds  = 0;
    regs.flags = 0x0200u;              /* IF=1（CF 清零，等待 BIOS 回写） */
    regs.edi = 0;
    regs.esi = 0;

    bios_interrupt(0x13, &regs);
    return (regs.flags & 0x01u) ? -1 : 0;  /* CF=1 -> 失败 */
}

/* disk_read_lba: 从 lba 起连续读 count 个扇区。返回 0 成功 / -1 失败 */
int disk_read_lba(u8 drive, u32 lba, void *buf, u32 count)
{
    u32 i;
    for (i = 0; i < count; i++) {
        if (disk_read_sector(drive, lba + i, (u8 *)buf + i * 512) < 0)
            return -1;
    }
    return 0;
}

/* ---- loader_main: 演示磁盘读 --------------------------------------------- */
static u8 sector_buf[512];

void loader_main(void)
{
    int i;

    vga_clear();
    vga_puts("B05 Mini-GRUB: protected-mode disk read via BIOS callback\n");
    vga_puts("bios_interrupt -> prot_to_real -> int 0x13 -> real_to_prot\n");

    if (disk_read_lba(0, 0, sector_buf, 1) < 0) {
        vga_puts("B05 error: disk_read_lba(0, LBA0) failed\n");
        return;
    }
    vga_puts("LBA0 head: ");
    for (i = 0; i < 8; i++) {
        vga_hex(sector_buf[i], 2);
        vga_putc(' ');
    }
    vga_puts("\n");
    if (sector_buf[510] == 0x55 && sector_buf[511] == 0xAA) {
        vga_puts("LBA0 signature 55 aa: boot sector read OK\n");
    } else {
        vga_puts("B05 error: LBA0 signature mismatch\n");
    }

    if (disk_read_lba(0, 1, sector_buf, 1) == 0) {
        vga_puts("LBA1 head: ");
        for (i = 0; i < 4; i++) {
            vga_hex(sector_buf[i], 2);
            vga_putc(' ');
        }
        vga_puts("\n");
    }
    vga_puts("B05 done: prot_to_real/real_to_prot cycle OK\n");
}

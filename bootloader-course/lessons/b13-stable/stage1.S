# Lesson B13: stage1 — El Torito 光盘引导镜像（2048 字节）
#
# B13 把整个 loader（stage1 + stage2 + loader.c）打包成一个 El Torito 引导
# 镜像，由 xorriso 写入 CD（-b boot.bin -no-emul-boot -boot-load-size N，
# N 为 512 字节扇区数）。SeaBIOS 读取引导目录中的 load segment（0x07C0）
# 并把镜像连续加载到 0x7C00，因此 stage1 只需要：
#   1. 保存 BIOS 给的启动盘号（no-emul 模式 = CD 盘号，SeaBIOS 1.17 下为 0xE0）；
#   2. 打印一行提示；
#   3. 远跳到紧随其后的 stage2（链接在 0x8400 = 0x7C00 + 2048）。
# 参照：grub-core/boot/i386/pc/cdboot.S（El Torito 引导镜像入口，64 字节
#       跳转结构；GRUB 用它读取 core image，本课 loader 直接内嵌，B15 再
#       复刻"读 core image"部分）。
#
# 镜像布局（SeaBIOS 加载到 0x7C00，连续）：
#   0x7C00  stage1（本文件，2048 字节）
#   0x8400  stage2（stage2.S + loader.c，由 linker.ld 链接到 0x8400）

.code16
.section .text
.globl _start

.set BOOT_ADDR,   0x7C00     # SeaBIOS 把引导镜像加载到 0x7C00
.set STAGE1_SIZE, 2048       # stage1 占 2048 字节（1 个 CD 扇区）
.set STAGE2_OFF,  0x8400     # stage2 目标地址 = 0x7C00 + 2048
.set BOOT_DRIVE,  0x60000    # 引导盘号保存处（远高于镜像上沿，实模式 DS=0 可写）

_start:
    jmp 1f
    .fill 64 - 3, 1, 0x90     # 64 字节头部跳转结构（同 cdboot.S，为 B15 的
                              # -boot-info-table 预留；B13 未用信息表）
1:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $BOOT_ADDR, %sp
    sti
    # 注意：16 位模式的位移只有 16 位，0x60000 会被截断（汇编成 mov %dl,0x0），
    # 必须用段寄存器：ES=0x6000 -> ES:0 = 物理 0x60000。
    movw $0x6000, %ax
    movw %ax, %es
    movb %dl, %es:(0)         # 保存引导盘号（no-emul CD = 0xE0）
    xorw %ax, %ax
    movw %ax, %es

    movw $msg_stage1, %si
    call puts

    ljmp $0, $STAGE2_OFF      # 远跳到 stage2 实模式入口（0x0000:0x8400）

# ---- 实模式输出（INT 10）---------------------------------------------------
puts:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0E, %ah
    movb $0x00, %bh
    int $0x10
    jmp puts
2:  ret

msg_stage1:
    .asciz "B13 stage1: el torito boot image -> stage2 @0x8400"

    .fill STAGE1_SIZE - (. - _start), 1, 0

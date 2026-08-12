# Lesson B15: stage1 — El Torito 引导镜像：从 CD 读入 core image（2048 字节）
#
# 与 B13/B14（整包镜像）不同，本课复刻 GRUB cdboot.S 的 "core image" 模型：
#   boot.bin = stage1（本文件，2048 B，SeaBIOS 加载到 0x7C00）
#            + stage2+loader（core，链接在 0x8400，由本文件从 CD 读入）
# xorriso 加 -boot-info-table 会在引导镜像偏移 8-63 写入 BIF（Boot Info
# Table）：bi_pvd @ +8, bi_file @ +12, bi_length @ +16（相对镜像首字节）。
# stage1 用 bi_file（自身 LBA）+1 定位 core 的起始扇区（第 1 个 CD 扇区
# 已被 SeaBIOS 加载，core 紧随其后），读 (bi_length-2048) 字节到 0x8400，
# 再远跳。DAP 建在栈上（同 cdboot.S read_cdrom），AH=42 一次读多扇区。
# 参照：grub-core/boot/i386/pc/cdboot.S（bi_file/bi_length + read_cdrom）

.code16
.section .text
.globl _start

.set BOOT_ADDR,   0x7C00
.set STAGE1_SIZE, 2048
.set CORE_OFF,    0x8400        # core 目标地址 = 0x7C00 + 2048
.set BOOT_DRIVE,  0x60000       # 引导盘号保存处
.set BIF_FILE,    0x7C0C        # bi_file（镜像偏移 8+4）
.set BIF_LENGTH,  0x7C10        # bi_length（镜像偏移 8+8）

_start:
    jmp 1f
    # 注意：xorriso 的 -boot-info-table 会覆盖偏移 8-63（56 字节 BIF）。
    # 跳转目标必须落在偏移 64 之后（GRUB cdboot.S 同样把 1: 放在 BIF 后）：
    # jmp(2) + 62 个 nop = 覆盖 0-63，1: 在 64。
    .fill 64 - 2, 1, 0x90
1:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $BOOT_ADDR, %sp
    sti
    # 保存引导盘号：ES=0x6000 -> ES:0 = 物理 0x60000（16 位位移放不下 0x60000）
    movw $0x6000, %ax
    movw %ax, %es
    movb %dl, %es:(0)
    xorw %ax, %ax
    movw %ax, %es

    movw $msg_stage1, %si
    call puts

    # ---- 计算 core 的起始 LBA 与扇区数 ------------------------------------
    movl BIF_LENGTH, %ecx       # bi_length：整个引导文件字节数
    subl $STAGE1_SIZE, %ecx     # 减去已被 SeaBIOS 加载的 2048 字节
    jbe core_ok                 # 没有 core（<=0），跳过读取直接跳转
    addl $2047, %ecx
    shrl $11, %ecx              # 扇区数 = ceil(剩余字节 / 2048)
    movl BIF_FILE, %eax         # bi_file：引导文件起始 LBA
    addl $1, %eax               # core 起始 = bi_file + 1（第 1 扇区已加载）

    # ---- DAP 建在栈上（标准布局，同 cdboot.S read_cdrom）----
    #   布局：size/res(2) count(2) offset(2) segment(2) lba(8)
    pushl $0                    # lba 高 32 位
    pushl %eax                  # lba 低 32 位
    pushw $(CORE_OFF >> 4)      # 缓冲段 = 0x840
    pushw $(CORE_OFF & 0xF)     # 缓冲偏移 = 0
    pushw %cx                   # count（扇区数）
    pushw $0x10                 # size=0x10, reserved=0
    movw %sp, %si               # DS:SI = DAP（栈上，物理 < 64KB）
    movw $0x4200, %ax
    movw $0x6000, %bx
    movw %bx, %es
    movb %es:(0), %dl           # DL = 引导盘号（CD = 0xE0）
    int $0x13
    addw $0x10, %sp             # 清理 DAP
    jnc core_ok
    movw $msg_err, %si
    call puts
1:  hlt
    jmp 1b

core_ok:
    ljmp $0, $CORE_OFF          # 跳到 core（stage2 实模式入口）

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
    .asciz "B22 stage1: eltorito core image read from CD -> 0x8400"
msg_err:
    .asciz "B22 stage1: core read FAILED"

    .fill STAGE1_SIZE - (. - _start), 1, 0

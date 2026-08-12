# Lesson B02: stage1 — 512 字节引导扇区，读出 stage2 并远跳
#
# 引导扇区只有 512 字节，装不下真正的加载逻辑，因此只做三件事：
#   1. 打印一条提示；
#   2. 用 BIOS INT 13 AH=02（CHS 读扇区）把 stage2 从软盘 LBA 1..8 读入 0x7E00；
#   3. 远跳到 0x0000:0x7E00 执行 stage2。
# 参照：grub-core/boot/i386/pc/boot.S（stage1 定位 core image 的角色）。

.code16
.section .text
.globl _start

# ---- 常量 ----------------------------------------------------------------
.set BOOT_ADDR,   0x7C00     # BIOS 把本扇区加载到 0x7C00
.set STAGE2_OFF,  0x7E00     # stage2 目标地址 = 0x7C00 + 512
.set STAGE2_SECT, 8          # stage2 占 8 个扇区（4096 字节）
.set SPT,         18         # 1.44MB 软盘：每磁道 18 扇区
.set SPH,         2          # 每柱面 2 磁头
.set SPC,         36         # 每柱面扇区数 = SPT * SPH

_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds           # DS = 0
    movw %ax, %es           # ES = 0（INT 13 读盘用 ES:BX 指缓冲区）
    movw %ax, %ss
    movw $BOOT_ADDR, %sp    # 栈顶 = 0x7C00，向下增长
    sti
    movb %dl, boot_drive    # 保存 BIOS 给的启动盘号（软盘为 0）

    movw $msg_stage1, %si
    call puts

    # ---- 读 stage2：LBA 1..8 -> 0x0000:0x7E00 -----------------------------
    movw $STAGE2_OFF, %bx   # 缓冲区起点（ES=0，物理 0x7E00）
    movw $1, %di            # 起始 LBA（0 基）
    movw $STAGE2_SECT, %cx  # 剩余扇区数（也是 loop 计数器）
read_loop:
    pushw %cx               # 保存计数（lba_to_chs 会破坏 CX）
    pushw %di               # 保存当前 LBA（INT 13 会破坏部分寄存器）
    movw %di, %ax
    call lba_to_chs         # 输入 AX=LBA，输出 CH=柱面 DH=磁头 CL=扇区
    movb boot_drive, %dl
    movb $0x02, %ah         # INT 13 AH=02：读扇区
    movb $1, %al            # 每次读 1 个扇区
    int $0x13
    jc read_fail            # CF=1 表示读盘失败
    popw %di
    popw %cx
    addw $512, %bx          # 缓冲区前进一个扇区
    incw %di                # 下一个 LBA
    loop read_loop

    movw $msg_jump, %si
    call puts
    ljmp $0x0000, $STAGE2_OFF   # 远跳：CS=0, IP=0x7E00 -> stage2

read_fail:
    movw $msg_fail, %si
    call puts
1:  hlt
    jmp 1b

# ---- lba_to_chs: LBA(0 基) -> CHS ------------------------------------------
# 输入：AX = LBA（0 基）；输出：CH=柱面 DH=磁头 CL=扇区（1 基）
# 破坏：AX、CX、DX
# 换算：柱面 = LBA / SPC；磁头 = (LBA % SPC) / SPT；扇区 = (LBA % SPT) + 1
lba_to_chs:
    xorw %dx, %dx
    movw $SPC, %cx
    divw %cx                # DX:AX / CX -> AX=柱面, DX=LBA%SPC
    movb %al, %ch           # 柱面(低 8 位) -> CH
    movw %dx, %ax           # AX = LBA%SPC（道内序号，0 基）
    xorw %dx, %dx
    movw $SPT, %cx
    divw %cx                # AX=磁头, DX=道内扇区(0 基)
    movb %al, %dh           # 磁头 -> DH
    movb %dl, %cl           # 扇区(0 基) -> CL
    incb %cl                # INT 13 的扇区号是 1 基
    andb $0x3F, %cl         # CL 高 2 位是柱面高 2 位；1.44MB 软盘柱面<1024 恒为 0
    ret

# ---- puts: INT 10 逐字符打印（同 B01）-------------------------------------
puts:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0E, %ah
    movb $0x00, %bh
    int $0x10
    jmp puts
2:  ret

# ---- 数据 ----------------------------------------------------------------
msg_stage1:
    .asciz "B02 stage1: reading stage2 from floppy"
msg_jump:
    .asciz "B02 stage1: jumping to stage2"
msg_fail:
    .asciz "B02 stage1: disk read failed!"
boot_drive:
    .byte 0

# ---- 引导签名 --------------------------------------------------------------
    .fill 510 - (. - _start), 1, 0
    .word 0xAA55

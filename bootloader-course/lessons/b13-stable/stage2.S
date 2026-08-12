# Lesson B13: stage2 — 实模式入口 + 保护模式 + BIOS 回调（CD 读盘用）
#
# 由 stage1 远跳进入（链接在 0x8400），流程与 B05 相同：
#   实模式（打印提示）-> 开 A20 -> 载 GDT -> 置 CR0.PE -> 远跳 32 位 ->
#   call loader_main（loader.c：ISO9660 解析）。
# loader.c 通过 prot_to_real / real_to_prot / bios_interrupt 在实模式
# 下调 INT 13 AH=42（EDD）读 CD 扇区。
# 参照：grub-core/kern/i386/realmode.S（prot_to_real/real_to_prot）
#        grub-core/kern/i386/int.S（grub_bios_interrupt）
#        include/grub/i386/memory_raw.h（REAL_STACK 0x1FF0 / PROT_STACK 0x70FF0）
#
# 内存布局（与 GRUB 2.14 的 i386-pc 约定一致）：
#   0x7C00..     El Torito 引导镜像（stage1 2048 字节 + 本文件 + loader.c）
#   0x1FF0       实模式栈 / 返回地址暂存（REAL_STACK = 0x2000 - 0x10）
#   0x2000       寄存器快照区（BIOS_SNAPSHOT）
#   0x2020       寄存器结构指针暂存（REGS_PTR）
#   0x60000      stage1 保存的引导盘号（BOOT_DRIVE）
#   0x68000      CD 数据缓冲（PVD/目录/文件，GRUB 的 scratch 区）
#   0x70FF0      保护模式栈（PROT_STACK）

.code16
.section .text
.globl stage2_entry

# ---- 常量（与 GRUB memory_raw.h 一致）-------------------------------------
.set PROT_MODE_CSEG,    0x08     # 保护模式代码段选择子
.set PROT_MODE_DSEG,    0x10     # 保护模式数据段选择子
.set PSEUDO_REAL_CSEG,  0x18     # 伪实模式代码段选择子（16 位）
.set PSEUDO_REAL_DSEG,  0x20     # 伪实模式数据段选择子（16 位）
.set REAL_STACK,        0x1FF0   # 实模式栈 / 返回地址暂存
.set PROT_STACK,        0x70FF0  # 保护模式栈顶
.set BIOS_SNAPSHOT,     0x2000   # 寄存器快照（32 字节，实模式 DS/CS=0 可访问）
.set REGS_PTR,          0x2020   # 寄存器结构指针暂存（4 字节）

stage2_entry:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    movw $msg_real, %si
    call puts

    # ---- A20 ---------------------------------------------------------
    movw $0x2401, %ax
    int $0x15
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    # ---- GDT / CR0.PE -------------------------------------------------
    lgdt gdtdesc
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmpl $PROT_MODE_CSEG, $pm32

# ---- GDT：与 GRUB realmode.S 完全一致（null + code32 + data32 +
#      伪实模式 code16 + 伪实模式 data16）---------------------------------
    .p2align 5               # 32 字节对齐（同 GRUB）
gdt:
    .word 0, 0
    .byte 0, 0, 0, 0         # 0x00 null
    .word 0xFFFF, 0
    .byte 0, 0x9A, 0xCF, 0   # 0x08 code32: P=1 DPL=0 S=1 type=0xA, G=1 D=1
    .word 0xFFFF, 0
    .byte 0, 0x92, 0xCF, 0   # 0x10 data32: read/write, G=1 D=1
    .word 0xFFFF, 0
    .byte 0, 0x9E, 0, 0      # 0x18 code16 伪实模式: type=0xE(exec/read/conforming),
                             #       G=0 D=0 -> limit 0xFFFF 字节粒度
    .word 0xFFFF, 0
    .byte 0, 0x92, 0, 0      # 0x20 data16 伪实模式: read/write, G=0 D=0
gdt_end:
    .p2align 5
gdtdesc:
    .word gdt_end - gdt - 1  # 5 个描述符 = 40 字节，limit = 0x27
    .long gdt

# 实模式 IDT 与保护模式 IDT 的描述符区（与 GRUB 相同，会被 sidt/lidt 改写）
realidt:
    .word 0x400              # 实模式 IDT：limit 0x400（覆盖 0-0x400 的 IVT）
    .long 0                  # 基址 0
protidt:
    .word 0                  # 保护模式 IDT：空（limit 0，任何中断都异常）
    .long 0

# ---- 32 位保护模式入口 -----------------------------------------------------
.code32
pm32:
    movw $PROT_MODE_DSEG, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $PROT_STACK, %esp   # 保护模式栈（GRUB 的 PROT_STACK）
    call loader_main         # loader.c 的 C 入口
    hlt
1:  jmp 1b

# ---- real_to_prot: 实模式 -> 保护模式（16 位代码调用）----------------------
# 结构完全镜像 GRUB realmode.S：调用方用 calll（32 位返回地址在 SS:SP），
# 返回时在保护模式栈顶给出同一返回地址。破坏：EAX、EBP。
.globl real_to_prot
.code16
real_to_prot:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    lgdtl gdtdesc            # 载入 GDT（lgdtl：32 位基址）
    movl %cr0, %eax
    orl $0x1, %eax           # PE=1
    movl %eax, %cr0
    ljmpl $PROT_MODE_CSEG, $protcseg

.code32
protcseg:
    movw $PROT_MODE_DSEG, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    # 把返回地址从实模式栈搬到保护模式栈
    movl (%esp), %eax        # 实模式栈上的 32 位返回地址（SS 基址 0）
    movl %eax, REAL_STACK    # 暂存到 0x1FF0 已知位置
    movl protstack, %eax     # 取保护模式栈顶
    movl %eax, %esp
    movl %eax, %ebp
    movl REAL_STACK, %eax
    movl %eax, (%esp)        # 返回地址放到保护模式栈顶
    xorl %eax, %eax
    sidt realidt             # 保存实模式 IDT（刷新静态 realidt）
    lidt protidt             # 保护模式用空 IDT（保持 cli 期间安全）
    ret                      # 32 位 ret：从保护模式栈弹出返回地址

# ---- prot_to_real: 保护模式 -> 实模式（32 位代码调用）----------------------
.globl prot_to_real
.code32
prot_to_real:
    lgdt gdtdesc             # 确保 GDT 就位
    sidt protidt             # 保存保护模式 IDT（空）
    lidt realidt             # 载入实模式 IDT（0x400@0）
    movl %esp, %eax
    movl %eax, protstack     # 保存保护模式栈顶
    movl (%esp), %eax        # 32 位返回地址
    movl %eax, REAL_STACK    # 存到 0x1FF0（同时是实模式栈的"栈顶内容"）
    movl $REAL_STACK, %eax
    movl %eax, %esp          # 切到实模式栈
    movl %eax, %ebp
    movw $PSEUDO_REAL_DSEG, %ax
    movw %ax, %ds            # 伪实模式数据段（base 0, limit 64K, 16 位）
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $PSEUDO_REAL_CSEG, $tmpcseg   # 远跳：CS=0x18（16 位代码段）

.code16
tmpcseg:
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax   # PE=0：回到实模式
    movl %eax, %cr0
    ljmpl $0, $realcseg      # 刷新 CS=0（真实模式）

realcseg:
    xorl %eax, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    sti                      # 恢复中断（GRUB PCBIOS 同样 sti）
    retl                     # 32 位 ret：从 0x1FF0 弹出返回地址

# ---- bios_interrupt: 保护模式下调 BIOS 中断（镜像 int.S 的 grub_bios_interrupt）
# void bios_interrupt(u8 intno, struct bios_regs *regs)   （cdecl）
# regs 布局与 GRUB struct grub_bios_int_registers 一致：
#   0: eax(u32)  4: es(u16)  6: ds(u16)  8: flags(u16)  12: ebx  16: ecx
#   20: edi  24: esi  28: edx
.globl bios_interrupt
.code32
bios_interrupt:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ecx
    pushl %edx

    movb 8(%ebp), %al        # intno（cdecl：8(%ebp)）
    movb %al, int_stub + 1   # 自修改：把 int 指令的中断号字节改成 intno（同 GRUB）

    movl 12(%ebp), %edx      # regs 指针
    movl %edx, REGS_PTR      # 存到低内存（跨模式切换保存）

    # 拷贝 regs 结构 -> 低内存快照 BIOS_SNAPSHOT
    movl 0(%edx), %eax
    movl %eax, BIOS_SNAPSHOT + 0
    movw 4(%edx), %ax
    movw %ax, BIOS_SNAPSHOT + 4
    movw 6(%edx), %ax
    movw %ax, BIOS_SNAPSHOT + 6
    movw 8(%edx), %ax
    movw %ax, BIOS_SNAPSHOT + 8
    movl 12(%edx), %eax
    movl %eax, BIOS_SNAPSHOT + 12
    movl 16(%edx), %eax
    movl %eax, BIOS_SNAPSHOT + 16
    movl 20(%edx), %eax
    movl %eax, BIOS_SNAPSHOT + 20
    movl 24(%edx), %eax
    movl %eax, BIOS_SNAPSHOT + 24
    movl 28(%edx), %eax
    movl %eax, BIOS_SNAPSHOT + 28

    call prot_to_real
    .code16
    # ---- 实模式：从快照装载寄存器 ----
    # 此刻 DS=0（realcseg 已清零），全部快照读取用 DS 相对寻址；
    # 注意 EAX 的 moffs 编码与 %cs: 段前缀冲突（汇编成 ljmp），不能用 %cs: 读 EAX。
    movl BIOS_SNAPSHOT + 0, %eax
    pushl %eax               # 暂存 EAX（popfw 会破坏 AX，最后再恢复）
    movl BIOS_SNAPSHOT + 12, %ebx
    movl BIOS_SNAPSHOT + 16, %ecx
    movl BIOS_SNAPSHOT + 20, %edi
    movl BIOS_SNAPSHOT + 24, %esi
    movl BIOS_SNAPSHOT + 28, %edx
    movw BIOS_SNAPSHOT + 4, %ax
    pushw %ax                # 暂存 ES
    movw BIOS_SNAPSHOT + 6, %ax
    pushw %ax                # 暂存 DS
    movw BIOS_SNAPSHOT + 8, %ax
    pushw %ax                # 暂存 FLAGS
    # 恢复 FLAGS / DS / ES / EAX（顺序保证 EAX 最后赋值）
    popw %ax
    pushw %ax                # popfw 从栈顶取，需把 flags 值放回栈顶
    popfw                    # FLAGS = 快照值（破坏 AX）
    popw %ax
    movw %ax, %ds
    popw %ax
    movw %ax, %es
    popl %eax                # EAX = 快照值
int_stub:
    int $0x13                # 中断号字节由入口处自修改（B13 固定 0x13）

    # ---- 保存结果回快照（全部 %cs:，与 DS 无关）----
    movl %eax, %cs:BIOS_SNAPSHOT + 0
    movw %es, %ax
    movw %ax, %cs:BIOS_SNAPSHOT + 4
    movw %ds, %ax
    movw %ax, %cs:BIOS_SNAPSHOT + 6
    pushfw
    popw %ax
    movw %ax, %cs:BIOS_SNAPSHOT + 8
    movl %ebx, %cs:BIOS_SNAPSHOT + 12
    movl %ecx, %cs:BIOS_SNAPSHOT + 16
    movl %edi, %cs:BIOS_SNAPSHOT + 20
    movl %esi, %cs:BIOS_SNAPSHOT + 24
    movl %edx, %cs:BIOS_SNAPSHOT + 28

    calll real_to_prot
    .code32
    # ---- 保护模式：快照 -> regs 结构 ----
    movl REGS_PTR, %edx
    movl BIOS_SNAPSHOT + 0, %eax
    movl %eax, 0(%edx)
    movw BIOS_SNAPSHOT + 4, %ax
    movw %ax, 4(%edx)
    movw BIOS_SNAPSHOT + 6, %ax
    movw %ax, 6(%edx)
    movw BIOS_SNAPSHOT + 8, %ax
    movw %ax, 8(%edx)
    movl BIOS_SNAPSHOT + 12, %eax
    movl %eax, 12(%edx)
    movl BIOS_SNAPSHOT + 16, %eax
    movl %eax, 16(%edx)
    movl BIOS_SNAPSHOT + 20, %eax
    movl %eax, 20(%edx)
    movl BIOS_SNAPSHOT + 24, %eax
    movl %eax, 24(%edx)
    movl BIOS_SNAPSHOT + 28, %eax
    movl %eax, 28(%edx)

    popl %edx
    popl %ecx
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

# ---- 变量区（位于 .text，链接后写入）--------------------------------------
protstack:
    .long PROT_STACK         # 保护模式栈顶（prot_to_real 会更新它）

# ---- 实模式工具 ------------------------------------------------------------
.code16
puts:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0E, %ah
    movb $0x00, %bh
    int $0x10
    jmp puts
2:  ret

msg_real:
    .asciz "B13 stage2: real mode -> protected mode -> ISO9660"

# Lesson B03: stage2 — 实模式 → 保护模式切换
#
# 本课在 stage2（0x7E00）中完成 A20、GDT、CR0.PE 三步切换，进入 32 位保护
# 模式后改用"直接写 VGA 文本内存 0xB8000"输出（INT 10 在保护模式下不可用）。
# 参照：grub-core/kern/i386/pc/startup.S（A20/GDT/CR0.PE 的切换顺序）。

.code16
.section .text
.globl stage2_entry

# ---- 常量 ----------------------------------------------------------------
.set CODE32_SEL, 0x08       # GDT 中 code32 描述符的选择子
.set DATA32_SEL, 0x10       # GDT 中 data32 描述符的选择子
.set STACK_TOP,  0x00090000 # 常规内存高端的栈顶（EBDA 之下）

stage2_entry:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    # 实模式阶段用 INT 10 打印（保护模式后不可用）
    movw $msg_real, %si
    call puts

    # ---- 1. A20 地址线 -----------------------------------------------
    movw $0x2401, %ax
    int $0x15                # INT 15 AX=2401：使能 A20
    inb $0x92, %al           # fallback：fast A20（端口 0x92 置位）
    orb $0x02, %al
    outb %al, $0x92

    # ---- 2. GDT ------------------------------------------------------
    lgdt gdtr                # 载入 GDTR（基址 = GDT 的线性地址）

    # ---- 3. CR0.PE ---------------------------------------------------
    movl %cr0, %eax
    orl $0x1, %eax           # PE = 1：进入保护模式
    movl %eax, %cr0
    ljmp $CODE32_SEL, $pm32 # 远跳：刷新 CS 为 code32，同时跳到 32 位代码

# ---- GDT：平坦分段（base 0, limit 4G）------------------------------------
gdt:
    .long 0, 0               # 0x00 null 描述符（必须全零）
gdt_code32:
    .word 0xFFFF             # 0x08 code32：limit 15:0
    .word 0x0000             # base 15:0
    .byte 0x00               # base 23:16
    .byte 0x9A               # P=1 DPL=0 S=1 type=0xA(exec/read)
    .byte 0xCF               # G=1 D=1 limit 19:16 = 0xF -> 4G
    .byte 0x00               # base 31:24
gdt_data32:
    .word 0xFFFF             # 0x10 data32：base 0 limit 4G
    .word 0x0000
    .byte 0x00
    .byte 0x92               # type=0x2(data read/write)
    .byte 0xCF
    .byte 0x00
gdt_code16:
    .word 0xFFFF             # 0x18 code16：base 0 limit 4G
    .word 0x0000
    .byte 0x00
    .byte 0x9A
    .byte 0x0F               # G=1 D=0（16 位）
    .byte 0x00
gdt_end:

gdtr:
    .word gdt_end - gdt - 1  # GDT 大小 - 1
    .long gdt                # GDT 线性地址（链接在 0x7E00，基址 0）

# ---- 32 位保护模式代码 -----------------------------------------------------
.code32
pm32:
    movw $DATA32_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $STACK_TOP, %esp    # 常规内存高端的栈（向下增长）

    # 直接写 VGA 文本内存。第 0 行保留实模式 INT 10 的输出（同屏对照两种
    # 输出方式），本课的消息写在第 1 行：偏移 = 80 字符 * 2 字节。
    cld
    movl $0xB8000 + 160, %edi
    movl $msg_pm, %esi
1:  lodsb
    testb %al, %al
    jz 2f
    movb %al, (%edi)
    movb $0x1F, 1(%edi)
    addl $2, %edi
    jmp 1b
2:  hlt                      # 挂起（无操作系统可返回）
    jmp 2b

# ---- 实模式工具（保护模式前使用）------------------------------------------
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
    .asciz "B03 stage2: real mode, enabling A20/GDT/CR0.PE"

msg_pm:
    .asciz "B03 Mini-GRUB in 32-bit protected mode"

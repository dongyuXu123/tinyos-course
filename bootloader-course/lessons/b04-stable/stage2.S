# Lesson B04: stage2 — 保护模式切换后进入 C 运行时
#
# 本课在 B03 的切换代码基础上，把 loader 主体交给 C（loader.c 的 loader_main）：
#   实模式打印 -> A20/GDT/CR0.PE -> pm32（保护模式）-> call loader_main -> hlt
# 参照：grub-core/kern/i386/pc/startup.S（切换）+ grub-core/kern/main.c
# （grub_main 把控制交给逐层的 C 初始化）。

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
    movw $msg_real, %si
    call puts

    # ---- A20 ---------------------------------------------------------
    movw $0x2401, %ax
    int $0x15                # INT 15 AX=2401：使能 A20
    inb $0x92, %al           # fallback：fast A20
    orb $0x02, %al
    outb %al, $0x92

    # ---- GDT / CR0.PE ------------------------------------------------
    lgdt gdtr
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmp $CODE32_SEL, $pm32

# ---- GDT：平坦分段（base 0, limit 4G）------------------------------------
gdt:
    .long 0, 0               # 0x00 null
gdt_code32:
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x9A               # P=1 DPL=0 S=1 type=0xA(exec/read)
    .byte 0xCF               # G=1 D=1 -> 4G
    .byte 0x00
gdt_data32:
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x92               # data read/write
    .byte 0xCF
    .byte 0x00
gdt_code16:
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x9A
    .byte 0x0F               # G=1 D=0（16 位）
    .byte 0x00
gdt_end:

gdtr:
    .word gdt_end - gdt - 1
    .long gdt

# ---- 32 位保护模式：进入 C --------------------------------------------------
.code32
pm32:
    movw $DATA32_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $STACK_TOP, %esp
    call loader_main         # loader.c 中的 C 入口（链接器解析符号）
    hlt                      # loader_main 返回后挂起
1:  jmp 1b

# ---- 实模式工具 ------------------------------------------------------------
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
    .asciz "B04 stage2: real mode -> protected mode -> C"

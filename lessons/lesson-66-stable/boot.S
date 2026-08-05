/* 第八课：GRUB 的 Multiboot2 i386 交接后，由内核进入 x86_64 long mode。
 * Intel SDM 是 CR4/EFER/CR0/GDT/far jump 的规范来源。 */
.set MB2_HEADER_MAGIC, 0xe85250d6
.set MB2_ARCHITECTURE_I386, 0
.set MB2_HEADER_LENGTH, mb2_header_end - mb2_header
.set MB2_CHECKSUM, -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)
.set MB2_HEADER_TAG_INFO_REQUEST, 1
	.set MB2_HEADER_TAG_GRAPHICS, 5
.set MB2_BOOT_TAG_MMAP, 6
	.set MB2_BOOT_TAG_FRAMEBUFFER, 8
.set IA32_EFER, 0xc0000080
.set EFER_LME, 0x00000100
.set CR4_PAE, 0x00000020
.set CR0_PG, 0x80000000
.set CODE64_SELECTOR, 0x08
.set DATA_SELECTOR, 0x10

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_HEADER_MAGIC
    .long MB2_ARCHITECTURE_I386
    .long MB2_HEADER_LENGTH
    .long MB2_CHECKSUM
    .short MB2_HEADER_TAG_INFO_REQUEST
    .short 0
    .long 16
    .long MB2_BOOT_TAG_MMAP
    .long MB2_BOOT_TAG_FRAMEBUFFER
    .short MB2_HEADER_TAG_GRAPHICS
    .short 0
    .long 20
    .long 800
    .long 600
    .long 32
    .align 8
    .short 0
    .short 0
    .long 8
mb2_header_end:

.section .text
.code32
.globl _start
.type _start, @function
.extern kernel_main32
_start:
    cli
    movl $stack_top, %esp
    xorl %ebp, %ebp
    pushl %ebx
    pushl %eax
    call kernel_main32
    addl $8, %esp
    testl %eax, %eax
    jz halt32
    movl %eax, %ebx
    call enter_long_mode
halt32:
    hlt
    jmp halt32
.size _start, . - _start

/* EBX contains the physical PML4 address returned by 32-bit setup. */
enter_long_mode:
    lgdt gdt64_pointer
    movl %ebx, %cr3
    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4
    movl $IA32_EFER, %ecx
    rdmsr
    orl $EFER_LME, %eax
    wrmsr
    movl %cr0, %eax
    orl $CR0_PG, %eax
    movl %eax, %cr0
    ljmp $CODE64_SELECTOR, $long_mode_start

/* 32-bit far jump reloads CS; these bytes run in 64-bit submode even though
 * GRUB's outer Multiboot image remains ELF32. */
long_mode_start:
    .byte 0x66, 0xb8, DATA_SELECTOR, 0x00
    .byte 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0
    /* Low and high aliases are live.  Move stack, handoff, and control flow
     * to the high alias without a rel32 call that cannot span the boundary. */
    .byte 0x48, 0xbc
    .quad 0xffffffff80000000
    .byte 0xb8
    .long stack_top
    .byte 0x48, 0x01, 0xc4, 0x48, 0x83, 0xe4, 0xf0, 0x48, 0x31, 0xed
    /* SysV x86_64: RDI=high handoff and RSP is 16-byte aligned before call. */
    .byte 0x48, 0xbf
    .quad 0xffffffff80000000
    .byte 0xb8
    .long long_mode_handoff
    .byte 0x48, 0x01, 0xc7
    .byte 0x48, 0xb8
    .quad 0xffffffff80000000
    .byte 0xb9
    .long kernel_main64
    .byte 0x48, 0x01, 0xc8, 0xff, 0xd0
1:
    .byte 0xf4, 0xeb, 0xfd

.section .rodata
.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt64_end:
gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.section .text64, "ax"
/* Keep the raw continuation page-aligned so its static TSS/IST stacks retain
 * their declared page alignment after the outer ELF embeds the binary. */
.align 4096
.globl kernel_main64
kernel_main64:
    .incbin "build/kernel64.bin"

.section .bss
.align 16
.globl stack_bottom
.globl stack_top
stack_bottom:
.skip 16384
stack_top:

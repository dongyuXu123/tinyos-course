/* 第五课：接收 GRUB 的 Multiboot2 i386 交接并显示 memory map。
 * Multiboot2 规范定义 EAX magic 与 EBX boot-information physical address；
 * Linux v6.12/arch/x86/kernel/head_64.S:119 提供受控早期入口的工程对照。
 */

.set MB2_HEADER_MAGIC,       0xe85250d6
.set MB2_ARCHITECTURE_I386,  0
.set MB2_HEADER_LENGTH,      mb2_header_end - mb2_header
.set MB2_CHECKSUM,           -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)
.set MB2_HEADER_TAG_INFO_REQUEST, 1
.set MB2_BOOT_TAG_MMAP,      6

.section .multiboot, "a"
.align 8
mb2_header:
	.long MB2_HEADER_MAGIC
	.long MB2_ARCHITECTURE_I386
	.long MB2_HEADER_LENGTH
	.long MB2_CHECKSUM

	/* Multiboot2 information-request tag：显式请求 type 6 memory map。 */
	.short MB2_HEADER_TAG_INFO_REQUEST
	.short 0
	.long 12
	.long MB2_BOOT_TAG_MMAP
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
	/* GRUB 已按 Multiboot2 i386 ABI 提供 EAX/EBX；先建立自己的栈。 */
	cli
	movl $stack_top, %esp
	xorl %ebp, %ebp
	/* i386 cdecl：从右到左传入 (magic, mbi_address)。 */
	pushl %ebx
	pushl %eax
	call kernel_main32
	addl $8, %esp
1:
	hlt
	jmp 1b
.size _start, . - _start

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

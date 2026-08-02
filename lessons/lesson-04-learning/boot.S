/* 第一课：GRUB 的 Multiboot2 32 位交接和可见 VGA 文本输出。
 * 参考 Linux v6.12:
 *   arch/x86/boot/header.S（早期启动镜像布局）；
 *   arch/x86/kernel/head_64.S:119（早期入口保持中断关闭）。
 * 本课是 x86_64 课程的启动第 0 阶段；long mode 留待后续独立课程。
 */

.set MB2_HEADER_MAGIC,       0xe85250d6
.set MB2_ARCHITECTURE_I386,  0
.set MB2_HEADER_LENGTH,      mb2_header_end - mb2_header
.set MB2_CHECKSUM,           -(MB2_HEADER_MAGIC + MB2_ARCHITECTURE_I386 + MB2_HEADER_LENGTH)

.section .multiboot, "a"
.align 8
mb2_header:
	.long MB2_HEADER_MAGIC
	.long MB2_ARCHITECTURE_I386
	.long MB2_HEADER_LENGTH
	.long MB2_CHECKSUM
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
	/* Linux v6.12/arch/x86/kernel/head_64.S:119 的受控入口原则。 */
	cli
	movl $stack_top, %esp
	xorl %ebp, %ebp
	call kernel_main32
1:
	hlt
	jmp 1b
.size _start, . - _start

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

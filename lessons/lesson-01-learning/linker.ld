/* 参考 Linux v6.12/arch/x86/kernel/vmlinux.lds.S：显式控制镜像段布局。
 * Multiboot2 header 必须在镜像最初 32768 字节内，且按 8 字节对齐。
 */
ENTRY(_start)

SECTIONS
{
	. = 1M;

	.multiboot ALIGN(8) : {
		KEEP(*(.multiboot))
	}

	.text ALIGN(16) : { *(.text .text.*) }
	.rodata ALIGN(16) : { *(.rodata .rodata.*) }

	/* 把可写段移到新页，避免生成 RWX 的 PT_LOAD 段。 */
	. = ALIGN(CONSTANT(MAXPAGESIZE));
	.data ALIGN(16) : { *(.data .data.*) }
	.bss ALIGN(16) : {
		*(.bss .bss.*)
		*(COMMON)
	}
}

/* Lesson B06/B08: test-kernel 链接脚本
 * 与 TinyOS 主线约定一致：内核从物理 1 MiB 开始，.multiboot 段在最前；
 * .data/.bss 页对齐后放入独立的 RW LOAD 段（对照 TinyOS linker.ld 的
 * `. = ALIGN(CONSTANT(MAXPAGESIZE))` 手法，避免出现 RWX 段）。
 */
ENTRY(_start)
SECTIONS
{
    . = 1M;
    .multiboot : { *(.multiboot) }
    .text      : { *(.text) }
    .rodata    : { *(.rodata) }
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data      : { *(.data) }
    .bss       : { *(.bss) }
}

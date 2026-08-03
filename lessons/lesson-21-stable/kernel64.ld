SECTIONS
{
    /* The raw blob is linked at zero; its RIP-relative code runs at the
     * high alias selected by the bootstrap's indirect transfer. */
    . = 0;
    .text64 : { *(.text64.entry) *(.text64 .text64.*) }
    .rodata : { *(.rodata .rodata.*) }
    /* objcopy emits PROGBITS, not NOBITS.  Keep persistent IRQ queue state
     * in the raw continuation, even when C places zero-initialized objects in .bss. */
    .data : { *(.data .data.*) *(.bss .bss.* COMMON) BYTE(0) }
}

SECTIONS
{
    . = 0;
    .text64 : { *(.text64.entry) *(.text64 .text64.*) }
    .rodata : { *(.rodata .rodata.*) }
}

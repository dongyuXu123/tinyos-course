SECTIONS
{
    /* Raw continuation VMA is zero; bootstrap executes it through high alias. */
    . = 0;
    .text64 : { *(.text64.entry) *(.text64 .text64.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : {
        . = ALIGN(0x1000);
        __idle_guard_start = .; . += 0x1000;
        __idle_stack_start = .; *(.data.stack.idle) __idle_stack_end = .;
        . = ALIGN(0x1000);
        __rsp0_guard_start = .; . += 0x1000;
        __rsp0_stack_start = .; *(.data.stack.rsp0) __rsp0_stack_end = .;
        . = ALIGN(0x1000);
        __ist1_guard_start = .; . += 0x1000;
        __ist1_stack_start = .; *(.data.stack.ist1) __ist1_stack_end = .;
        . = ALIGN(0x1000);
        *(.data .data.*)
        *(.bss .bss.* COMMON)
        BYTE(0)
    }
    ASSERT(__idle_stack_end - __idle_stack_start == 0x1000, "idle stack size")
    ASSERT(__rsp0_stack_end - __rsp0_stack_start == 0x1000, "rsp0 stack size")
    ASSERT(__ist1_stack_end - __ist1_stack_start == 0x1000, "ist1 stack size")
}

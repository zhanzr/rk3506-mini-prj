/* linker: everything in the 32 KB M0 SRAM, seen at 0x00000000 */
ENTRY(reset_handler)

MEMORY
{
    RAM (rxw) : ORIGIN = 0x00000000, LENGTH = 32K
}

SECTIONS
{
    . = 0x0;

    .vectors : { KEEP(*(.vectors)) } > RAM
    .text    : { *(.text*) *(.rodata*) } > RAM
    .data    : { *(.data*) } > RAM
    .bss     : { *(.bss*) *(COMMON) } > RAM

    /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}

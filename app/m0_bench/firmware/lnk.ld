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
    .data    :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) :
    {
        _sbss = .;
        *(.bss*) *(COMMON)
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.comment) *(.ARM.attributes) }

    ASSERT(. <= ORIGIN(RAM) + LENGTH(RAM) - 8192,
           "M0 image leaves less than 8 KB for stack")
}

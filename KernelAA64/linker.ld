ENTRY(_AuMain)

SECTIONS
{
    /* High half address from MSVC project */
    . = 0xFFFFC00000000000;

    .text : ALIGN(4096)
    {
        *(.text*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
    }

    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss*)
    }
}

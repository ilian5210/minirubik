OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : { *(.text .text.*) }
    . = 0x10000;
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) }
    ASSERT(SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss) <= 131072,
           "RV32 static data exceeds 128 KiB")
    /DISCARD/ : { *(.comment) *(.note*) }
}

ENTRY(start)
MEMORY {
 IMAGE (rwx) : ORIGIN = DEFINED(RAM_ONLY) ? 0x20000000 : 0, LENGTH = DEFINED(RAM_ONLY) ? 64K : 768K
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
SECTIONS {
 .vectors : { KEEP(*(.vectors)) } > IMAGE
 .text : { *(.text*) *(.rodata*) } > IMAGE
 .data (DEFINED(RAM_ONLY) ? ALIGN(.,8) : ORIGIN(RAM)) : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text),8)) {
    data_begin = .; *(.data*); data_end = .;
 } > RAM
 data_load = LOADADDR(.data);
 .diag (NOLOAD) : { *(.diag) } > RAM
 .bss (NOLOAD) : { bss_begin = .; *(.bss*) *(COMMON); bss_end = .; } > RAM
 stack_top = ORIGIN(RAM) + LENGTH(RAM);
 ASSERT(. <= stack_top - 2048, "radio firmware exceeds memory budget")
 /DISCARD/ : { *(.comment*) *(.ARM.exidx*) *(.ARM.extab*) }
}

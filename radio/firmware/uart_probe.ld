ENTRY(start)
MEMORY { SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K }
SECTIONS {
  .vectors ORIGIN(SRAM) : { KEEP(*(.vectors)) } > SRAM
  .diag 0x20000040 (NOLOAD) : { KEEP(*(.diag)) } > SRAM
  .text : { *(.text*) *(.rodata*) } > SRAM
  .data : { *(.data*) } > SRAM
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > SRAM
  stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
  ASSERT(. <= stack_top - 1024, "insufficient stack")
  /DISCARD/ : { *(.comment*) *(.ARM.exidx*) }
}

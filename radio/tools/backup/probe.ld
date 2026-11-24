ENTRY(start)
SECTIONS {
 . = 0x40378000;
 .text : { *(.literal .literal.*) *(.entry.literal) *(.entry) *(.text .text.*) }
 /DISCARD/ : { *(.comment) *(.xtensa.info) *(.xt.prop*) *(.xt.lit*) *(.note*) }
}

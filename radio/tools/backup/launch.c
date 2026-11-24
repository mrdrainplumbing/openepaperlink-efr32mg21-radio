#include "swd.h"

#define MG_SRAM 0x20000000u
#define MG_END (MG_SRAM + 0x10000u)
#define IMAGE ((volatile u32 *)0x3fca5000u)
#define DHCSR 0xe000edf0u
#define DCRSR 0xe000edf4u
#define DCRDR 0xe000edf8u
#define DEMCR 0xe000edfcu
#define AIRCR 0xe000ed0cu
#define VTOR 0xe000ed08u
#define KEY 0xa05f0000u

#include "mem_ap.h"
static void core_write(unsigned reg, u32 value) {
    mem_write(DCRDR, value);
    mem_write(DCRSR, (1u << 16) | reg);
    wait_reg(DHCSR, 1u << 16, 1u << 16);
}
static void reset_core(void) {
    u32 saved = mem_read(AIRCR) & 0x700;
    mem_write(AIRCR, 0x05fa0004 | saved);
}
void run(void) {
    while (!(REG(0x6000001c) & 1023) || (REG(0x60000000) & 255) != 75) {
    }
    u32 size = REG(ARG), mode = REG(ARG + 4);
    connect_swd(0, 0x22000002);
    if (mode == 2) {
        mem_write(DEMCR, mem_read(DEMCR) & ~1u);
        mem_write(DHCSR, KEY);
        reset_core();
        serial_word(0x21444e45);
        for (;;) {
        }
    }
    if (mode == 3) {
        serial_word(0x21474552); /* REG! */
        serial_word(mem_read(REG(ARG + 8)));
        for (;;) {
        }
    }
    u32 initial_sp = IMAGE[0], entry = IMAGE[1];
    if (size < 8 || size > 0x10000 || size % 4 || initial_sp <= MG_SRAM || initial_sp > MG_END ||
        initial_sp % 4 || !(entry & 1) || entry < MG_SRAM || entry >= MG_SRAM + size)
        fail(0x400, entry);
    u32 demcr = mem_read(DEMCR);
    mem_write(DHCSR, KEY | 3);
    wait_reg(DHCSR, 1u << 17, 1u << 17);
    mem_write(DEMCR, demcr | 1);
    reset_core();
    wait_reg(DHCSR, 1u << 17, 1u << 17);
    mem_write(DEMCR, demcr & ~1u);
    /* Reprogram TAR before its 1 KiB auto-increment boundary. */
    transfer(1, 0, 0, 0x22000012);
    for (unsigned i = 0; i < size / 4; i++) {
        if ((i & 255) == 0)
            transfer(1, 0, 4, MG_SRAM + 4 * i);
        transfer(1, 0, 12, IMAGE[i]);
    }
    transfer(0, 1, 12, 0);
    for (unsigned pos = 0; pos < size; pos += 1024) {
        unsigned words = (size - pos > 1024 ? 1024 : size - pos) / 4;
        transfer(1, 0, 4, MG_SRAM + pos);
        transfer(1, 1, 12, 0);
        for (unsigned j = 0; j < words; j++) {
            u32 word = (j + 1 == words) ? transfer(0, 1, 12, 0) : transfer(1, 1, 12, 0);
            if (word != IMAGE[pos / 4 + j])
                fail(0x401, MG_SRAM + pos + 4 * j);
        }
    }
    transfer(1, 0, 0, 0x22000002);
    mem_write(VTOR, MG_SRAM);
    core_write(16, 0x01000000u); /* ARMv8-M xPSR, Thumb Thread mode */
    core_write(17, initial_sp);  /* MSP */
    core_write(13, initial_sp);  /* currently selected SP */
    core_write(15, entry & ~1u);
    core_write(14, 0xffffffffu);
    mem_write(DHCSR, KEY | 1);
    serial_word(0x214e5552);      /* RUN! */
    serial_word(mem_read(DHCSR)); /* may report lockup if the app faulted */
    serial_word(mem_read(VTOR));
    transfer(0, 0, 4, 0);
    OE_CLEAR = CLK | DIO;
    for (;;) {
    }
}

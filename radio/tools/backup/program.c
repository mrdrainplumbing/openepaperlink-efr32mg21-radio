/* GSDK EFR32MG21 MSC_WriteWord/writeBurst, with restricted target addressing
 * and host verification. Only write command issued is WRITEEND. */
#include "mem_ap.h"
#include "swd.h"
#define MSC 0x50030000u /* MSC_NS_BASE for this debug access port. */
#define WRITECTRL 0x0cu
#define WRITECMD 0x10u
#define ADDRB 0x14u
#define WDATA 0x18u
#define STATUS 0x1cu
#define LOCK 0x3cu
#define WREN 1u
#define REGLOCK (1u << 16)
#define WRITEEND 4u
#define WDATAREADY 8u
#define BUSY_PENDING 0x21u
#define LOCKED_INVADDR 6u
#define BLANK 0xffffffffu
#define MAIN_BYTES (768u * 1024u)
#define DHCSR 0xe000edf0u
static unsigned char serial_receive(void) {
    while (!(REG(0x6000001c) & 1023)) {
    }
    return REG(0x60000000);
}
static u32 receive_crc(void) {
    u32 x = 0;
    for (unsigned i = 0; i < 4; i++)
        x |= ((u32)serial_receive()) << (8 * i);
    return x;
}
static u32 crc_byte(u32 crc, u32 byte) {
    crc ^= byte;
    for (int j = 0; j < 8; j++)
        crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320);
    return crc;
}
static void msc_wait(u32 mask, u32 desired) {
    for (int i = 0; i < 2000; i++) {
        u32 value = mem_read(MSC + STATUS);
        if ((mask & BUSY_PENDING) && (value & mask) != desired)
            continue;
        if (value & (LOCKED_INVADDR | REGLOCK))
            fail(0x510, value);
        if ((value & mask) == desired)
            return;
    }
    fail(0x511, mask);
}
static void write_run(u32 address, unsigned start, unsigned end) {
    /* The caller limits each contiguous run to a single 1-KiB region,
     * which never crosses the 8-KiB flash-page boundary. */
    mem_write(MSC + ADDRB, address);
    if (mem_read(MSC + STATUS) & 4)
        fail(0x512, address);
    for (unsigned j = start; j < end; j++) {
        if (j != start)
            msc_wait(WDATAREADY, WDATAREADY);
        mem_write(MSC + WDATA, BUFFER[j]);
    }
    mem_write(MSC + WRITECMD, WRITEEND);
    msc_wait(BUSY_PENDING, 0);
    msc_wait(BUSY_PENDING, 0);
}
static void chunk(u32 address, u32 count, u32 total) {
    if (!count || count > 1024 || count % 4 || address >= total || count > total - address ||
        (address >> 10) != ((address + count - 1) >> 10))
        fail(0x513, address);
    serial_word(0x214d4750); /* PGM! */
    serial_word(address);
    serial_word(count);
    u32 crc = BLANK;
    for (u32 i = 0; i < count; i++) {
        unsigned char c = serial_receive();
        ((unsigned char *)BUFFER)[i] = c;
        crc = crc_byte(crc, c);
    }
    if (receive_crc() != ~crc)
        fail(0x514, address);
    unsigned words = count / 4;
    /* Resolve all collisions in this block before programming any of it. */
    for (unsigned i = 0; i < words; i++) {
        u32 old = mem_read(address + 4 * i);
        if (old != BLANK && old != BUFFER[i])
            fail(0x515, address + 4 * i);
    }
    unsigned first = 0;
    for (unsigned i = 0; i <= words; i++) {
        if (i < words && mem_read(address + i * 4) != BUFFER[i])
            continue;
        if (first < i)
            write_run(address + first * 4, first, i);
        first = i + 1;
    }
    for (unsigned i = 0; i < words; i++)
        if (mem_read(address + 4 * i) != BUFFER[i])
            fail(0x516, address + 4 * i);
}
void run(void) {
    while (serial_receive() != 75) {
    }
    u32 total = REG(ARG);
    if (REG(ARG + 4) != 0x21bb004e || total < 8 || total > MAIN_BYTES || total % 4)
        fail(0x500, total);
    connect_swd(0, 0x22000002);
    u32 part = mem_read(0x0fe08004);
    if (part != 0x01150014 || mem_read(0x0fe0800c) != 0x00400300)
        fail(0x501, part);
    mem_write(DHCSR, 0xa05f0003);
    wait_reg(DHCSR, 1u << 17, 1u << 17);
    u32 was_locked = mem_read(MSC + STATUS) & REGLOCK;
    mem_write(MSC + LOCK, 0x1b71);
    mem_write(MSC + 0x1000 + WRITECTRL, WREN);
    if (!(mem_read(MSC + WRITECTRL) & WREN))
        fail(0x502, mem_read(MSC + STATUS));
    for (u32 pos = 8; pos < total;) {
        u32 count = 1024 - (pos & 1023);
        if (count > total - pos)
            count = total - pos;
        chunk(pos, count, total);
        pos += count;
    }
    chunk(0, 8, total); /* Activate vectors only after all payload blocks verify. */
    mem_write(MSC + 0x2000 + WRITECTRL, WREN);
    if (was_locked)
        mem_write(MSC + LOCK, 0);
    serial_word(0x21444e45);
    /* Keep Cortex-M halted for independent host readback. */
    OE_CLEAR = CLK | DIO;
    for (;;) {
    }
}

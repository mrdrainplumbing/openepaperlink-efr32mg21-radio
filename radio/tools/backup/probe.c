#include "swd.h"
void run(void) {
    while (!(REG(0x6000001c) & 1023) || (REG(0x60000000) & 255) != 75) {
    }
    u32 start = REG(ARG), size = REG(ARG + 4), csw = REG(ARG + 8);
    unsigned select_ap = REG(ARG + 12);
    connect_swd(select_ap, csw);
    for (u32 pos = 0; pos < size;) {
        u32 address = start + pos;
        unsigned count = 1024 - (address & 1023);
        if (count > size - pos)
            count = size - pos;
        transfer(1, 0, 4, address);
        transfer(1, 1, 12, 0);
        for (unsigned i = 0; i < count / 4; i++)
            BUFFER[i] = (i + 1 == count / 4) ? transfer(0, 1, 12, 0) : transfer(1, 1, 12, 0);
        serial_word(0x214b4c42); /* BLK! */
        serial_word(address);
        serial_word(count);
        u32 crc = 0xffffffff;
        for (unsigned i = 0; i < count; i++) {
            unsigned b = ((unsigned char *)BUFFER)[i];
            serial_byte(b);
            crc ^= b;
            for (int bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320);
        }
        serial_word(~crc);
        pos += count;
    }
    transfer(0, 0, 4, 0);
    OE_CLEAR = CLK | DIO;
    serial_word(0x21444e45); /* END! */
    for (;;) {
    }
}

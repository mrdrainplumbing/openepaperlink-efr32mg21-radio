#pragma once
/* ESP32-S3 RAM image. Only writes debug-port control/address registers on target. */
typedef unsigned int u32;
#define REG(address) (*(volatile u32 *)(address))
#define GPIO 0x60004000u
#define CLK (1u << 16)
#define DIO (1u << 20)
#define ARG 0x3fca0000u
#define BUFFER ((u32 *)(ARG + 0x100))
#define INPUT (REG(GPIO + 0x3c))
#define OE_SET REG(GPIO + 0x24)
#define OE_CLEAR REG(GPIO + 0x28)
#define HIGH REG(GPIO + 8)
#define LOW REG(GPIO + 12)

static void clock_bit(void) {
    HIGH = CLK;
    __asm__ volatile("memw; nop; nop; nop; nop");
    LOW = CLK;
    __asm__ volatile("memw; nop; nop; nop; nop");
}
static void send_bits(u32 data, unsigned count) {
    while (count--) {
        if (data & 1)
            HIGH = DIO;
        else
            LOW = DIO;
        clock_bit();
        data >>= 1;
    }
}
static u32 read_bits(unsigned count) {
    u32 data = 0;
    for (unsigned i = 0; i < count; i++) {
        if (INPUT & DIO)
            data |= 1u << i;
        clock_bit();
    }
    return data;
}
static void serial_byte(unsigned x) {
    while (((REG(0x6000001c) >> 16) & 1023) >= 96) {
    }
    REG(0x60000000) = x & 255;
}
static void serial_word(u32 x) {
    for (int i = 0; i < 4; i++, x >>= 8)
        serial_byte(x);
}
static void fail(u32 reason, u32 detail) {
    OE_CLEAR = CLK | DIO;
    serial_word(0x21646142); /* Bad! */
    serial_word(reason);
    serial_word(detail);
    for (;;) {
    }
}
static u32 parity(u32 word) {
    word ^= word >> 16;
    word ^= word >> 8;
    word ^= word >> 4;
    return (0x6996 >> (word & 15)) & 1;
}
static void swd_start(void) {
    HIGH = DIO;
    LOW = CLK;
    OE_SET = CLK | DIO;
    for (int i = 0; i < 60; i++)
        clock_bit();
    send_bits(0xe79e, 16);
    HIGH = DIO;
    for (int i = 0; i < 60; i++)
        clock_bit();
    send_bits(0, 8);
}
static u32 transfer(unsigned ap, unsigned read, unsigned addr, u32 data) {
    unsigned fields = ap | (read << 1) | (addr & 12);
    unsigned req = 0x81 | (fields << 1) | (parity(fields) << 5);
    for (unsigned attempt = 0; attempt < 64; attempt++) {
        send_bits(req, 8);
        OE_CLEAR = DIO;
        clock_bit();
        unsigned ack = read_bits(3);
        if (ack != 1) {
            clock_bit();
            LOW = DIO;
            OE_SET = DIO;
            send_bits(0, 8);
            if (ack == 2)
                continue;
            fail(req, ack);
        }
        if (read) {
            data = read_bits(32);
            unsigned actual = read_bits(1);
            clock_bit();
            LOW = DIO;
            OE_SET = DIO;
            if (actual != parity(data))
                fail(0x100 | req, data);
        } else {
            clock_bit();
            LOW = DIO;
            OE_SET = DIO;
            send_bits(data, 32);
            send_bits(parity(data), 1);
        }
        send_bits(0, 8);
        return data;
    }
    fail(0x200 | req, 2);
    return 0;
}
static void connect_swd(unsigned select_ap, u32 csw) {
    REG(0x60038018) &= ~(1u << 14);
    OE_CLEAR = CLK | DIO;
    REG(0x60009044) = 0x1a00;
    REG(0x60009054) = 0x1b00;
    REG(0x60004594) = REG(0x600045a4) = 0x500;
    swd_start();
    u32 id = transfer(0, 1, 0, 0);
    if (id != 0x6ba02477)
        fail(0x300, id);
    transfer(0, 0, 0, 0x1e);
    transfer(0, 0, 4, 0x50000000);
    int countdown = 100;
    while ((transfer(0, 1, 4, 0) & 0xa0000000) != 0xa0000000) {
        if (--countdown == 0)
            fail(0x301, 0);
    }
    transfer(0, 0, 8, select_ap << 24);
    transfer(1, 0, 0, csw);
}

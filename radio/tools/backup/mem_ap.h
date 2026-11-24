#pragma once
#include "swd.h"
static u32 mem_read(u32 addr) {
    transfer(1, 0, 4, addr);
    transfer(1, 1, 12, 0);
    return transfer(0, 1, 12, 0);
}
static void mem_write(u32 addr, u32 value) {
    transfer(1, 0, 4, addr);
    transfer(1, 0, 12, value);
    transfer(0, 1, 12, 0); /* Wait for posted write completion. */
}
static u32 wait_reg(u32 addr, u32 mask, u32 value) {
    u32 actual = 0;
    for (int i = 0; i < 512; i++) {
        actual = mem_read(addr);
        if ((actual & mask) == value)
            return actual;
    }
    fail(addr, actual);
    return 0;
}

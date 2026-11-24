#pragma once
#include <stdint.h>

void board_uart_init(void);
void board_uart_write(const uint8_t *data, unsigned length);
int board_uart_read(void);

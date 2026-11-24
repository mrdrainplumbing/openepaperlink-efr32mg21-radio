#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Foreground entry points. Pass PSDUs without a PHY-length byte or FCS. */
void oepl_init(const uint8_t eui64[8]);
void oepl_uart_byte(uint8_t byte);
void oepl_rx_frame(const uint8_t *psdu, unsigned length, int8_t rssi, uint8_t lqi);
void oepl_tick(void);

/* Board implementation. TX must copy/consume the frame before returning. */
void board_uart_write(const uint8_t *data, unsigned length);
bool board_radio_set_channel(uint8_t channel);
bool board_radio_tx(const uint8_t *psdu, unsigned length);
uint32_t board_millis(void);

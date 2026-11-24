#include "efr32mg21a020f768im32.h"
#include "board_uart.h"

void board_uart_init(void)
{
    CMU->SYSCLKCTRL = CMU_SYSCLKCTRL_CLKSEL_FSRCO | CMU_SYSCLKCTRL_HCLKPRESC_DIV1
                      | CMU_SYSCLKCTRL_PCLKPRESC_DIV1;
    GPIO->P[1].DOUT |= 2U;
    GPIO->P[1].MODEL = (GPIO->P[1].MODEL & ~0xffU) | GPIO_P_MODEL_MODE0_INPUT
                       | GPIO_P_MODEL_MODE1_PUSHPULL;
    GPIO->USARTROUTE[0].RXROUTE = 1U; /* port B, pin 0 */
    GPIO->USARTROUTE[0].TXROUTE = 1U | (1U << _GPIO_USART_TXROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[0].ROUTEEN = GPIO_USART_ROUTEEN_RXPEN | GPIO_USART_ROUTEEN_TXPEN;
    USART0->EN = USART_EN_EN;
    USART0->CMD = USART_CMD_RXDIS | USART_CMD_TXDIS | USART_CMD_CLEARRX | USART_CMD_CLEARTX;
    USART0->CTRL = USART_CTRL_OVS_X16;
    USART0->FRAME = USART_FRAME_DATABITS_EIGHT | USART_FRAME_STOPBITS_ONE;
    /* From 20 MHz FSRCO, nearest representable divider in units of 1/32. */
    const uint32_t units = (20000000U * 2U + 115200U / 2U) / 115200U;
    USART0->CLKDIV = (units - 32U) * 8U;
    USART0->CMD = USART_CMD_RXEN | USART_CMD_TXEN;
}

void board_uart_write(const uint8_t *data, unsigned length)
{
    for (unsigned i = 0; i < length; i++) {
        while (!(USART0->STATUS & USART_STATUS_TXBL)) {}
        USART0->TXDATA = data[i];
    }
}

int board_uart_read(void)
{
    if (!(USART0->STATUS & USART_STATUS_RXDATAV)) return -1;
    return USART0->RXDATA & 255U;
}

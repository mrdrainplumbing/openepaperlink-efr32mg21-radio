/* Diagnostic application for EFR32MG21A020F768IM32. No RF or flash operations. */
#include "efr32mg21a020f768im32.h"
#include "board_uart.h"

extern unsigned char stack_top;
void start(void);
static void fault(void);

__attribute__((used, section(".vectors")))
const uintptr_t vectors[16] = {
    (uintptr_t)&stack_top, (uintptr_t)start,
    [2 ... 15] = (uintptr_t)fault
};

struct diagnostics {
    uint32_t stage;
    uint32_t received;
    uint32_t last_four;
    uint32_t answers;
};
__attribute__((used, section(".diag"))) volatile struct diagnostics status;

static void fault(void)
{
    status.stage = 0xFA170000 | __get_IPSR();
    for (;;) {}
}

static void send_text(const char *s)
{
    while (*s) {
        while (!(USART0->STATUS & USART_STATUS_TXBL)) {}
        USART0->TXDATA = (uint8_t)*s++;
    }
}

static void send_hex(uint32_t word)
{
    char result[9];
    for (int i = 7; i >= 0; --i) {
        result[i] = "0123456789ABCDEF"[word & 15U];
        word >>= 4;
    }
    result[8] = 0;
    send_text(result);
}

void start(void)
{
    __disable_irq();
    SCB->VTOR = (uintptr_t)vectors;
    __DSB();
    __ISB();
    status.stage = 1;
    status.received = status.last_four = status.answers = 0;
    board_uart_init();
    status.stage = 3;
    send_text("MG21-UART\r\n");
    status.stage = 4;
    while (1) {
        if (!(USART0->STATUS & USART_STATUS_RXDATAV)) continue;
        status.last_four = (status.last_four << 8) | (USART0->RXDATA & 255U);
        ++status.received;
        switch (status.last_four) {
        case 0x5244593f: /* RDY? */
            send_text("ACK>");
            ++status.answers;
            break;
        case 0x4e464f3f: /* NFO? */
            /* FE is diagnostic-only. Do not identify as a supported C6 radio. */
            send_text("ACK>TYP>FEVER>0001MAC>");
            send_hex(DEVINFO->EUI64H);
            send_hex(DEVINFO->EUI64L);
            send_text("CAP>0000");
            ++status.answers;
            break;
        }
    }
}

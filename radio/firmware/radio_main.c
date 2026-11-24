#include "efr32mg21a020f768im32.h"
#include "board_uart.h"
#include "oepl.h"

extern uint8_t stack_top, bss_begin, bss_end, data_begin, data_end, data_load;
extern bool radio_begin(void);
extern void radio_poll(void);
static volatile uint32_t milliseconds;
__attribute__((section(".diag"))) volatile uint32_t radio_stage;
__attribute__((section(".diag"))) volatile uint32_t radio_rx_count;
__attribute__((section(".diag"))) volatile uint32_t radio_tx_count;
void start(void);
static void fault(void) { radio_stage = 0xFA170000 | __get_IPSR(); for (;;) {} }
static void tick(void) { ++milliseconds; }
uint32_t board_millis(void) { return milliseconds; }
#define RADIO_IRQS(F) F(AGC) F(BUFC) F(FRC_PRI) F(FRC) F(MODEM) F(PROTIMER) F(RAC_RSM) F(RAC_SEQ) F(PRORTC) F(SYNTH) F(EMUDG)
#define DECLARE(name) extern void name##_IRQHandler(void);
RADIO_IRQS(DECLARE)
#define VECTOR(name) [16 + name##_IRQn] = (uintptr_t)name##_IRQHandler,
__attribute__((section(".vectors"), used))
const uintptr_t vectors[16 + 61] = {
    (uintptr_t)&stack_top, (uintptr_t)start,
    [2 ... 14] = (uintptr_t)fault, [15] = (uintptr_t)tick,
    [16 + SMU_SECURE_IRQn] = (uintptr_t)fault,
    RADIO_IRQS(VECTOR)
};

void start(void)
{
    __disable_irq();
    for (uint8_t *p=&bss_begin; p < &bss_end; ++p) *p=0;
    if ((uintptr_t)&data_load != (uintptr_t)&data_begin) {
        const uint8_t *src=&data_load;
        for (uint8_t *p=&data_begin; p < &data_end; ++p) *p=*src++;
    }
    radio_stage=1;
    radio_tx_count=radio_rx_count=0;
    /* The GSDK single-protocol library uses the standard peripheral aliases.
     * Mirror the vendor's single-image SystemInit attribution policy. */
    SMU->PPUSATD0_CLR = _SMU_PPUSATD0_MASK;
    SMU->PPUSATD1_CLR = _SMU_PPUSATD1_MASK & ~(SMU_PPUSATD1_SMU | SMU_PPUSATD1_SEMAILBOX);
    SAU->CTRL = SAU_CTRL_ALLNS_Msk;
    SCB->VTOR = (uintptr_t)vectors;
    SCB->CPACR |= 0xFU << 20;
    __DSB();
    __ISB();
    board_uart_init();
    SysTick_Config(20000000U / 1000U);
    __enable_irq();
    radio_stage=2;
    HFXO0->CTRL_SET = HFXO_CTRL_FORCEEN;
    uint32_t t = board_millis();
    while (!(HFXO0->STATUS & HFXO_STATUS_RDY)) {
        if ((uint32_t)(board_millis()-t) > 150) { radio_stage=0xE002; for (;;) {} }
    }
    CMU->RADIOCLKCTRL_SET = CMU_RADIOCLKCTRL_EN;
    radio_stage=3;
    if (!radio_begin()) { radio_stage |= 0xE000; for (;;) {} }
    uint8_t mac[8];
    uint32_t lo = DEVINFO->EUI64L, hi = DEVINFO->EUI64H;
    for (unsigned i=0; i<4; ++i) {
        mac[i] = lo >> (8*i);
        mac[i+4] = hi >> (8*i);
    }
    oepl_init(mac);
    while (1) {
        int ch = board_uart_read();
        if (ch >= 0) oepl_uart_byte((uint8_t)ch);
        radio_poll();
        oepl_tick();
    }
}

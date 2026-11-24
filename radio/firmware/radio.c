#include "board_uart.h"
#include "oepl.h"
#include "rail.h"
#include "rail_ieee802154.h"

#ifdef MG21_RF_CAPTURE
void capture_rx(const uint8_t *psdu, unsigned length, uint8_t channel,
                int8_t rssi, uint8_t lqi, uint32_t time_us, uint8_t flags);
#endif

static RAIL_Handle_t handle;
volatile uint32_t radio_channel;
volatile uint32_t radio_tx_errors;
static volatile bool tx_active;
static volatile bool calibrate;
static uint8_t tx_fifo[128] __attribute__((aligned(4)));
extern volatile uint32_t radio_stage;
extern volatile uint32_t radio_rx_count;
extern volatile uint32_t radio_tx_count;

void RAILCb_AssertFailed(RAIL_Handle_t rail, RAIL_AssertErrorCodes_t code)
{
    (void)rail;
    radio_stage = 0xAA000000U | code;
    for (;;) {}
}

static void event(RAIL_Handle_t rail, RAIL_Events_t events)
{
    if (events & RAIL_EVENT_RX_PACKET_RECEIVED) (void)RAIL_HoldRxPacket(rail);
    if (events & RAIL_EVENTS_TX_COMPLETION) {
        if (events & RAIL_EVENT_TX_PACKET_SENT) ++radio_tx_count;
        else ++radio_tx_errors;
        tx_active = false;
    }
    if (events & RAIL_EVENT_CAL_NEEDED) calibrate = true;
}

bool board_radio_set_channel(uint8_t next)
{
    if (next < 11 || next > 26 || tx_active) return false;
    if (RAIL_StartRx(handle, next, NULL) != RAIL_STATUS_NO_ERROR) return false;
    radio_channel = next;
    return true;
}

bool board_radio_tx(const uint8_t *psdu, unsigned length)
{
    if (tx_active || !length || length > 125) return false;
    uint8_t phr = length + 2; /* RAIL appends the two FCS bytes. */
    if (RAIL_WriteTxFifo(handle, &phr, 1, true) != 1 ||
        RAIL_WriteTxFifo(handle, psdu, length, false) != length) return false;
    tx_active = true;
    if (RAIL_StartTx(handle, radio_channel, RAIL_TX_OPTIONS_DEFAULT, NULL) != RAIL_STATUS_NO_ERROR) {
        tx_active = false;
        ++radio_tx_errors;
        return false;
    }
    return true;
}

void radio_poll(void)
{
    if (calibrate && !tx_active) {
        calibrate = false;
        if (RAIL_Calibrate(handle, NULL, RAIL_CAL_ALL_PENDING) != RAIL_STATUS_NO_ERROR)
            calibrate = true;
    }
    RAIL_RxPacketInfo_t info;
    RAIL_RxPacketHandle_t packet = RAIL_GetRxPacketInfo(handle, RAIL_RX_PACKET_HANDLE_OLDEST_COMPLETE, &info);
    if (packet == RAIL_RX_PACKET_HANDLE_INVALID) return;
    RAIL_RxPacketDetails_t detail;
    uint8_t bytes[128];
    bool valid = info.packetStatus == RAIL_RX_PACKET_READY_SUCCESS &&
                 info.packetBytes >= 2 && info.packetBytes <= sizeof(bytes) &&
                 RAIL_GetRxPacketDetailsAlt(handle, packet, &detail) == RAIL_STATUS_NO_ERROR;
    if (valid) RAIL_CopyRxPacket(bytes, &info);
    RAIL_ReleaseRxPacket(handle, packet);
#ifdef MG21_RF_CAPTURE
    /* This build retains FCS in the RAIL FIFO. PHR counts all PSDU bytes. */
    if (valid && bytes[0] + 1U == info.packetBytes) {
        ++radio_rx_count;
        detail.timeReceived.totalPacketBytes = info.packetBytes;
        bool exact = RAIL_GetRxTimeFrameEndAlt(handle, &detail) == RAIL_STATUS_NO_ERROR;
        capture_rx(bytes + 1, bytes[0], detail.channel, detail.rssi, detail.lqi,
                   exact ? detail.timeReceived.packetTime : RAIL_GetTime(), exact ? 1U : 0U);
    }
#else
    if (valid && bytes[0] == info.packetBytes + 1U) {
        ++radio_rx_count;
        oepl_rx_frame(bytes + 1, info.packetBytes - 1U, detail.rssi, detail.lqi);
    }
#endif
}

bool radio_begin(void)
{
    static RAIL_Config_t config = { .eventsCallback = event };
    const RAIL_StateTransitions_t back_to_rx = { .success = RAIL_RF_STATE_RX, .error = RAIL_RF_STATE_RX };
    const RAIL_IEEE802154_Config_t mac = {
        .ackConfig = {.enable = false, .ackTimeout = 672,
                      .rxTransitions = {.success=RAIL_RF_STATE_RX, .error=RAIL_RF_STATE_IDLE},
                      .txTransitions = {.success=RAIL_RF_STATE_RX, .error=RAIL_RF_STATE_IDLE}},
        .timings = {.idleToRx=100, .idleToTx=100, .rxToTx=192, .txToRx=182},
#if defined(MG21_RF_SCAN) || defined(MG21_RF_CAPTURE)
        .framesMask = RAIL_IEEE802154_ACCEPT_STANDARD_FRAMES,
#else
        .framesMask = RAIL_IEEE802154_ACCEPT_DATA_FRAMES,
#endif
        .promiscuousMode = true,
        .isPanCoordinator = true,
    };
    const RAIL_TxPowerConfig_t pa = {.mode=RAIL_TX_POWER_MODE_2P4GIG_LP, .voltage=3300, .rampTime=10};
    radio_stage = 10;
    handle = RAIL_Init(&config, NULL);
    if (handle == NULL) return false;
    radio_stage = 11;
    if (RAIL_SetTxFifo(handle, tx_fifo, 0, sizeof(tx_fifo)) != sizeof(tx_fifo)) return false;
    if (RAIL_ConfigCal(handle, RAIL_CAL_ALL) != RAIL_STATUS_NO_ERROR) return false;
    radio_stage = 12;
    if (RAIL_IEEE802154_Init(handle, &mac) != RAIL_STATUS_NO_ERROR ||
        RAIL_IEEE802154_Config2p4GHzRadio(handle) != RAIL_STATUS_NO_ERROR) return false;
#ifdef MG21_RF_CAPTURE
    if (RAIL_ConfigRxOptions(handle, RAIL_RX_OPTION_STORE_CRC | RAIL_RX_OPTION_IGNORE_CRC_ERRORS,
                             RAIL_RX_OPTION_STORE_CRC) != RAIL_STATUS_NO_ERROR) return false;
#endif
    radio_stage = 13;
    if (RAIL_ConfigTxPower(handle, &pa) != RAIL_STATUS_NO_ERROR ||
        RAIL_SetTxPower(handle, RAIL_TX_POWER_LEVEL_2P4_LP_MAX) != RAIL_STATUS_NO_ERROR) return false;
    if (RAIL_SetRxTransitions(handle, &back_to_rx) != RAIL_STATUS_NO_ERROR ||
        RAIL_SetTxTransitions(handle, &back_to_rx) != RAIL_STATUS_NO_ERROR) return false;
    radio_stage = 14;
    if (RAIL_ConfigEvents(handle, RAIL_EVENTS_ALL, RAIL_EVENT_RX_PACKET_RECEIVED |
                          RAIL_EVENTS_TX_COMPLETION | RAIL_EVENT_CAL_NEEDED) != RAIL_STATUS_NO_ERROR) return false;
    if (!board_radio_set_channel(25)) return false;
    uint32_t now = board_millis();
    while (!(RAIL_GetRadioState(handle) & RAIL_RF_STATE_RX)) {
        if ((uint32_t)(board_millis() - now) >= 100) return false;
    }
    radio_stage = 15;
    return true;
}

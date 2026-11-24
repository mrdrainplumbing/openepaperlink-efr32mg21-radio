/* One short 2.4 GHz IEEE 802.15.4 discovery scan. No joins or application data. */
#include "oepl.h"

struct channel_result {
    uint32_t received;
    uint32_t beacons;
    uint32_t request_sent;
    int32_t strongest_rssi;
};

struct scan_results {
    uint32_t started;
    uint32_t finished;
    uint32_t channel;
    uint32_t channel_errors;
    struct channel_result result[16];
};

/* Read this symbol via SWD after finished becomes one. */
volatile struct scan_results rf_scan_results;
static uint32_t since;
static uint8_t channel;
static bool request_attempted;

static void tune(void) {
    if (!board_radio_set_channel(channel)) {
        rf_scan_results.channel_errors++;
    }
    rf_scan_results.channel = channel;
    rf_scan_results.result[channel - 11].strongest_rssi = -128;
    request_attempted = false;
    since = board_millis();
}

void oepl_init(const uint8_t eui64[8]) {
    (void)eui64;
    channel = 11;
    tune();
    rf_scan_results.started = 1;
}

void oepl_uart_byte(uint8_t byte) { (void)byte; }

void oepl_rx_frame(const uint8_t *psdu, unsigned length, int8_t rssi, uint8_t lqi) {
    (void)lqi;
    if (!rf_scan_results.started || rf_scan_results.finished || length < 3) return;
    volatile struct channel_result *r = &rf_scan_results.result[channel - 11];
    r->received++;
    if ((psdu[0] & 7U) == 0) r->beacons++;
    if (rssi > r->strongest_rssi) r->strongest_rssi = rssi;
}

void oepl_tick(void) {
    if (!rf_scan_results.started || rf_scan_results.finished) return;
    uint32_t elapsed = board_millis() - since;
    if (elapsed >= 100 && !request_attempted) {
        /* Beacon request to broadcast PAN and address, no source address. */
        uint8_t frame[] = {0x03,0x08,channel,0xff,0xff,0xff,0xff,0x07};
        rf_scan_results.result[channel - 11].request_sent = board_radio_tx(frame, sizeof frame);
        request_attempted = true;
    }
    if (elapsed >= 750) {
        if (channel == 26) {
            rf_scan_results.finished = 1;
        } else {
            channel++;
            tune();
        }
    }
}

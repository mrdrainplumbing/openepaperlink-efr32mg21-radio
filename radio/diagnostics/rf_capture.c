/* Bounded IEEE 802.15.4 radio diagnostic. Inspect rf_capture only after finished=1. */
#include "oepl.h"
#include <stddef.h>

enum { SWEEPS = 3, CHANNELS = 16, BUCKETS = SWEEPS * CHANNELS, FRAME_CAPACITY = 144 };

struct capture_frame {
    uint32_t time_us;
    uint8_t channel;
    int8_t rssi_dbm;
    uint8_t lqi;
    uint8_t length;
    uint8_t psdu[127];
    uint8_t flags;
};

struct capture_buffer {
    uint32_t magic, version, finished, sweep, channel, tune_errors, requests, stride;
    uint32_t seen[BUCKETS];
    struct capture_frame frames[FRAME_CAPACITY];
};

_Static_assert(sizeof(struct capture_frame) == 136, "decoder stride");
_Static_assert(offsetof(struct capture_buffer, frames) == 224, "decoder layout");
_Static_assert(sizeof(struct capture_buffer) == 19808, "decoder size");
volatile struct capture_buffer rf_capture;
static uint32_t entered;
static bool attempted, tuned;
static uint8_t desired;
static uint8_t stored;

static void dwell(uint8_t channel)
{
    desired = channel;
    attempted = false;
    tuned = board_radio_set_channel(channel);
    if (!tuned) ++rf_capture.tune_errors;
    rf_capture.channel = tuned ? channel : 0;
    entered = board_millis();
}

void oepl_init(const uint8_t eui64[8])
{
    (void)eui64;
    rf_capture.magic = 0x31464352;
    rf_capture.version = 2;
    rf_capture.stride = sizeof(struct capture_frame);
    dwell(11);
}

void oepl_uart_byte(uint8_t value) { (void)value; }

/* Invoked in the foreground from radio_poll, with the channel for this packet. */
void capture_rx(const uint8_t *psdu, unsigned length, uint8_t channel,
                int8_t rssi, uint8_t lqi, uint32_t time_us, uint8_t flags)
{
    if (!tuned || rf_capture.finished || channel != desired || length > 127 || length < 2)
        return;
    unsigned bucket = rf_capture.sweep * CHANNELS + channel - 11;
    if (stored < FRAME_CAPACITY) {
        volatile struct capture_frame *record = &rf_capture.frames[stored++];
        record->time_us = time_us;
        record->channel = channel;
        record->rssi_dbm = rssi;
        record->lqi = lqi;
        record->length = length;
        record->flags = flags;
        for (unsigned i=0; i<length; ++i) record->psdu[i] = psdu[i];
    }
    ++rf_capture.seen[bucket];
}

void oepl_tick(void)
{
    if (rf_capture.finished) return;
    uint32_t elapsed = board_millis() - entered;
    if (tuned && !attempted && elapsed >= 100) {
        unsigned sequence = rf_capture.sweep * CHANNELS + desired - 11;
        uint8_t frame[] = { 0x03, 0x08, sequence, 0xff, 0xff, 0xff, 0xff, 0x07 };
        if (board_radio_tx(frame, sizeof(frame))) ++rf_capture.requests;
        attempted = true;
    }
    if (elapsed < 750) return;
    if (desired != 26) {
        dwell(desired + 1);
    } else if (rf_capture.sweep + 1 < SWEEPS) {
        ++rf_capture.sweep;
        dwell(11);
    } else {
        rf_capture.finished = 1;
    }
}

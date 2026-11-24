/* SPDX-License-Identifier: CC-BY-NC-SA-4.0. See NOTICE and LICENSE. */
#include "oepl.h"
#include "proto.h"

#define TAG_SLOTS 20
#define CHANNEL_DEFAULT 25
#define BLOCK_BYTES (4u + BLOCK_DATA_SIZE)
#define BURST_COUNT BLOCK_MAX_PARTS

static uint8_t self[8], sequence, channel;
static struct pendingData slots[TAG_SLOTS];
static uint32_t last_minute;
static enum { HEADER, PENDING, CANCEL, CONFIG, BLOCK } parser;
static uint8_t header[4], input[sizeof(struct pendingData)];
static unsigned position;
static uint32_t last_byte;
static struct {
    bool in_use, expecting, valid, sending;
    uint8_t tag[8];
    struct blockRequest request;
    uint32_t received_at, requested_at, due_at;
    unsigned next_part, sent;
    uint8_t data[BLOCK_MAX_PARTS * BLOCK_PART_DATA_SIZE];
} block;

static void copy(void *dst, const void *src, unsigned n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}
static bool equal(const void *a, const void *b, unsigned n) {
    const uint8_t *p = a, *q = b;
    while (n--) if (*p++ != *q++) return false;
    return true;
}
static uint8_t checksum(const void *buffer, unsigned length) {
    const uint8_t *p = buffer;
    uint8_t sum = 0;
    for (unsigned i = 1; i < length; ++i) sum += p[i];
    return sum;
}
static int find_slot(const uint8_t *tag) {
    for (unsigned i = 0; i < TAG_SLOTS; ++i)
        if (slots[i].attemptsLeft && equal(slots[i].targetMac, tag, 8)) return (int)i;
    return -1;
}
static void uart_text(const char *s, unsigned n) {
    board_uart_write((const uint8_t *)s, n);
}
#define UART(s) uart_text((s), sizeof(s) - 1)
static void hex_byte(uint8_t byte) {
    static const uint8_t digits[] = "0123456789ABCDEF";
    uint8_t data[2] = {digits[byte >> 4], digits[byte & 15]};
    board_uart_write(data, 2);
}
static void info(void) {
    unsigned pending = 0, no_update = 0;
    for (unsigned i = 0; i < TAG_SLOTS; ++i)
        if (slots[i].attemptsLeft) {
            if (slots[i].availdatainfo.dataType) ++pending;
            else ++no_update;
        }
    /* Project-local radio type; keeps the S3 at the 115200 legacy link speed. */
    UART("TYP>A1VER>0001MAC>");
    for (unsigned i = 0; i < 8; ++i) hex_byte(self[i]);
    UART("ZCH>"); hex_byte(channel);
    UART("PEN>"); hex_byte(pending);
    UART("NOP>"); hex_byte(no_update);
}
static void notify_tag(const char type[4], const uint8_t *tag) {
    struct espXferComplete ev;
    copy(ev.src, tag, 8);
    ev.checksum = checksum(&ev, sizeof ev);
    uart_text(type, 4);
    board_uart_write((const uint8_t *)&ev, sizeof ev);
}
static void invalidate_tag(const uint8_t *tag) {
    if (block.in_use && equal(block.tag, tag, 8)) {
        block.in_use = block.valid = block.expecting = block.sending = false;
    }
}
static bool validate_block(void) {
    unsigned n = block.data[0] | ((unsigned)block.data[1] << 8);
    unsigned want = block.data[2] | ((unsigned)block.data[3] << 8);
    if (!n || n > BLOCK_DATA_SIZE) return false;
    uint16_t sum = 0;
    for (unsigned i = 0; i < n; ++i) sum += block.data[4 + i];
    return sum == want;
}
static void queue_command(void) {
    struct pendingData pd;
    copy(&pd, input, sizeof pd);
    if (pd.availdatainfo.checksum != checksum(&pd, sizeof pd)) { UART("NOK>"); return; }
    int slot = find_slot(pd.targetMac);
    if (parser == CANCEL) {
        if (slot >= 0) slots[slot].attemptsLeft = 0;
        invalidate_tag(pd.targetMac);
    } else {
        if (slot < 0) {
            for (unsigned i = 0; i < TAG_SLOTS; ++i)
                if (!slots[i].attemptsLeft) { slot = (int)i; break; }
        }
        if (slot < 0) { UART("NOQ>"); return; }
        if (slots[slot].availdatainfo.dataVer != pd.availdatainfo.dataVer)
            invalidate_tag(pd.targetMac);
        slots[slot] = pd;
    }
    UART("ACK>");
}
void oepl_uart_byte(uint8_t byte) {
    uint32_t now = board_millis();
    if (parser != HEADER && now - last_byte > 1000) parser = HEADER;
    last_byte = now;
    if (parser == BLOCK) {
        block.data[position++] = byte ^ 0xaa;
        if (position == BLOCK_BYTES) {
            block.valid = block.expecting && validate_block();
            block.expecting = false;
            parser = HEADER;
        }
        return;
    }
    if (parser != HEADER) {
        input[position++] = byte;
        if (parser == CONFIG) {
            if (position < sizeof(struct espSetChannelPower)) return;
            if (input[0] == checksum(input, position) && input[1] >= 11 &&
                input[1] <= 26 && board_radio_set_channel(input[1])) {
                channel = input[1];
                UART("ACK>");
            } else UART("NOK>");
        } else {
            if (position < sizeof(struct pendingData)) return;
            queue_command();
        }
        parser = HEADER;
        return;
    }
    for (unsigned i = 0; i < 3; ++i) header[i] = header[i+1];
    header[3] = byte;
    position = 0;
    if (equal(header, "RDY?", 4) || equal(header, "RSET", 4)) { UART("ACK>"); }
    else if (equal(header, "NFO?", 4)) { UART("ACK>"); info(); }
    else if (equal(header, "SCP>", 4)) parser = CONFIG;
    else if (equal(header, "SDA>", 4)) parser = PENDING;
    else if (equal(header, "CXD>", 4)) parser = CANCEL;
    else if (equal(header + 1, ">D>", 3)) {
        if (block.expecting) { block.valid = false; UART("ACK>"); parser = BLOCK; }
        else UART("NOK>");
    }
}
static bool reply(const uint8_t *tag, uint8_t type, const void *payload, unsigned size) {
    uint8_t frame[RADIO_MAX_PACKET_LEN];
    struct MacFrameNormal h = {0};
    h.fcs.frameType = 1; h.fcs.panIdCompressed = 1;
    h.fcs.destAddrType = 3; h.fcs.srcAddrType = 3;
    h.pan = PROTO_PAN_ID; h.seq = sequence++;
    copy(h.dst, tag, 8); copy(h.src, self, 8);
    if (sizeof h + 1 + size > sizeof frame) return false;
    copy(frame, &h, sizeof h);
    frame[sizeof h] = type;
    copy(frame + sizeof h + 1, payload, size);
    return board_radio_tx(frame, sizeof h + 1 + size);
}
static void check_in(const uint8_t *tag, const uint8_t *payload, unsigned size) {
    struct espAvailDataReq ev = {0};
    struct AvailDataInfo available = {0};
    if (size && payload[0] != checksum(payload, size)) return;
    int slot = find_slot(tag);
    if (slot >= 0) available = slots[slot].availdatainfo;
    available.checksum = checksum(&available, sizeof available);
    reply(tag, PKT_AVAIL_DATA_INFO, &available, sizeof available);
    copy(ev.src, tag, 8); copy(&ev.adr, payload, size);
    ev.checksum = checksum(&ev, sizeof ev);
    UART("ADR>"); board_uart_write((const uint8_t *)&ev, sizeof ev);
}
static void request_block(const uint8_t *tag, const uint8_t *payload) {
    struct blockRequest req;
    copy(&req, payload, sizeof req);
    if (req.checksum != checksum(&req, sizeof req)) return;
    int slot = find_slot(tag);
    uint32_t now = board_millis();
    if (slot < 0 || slots[slot].availdatainfo.dataVer != req.ver ||
        (block.in_use && !equal(tag, block.tag, 8) && now - block.received_at < 1200)) {
        reply(tag, PKT_CANCEL_XFER, 0, 0);
        return;
    }
    bool same = block.in_use && equal(tag, block.tag, 8) &&
                req.ver == block.request.ver && req.blockId == block.request.blockId;
    /* A legacy >D> reply carries no block identity. Keep a live host request
     * paired with its original tag and block until it finishes or times out. */
    if (!same && (parser == BLOCK || (block.expecting && now - block.requested_at < 1000))) {
        reply(tag, PKT_CANCEL_XFER, 0, 0);
        return;
    }
    bool cached = same && block.valid;
    bool waiting = same && block.expecting && (parser == BLOCK || now - block.requested_at < 1000);
    struct blockRequestAck ack = {.pleaseWaitMs = cached ? 30 : 550};
    ack.checksum = checksum(&ack, sizeof ack);
    if (!reply(tag, PKT_BLOCK_REQUEST_ACK, &ack, sizeof ack)) return;
    block.in_use = block.sending = true;
    block.valid = cached;
    block.received_at = now;
    block.due_at = now + ack.pleaseWaitMs;
    block.request = req;
    copy(block.tag, tag, 8);
    block.sent = block.next_part = 0;
    bool any = false;
    for (unsigned i = 0; i < BLOCK_MAX_PARTS; ++i)
        if (req.requestedParts[i/8] & (1u << (i%8))) any = true;
    if (!any) block.request.requestedParts[0] = 1;
    if (!cached && !waiting) {
        block.expecting = true;
        block.requested_at = now;
        struct espBlockRequest ev = {.ver = req.ver, .blockId = req.blockId};
        copy(ev.src, tag, 8); ev.checksum = checksum(&ev, sizeof ev);
        UART("RQB>"); board_uart_write((const uint8_t *)&ev, sizeof ev);
    }
}
void oepl_rx_frame(const uint8_t *frame, unsigned len, int8_t rssi, uint8_t lqi) {
    /* Tag reports its downlink signal quality in AvailDataReq. */
    (void)rssi; (void)lqi;
    if (len > RADIO_MAX_PACKET_LEN || len < 2) return;
    if ((frame[0] & 0xdf) == 0x01 && frame[1] == 0xc8 && len > sizeof(struct MacFrameBcast)) {
        struct MacFrameBcast h;
        copy(&h, frame, sizeof h);
        if (h.dstPan != PROTO_PAN_ID || h.srcPan != PROTO_PAN_ID || h.dstAddr != 0xffff) return;
        const uint8_t *p = frame + sizeof h + 1;
        unsigned n = len - sizeof h - 1;
        switch (frame[sizeof h]) {
        case PKT_PING: reply(h.src, PKT_PONG, &channel, 1); break;
        case PKT_AVAIL_DATA_SHORTREQ: if (!n) check_in(h.src, p, n); break;
        case PKT_AVAIL_DATA_REQ:
            /* TLSR tag firmware includes a spare byte after these payloads. */
            if (n == sizeof(struct AvailDataReq) || n == sizeof(struct AvailDataReq)+1)
                check_in(h.src, p, sizeof(struct AvailDataReq));
            else if (n == sizeof(struct oldAvailDataReq) || n == sizeof(struct oldAvailDataReq)+1)
                check_in(h.src, p, sizeof(struct oldAvailDataReq));
            break;
        }
    } else if ((frame[0] & 0xdf) == 0x41 && frame[1] == 0xcc && len > sizeof(struct MacFrameNormal)) {
        struct MacFrameNormal h;
        copy(&h, frame, sizeof h);
        if (h.pan != PROTO_PAN_ID || !equal(h.dst, self, 8)) return;
        const uint8_t *p = frame + sizeof h + 1;
        unsigned n = len - sizeof h - 1;
        switch (frame[sizeof h]) {
        case PKT_BLOCK_REQUEST: case PKT_BLOCK_PARTIAL_REQUEST:
            if (n == sizeof(struct blockRequest) || n == sizeof(struct blockRequest)+1) request_block(h.src, p);
            break;
        case PKT_XFER_COMPLETE: {
            if (n > 1) return;
            reply(h.src, PKT_XFER_COMPLETE_ACK, 0, 0);
            int slot = find_slot(h.src);
            if (slot >= 0) {
                slots[slot].attemptsLeft = 0;
                invalidate_tag(h.src);
                notify_tag("XFC>", h.src);
            }
            break;
        }
        }
    }
}
void oepl_tick(void) {
    uint32_t now = board_millis();
    if (now - last_minute >= 60000) {
        last_minute += 60000;
        for (unsigned i = 0; i < TAG_SLOTS; ++i) {
            if (!slots[i].attemptsLeft) continue;
            if (!--slots[i].attemptsLeft) {
                invalidate_tag(slots[i].targetMac);
                if (slots[i].availdatainfo.dataType) notify_tag("XTO>", slots[i].targetMac);
            } else if (slots[i].availdatainfo.nextCheckIn) --slots[i].availdatainfo.nextCheckIn;
        }
    }
    if (!block.sending || !block.valid || (int32_t)(now - block.due_at) < 0) return;
    if (now - block.received_at > 2000) { block.sending = false; return; }
    unsigned part = block.next_part;
    while (!(block.request.requestedParts[part/8] & (1u << (part%8)))) part = (part+1) % BLOCK_MAX_PARTS;
    uint8_t payload[3 + BLOCK_PART_DATA_SIZE] = {0, block.request.blockId, part};
    copy(payload+3, block.data + part*BLOCK_PART_DATA_SIZE, BLOCK_PART_DATA_SIZE);
    payload[0] = checksum(payload, sizeof payload);
    if (reply(block.tag, PKT_BLOCK_PART, payload, sizeof payload)) {
        block.next_part = (part+1) % BLOCK_MAX_PARTS;
        if (++block.sent == BURST_COUNT) block.sending = false;
    }
}
void oepl_init(const uint8_t eui64[8]) {
    copy(self, eui64, 8);
    channel = CHANNEL_DEFAULT;
    last_minute = board_millis();
    UART("RES>RDY>");
}

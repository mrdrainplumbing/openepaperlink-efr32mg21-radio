/* SPDX-License-Identifier: CC-BY-NC-SA-4.0 */
#include "../oepl.h"
#include "../proto.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t uart[256], air[125];
static unsigned uart_len, air_len;
static bool tx_busy;
static uint32_t now;

uint32_t board_millis(void) { return now; }
bool board_radio_set_channel(uint8_t ch) { return ch == 25; }
void board_uart_write(const uint8_t *p, unsigned n) {
    assert(uart_len + n <= sizeof uart);
    memcpy(uart + uart_len, p, n); uart_len += n;
}
bool board_radio_tx(const uint8_t *p, unsigned n) {
    if (tx_busy) return false;
    assert(n <= sizeof air);
    memcpy(air, p, n); air_len = n;
    return true;
}
static void send_uart(const void *data, unsigned n) {
    const uint8_t *p = data;
    while (n--) oepl_uart_byte(*p++);
}
static void expect(const char *s) {
    assert(uart_len == strlen(s) && !memcmp(uart, s, uart_len));
    uart_len = 0;
}
static void sign(void *p, unsigned n) {
    uint8_t *v = p; v[0] = 0;
    for (unsigned i = 1; i < n; ++i) v[0] += v[i];
}
static void queue(const uint8_t tag[8], unsigned ver, const char *response) {
    struct pendingData p = {.availdatainfo = {.dataSize=4096, .dataType=0x20, .dataVer=ver}, .attemptsLeft=10};
    memcpy(p.targetMac, tag, 8);
    sign(&p, sizeof p);
    send_uart("SDA>",4);
    send_uart(&p,sizeof p);
    expect(response);
}
int main(void) {
    const uint8_t self[8] = {1,2,3,4,5,6,7,8};
    uint8_t tag[8] = {90};
    oepl_init(self); expect("RES>RDY>");
    send_uart("RDY?",4); expect("ACK>");
    send_uart("SCP>",4);
    send_uart((uint8_t[]){35,25,11,255},4); expect("ACK>");
    send_uart("NFO?",4); expect("ACK>TYP>A1VER>0001MAC>0102030405060708ZCH>19PEN>00NOP>00");

    uint8_t frame[125] = {0x21,0xc8,0,0x47,0x44,0xff,0xff,0x47,0x44};
    memcpy(frame+9,tag,8);
    frame[17] = PKT_PING;
    oepl_rx_frame(frame,18,0,0);
    assert(air_len == 23 && air[21] == PKT_PONG && air[22] == 25);
    for (unsigned i=0; i<20; ++i) { tag[1]=i; queue(tag, 0x12, "ACK>"); }
    tag[1]=20; queue(tag, 0x12, "NOQ>"); tag[1]=0;
    frame[17] = PKT_AVAIL_DATA_REQ;
    struct AvailDataReq adr = {.batteryMv=3000,.hwType=0x60};
    sign(&adr,sizeof adr); memcpy(frame+18,&adr,sizeof adr);
    oepl_rx_frame(frame,18+sizeof adr+1,0,0); /* One extra byte in TLSR firmware. */
    assert(uart_len == 4+sizeof(struct espAvailDataReq) && !memcmp(uart,"ADR>",4));
    uart_len = 0;
    struct AvailDataInfo advertised;
    memcpy(&advertised,air+22,sizeof advertised);
    assert(air[21]==PKT_AVAIL_DATA_INFO && advertised.dataType==0x20 && advertised.dataSize==4096);

    frame[0]=0x41; frame[1]=0xcc; memcpy(frame+5,self,8); memcpy(frame+13,tag,8);
    frame[21]=PKT_BLOCK_REQUEST;
    struct blockRequest req = {.ver=0x12};
    memset(req.requestedParts,0xff,sizeof req.requestedParts);
    sign(&req,sizeof req); memcpy(frame+22,&req,sizeof req);
    oepl_rx_frame(frame,22+sizeof req+1,0,0);
    assert(air[21]==PKT_BLOCK_REQUEST_ACK);
    assert(uart_len==4+sizeof(struct espBlockRequest) && !memcmp(uart,"RQB>",4)); uart_len=0;
    send_uart(">D>",3); expect("ACK>");
    uint8_t image[4100] = {0x00,0x10};
    uint16_t sum = 0;
    for (unsigned i=0; i<4096; ++i) sum += (image[4+i]=(uint8_t)(i*37+13));
    image[2]=sum; image[3]=sum>>8;
    for(unsigned i=0;i<sizeof image;++i) oepl_uart_byte(image[i]^0xaa);
    now+=551;
    tx_busy=true; oepl_tick(); tx_busy=false; /* retry same part */
    uint8_t rebuilt[42*99]={0};
    for(unsigned i=0; i<42; ++i) {
        oepl_tick();
        assert(air_len == 124 && air[21]==PKT_BLOCK_PART && air[24]==i);
        memcpy(rebuilt+i*99,air+25,99);
    }
    assert(!memcmp(rebuilt,image,sizeof image));
    frame[21]=PKT_XFER_COMPLETE;
    oepl_rx_frame(frame,23,0,0);
    assert(air[21]==PKT_XFER_COMPLETE_ACK && uart_len==4+sizeof(struct espXferComplete) && !memcmp(uart,"XFC>",4));
    uart_len=0;
    oepl_rx_frame(frame,23,0,0); assert(!uart_len);
    puts("legacy discovery, 20 slots, padded TLSR check-in, 4096-byte image, TX busy retry, and completion passed");
}

#include "icmp.h"
#include "ipv4.h"
#include "net.h"
#include "kernel.h"
#include "string.h"

volatile int      icmp_got_reply = 0;
volatile uint32_t icmp_reply_src = 0;

void icmp_send_echo(uint32_t dst_ip, uint8_t* dst_mac) {
    uint8_t buf[sizeof(icmp_header_t) + 32];
    memset(buf, 0, sizeof(buf));
    icmp_header_t* h = (icmp_header_t*)buf;
    h->type     = 8; /* Echo Request */
    h->code     = 0;
    h->id       = 0x1234;
    h->sequence = 1;
    /* fill payload */
    for (int i = 0; i < 32; i++) buf[sizeof(icmp_header_t) + i] = (uint8_t)('A' + i);
    h->checksum = net_checksum((uint16_t*)buf, sizeof(buf));
    ipv4_send_packet(dst_mac, dst_ip, 1, buf, sizeof(buf));
}

void icmp_receive_packet(uint32_t src_ip, uint8_t* src_mac, uint8_t* payload, uint32_t length) {
    if (length < sizeof(icmp_header_t)) return;
    icmp_header_t* icmp = (icmp_header_t*)payload;

    if (icmp->type == 8) { /* Echo Request — send reply */
        uint8_t reply[2048];
        if (length > 2048) return;
        memcpy(reply, payload, length);
        icmp_header_t* r = (icmp_header_t*)reply;
        r->type     = 0;
        r->checksum = 0;
        r->checksum = net_checksum((uint16_t*)reply, length);
        ipv4_send_packet(src_mac, src_ip, 1, reply, length);
    } else if (icmp->type == 0) { /* Echo Reply */
        icmp_reply_src = src_ip;
        icmp_got_reply = 1;
    }
}

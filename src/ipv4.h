#ifndef IPV4_H
#define IPV4_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t length;
    uint16_t ident;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_header_t;
#pragma pack(pop)

void ipv4_receive_packet(uint8_t* payload, uint32_t length);
void ipv4_send_packet(uint8_t* dst_mac, uint32_t dst_ip, uint8_t protocol, uint8_t* payload, uint32_t payload_length);

#endif

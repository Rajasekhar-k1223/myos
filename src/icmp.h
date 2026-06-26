#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} icmp_header_t;
#pragma pack(pop)

void icmp_receive_packet(uint32_t src_ip, uint8_t* src_mac, uint8_t* payload, uint32_t length);

#endif

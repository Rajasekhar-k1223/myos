#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
} arp_packet_t;
#pragma pack(pop)

void arp_receive_packet(uint8_t* payload, uint32_t length);

#endif

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

// Resolve an IP -> MAC. Returns 1 on success, 0 on timeout.
// On success fills `mac_out` with the 6-byte MAC.
int  arp_resolve(uint32_t ip, uint8_t* mac_out);

// Send an ARP request for a given IP
void arp_send_request(uint32_t target_ip);

#endif

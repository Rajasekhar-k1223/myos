#ifndef ETHERNET_H
#define ETHERNET_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} ethernet_header_t;
#pragma pack(pop)

void ethernet_receive_packet(uint8_t* payload, uint32_t length);
void ethernet_send_packet(uint8_t* dst_mac, uint16_t ethertype, uint8_t* payload, uint32_t payload_length);

#endif

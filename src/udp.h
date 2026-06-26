#ifndef UDP_H
#define UDP_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;
#pragma pack(pop)

void udp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip);
void udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t* payload, uint32_t payload_length);

#endif

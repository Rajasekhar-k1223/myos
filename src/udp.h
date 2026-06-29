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

// Per-port UDP handler callback: called with (data, data_len)
typedef void (*udp_handler_t)(uint8_t* data, uint32_t len);

void udp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip);
void udp_receive_packet_v6(uint8_t* payload, uint32_t length, const uint8_t* src_ip, const uint8_t* dst_ip);
void udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t* payload, uint32_t payload_length);
void udp_send_v6(const uint8_t* dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t* payload, uint32_t payload_length);

// Register / unregister a handler for an incoming UDP destination port
void udp_register_handler(uint16_t port, udp_handler_t handler);
void udp_unregister_handler(uint16_t port);

#endif

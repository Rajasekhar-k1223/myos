#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t reserved_offset; // 4 bits data offset, 4 bits reserved
    uint8_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t reserved;
    uint8_t protocol;
    uint16_t tcp_length;
} tcp_pseudo_header_t;
#pragma pack(pop)

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

void tcp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip);
int tcp_connect(uint32_t dst_ip, uint16_t dst_port);
int tcp_send_data(uint8_t* data, uint32_t len);

extern volatile int tcp_is_connected;
extern volatile int tcp_has_data;
extern uint8_t tcp_recv_buffer[4096];
extern uint32_t tcp_recv_len;

#endif

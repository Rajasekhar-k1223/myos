#ifndef IPV6_H
#define IPV6_H

#include <stdint.h>
#include <stddef.h>
#include "net.h"
#include "ethernet.h"

#define ETHERTYPE_IPV6 0x86DD

typedef struct {
    uint8_t  version_tc_flow[4]; // 4 bits version, 8 bits traffic class, 20 bits flow label
    uint16_t payload_length;
    uint8_t  next_header;
    uint8_t  hop_limit;
    uint8_t  src_ip[16];
    uint8_t  dst_ip[16];
} __attribute__((packed)) ipv6_header_t;

void ipv6_init(void);
void ipv6_receive(const uint8_t* packet, uint32_t length);
int  ipv6_send(const uint8_t* dst_ip, uint8_t next_header, const uint8_t* payload, uint32_t payload_len);

extern uint8_t my_ipv6_addr[16];

#endif

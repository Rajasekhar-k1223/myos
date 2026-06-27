#ifndef FIREWALL_H
#define FIREWALL_H

#include <stdint.h>

#define FIREWALL_ACTION_ALLOW 0
#define FIREWALL_ACTION_DROP  1

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol; /* 6 for TCP, 17 for UDP */
    int      action;
} firewall_rule_t;

void firewall_init(void);
void firewall_add_rule(firewall_rule_t rule);
int  firewall_check(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol, uint16_t src_port, uint16_t dst_port);

#endif

#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>

/* DHCP client — sends DISCOVER, waits for OFFER, sends REQUEST, waits for ACK.
 * Returns 1 on success, 0 on failure / timeout.
 * On success the four globals below are updated with the assigned parameters. */
int dhcp_discover(void);

/* Globals updated on a successful DHCP exchange.
 * They are also referenced by other modules (ipv4, arp, etc.) so they live
 * here and are defined in dhcp.c */
extern uint32_t my_ip;
extern uint32_t gateway_ip;
extern uint32_t subnet_mask;
extern uint32_t dns_server_ip;

#endif /* DHCP_H */

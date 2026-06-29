#include "udp.h"
#include "ipv4.h"
#include "arp.h"
#include "net.h"
#include "string.h"
#include "kernel.h"

// ─── Per-port handler table ───────────────────────────────────────────────────
#define UDP_HANDLER_MAX 8

typedef struct {
    uint16_t      port;
    udp_handler_t handler;
    int           active;
} udp_port_binding_t;

static udp_port_binding_t udp_handlers[UDP_HANDLER_MAX];

void udp_register_handler(uint16_t port, udp_handler_t handler) {
    // Update existing binding first
    for (int i = 0; i < UDP_HANDLER_MAX; i++) {
        if (udp_handlers[i].active && udp_handlers[i].port == port) {
            udp_handlers[i].handler = handler;
            return;
        }
    }
    // Find empty slot
    for (int i = 0; i < UDP_HANDLER_MAX; i++) {
        if (!udp_handlers[i].active) {
            udp_handlers[i].port    = port;
            udp_handlers[i].handler = handler;
            udp_handlers[i].active  = 1;
            return;
        }
    }
    terminal_printf("[UDP] Warning: handler table full, could not register port %d\n", port);
}

void udp_unregister_handler(uint16_t port) {
    for (int i = 0; i < UDP_HANDLER_MAX; i++) {
        if (udp_handlers[i].active && udp_handlers[i].port == port) {
            udp_handlers[i].active = 0;
            return;
        }
    }
}

#pragma pack(push, 1)
typedef struct {
    uint8_t  src_ip[16];
    uint8_t  dst_ip[16];
    uint32_t udp_length;
    uint8_t  zeros[3];
    uint8_t  next_header;
} udp_ipv6_pseudo_header_t;
#pragma pack(pop)

static uint16_t udp_checksum_v6(udp_header_t* udp, uint32_t udp_len, const uint8_t* src_ip, const uint8_t* dst_ip) {
    udp_ipv6_pseudo_header_t ph;
    memcpy(ph.src_ip, src_ip, 16);
    memcpy(ph.dst_ip, dst_ip, 16);
    ph.udp_length = htonl(udp_len);
    ph.zeros[0] = ph.zeros[1] = ph.zeros[2] = 0;
    ph.next_header = 17;

    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)&ph;
    for (int i = 0; i < (int)(sizeof(udp_ipv6_pseudo_header_t) / 2); i++) sum += ptr[i];

    ptr = (uint16_t*)udp;
    for (uint32_t i = 0; i < udp_len / 2; i++) sum += ptr[i];
    if (udp_len & 1) sum += ((uint8_t*)udp)[udp_len - 1];

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

// ─── Receive ─────────────────────────────────────────────────────────────────
void udp_receive_packet_v6(uint8_t* payload, uint32_t length, const uint8_t* src_ip, const uint8_t* dst_ip) {
    if (length < sizeof(udp_header_t)) return;
    (void)src_ip;
    (void)dst_ip;

    udp_header_t* udp = (udp_header_t*)payload;
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t pkt_len  = ntohs(udp->length);

    if (pkt_len > length) return;

    uint8_t*  data     = payload + sizeof(udp_header_t);
    uint32_t  data_len = pkt_len - sizeof(udp_header_t);

    for (int i = 0; i < UDP_HANDLER_MAX; i++) {
        if (udp_handlers[i].active && udp_handlers[i].port == dst_port) {
            udp_handlers[i].handler(data, data_len);
            return;
        }
    }
}

void udp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip) {
    if (length < sizeof(udp_header_t)) return;

    udp_header_t* udp = (udp_header_t*)payload;
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t pkt_len  = ntohs(udp->length);

    if (pkt_len > length) return;

    uint8_t*  data     = payload + sizeof(udp_header_t);
    uint32_t  data_len = pkt_len - sizeof(udp_header_t);

    // Call handler if registered
    for (int i = 0; i < UDP_HANDLER_MAX; i++) {
        if (udp_handlers[i].active && udp_handlers[i].port == dst_port) {
            udp_handlers[i].handler(data, data_len);
            return;
        }
    }

    // No handler registered — log it (only if it's not DHCP broadcast noise)
    if (dst_port != 68 && dst_port != 67) {
        terminal_printf("[UDP] Unhandled packet from %d.%d.%d.%d:%d -> port %d, len=%d\n",
            (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
            (src_ip >>  8) & 0xFF,  src_ip & 0xFF,
            src_port, dst_port, data_len);
    }
    (void)src_port; // suppress unused warning
}

// ─── Send ─────────────────────────────────────────────────────────────────────
void udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              uint8_t* payload, uint32_t payload_length) {
    uint32_t total_length = sizeof(udp_header_t) + payload_length;
    uint8_t buffer[2048];

    if (total_length > 2048) return;

    udp_header_t* udp = (udp_header_t*)buffer;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons((uint16_t)total_length);
    udp->checksum = 0; // Checksum optional for UDP over IPv4

    memcpy(buffer + sizeof(udp_header_t), payload, payload_length);

    // Resolve destination MAC via ARP (route via gateway for off-subnet)
    static const uint32_t MY_IP      = 0x0F02000A; // 10.0.2.15
    static const uint32_t GATEWAY_IP = 0x0202000A; // 10.0.2.2
    uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t nexthop = dst_ip;
    if ((nexthop & 0x00FFFFFF) != (MY_IP & 0x00FFFFFF)) nexthop = GATEWAY_IP;
    arp_resolve(nexthop, dst_mac);

    ipv4_send_packet(dst_mac, dst_ip, 17, buffer, total_length); // 17 = UDP
}

void udp_send_v6(const uint8_t* dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t* payload, uint32_t payload_length) {
    uint32_t udp_len = sizeof(udp_header_t) + payload_length;
    uint8_t buffer[2048];
    if (udp_len > sizeof(buffer)) return;

    udp_header_t* udp = (udp_header_t*)buffer;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(udp_len);
    udp->checksum = 0;

    if (payload_length > 0 && payload) {
        memcpy(buffer + sizeof(udp_header_t), payload, payload_length);
    }

    extern uint8_t my_ipv6_addr[16];
    udp->checksum = udp_checksum_v6(udp, udp_len, my_ipv6_addr, dst_ip);

    extern int ipv6_send(const uint8_t*, uint8_t, const uint8_t*, uint32_t);
    ipv6_send(dst_ip, 17, buffer, udp_len);
}

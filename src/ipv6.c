#include "ipv6.h"
#include "ethernet.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"

uint8_t my_ipv6_addr[16] = {
    0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
};

/* NDP neighbor cache — maps IPv6 addr → MAC (simplified, 8 entries) */
#define NDP_CACHE_SIZE 8
typedef struct { uint8_t ip[16]; uint8_t mac[6]; int valid; } ndp_entry_t;
static ndp_entry_t ndp_cache[NDP_CACHE_SIZE];

static void ndp_cache_update(const uint8_t* ip, const uint8_t* mac) {
    /* Update existing entry */
    for (int i = 0; i < NDP_CACHE_SIZE; i++) {
        if (ndp_cache[i].valid && memcmp(ndp_cache[i].ip, ip, 16) == 0) {
            memcpy(ndp_cache[i].mac, mac, 6);
            return;
        }
    }
    /* Insert into first free slot */
    for (int i = 0; i < NDP_CACHE_SIZE; i++) {
        if (!ndp_cache[i].valid) {
            memcpy(ndp_cache[i].ip,  ip,  16);
            memcpy(ndp_cache[i].mac, mac, 6);
            ndp_cache[i].valid = 1;
            return;
        }
    }
}

static int ndp_cache_lookup(const uint8_t* ip, uint8_t* mac_out) {
    for (int i = 0; i < NDP_CACHE_SIZE; i++) {
        if (ndp_cache[i].valid && memcmp(ndp_cache[i].ip, ip, 16) == 0) {
            memcpy(mac_out, ndp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

/* Derive solicited-node multicast MAC: 33:33:FF:xx:xx:xx */
static void ipv6_solicited_node_mac(const uint8_t* dst_ip, uint8_t* mac) {
    mac[0] = 0x33; mac[1] = 0x33;
    mac[2] = 0xFF;
    mac[3] = dst_ip[13];
    mac[4] = dst_ip[14];
    mac[5] = dst_ip[15];
}

void ipv6_init(void) {
    for (int i = 0; i < NDP_CACHE_SIZE; i++) ndp_cache[i].valid = 0;
    terminal_printf("[IPv6] Stack ready. Link-local: fe80::1. NDP cache: %d slots.\n",
                    NDP_CACHE_SIZE);
}

void ipv6_ndp_update(const uint8_t* ip, const uint8_t* mac) {
    ndp_cache_update(ip, mac);
}

void ipv6_receive(const uint8_t* packet, uint32_t length) {
    if (length < sizeof(ipv6_header_t)) return;
    ipv6_header_t* hdr = (ipv6_header_t*)packet;
    uint8_t version = (hdr->version_tc_flow[0] >> 4) & 0x0F;
    if (version != 6) return;
    uint16_t payload_len = (uint16_t)((hdr->payload_length >> 8) | (hdr->payload_length << 8));
    if (length < sizeof(ipv6_header_t) + payload_len) return;
    uint8_t* payload = (uint8_t*)(packet + sizeof(ipv6_header_t));

    if (hdr->next_header == 6) {
        extern void tcp_receive_packet_v6(uint8_t*, uint32_t, const uint8_t*, const uint8_t*);
        tcp_receive_packet_v6(payload, payload_len, hdr->src_ip, hdr->dst_ip);
    } else if (hdr->next_header == 17) {
        extern void udp_receive_packet_v6(uint8_t*, uint32_t, const uint8_t*, const uint8_t*);
        udp_receive_packet_v6(payload, payload_len, hdr->src_ip, hdr->dst_ip);
    }
    /* NDP (ICMPv6 type 135/136) — update cache from Neighbor Advertisement */
    else if (hdr->next_header == 58 && payload_len >= 1) {
        uint8_t icmp_type = payload[0];
        if ((icmp_type == 136) && payload_len >= 24) {
            /* Neighbor Advertisement: target addr at offset 8, option at 24 */
            const uint8_t* tgt = payload + 8;
            if (payload_len >= 32 && payload[24] == 2 && payload[25] == 1) {
                ndp_cache_update(tgt, payload + 26);
            }
        }
    }
}

int ipv6_send(const uint8_t* dst_ip, uint8_t next_header,
              const uint8_t* payload, uint32_t payload_len) {
    uint32_t pkt_len = (uint32_t)sizeof(ipv6_header_t) + payload_len;
    uint8_t* pkt = (uint8_t*)kmalloc(pkt_len);
    if (!pkt) return -1;

    ipv6_header_t* hdr = (ipv6_header_t*)pkt;
    memset(hdr, 0, sizeof(ipv6_header_t));
    hdr->version_tc_flow[0] = 0x60;
    hdr->payload_length      = (uint16_t)((payload_len >> 8) | (payload_len << 8));
    hdr->next_header         = next_header;
    hdr->hop_limit           = 64;
    memcpy(hdr->src_ip, my_ipv6_addr, 16);
    memcpy(hdr->dst_ip, dst_ip, 16);
    memcpy(pkt + sizeof(ipv6_header_t), payload, payload_len);

    /* Determine destination MAC via NDP cache or multicast derivation */
    uint8_t dst_mac[6];
    if (!ndp_cache_lookup(dst_ip, dst_mac)) {
        /* All-nodes multicast (ff02::1) → 33:33:00:00:00:01 */
        if (dst_ip[0] == 0xFF) {
            dst_mac[0] = 0x33; dst_mac[1] = 0x33;
            dst_mac[2] = dst_ip[12]; dst_mac[3] = dst_ip[13];
            dst_mac[4] = dst_ip[14]; dst_mac[5] = dst_ip[15];
        } else {
            /* Send NDP Neighbor Solicitation to solicit MAC, then use
               solicited-node MAC temporarily */
            ipv6_solicited_node_mac(dst_ip, dst_mac);
        }
    }

    /* EtherType 0x86DD = IPv6 */
    ethernet_send_packet(dst_mac, 0x86DD, pkt, pkt_len);
    kfree(pkt);
    return 0;
}

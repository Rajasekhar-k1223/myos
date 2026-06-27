#include "arp.h"
#include "ethernet.h"
#include "net.h"
#include "kernel.h"
#include "string.h"
#include "rtl8139.h"
#include "pit.h"

// Our IP: 10.0.2.15 stored as little-endian uint32
static const uint32_t MY_IP = 0x0F02000A;

// ─── ARP Cache ────────────────────────────────────────────────────────────────
#define ARP_CACHE_SIZE 16
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
} arp_cache_entry_t;
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];

static void arp_cache_store(uint32_t ip, uint8_t* mac) {
    // Check if already in cache
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    // Find empty slot
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            arp_cache[i].valid = 1;
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    // Evict slot 0 (simple LRU-like)
    arp_cache[0].ip = ip;
    arp_cache[0].valid = 1;
    memcpy(arp_cache[0].mac, mac, 6);
}

static int arp_cache_lookup(uint32_t ip, uint8_t* mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

// ─── Send an ARP Request ─────────────────────────────────────────────────────
void arp_send_request(uint32_t target_ip) {
    arp_packet_t req;
    req.htype = htons(1);
    req.ptype = htons(0x0800);
    req.hlen  = 6;
    req.plen  = 4;
    req.oper  = htons(1); // ARP Request

    memcpy(req.sha, rtl8139_get_mac(), 6);
    req.spa = MY_IP;

    memset(req.tha, 0, 6);
    req.tpa = target_ip;

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ethernet_send_packet(bcast, 0x0806, (uint8_t*)&req, sizeof(arp_packet_t));
}

// ─── Resolve IP -> MAC (blocking, with timeout) ───────────────────────────────
int arp_resolve(uint32_t ip, uint8_t* mac_out) {
    // Check cache first
    if (arp_cache_lookup(ip, mac_out)) return 1;

    // Send ARP request and wait
    arp_send_request(ip);
    uint32_t deadline = pit_get_ticks() + 3000; // 3-second timeout
    while (pit_get_ticks() < deadline) {
        if (arp_cache_lookup(ip, mac_out)) return 1;
        // The interrupt handler fills the cache asynchronously
    }
    return 0;
}

// ─── Receive ARP Packets ─────────────────────────────────────────────────────
void arp_receive_packet(uint8_t* payload, uint32_t length) {
    if (length < sizeof(arp_packet_t)) return;

    arp_packet_t* arp = (arp_packet_t*)payload;
    uint16_t oper = ntohs(arp->oper);

    // Always learn the sender MAC into our cache
    if (arp->spa != 0) {
        arp_cache_store(arp->spa, arp->sha);
    }

    if (oper == 1) { // ARP Request
        if (arp->tpa == MY_IP) {
            terminal_printf("  [ARP] Request: Who has 10.0.2.15? Sending Reply...\n");

            arp_packet_t reply;
            reply.htype = htons(1);
            reply.ptype = htons(0x0800);
            reply.hlen  = 6;
            reply.plen  = 4;
            reply.oper  = htons(2); // ARP Reply

            memcpy(reply.sha, rtl8139_get_mac(), 6);
            reply.spa = MY_IP;
            memcpy(reply.tha, arp->sha, 6);
            reply.tpa = arp->spa;

            ethernet_send_packet(arp->sha, 0x0806, (uint8_t*)&reply, sizeof(arp_packet_t));
        }
    }
    // Replies are handled by the cache-fill above
}

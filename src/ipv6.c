#include "ipv6.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"

uint8_t my_ipv6_addr[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}; // Link-local dummy

void ipv6_init(void) {
    terminal_printf("[IPv6] Initialized stack with link-local address.\n");
}

void ipv6_receive(const uint8_t* packet, uint32_t length) {
    if (length < sizeof(ipv6_header_t)) return;
    
    ipv6_header_t* hdr = (ipv6_header_t*)packet;
    
    // Check version (4 bits)
    uint8_t version = (hdr->version_tc_flow[0] >> 4) & 0x0F;
    if (version != 6) return;
    
    // Convert payload length from network byte order
    uint16_t payload_len = (hdr->payload_length >> 8) | (hdr->payload_length << 8);
    
    if (length < sizeof(ipv6_header_t) + payload_len) return;
    
    uint8_t* payload = (uint8_t*)(packet + sizeof(ipv6_header_t));
    
    if (hdr->next_header == 6) {
        extern void tcp_receive_packet_v6(uint8_t*, uint32_t, const uint8_t*, const uint8_t*);
        tcp_receive_packet_v6(payload, payload_len, hdr->src_ip, hdr->dst_ip);
    } else if (hdr->next_header == 17) {
        extern void udp_receive_packet_v6(uint8_t*, uint32_t, const uint8_t*, const uint8_t*);
        udp_receive_packet_v6(payload, payload_len, hdr->src_ip, hdr->dst_ip);
    }
}

int ipv6_send(const uint8_t* dst_ip, uint8_t next_header, const uint8_t* payload, uint32_t payload_len) {
    uint32_t packet_len = sizeof(ipv6_header_t) + payload_len;
    uint8_t* packet = (uint8_t*)kmalloc(packet_len);
    if (!packet) return -1;
    
    ipv6_header_t* hdr = (ipv6_header_t*)packet;
    memset(hdr, 0, sizeof(ipv6_header_t));
    
    hdr->version_tc_flow[0] = 0x60; // Version 6, TC=0
    hdr->payload_length = (payload_len >> 8) | (payload_len << 8); // htons
    hdr->next_header = next_header;
    hdr->hop_limit = 64;
    
    memcpy(hdr->src_ip, my_ipv6_addr, 16);
    memcpy(hdr->dst_ip, dst_ip, 16);
    
    memcpy(packet + sizeof(ipv6_header_t), payload, payload_len);
    
    // To send, we'd need NDP (Neighbor Discovery Protocol) to find MAC.
    // We skip actual sending for this stub implementation.
    
    extern void kfree(void* ptr);
    kfree(packet);
    
    return 0;
}

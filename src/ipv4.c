#include "ipv4.h"
#include "ethernet.h"
#include "net.h"
#include "kernel.h"
#include "string.h"
#include "icmp.h"

static const uint32_t MY_IP = 0x0F02000A; // 10.0.2.15

static uint16_t ip_id = 1;

void ipv4_receive_packet(uint8_t* payload, uint32_t length) {
    if (length < sizeof(ipv4_header_t)) return;
    
    ipv4_header_t* ip = (ipv4_header_t*)payload;
    if (ip->dst_ip != MY_IP && ip->dst_ip != 0xFFFFFFFF) return;
    
    uint16_t len = ntohs(ip->length);
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    
    if (ip->protocol == 1) { // ICMP
        uint8_t* src_mac = payload - sizeof(ethernet_header_t) + 6;
        icmp_receive_packet(ip->src_ip, src_mac, payload + ihl, len - ihl);
    } else if (ip->protocol == 6) { // TCP
        extern void tcp_receive_packet(uint8_t*, uint32_t, uint32_t);
        tcp_receive_packet(payload + ihl, len - ihl, ip->src_ip);
    } else if (ip->protocol == 17) { // UDP
        extern void udp_receive_packet(uint8_t*, uint32_t, uint32_t);
        udp_receive_packet(payload + ihl, len - ihl, ip->src_ip);
    }
}

void ipv4_send_packet(uint8_t* dst_mac, uint32_t dst_ip, uint8_t protocol, uint8_t* payload, uint32_t payload_length) {
    uint32_t total_length = sizeof(ipv4_header_t) + payload_length;
    uint8_t buffer[2048];
    
    if (total_length > 2048) return;
    
    ipv4_header_t* ip = (ipv4_header_t*)buffer;
    ip->version_ihl = 0x45; // Version 4, IHL 5 (20 bytes)
    ip->dscp_ecn = 0;
    ip->length = htons(total_length);
    ip->ident = htons(ip_id++);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = MY_IP;
    ip->dst_ip = dst_ip;
    
    ip->checksum = net_checksum((uint16_t*)ip, sizeof(ipv4_header_t));
    
    memcpy(buffer + sizeof(ipv4_header_t), payload, payload_length);
    
    ethernet_send_packet(dst_mac, 0x0800, buffer, total_length);
}

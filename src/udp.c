#include "udp.h"
#include "ipv4.h"
#include "net.h"
#include "string.h"
#include "kernel.h"

void udp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip) {
    if (length < sizeof(udp_header_t)) return;
    
    udp_header_t* udp = (udp_header_t*)payload;
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t len = ntohs(udp->length);
    
    if (len > length) return;
    
    uint8_t* data = payload + sizeof(udp_header_t);
    uint32_t data_len = len - sizeof(udp_header_t);
    
    terminal_printf("[UDP] Received packet from %d.%d.%d.%d:%d to port %d, len %d\n",
                    (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF, (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                    src_port, dst_port, data_len);
}

void udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t* payload, uint32_t payload_length) {
    uint32_t total_length = sizeof(udp_header_t) + payload_length;
    uint8_t buffer[2048];
    
    if (total_length > 2048) return;
    
    udp_header_t* udp = (udp_header_t*)buffer;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(total_length);
    udp->checksum = 0; // Checksum is optional in UDP over IPv4
    
    memcpy(buffer + sizeof(udp_header_t), payload, payload_length);
    
    // Using broadcast MAC for simplicity without ARP cache
    uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    ipv4_send_packet(dst_mac, dst_ip, 17, buffer, total_length); // 17 is UDP
}

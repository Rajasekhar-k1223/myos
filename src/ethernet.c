#include "ethernet.h"
#include "rtl8139.h"
#include "net.h"
#include "kernel.h"
#include "string.h"
#include "arp.h"

void ethernet_receive_packet(uint8_t* payload, uint32_t length) {
    if (length < sizeof(ethernet_header_t)) return;
    
    ethernet_header_t* eth = (ethernet_header_t*)payload;
    uint16_t type = ntohs(eth->ethertype);
    
    if (type == 0x0806) { // ARP
        arp_receive_packet(payload + sizeof(ethernet_header_t), length - sizeof(ethernet_header_t));
    } else if (type == 0x0800) { // IPv4
        // terminal_printf("  [ETH] Received IPv4 packet.\n");
    }
}

void ethernet_send_packet(uint8_t* dst_mac, uint16_t ethertype, uint8_t* payload, uint32_t payload_length) {
    uint32_t total_length = sizeof(ethernet_header_t) + payload_length;
    uint8_t buffer[2048]; // Max ethernet frame is 1518
    
    if (total_length > 2048) return;
    
    ethernet_header_t* eth = (ethernet_header_t*)buffer;
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, rtl8139_get_mac(), 6);
    eth->ethertype = htons(ethertype);
    
    memcpy(buffer + sizeof(ethernet_header_t), payload, payload_length);
    
    rtl8139_send_packet(buffer, total_length);
}

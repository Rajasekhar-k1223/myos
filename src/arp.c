#include "arp.h"
#include "ethernet.h"
#include "net.h"
#include "kernel.h"
#include "string.h"
#include "rtl8139.h"

// Hardcode ElseaOS IP: 10.0.2.15 (0x0A00020F)
static const uint32_t MY_IP = 0x0F02000A; // Little-endian format for 10.0.2.15 (0x0A, 0x00, 0x02, 0x0F reversed is 0x0F02000A)

void arp_receive_packet(uint8_t* payload, uint32_t length) {
    if (length < sizeof(arp_packet_t)) return;
    
    arp_packet_t* arp = (arp_packet_t*)payload;
    
    if (ntohs(arp->oper) == 1) { // ARP Request
        if (arp->tpa == MY_IP) {
            terminal_printf("  [ARP] Received Request: Who has 10.0.2.15?\n");
            
            // Build ARP Reply
            arp_packet_t reply;
            reply.htype = htons(1); // Ethernet
            reply.ptype = htons(0x0800); // IPv4
            reply.hlen = 6;
            reply.plen = 4;
            reply.oper = htons(2); // ARP Reply
            
            // Sender is Us
            memcpy(reply.sha, rtl8139_get_mac(), 6);
            reply.spa = MY_IP;
            
            // Target is Them
            memcpy(reply.tha, arp->sha, 6);
            reply.tpa = arp->spa;
            
            terminal_printf("  [ARP] Sending Reply: 10.0.2.15 is at our MAC!\n");
            ethernet_send_packet(arp->sha, 0x0806, (uint8_t*)&reply, sizeof(arp_packet_t));
        }
    }
}

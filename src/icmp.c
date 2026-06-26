#include "icmp.h"
#include "ipv4.h"
#include "net.h"
#include "kernel.h"
#include "string.h"

void icmp_receive_packet(uint32_t src_ip, uint8_t* src_mac, uint8_t* payload, uint32_t length) {
    if (length < sizeof(icmp_header_t)) return;
    
    icmp_header_t* icmp = (icmp_header_t*)payload;
    
    if (icmp->type == 8) { // Echo Request (Ping)
        terminal_printf("  [ICMP] Echo Request received from IP %d.%d.%d.%d!\n",
            (src_ip) & 0xFF, (src_ip >> 8) & 0xFF, (src_ip >> 16) & 0xFF, (src_ip >> 24) & 0xFF);
            
        // Build Echo Reply (Type 0)
        // We reuse the payload buffer directly to bounce it back
        uint8_t reply_buffer[2048];
        if (length > 2048) return;
        
        memcpy(reply_buffer, payload, length);
        
        icmp_header_t* reply = (icmp_header_t*)reply_buffer;
        reply->type = 0; // Echo Reply
        reply->code = 0;
        reply->checksum = 0;
        
        // Recalculate checksum over the entire ICMP message
        reply->checksum = net_checksum((uint16_t*)reply_buffer, length);
        
        terminal_printf("  [ICMP] Sending Echo Reply!\n");
        ipv4_send_packet(src_mac, src_ip, 1, reply_buffer, length); // Protocol 1 is ICMP
    }
}

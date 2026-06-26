#include "tcp.h"
#include "ipv4.h"
#include "net.h"
#include "string.h"
#include "kernel.h"
#include "pit.h"

// Simplified TCP State Machine for a SINGLE active connection
static uint32_t tcp_conn_dst_ip = 0;
static uint16_t tcp_conn_dst_port = 0;
static uint16_t tcp_conn_src_port = 49152;
static uint32_t tcp_seq_num = 0;
static uint32_t tcp_ack_num = 0;

volatile int tcp_is_connected = 0;
volatile int tcp_has_data = 0;
uint8_t tcp_recv_buffer[4096];
uint32_t tcp_recv_len = 0;

static const uint32_t MY_IP = 0x0F02000A; // 10.0.2.15

static uint16_t tcp_checksum(tcp_header_t* tcp, uint32_t tcp_len, uint32_t src_ip, uint32_t dst_ip) {
    tcp_pseudo_header_t ph;
    ph.src_ip = src_ip;
    ph.dst_ip = dst_ip;
    ph.reserved = 0;
    ph.protocol = 6;
    ph.tcp_length = htons(tcp_len);
    
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)&ph;
    for (int i = 0; i < sizeof(tcp_pseudo_header_t)/2; i++) {
        sum += ptr[i];
    }
    
    ptr = (uint16_t*)tcp;
    for (int i = 0; i < tcp_len/2; i++) {
        sum += ptr[i];
    }
    
    if (tcp_len % 2) {
        sum += ((uint8_t*)tcp)[tcp_len - 1]; // Pad with zero
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

static void tcp_send_packet(uint8_t flags, uint8_t* payload, uint32_t payload_len) {
    uint8_t buffer[2048];
    uint32_t tcp_len = sizeof(tcp_header_t) + payload_len;
    
    tcp_header_t* tcp = (tcp_header_t*)buffer;
    tcp->src_port = htons(tcp_conn_src_port);
    tcp->dst_port = htons(tcp_conn_dst_port);
    tcp->seq = htonl(tcp_seq_num);
    tcp->ack = htonl(tcp_ack_num);
    tcp->reserved_offset = (sizeof(tcp_header_t) / 4) << 4;
    tcp->flags = flags;
    tcp->window_size = htons(8192);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    if (payload_len > 0 && payload) {
        memcpy(buffer + sizeof(tcp_header_t), payload, payload_len);
    }
    
    tcp->checksum = tcp_checksum(tcp, tcp_len, MY_IP, tcp_conn_dst_ip);
    
    // Using broadcast MAC for simplicity
    uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ipv4_send_packet(dst_mac, tcp_conn_dst_ip, 6, buffer, tcp_len); // 6 is TCP
}

void tcp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip) {
    if (length < sizeof(tcp_header_t)) return;
    
    tcp_header_t* tcp = (tcp_header_t*)payload;
    if (src_ip != tcp_conn_dst_ip || ntohs(tcp->dst_port) != tcp_conn_src_port) return; // Ignore other connections
    
    uint8_t header_len = (tcp->reserved_offset >> 4) * 4;
    uint32_t payload_len = length - header_len;
    
    if (tcp->flags & TCP_SYN) {
        if (tcp->flags & TCP_ACK) {
            // Received SYN-ACK
            tcp_ack_num = ntohl(tcp->seq) + 1;
            tcp_seq_num = ntohl(tcp->ack);
            
            // Send ACK
            tcp_send_packet(TCP_ACK, NULL, 0);
            tcp_is_connected = 1;
            terminal_printf("[TCP] Connected to %d.%d.%d.%d:%d!\n", 
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF, (src_ip >> 8) & 0xFF, src_ip & 0xFF, ntohs(tcp->src_port));
        }
    } else if (payload_len > 0) {
        // Received Data
        if (tcp_recv_len + payload_len < sizeof(tcp_recv_buffer)) {
            memcpy(tcp_recv_buffer + tcp_recv_len, payload + header_len, payload_len);
            tcp_recv_len += payload_len;
            tcp_recv_buffer[tcp_recv_len] = '\0';
            tcp_has_data = 1;
        }
        
        tcp_ack_num = ntohl(tcp->seq) + payload_len;
        tcp_send_packet(TCP_ACK, NULL, 0);
    } else if (tcp->flags & TCP_FIN) {
        // Connection closed by remote
        tcp_ack_num = ntohl(tcp->seq) + 1;
        tcp_send_packet(TCP_ACK | TCP_FIN, NULL, 0);
        tcp_is_connected = 0;
        terminal_printf("[TCP] Connection closed by remote.\n");
    }
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    tcp_conn_dst_ip = dst_ip;
    tcp_conn_dst_port = dst_port;
    tcp_conn_src_port++; // Increment local port
    tcp_seq_num = 12345; // Random ISN
    tcp_ack_num = 0;
    tcp_is_connected = 0;
    tcp_has_data = 0;
    tcp_recv_len = 0;
    
    tcp_send_packet(TCP_SYN, NULL, 0);
    
    // Wait for connection
    uint32_t timeout = pit_get_ticks() + 2000;
    while (!tcp_is_connected) {
        if (pit_get_ticks() > timeout) {
            terminal_printf("[TCP] Connection timed out.\n");
            return 0;
        }
    }
    return 1;
}

int tcp_send_data(uint8_t* data, uint32_t len) {
    if (!tcp_is_connected) return 0;
    tcp_send_packet(TCP_ACK | TCP_PSH, data, len);
    tcp_seq_num += len;
    return 1;
}

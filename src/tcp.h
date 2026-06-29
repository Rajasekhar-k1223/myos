#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  reserved_offset; /* 4 bits data offset, 4 bits reserved */
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  reserved;
    uint8_t  protocol;
    uint16_t tcp_length;
} tcp_pseudo_header_t;
#pragma pack(pop)

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* ── Packet receive (called from ipv4.c) ────────────────────────────────── */
void tcp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip);
void tcp_receive_packet_v6(uint8_t* payload, uint32_t length, const uint8_t* src_ip, const uint8_t* dst_ip);

/* ── Multi-connection API ─────────────────────────────────────────────────
 * tcp_connect  — open a connection; returns conn_id (0-7) or -1 on failure.
 *                NOTE: old callers tested `if (tcp_connect(...))` which
 *                still works because -1 is truthy in C.
 * tcp_send     — send data on a connection.
 * tcp_recv     — receive data (blocking up to timeout_ms); returns bytes read.
 * tcp_close    — send FIN and free the slot.
 * ─────────────────────────────────────────────────────────────────────── */
int  tcp_connect(uint32_t dst_ip, uint16_t dst_port);
int  tcp_connect_v6(const uint8_t* dst_ip, uint16_t dst_port);
int  tcp_send(int conn_id, const uint8_t* data, uint32_t len);
int  tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len, uint32_t timeout_ms);
void tcp_close(int conn_id);

/* ── Legacy single-connection shims (browser.c / shell.c) ──────────────── */
int  tcp_send_data(uint8_t* data, uint32_t len);

extern volatile int tcp_is_connected;  /* mirrors connection slot 0 */
extern volatile int tcp_has_data;      /* mirrors connection slot 0 */
extern uint8_t      tcp_recv_buffer[4096]; /* mirrors connection slot 0 */
extern uint32_t     tcp_recv_len;          /* mirrors connection slot 0 */

#endif /* TCP_H */

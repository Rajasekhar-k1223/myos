#ifndef RAW_SOCKET_H
#define RAW_SOCKET_H

#include <stdint.h>

#define SOCK_RAW_MAX 4

typedef struct {
    int      used;
    int      protocol; /* 0=all, 1=ICMP, 6=TCP, 17=UDP */
    uint8_t  rx_buf[4096];
    uint32_t rx_head, rx_tail;
} raw_socket_t;

void raw_socket_init(void);
int  raw_socket_open(int protocol);
int  raw_socket_send(int fd, const uint8_t* pkt, uint32_t len);
int  raw_socket_recv(int fd, uint8_t* buf, uint32_t max_len, uint32_t timeout_ms);
void raw_socket_close(int fd);
void raw_socket_deliver(const uint8_t* pkt, uint32_t len);

#endif

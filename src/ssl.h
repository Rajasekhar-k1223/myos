#ifndef SSL_H
#define SSL_H

#include <stdint.h>

void ssl_init(void);
int  ssl_connect(const char* host, uint16_t port);
int  ssl_send(int socket, const char* data, uint32_t len);
int  ssl_receive(int socket, char* buffer, uint32_t max_len);
void ssl_close(int socket);

#endif

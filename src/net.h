#ifndef NET_H
#define NET_H

#include <stdint.h>

static inline uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

static inline uint16_t ntohs(uint16_t v) {
    return htons(v);
}

static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFF) << 24) |
           ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) |
           ((v & 0xFF000000) >> 24);
}

static inline uint32_t ntohl(uint32_t v) {
    return htonl(v);
}

#endif

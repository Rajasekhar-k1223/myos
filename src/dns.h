#ifndef DNS_H
#define DNS_H

#include <stdint.h>

#pragma pack(push, 1)

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;   // Question count
    uint16_t ancount;   // Answer count
    uint16_t nscount;   // Authority count
    uint16_t arcount;   // Additional count
} dns_header_t;

typedef struct {
    uint16_t qtype;
    uint16_t qclass;
} dns_question_footer_t;

typedef struct {
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
} dns_answer_t;

#pragma pack(pop)

// DNS Flags
#define DNS_FLAG_QR_RESPONSE  (1 << 15)  // Query=0, Response=1
#define DNS_FLAG_RD           (1 << 8)   // Recursion Desired
#define DNS_FLAG_RA           (1 << 7)   // Recursion Available

// DNS Record Types
#define DNS_TYPE_A    1   // IPv4 address
#define DNS_TYPE_AAAA 28  // IPv6 address
#define DNS_CLASS_IN  1   // Internet

// Maximum entries in the DNS cache
#define DNS_CACHE_SIZE 8

int dns_resolve(const char* hostname, uint32_t* ip_out);

#endif

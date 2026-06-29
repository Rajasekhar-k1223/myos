#include "dns.h"
#include "udp.h"
#include "net.h"
#include "kernel.h"
#include "string.h"
#include "pit.h"

// Google Public DNS — 8.8.8.8 — reachable via QEMU user-net
static const uint32_t DNS_SERVER_IP = 0x08080808; // 8.8.8.8 big-endian
static const uint16_t DNS_SRC_PORT  = 5353;
static const uint16_t DNS_DST_PORT  = 53;

// --- DNS Cache ------------------------------------------------------------------
typedef struct {
    char     hostname[64];
    uint32_t ip;
    int      valid;
} dns_cache_entry_t;

static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];

static uint32_t dns_cache_lookup(const char* hostname) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            return dns_cache[i].ip;
        }
    }
    return 0;
}

static void dns_cache_store(const char* hostname, uint32_t ip) {
    // Update existing entry
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            dns_cache[i].ip = ip;
            return;
        }
    }
    // Find empty slot
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            strncpy(dns_cache[i].hostname, hostname, 63);
            dns_cache[i].hostname[63] = '\0';
            dns_cache[i].ip = ip;
            dns_cache[i].valid = 1;
            return;
        }
    }
    // Evict slot 0 (simple round-robin)
    strncpy(dns_cache[0].hostname, hostname, 63);
    dns_cache[0].hostname[63] = '\0';
    dns_cache[0].ip = ip;
    dns_cache[0].valid = 1;
}

// --- DNS Response State ---------------------------------------------------------
static volatile uint32_t dns_pending_id = 0;
static volatile uint32_t dns_resolved_ip = 0;
static volatile int      dns_got_response = 0;

// Called from udp_receive_packet when a packet arrives on DNS_SRC_PORT
void dns_receive_response(uint8_t* data, uint32_t len) {
    if (len < sizeof(dns_header_t)) return;

    dns_header_t* hdr = (dns_header_t*)data;
    uint16_t id    = ntohs(hdr->id);
    uint16_t flags = ntohs(hdr->flags);

    // Must be a response to our query
    if (id != (uint16_t)dns_pending_id) return;
    if (!(flags & DNS_FLAG_QR_RESPONSE)) return;

    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) {
        dns_got_response = 1; // No answers, but received something
        return;
    }

    // Skip past the header and question section
    uint8_t* ptr = data + sizeof(dns_header_t);
    uint8_t* end = data + len;

    // Skip question: name + qtype + qclass
    // Name is encoded as labels; skip until null label
    while (ptr < end && *ptr != 0) {
        if ((*ptr & 0xC0) == 0xC0) { // Pointer compression
            ptr += 2;
            goto skip_question_end;
        }
        ptr += *ptr + 1; // label length byte + label bytes
    }
    ptr++; // null terminator
skip_question_end:
    ptr += 4; // qtype (2) + qclass (2)

    // Parse answers
    for (uint16_t i = 0; i < ancount && ptr < end; i++) {
        // Skip name (may be compressed pointer)
        if (ptr < end && (*ptr & 0xC0) == 0xC0) {
            ptr += 2; // compressed pointer
        } else {
            while (ptr < end && *ptr != 0) {
                if ((*ptr & 0xC0) == 0xC0) { ptr += 2; goto skip_ans_name; }
                ptr += *ptr + 1;
            }
            ptr++;
        skip_ans_name:;
        }

        if (ptr + sizeof(dns_answer_t) > end) break;

        dns_answer_t* ans = (dns_answer_t*)ptr;
        ptr += sizeof(dns_answer_t);

        uint16_t type     = ntohs(ans->type);
        uint16_t rdlength = ntohs(ans->rdlength);

        if (type == DNS_TYPE_A && rdlength == 4 && ptr + 4 <= end) {
            // Found an A record — read the IPv4 address
            uint32_t ip = *(uint32_t*)ptr;
            dns_resolved_ip  = ip;
            dns_got_response = 1;
            return;
        }
        ptr += rdlength;
    }

    dns_got_response = 1;
}

// --- Build DNS Query ------------------------------------------------------------
static uint32_t dns_query_id = 1;

static uint32_t dns_build_query(uint8_t* buf, uint32_t buf_size, const char* hostname) {
    if (buf_size < 512) return 0;

    uint8_t* ptr = buf;

    dns_header_t* hdr = (dns_header_t*)ptr;
    hdr->id      = htons((uint16_t)dns_query_id);
    hdr->flags   = htons(DNS_FLAG_RD);  // Recursion desired
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    ptr += sizeof(dns_header_t);

    // Encode hostname as DNS labels: "google.com" -> "\x06google\x03com\x00"
    uint8_t* buf_end = buf + buf_size - sizeof(dns_question_footer_t) - 1;
    const char* src = hostname;
    while (*src) {
        if (ptr + 1 >= buf_end) return 0;  // not enough space for len byte + terminator
        uint8_t* len_byte = ptr++;
        uint8_t  count    = 0;
        while (*src && *src != '.') {
            if (ptr >= buf_end) return 0;
            *ptr++ = (uint8_t)*src++;
            count++;
        }
        *len_byte = count;
        if (*src == '.') src++;
    }
    *ptr++ = 0;   // null terminator

    // Question type and class
    dns_question_footer_t* qf = (dns_question_footer_t*)ptr;
    qf->qtype  = htons(DNS_TYPE_A);
    qf->qclass = htons(DNS_CLASS_IN);
    ptr += sizeof(dns_question_footer_t);

    return (uint32_t)(ptr - buf);
}

// --- Public API -----------------------------------------------------------------
int dns_resolve(const char* hostname, uint32_t* ip_out) {
    if (!hostname || !ip_out) return 0;

    // Return immediately for raw IP addresses
    int dots = 0;
    const char* p = hostname;
    while (*p) {
        if (*p == '.') dots++;
        p++;
    }
    if (dots == 3) {
        // Attempt to parse as dotted-decimal
        int i = 0, part = 0, val = 0;
        uint32_t ip = 0;
        int is_numeric = 1;
        while (hostname[i] && is_numeric) {
            if (hostname[i] >= '0' && hostname[i] <= '9') {
                val = val * 10 + (hostname[i] - '0');
            } else if (hostname[i] == '.') {
                ip |= (val << (24 - part * 8));
                part++;
                val = 0;
            } else {
                is_numeric = 0;
            }
            i++;
        }
        if (is_numeric) {
            ip |= val;
            *ip_out = ip;
            return 1;
        }
    }

    // Check DNS cache
    uint32_t cached = dns_cache_lookup(hostname);
    if (cached) {
        *ip_out = cached;
        return 1;
    }

    // Build DNS query
    uint8_t query_buf[512];
    uint32_t query_len = dns_build_query(query_buf, sizeof(query_buf), hostname);
    if (query_len == 0) return 0;

    // Set up state for response handler
    dns_pending_id   = dns_query_id++;
    dns_resolved_ip  = 0;
    dns_got_response = 0;

    terminal_printf("[DNS] Querying %s...\n", hostname);

    // Register our UDP handler for port DNS_SRC_PORT
    udp_register_handler(DNS_SRC_PORT, dns_receive_response);

    // Send UDP DNS query
    udp_send(DNS_SERVER_IP, DNS_SRC_PORT, DNS_DST_PORT, query_buf, query_len);

    // Wait for response with 3-second timeout
    uint32_t deadline = pit_get_ticks() + 3000;
    while (!dns_got_response && pit_get_ticks() < deadline) {
        // spin; RTL8139 IRQ handler fills dns_got_response asynchronously
    }

    udp_unregister_handler(DNS_SRC_PORT);

    if (!dns_got_response || dns_resolved_ip == 0) {
        terminal_printf("[DNS] Failed to resolve '%s'\n", hostname);
        return 0;
    }

    uint32_t ip = dns_resolved_ip;
    // Convert big-endian network byte-order to our little-endian representation
    uint32_t ip_le = ((ip & 0xFF) << 24) | (((ip >> 8) & 0xFF) << 16) |
                     (((ip >> 16) & 0xFF) << 8) | ((ip >> 24) & 0xFF);

    terminal_printf("[DNS] %s -> %d.%d.%d.%d\n", hostname,
        (ip >> 0) & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);

    dns_cache_store(hostname, ip_le);
    *ip_out = ip_le;
    return 1;
}

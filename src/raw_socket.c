/*
 * Raw IP socket layer for ElseaOS
 *
 * Up to SOCK_RAW_MAX (4) sockets can be open simultaneously.  Each socket
 * has its own 4 KB ring buffer.  raw_socket_deliver() is called by
 * ipv4_receive_packet() before the packet is dispatched to ICMP/TCP/UDP,
 * so raw sockets see every inbound IP packet.
 *
 * raw_socket_send() calls ipv4_send_packet() directly, using the broadcast
 * MAC as the destination (the ARP layer normally resolves this).  For a
 * real implementation the caller should supply a destination MAC; for now
 * we use a broadcast so the packet hits the wire.
 */

#include "raw_socket.h"
#include "ipv4.h"
#include "kernel.h"
#include "string.h"
#include "pit.h"

static raw_socket_t sockets[SOCK_RAW_MAX];

/* Broadcast MAC used when we have no ARP cache entry */
static uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ── Ring-buffer helpers ─────────────────────────────────────────────────── */

/* Number of bytes available to read */
static inline uint32_t ring_avail(const raw_socket_t* s) {
    return (s->rx_tail - s->rx_head + sizeof(s->rx_buf)) % sizeof(s->rx_buf);
}

/* Free space in the ring */
static inline uint32_t ring_free(const raw_socket_t* s) {
    return sizeof(s->rx_buf) - 1 - ring_avail(s);
}

/*
 * Write a packet into the ring buffer prefixed with a 2-byte little-endian
 * length word so the reader can find frame boundaries.
 */
static void ring_write(raw_socket_t* s, const uint8_t* data, uint32_t len) {
    if (len > 0xFFFF) return;
    uint32_t needed = 2 + len;
    if (ring_free(s) < needed) return; /* drop silently — buffer full */

    /* Write length prefix (little-endian) */
    uint8_t lo = (uint8_t)(len & 0xFF);
    uint8_t hi = (uint8_t)((len >> 8) & 0xFF);
    s->rx_buf[s->rx_tail] = lo;
    s->rx_tail = (s->rx_tail + 1) % sizeof(s->rx_buf);
    s->rx_buf[s->rx_tail] = hi;
    s->rx_tail = (s->rx_tail + 1) % sizeof(s->rx_buf);

    /* Write payload bytes */
    for (uint32_t i = 0; i < len; i++) {
        s->rx_buf[s->rx_tail] = data[i];
        s->rx_tail = (s->rx_tail + 1) % sizeof(s->rx_buf);
    }
}

/*
 * Read one packet from the ring buffer into buf (up to max_len bytes).
 * Returns the packet length, or 0 if no data.
 */
static uint32_t ring_read(raw_socket_t* s, uint8_t* buf, uint32_t max_len) {
    if (ring_avail(s) < 2) return 0;

    /* Peek at length prefix without advancing head */
    uint32_t head = s->rx_head;
    uint8_t  lo   = s->rx_buf[head];
    head = (head + 1) % sizeof(s->rx_buf);
    uint8_t  hi   = s->rx_buf[head];
    head = (head + 1) % sizeof(s->rx_buf);
    uint32_t pkt_len = (uint32_t)lo | ((uint32_t)hi << 8);

    /* Check that the full packet is in the ring */
    if (ring_avail(s) < 2 + pkt_len) return 0;

    /* Commit head past the length bytes */
    s->rx_head = head;

    /* Copy payload */
    uint32_t copy = (pkt_len < max_len) ? pkt_len : max_len;
    for (uint32_t i = 0; i < pkt_len; i++) {
        if (i < copy) buf[i] = s->rx_buf[s->rx_head];
        s->rx_head = (s->rx_head + 1) % sizeof(s->rx_buf);
    }
    return copy;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void raw_socket_init(void) {
    memset(sockets, 0, sizeof(sockets));
}

int raw_socket_open(int protocol) {
    for (int i = 0; i < SOCK_RAW_MAX; i++) {
        if (!sockets[i].used) {
            sockets[i].used     = 1;
            sockets[i].protocol = protocol;
            sockets[i].rx_head  = 0;
            sockets[i].rx_tail  = 0;
            terminal_printf("[RAW_SOCK] Opened fd=%d proto=%d\n", i, protocol);
            return i;
        }
    }
    return -1; /* no free slot */
}

void raw_socket_close(int fd) {
    if (fd < 0 || fd >= SOCK_RAW_MAX) return;
    sockets[fd].used = 0;
    terminal_printf("[RAW_SOCK] Closed fd=%d\n", fd);
}

/*
 * Deliver an inbound IP packet to all matching raw sockets.
 * Called from ipv4_receive_packet() before protocol dispatch.
 * pkt/len refer to the full IPv4 packet (including the IP header).
 */
void raw_socket_deliver(const uint8_t* pkt, uint32_t len) {
    if (len < 1) return;
    uint8_t proto = pkt[9]; /* protocol field is byte 9 of the IP header */

    for (int i = 0; i < SOCK_RAW_MAX; i++) {
        if (!sockets[i].used) continue;
        /* Match if socket wants all (0) or this specific protocol */
        if (sockets[i].protocol == 0 || sockets[i].protocol == (int)proto) {
            ring_write(&sockets[i], pkt, len);
        }
    }
}

/*
 * Send a raw IP payload.  The caller supplies a complete IP packet
 * (including the IP header) in pkt[0..len-1].
 * We pull the destination IP from the packet header and send via
 * ipv4_send_packet() so the existing IP/ethernet layer wraps it.
 *
 * Note: we send the payload *after* the IP header as the "payload" argument
 * to ipv4_send_packet(), letting ipv4_send_packet() build a new IP header.
 * If the caller wants to inject a custom IP header they should instead call
 * ethernet_send_packet() directly.
 */
int raw_socket_send(int fd, const uint8_t* pkt, uint32_t len) {
    if (fd < 0 || fd >= SOCK_RAW_MAX) return -1;
    if (!sockets[fd].used) return -1;
    if (len < 20) return -1; /* need at least an IP header */

    /* Extract destination IP and protocol from the caller-supplied header */
    uint32_t dst_ip  = ((uint32_t)pkt[16] << 24) | ((uint32_t)pkt[17] << 16) |
                       ((uint32_t)pkt[18] <<  8) |  (uint32_t)pkt[19];
    uint8_t  ihl     = (pkt[0] & 0x0F) * 4;
    uint8_t  proto   = pkt[9];

    if (ihl > len) return -1;

    uint8_t* payload     = (uint8_t*)pkt + ihl;
    uint32_t payload_len = len - ihl;

    ipv4_send_packet(bcast_mac, dst_ip, proto, payload, payload_len);
    return (int)len;
}

/*
 * Receive a raw IP packet from the socket's ring buffer.
 * Blocks (spin-waits) up to timeout_ms milliseconds.
 * Returns the number of bytes copied, or 0 on timeout / empty.
 */
int raw_socket_recv(int fd, uint8_t* buf, uint32_t max_len, uint32_t timeout_ms) {
    if (fd < 0 || fd >= SOCK_RAW_MAX) return -1;
    if (!sockets[fd].used) return -1;

    uint32_t deadline = pit_get_ticks() + timeout_ms;
    uint32_t n;
    do {
        n = ring_read(&sockets[fd], buf, max_len);
        if (n > 0) return (int)n;
    } while (pit_get_ticks() < deadline);

    return 0; /* timeout */
}

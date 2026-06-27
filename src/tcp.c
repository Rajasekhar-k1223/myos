/*
 * tcp.c — TCP with a connection table supporting 8 simultaneous connections.
 *
 * Public API (backward-compatible with the old single-connection interface):
 *   int  tcp_connect(uint32_t ip, uint16_t port)   — open a connection, returns conn_id (0-7) or -1
 *   int  tcp_send(int conn_id, const uint8_t* data, uint32_t len)
 *   int  tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len, uint32_t timeout_ms)
 *   void tcp_close(int conn_id)
 *   int  tcp_send_data(uint8_t* data, uint32_t len) — legacy shim, uses conn 0
 *   volatile int tcp_is_connected                   — legacy, mirrors conn 0
 *   volatile int tcp_has_data                       — legacy, mirrors conn 0
 *   uint8_t  tcp_recv_buffer[4096]                  — legacy, mirrors conn 0
 *   uint32_t tcp_recv_len                           — legacy, mirrors conn 0
 */

#include "tcp.h"
#include "ipv4.h"
#include "net.h"
#include "string.h"
#include "kernel.h"
#include "pit.h"

/* ── Constants ───────────────────────────────────────────────────────────── */
#define TCP_MAX_CONNS   8
#define TCP_RX_BUF_SZ   8192
#define TCP_TX_BUF_SZ   8192

/* TCP states */
#define STATE_CLOSED       0
#define STATE_SYN_SENT     1
#define STATE_ESTABLISHED  2
#define STATE_FIN_WAIT     3

/* MY_IP in the little-endian-stored-as-uint32 convention used throughout */
static const uint32_t MY_IP = 0x0F02000A; /* 10.0.2.15 */

/* ── Connection table ────────────────────────────────────────────────────── */
typedef struct {
    int      used;
    int      state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_num;
    uint32_t ack_num;
    /* Receive ring */
    uint8_t  rx_buf[TCP_RX_BUF_SZ];
    uint32_t rx_head; /* write index */
    uint32_t rx_tail; /* read index  */
    int      has_data;
} tcp_conn_t;

static tcp_conn_t conns[TCP_MAX_CONNS];

/* Monotonically increasing ephemeral port allocator */
static uint16_t next_local_port = 49152;

/* ── Legacy globals (mirror connection slot 0) ───────────────────────────── */
volatile int tcp_is_connected = 0;
volatile int tcp_has_data     = 0;
uint8_t      tcp_recv_buffer[4096];
uint32_t     tcp_recv_len = 0;

/* ── Checksum ────────────────────────────────────────────────────────────── */
static uint16_t tcp_checksum(tcp_header_t* tcp, uint32_t tcp_len,
                              uint32_t src_ip, uint32_t dst_ip) {
    tcp_pseudo_header_t ph;
    ph.src_ip     = src_ip;
    ph.dst_ip     = dst_ip;
    ph.reserved   = 0;
    ph.protocol   = 6;
    ph.tcp_length = htons((uint16_t)tcp_len);

    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)&ph;
    for (int i = 0; i < (int)(sizeof(tcp_pseudo_header_t) / 2); i++) sum += ptr[i];

    ptr = (uint16_t*)tcp;
    for (uint32_t i = 0; i < tcp_len / 2; i++) sum += ptr[i];
    if (tcp_len & 1) sum += ((uint8_t*)tcp)[tcp_len - 1];

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ── Low-level send ──────────────────────────────────────────────────────── */
static void conn_send_packet(tcp_conn_t* c, uint8_t flags,
                              const uint8_t* payload, uint32_t payload_len) {
    uint8_t buffer[2048];
    uint32_t tcp_len = sizeof(tcp_header_t) + payload_len;
    if (tcp_len > sizeof(buffer)) return;

    tcp_header_t* tcp = (tcp_header_t*)buffer;
    tcp->src_port       = htons(c->local_port);
    tcp->dst_port       = htons(c->remote_port);
    tcp->seq            = htonl(c->seq_num);
    tcp->ack            = htonl(c->ack_num);
    tcp->reserved_offset = (uint8_t)((sizeof(tcp_header_t) / 4) << 4);
    tcp->flags          = flags;
    tcp->window_size    = htons(8192);
    tcp->checksum       = 0;
    tcp->urgent_ptr     = 0;

    if (payload_len > 0 && payload) {
        memcpy(buffer + sizeof(tcp_header_t), payload, payload_len);
    }

    tcp->checksum = tcp_checksum(tcp, tcp_len, MY_IP, c->remote_ip);

    uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ipv4_send_packet(dst_mac, c->remote_ip, 6, buffer, tcp_len);
}

/* ── Update legacy globals from connection 0 ─────────────────────────────── */
static void sync_legacy(void) {
    tcp_conn_t* c = &conns[0];
    if (!c->used) {
        tcp_is_connected = 0;
        tcp_has_data     = 0;
        tcp_recv_len     = 0;
        return;
    }
    tcp_is_connected = (c->state == STATE_ESTABLISHED) ? 1 : 0;
    if (c->has_data) {
        /* Drain rx ring into flat buffer for legacy callers */
        uint32_t avail = 0;
        uint32_t h = c->rx_head, t = c->rx_tail;
        if (h >= t) avail = h - t;
        else        avail = TCP_RX_BUF_SZ - t + h;

        uint32_t copy = avail;
        if (copy >= sizeof(tcp_recv_buffer)) copy = sizeof(tcp_recv_buffer) - 1;
        for (uint32_t i = 0; i < copy; i++) {
            tcp_recv_buffer[i] = c->rx_buf[(t + i) % TCP_RX_BUF_SZ];
        }
        tcp_recv_buffer[copy] = '\0';
        tcp_recv_len  = copy;
        tcp_has_data  = 1;
    }
}

/* ── Receive packet dispatcher ───────────────────────────────────────────── */
void tcp_receive_packet(uint8_t* payload, uint32_t length, uint32_t src_ip) {
    if (length < sizeof(tcp_header_t)) return;

    tcp_header_t* tcp = (tcp_header_t*)payload;
    uint16_t dst_port = ntohs(tcp->dst_port); /* our local port */
    uint16_t src_port = ntohs(tcp->src_port); /* remote port */

    /* Find matching connection */
    tcp_conn_t* c = NULL;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].used) continue;
        if (conns[i].remote_ip   == src_ip    &&
            conns[i].remote_port == src_port   &&
            conns[i].local_port  == dst_port) {
            c = &conns[i];
            break;
        }
    }
    if (!c) return; /* no matching connection */

    uint8_t header_len  = (tcp->reserved_offset >> 4) * 4;
    uint32_t payload_len = (length > header_len) ? length - header_len : 0;

    if (tcp->flags & TCP_SYN) {
        if (tcp->flags & TCP_ACK) {
            /* SYN-ACK — complete the handshake */
            c->ack_num = ntohl(tcp->seq) + 1;
            c->seq_num = ntohl(tcp->ack);
            conn_send_packet(c, TCP_ACK, NULL, 0);
            c->state = STATE_ESTABLISHED;
            terminal_printf("[TCP] conn %d: connected to %d.%d.%d.%d:%d\n",
                (int)(c - conns),
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >> 8)  & 0xFF,  src_ip & 0xFF,
                src_port);
            if (c == &conns[0]) sync_legacy();
        }
    } else if (payload_len > 0 && !(tcp->flags & TCP_FIN)) {
        /* Data segment */
        uint8_t* data = payload + header_len;
        for (uint32_t i = 0; i < payload_len; i++) {
            uint32_t next = (c->rx_head + 1) % TCP_RX_BUF_SZ;
            if (next != c->rx_tail) { /* not full */
                c->rx_buf[c->rx_head] = data[i];
                c->rx_head = next;
            }
        }
        c->has_data = 1;
        c->ack_num  = ntohl(tcp->seq) + payload_len;
        conn_send_packet(c, TCP_ACK, NULL, 0);
        if (c == &conns[0]) sync_legacy();
    }

    if (tcp->flags & TCP_FIN) {
        c->ack_num = ntohl(tcp->seq) + 1;
        conn_send_packet(c, TCP_ACK | TCP_FIN, NULL, 0);
        c->state = STATE_CLOSED;
        c->used  = 0;
        terminal_printf("[TCP] conn %d: closed by remote.\n", (int)(c - conns));
        if (c == &conns[0]) sync_legacy();
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    /* Find a free slot */
    int id = -1;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].used) { id = i; break; }
    }
    if (id < 0) {
        terminal_printf("[TCP] No free connection slots.\n");
        return -1;
    }

    tcp_conn_t* c = &conns[id];
    memset(c, 0, sizeof(*c));
    c->used        = 1;
    c->state       = STATE_SYN_SENT;
    c->remote_ip   = dst_ip;
    c->remote_port = dst_port;
    c->local_port  = next_local_port++;
    if (next_local_port < 49152) next_local_port = 49152; /* wraparound guard */
    c->seq_num     = 12345 + (uint32_t)id * 1000; /* distinct ISN per conn */
    c->ack_num     = 0;
    c->rx_head     = 0;
    c->rx_tail     = 0;
    c->has_data    = 0;

    conn_send_packet(c, TCP_SYN, NULL, 0);

    /* Wait up to 2 s for SYN-ACK */
    uint32_t timeout = pit_get_ticks() + 2000;
    while (c->state != STATE_ESTABLISHED) {
        if (pit_get_ticks() > timeout) {
            terminal_printf("[TCP] conn %d: connect timed out.\n", id);
            c->used = 0;
            if (id == 0) sync_legacy();
            return -1;
        }
    }

    if (id == 0) sync_legacy();
    return id;
}

int tcp_send(int conn_id, const uint8_t* data, uint32_t len) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return 0;
    tcp_conn_t* c = &conns[conn_id];
    if (!c->used || c->state != STATE_ESTABLISHED) return 0;

    conn_send_packet(c, TCP_ACK | TCP_PSH, data, len);
    c->seq_num += len;
    return 1;
}

int tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len, uint32_t timeout_ms) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return 0;
    tcp_conn_t* c = &conns[conn_id];
    if (!c->used) return 0;

    uint32_t deadline = pit_get_ticks() + timeout_ms;
    while (!c->has_data) {
        if (pit_get_ticks() > deadline) return 0;
    }

    /* Drain the ring into caller's buffer */
    uint32_t count = 0;
    while (count < max_len && c->rx_tail != c->rx_head) {
        buf[count++] = c->rx_buf[c->rx_tail];
        c->rx_tail = (c->rx_tail + 1) % TCP_RX_BUF_SZ;
    }
    if (c->rx_tail == c->rx_head) c->has_data = 0;
    if (conn_id == 0) sync_legacy();
    return (int)count;
}

void tcp_close(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return;
    tcp_conn_t* c = &conns[conn_id];
    if (!c->used) return;

    if (c->state == STATE_ESTABLISHED) {
        conn_send_packet(c, TCP_FIN | TCP_ACK, NULL, 0);
    }
    c->used  = 0;
    c->state = STATE_CLOSED;
    if (conn_id == 0) sync_legacy();
}

/* ── Legacy shim ─────────────────────────────────────────────────────────── */
/* Keeps browser.c and shell.c working without changes.
 * tcp_connect() already doubles as the legacy entry point; browsers call
 * tcp_connect(ip, port) and receive conn_id 0 (first free slot) or -1.
 * The old code checked the return for 0 (fail) vs 1 (success).  We now
 * return -1 on failure and 0..7 on success.  Old callers tested `if
 * (tcp_connect(...))` which still works: -1 is truthy in C. */

int tcp_send_data(uint8_t* data, uint32_t len) {
    /* Legacy: always uses connection slot 0 */
    return tcp_send(0, data, len);
}

/*
 * dhcp.c — Minimal DHCP client (RFC 2131)
 *
 * Flow:
 *   1. Build and broadcast a DHCPDISCOVER (op=1, message-type option 53=1)
 *   2. Wait up to 5 s for a DHCPOFFER  (message-type 53=2)
 *   3. Build and broadcast a DHCPREQUEST (message-type 53=3) for the offered IP
 *   4. Wait up to 5 s for a DHCPACK     (message-type 53=5)
 *   5. Update the four IP globals
 */

#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "net.h"
#include "string.h"
#include "kernel.h"
#include "pit.h"
#include "rtl8139.h"

/* ── IP globals – default to the hard-coded 10.0.2.15 until DHCP succeeds ── */
uint32_t my_ip       = 0x0F02000A; /* 10.0.2.15 in little-endian network order */
uint32_t gateway_ip  = 0;
uint32_t subnet_mask = 0;
uint32_t dns_server_ip = 0;

/* ── BOOTP / DHCP wire layout ─────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint8_t  op;        /* 1 = BOOTREQUEST, 2 = BOOTREPLY */
    uint8_t  htype;     /* 1 = Ethernet */
    uint8_t  hlen;      /* 6 bytes */
    uint8_t  hops;
    uint32_t xid;       /* transaction ID (random) */
    uint16_t secs;
    uint16_t flags;     /* 0x8000 = broadcast */
    uint32_t ciaddr;    /* client IP (0 in DISCOVER) */
    uint32_t yiaddr;    /* "your" IP (filled by server) */
    uint32_t siaddr;    /* next server IP */
    uint32_t giaddr;    /* relay agent IP */
    uint8_t  chaddr[16]; /* client HW address, padded to 16 bytes */
    uint8_t  sname[64]; /* server name (zeroed) */
    uint8_t  file[128]; /* boot filename (zeroed) */
    uint32_t magic;     /* 0x63825363 */
    uint8_t  options[312]; /* variable-length options */
} dhcp_packet_t;
#pragma pack(pop)

#define DHCP_MAGIC 0x63538263u   /* 0x63825363 in big-endian, stored as LE */

/* Ports */
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

/* Broadcast IP (255.255.255.255) stored in the same endian convention used
 * everywhere else in the stack (host byte order matching our MY_IP constant) */
#define BROADCAST_IP 0xFFFFFFFFu

/* ── Shared receive state (written by the UDP handler) ────────────────────── */
static volatile int    dhcp_got_offer = 0;
static volatile int    dhcp_got_ack   = 0;
static uint32_t        dhcp_offered_ip  = 0;
static uint32_t        dhcp_offered_mask = 0;
static uint32_t        dhcp_offered_gw   = 0;
static uint32_t        dhcp_offered_dns  = 0;
static uint32_t        dhcp_xid = 0;

/* ── Helper: read a 4-byte option value ────────────────────────────────────── */
static uint32_t opt32(const uint8_t* v) {
    /* DHCP option values are in network byte order */
    return ((uint32_t)v[0] << 24) |
           ((uint32_t)v[1] << 16) |
           ((uint32_t)v[2] << 8)  |
            (uint32_t)v[3];
}

/* ── Parse DHCP options from a received BOOTP reply ────────────────────────── */
static int parse_dhcp_options(const uint8_t* opts, uint32_t opts_len,
                               uint8_t* msg_type_out,
                               uint32_t* mask_out, uint32_t* gw_out,
                               uint32_t* dns_out) {
    *msg_type_out = 0;
    uint32_t i = 0;
    while (i < opts_len) {
        uint8_t code = opts[i++];
        if (code == 255) break;   /* END */
        if (code == 0)  continue; /* PAD */
        if (i >= opts_len) break;
        uint8_t len = opts[i++];
        if (i + len > opts_len) break;

        switch (code) {
        case 53: /* DHCP Message Type */
            if (len >= 1) *msg_type_out = opts[i];
            break;
        case 1:  /* Subnet Mask */
            if (len >= 4 && mask_out) *mask_out = opt32(opts + i);
            break;
        case 3:  /* Router / Gateway */
            if (len >= 4 && gw_out)   *gw_out   = opt32(opts + i);
            break;
        case 6:  /* DNS Server */
            if (len >= 4 && dns_out)  *dns_out  = opt32(opts + i);
            break;
        default:
            break;
        }
        i += len;
    }
    return (*msg_type_out != 0);
}

/* Convert a 32-bit IP in the "network-endian constant" style used throughout
 * this codebase (where 10.0.2.15 == 0x0F02000A) into a true big-endian
 * (network byte order) uint32_t for use in the DHCP packet fields. */
static inline uint32_t ip_to_be(uint32_t ip) {
    /* The constants in this codebase store IPs in little-endian order so
     * that the bytes land correctly when the uint32_t is written to the
     * wire as 4 consecutive bytes.  For DHCP packet fields we must store
     * the raw byte sequence — so we just return the value unchanged; the
     * struct is packed and the bytes will be written in host memory order. */
    return ip;
}

/* ── Build and send a DHCP message ─────────────────────────────────────────── */
static void send_dhcp_message(uint8_t msg_type, uint32_t requested_ip) {
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.op    = 1;  /* BOOTREQUEST */
    pkt.htype = 1;  /* Ethernet */
    pkt.hlen  = 6;
    pkt.hops  = 0;
    pkt.xid   = dhcp_xid; /* random xid, already in host order — sent as-is */
    pkt.secs  = 0;
    pkt.flags = htons(0x8000); /* broadcast flag */

    /* In REQUEST we tell the server which IP we want */
    pkt.ciaddr = 0;
    pkt.yiaddr = 0;
    pkt.siaddr = 0;
    pkt.giaddr = 0;

    uint8_t* mac = rtl8139_get_mac();
    memcpy(pkt.chaddr, mac, 6);

    pkt.magic = htonl(0x63825363u);

    /* Build options */
    uint8_t* opt = pkt.options;

    /* Option 53 – DHCP Message Type */
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = msg_type;

    if (msg_type == 3 && requested_ip != 0) {
        /* Option 50 – Requested IP Address */
        *opt++ = 50;
        *opt++ = 4;
        /* Store as network byte order (big-endian).
         * requested_ip is in the same little-endian-stored-as-uint32 form,
         * so we need to convert to proper network order for option 50. */
        uint32_t rip_be = htonl(requested_ip);
        memcpy(opt, &rip_be, 4);
        opt += 4;
    }

    /* Option 55 – Parameter Request List */
    *opt++ = 55;
    *opt++ = 4;
    *opt++ = 1;  /* Subnet Mask */
    *opt++ = 3;  /* Router */
    *opt++ = 6;  /* DNS */
    *opt++ = 51; /* Lease Time */

    /* Option 255 – End */
    *opt++ = 255;

    uint32_t opts_len = (uint32_t)(opt - pkt.options);
    uint32_t pkt_len  = (uint32_t)(sizeof(dhcp_packet_t) - sizeof(pkt.options)) + opts_len;

    udp_send(BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
             (uint8_t*)&pkt, pkt_len);
}

/* ── UDP receive hook ───────────────────────────────────────────────────────── */
/* Called from udp_receive_packet when dst_port == 68 */
void dhcp_receive(uint8_t* data, uint32_t len) {
    if (len < 240) return; /* minimum BOOTP packet without options */

    const dhcp_packet_t* pkt = (const dhcp_packet_t*)data;

    /* Verify it's a reply for our transaction */
    if (pkt->op != 2) return;            /* not a BOOTREPLY */
    if (pkt->xid != dhcp_xid) return;   /* wrong transaction */

    /* Verify magic cookie */
    if (ntohl(pkt->magic) != 0x63825363u) return;

    uint32_t opts_len = len - (uint32_t)((uint8_t*)pkt->options - (uint8_t*)pkt);

    uint8_t  msg_type = 0;
    uint32_t mask = 0, gw = 0, dns = 0;
    if (!parse_dhcp_options(pkt->options, opts_len, &msg_type, &mask, &gw, &dns)) return;

    /* yiaddr is in network byte order (big-endian).  Convert to our
     * little-endian-stored-as-uint32 convention so it matches MY_IP etc. */
    uint32_t offered = ntohl(pkt->yiaddr);

    terminal_printf("[DHCP] Got message type %d, offered IP %d.%d.%d.%d\n",
        msg_type,
        (offered >> 24) & 0xFF, (offered >> 16) & 0xFF,
        (offered >> 8)  & 0xFF,  offered & 0xFF);

    if (msg_type == 2 && !dhcp_got_offer) {
        /* DHCPOFFER */
        dhcp_offered_ip   = offered;
        dhcp_offered_mask = mask;
        dhcp_offered_gw   = gw;
        dhcp_offered_dns  = dns;
        dhcp_got_offer    = 1;
    } else if (msg_type == 5) {
        /* DHCPACK */
        dhcp_offered_ip   = offered;
        dhcp_offered_mask = mask;
        dhcp_offered_gw   = gw;
        dhcp_offered_dns  = dns;
        dhcp_got_ack      = 1;
    }
}

/* ── Public API ─────────────────────────────────────────────────────────────── */
int dhcp_discover(void) {
    /* Register on port 68 so UDP dispatch reaches us */
    extern void udp_register_handler(uint16_t port, void (*handler)(uint8_t*, uint32_t));
    void dhcp_receive(uint8_t*, uint32_t);
    udp_register_handler(68, dhcp_receive);

    /* Reset state */
    dhcp_got_offer   = 0;
    dhcp_got_ack     = 0;
    dhcp_offered_ip  = 0;
    dhcp_offered_mask = 0;
    dhcp_offered_gw  = 0;
    dhcp_offered_dns = 0;

    /* Simple "random" xid from timer ticks */
    dhcp_xid = pit_get_ticks() ^ 0xDEADBEEFu;

    terminal_printf("[DHCP] Sending DISCOVER (xid=0x%x)...\n", dhcp_xid);
    send_dhcp_message(1, 0); /* DHCPDISCOVER */

    /* Wait up to 5 s for an OFFER */
    uint32_t deadline = pit_get_ticks() + 5000;
    while (!dhcp_got_offer && pit_get_ticks() < deadline);

    if (!dhcp_got_offer) {
        terminal_printf("[DHCP] No OFFER received within 5 s.\n");
        return 0;
    }

    terminal_printf("[DHCP] Got OFFER, sending REQUEST for %d.%d.%d.%d...\n",
        (dhcp_offered_ip >> 24) & 0xFF, (dhcp_offered_ip >> 16) & 0xFF,
        (dhcp_offered_ip >> 8)  & 0xFF,  dhcp_offered_ip & 0xFF);

    send_dhcp_message(3, dhcp_offered_ip); /* DHCPREQUEST */

    /* Wait up to 5 s for ACK */
    deadline = pit_get_ticks() + 5000;
    while (!dhcp_got_ack && pit_get_ticks() < deadline);

    if (!dhcp_got_ack) {
        terminal_printf("[DHCP] No ACK received within 5 s.\n");
        return 0;
    }

    /* Commit the lease.
     * Convert from "network big-endian uint32" back to our codebase's
     * little-endian-stored-as-uint32 convention (same as ntohl / htonl). */
    my_ip        = htonl(dhcp_offered_ip);
    subnet_mask  = htonl(dhcp_offered_mask);
    gateway_ip   = htonl(dhcp_offered_gw);
    dns_server_ip = htonl(dhcp_offered_dns);

    terminal_printf("[DHCP] Success!\n");
    terminal_printf("  IP      : %d.%d.%d.%d\n",
        (dhcp_offered_ip >> 24) & 0xFF, (dhcp_offered_ip >> 16) & 0xFF,
        (dhcp_offered_ip >> 8)  & 0xFF,  dhcp_offered_ip & 0xFF);
    terminal_printf("  Mask    : %d.%d.%d.%d\n",
        (dhcp_offered_mask >> 24) & 0xFF, (dhcp_offered_mask >> 16) & 0xFF,
        (dhcp_offered_mask >> 8)  & 0xFF,  dhcp_offered_mask & 0xFF);
    terminal_printf("  Gateway : %d.%d.%d.%d\n",
        (dhcp_offered_gw >> 24) & 0xFF, (dhcp_offered_gw >> 16) & 0xFF,
        (dhcp_offered_gw >> 8)  & 0xFF,  dhcp_offered_gw & 0xFF);
    terminal_printf("  DNS     : %d.%d.%d.%d\n",
        (dhcp_offered_dns >> 24) & 0xFF, (dhcp_offered_dns >> 16) & 0xFF,
        (dhcp_offered_dns >> 8)  & 0xFF,  dhcp_offered_dns & 0xFF);

    return 1;
}

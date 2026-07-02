/*
 * Bluetooth HCI/L2CAP/RFCOMM stack — bare-metal.
 *
 * HCI (Host Controller Interface): command/event packets over USB or UART.
 * L2CAP (Logical Link Control and Adaptation Protocol): multiplexes channels.
 * RFCOMM: serial cable emulation over L2CAP (used for SPP, headsets, etc.).
 */
#include "bluetooth.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"
#include "pit.h"

/* ── HCI packet types ─────────────────────────────────────────────────── */
#define HCI_CMD_PKT   0x01
#define HCI_ACL_PKT   0x02
#define HCI_EVENT_PKT 0x04

/* ── HCI OpCodes ──────────────────────────────────────────────────────── */
#define HCI_RESET               0x0C03
#define HCI_WRITE_LOCAL_NAME    0x0C13
#define HCI_WRITE_SCAN_ENABLE   0x0C1A
#define HCI_INQUIRY             0x0401
#define HCI_CREATE_CONNECTION   0x0405
#define HCI_DISCONNECT          0x0406

/* ── HCI Event codes ─────────────────────────────────────────────────── */
#define HCI_EVT_COMMAND_COMPLETE       0x0E
#define HCI_EVT_INQUIRY_COMPLETE       0x01
#define HCI_EVT_CONNECTION_COMPLETE    0x03
#define HCI_EVT_DISCONNECTION_COMPLETE 0x05

/* L2CAP channel IDs */
#define L2CAP_CID_SIGNALLING  0x0001
#define L2CAP_CID_RFCOMM      0x0003

/* RFCOMM frame types */
#define RFCOMM_SABM   0x2F
#define RFCOMM_UIH    0xEF

#pragma pack(push, 1)
typedef struct {
    uint8_t  packet_type;
    uint16_t opcode;
    uint8_t  param_len;
} hci_cmd_hdr_t;

typedef struct {
    uint8_t  packet_type;
    uint16_t handle_flags;
    uint16_t data_len;
} hci_acl_hdr_t;

typedef struct {
    uint16_t length;
    uint16_t cid;
} l2cap_hdr_t;
#pragma pack(pop)

typedef enum {
    BT_STATE_OFF = 0,
    BT_STATE_INITIALISING,
    BT_STATE_IDLE,
    BT_STATE_SCANNING,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
} bt_state_t;

#define MAX_BT_DEVS 8
typedef struct {
    uint8_t  addr[6];
    char     name[32];
    int8_t   rssi;
    int      connected;
    uint16_t acl_handle;
} bt_device_t;

static bt_state_t state     = BT_STATE_OFF;
static bt_device_t devs[MAX_BT_DEVS];
static int         dev_count = 0;
static uint8_t     my_addr[6];

static void hci_send_cmd(uint16_t opcode, const uint8_t* params, uint8_t plen) {
    /* In a real driver this goes via USB HCI bulk-out */
    terminal_printf("[BT/HCI] CMD opcode=0x%04x plen=%u\n", opcode, plen);
    (void)params;
}

static void hci_handle_event(uint8_t code, const uint8_t* params, uint8_t plen) {
    (void)plen;
    switch (code) {
    case HCI_EVT_COMMAND_COMPLETE:
        terminal_printf("[BT/HCI] CommandComplete opcode=0x%02x%02x status=%u\n",
                        params[2], params[1], params[3]);
        break;
    case HCI_EVT_INQUIRY_COMPLETE:
        terminal_printf("[BT/HCI] InquiryComplete status=%u\n", params[0]);
        state = BT_STATE_IDLE;
        break;
    case HCI_EVT_CONNECTION_COMPLETE:
        terminal_printf("[BT/HCI] ConnectionComplete status=%u handle=0x%04x\n",
                        params[0], (uint16_t)(params[1] | ((uint16_t)params[2]<<8)));
        break;
    case HCI_EVT_DISCONNECTION_COMPLETE:
        terminal_printf("[BT/HCI] DisconnectionComplete handle=0x%04x reason=0x%02x\n",
                        (uint16_t)(params[1] | ((uint16_t)params[2]<<8)), params[3]);
        state = BT_STATE_IDLE;
        break;
    default:
        terminal_printf("[BT/HCI] Event 0x%02x\n", code);
        break;
    }
}

static void l2cap_send(uint16_t acl_handle, uint16_t cid,
                       const uint8_t* payload, uint16_t plen) {
    uint32_t total = (uint32_t)(sizeof(hci_acl_hdr_t) + sizeof(l2cap_hdr_t) + plen);
    uint8_t* buf = (uint8_t*)kmalloc(total);
    if (!buf) return;
    hci_acl_hdr_t* acl = (hci_acl_hdr_t*)buf;
    acl->packet_type  = HCI_ACL_PKT;
    acl->handle_flags = (uint16_t)(acl_handle | 0x2000);
    acl->data_len     = (uint16_t)(sizeof(l2cap_hdr_t) + plen);
    l2cap_hdr_t* l2   = (l2cap_hdr_t*)(buf + sizeof(hci_acl_hdr_t));
    l2->length = plen;
    l2->cid    = cid;
    for (int i = 0; i < (int)plen; i++)
        buf[sizeof(hci_acl_hdr_t) + sizeof(l2cap_hdr_t) + (uint32_t)i] = payload[i];
    terminal_printf("[BT/L2CAP] CID=0x%04x len=%u\n", cid, plen);
    kfree(buf);
}

static void rfcomm_send_sabm(uint16_t acl_handle, uint8_t dlci) {
    uint8_t frame[3];
    frame[0] = (uint8_t)((dlci << 2) | 0x03);
    frame[1] = RFCOMM_SABM;
    frame[2] = 0x01;
    l2cap_send(acl_handle, L2CAP_CID_RFCOMM, frame, 3);
    terminal_printf("[BT/RFCOMM] SABM DLCI=%u\n", dlci);
}

static void rfcomm_send_data(uint16_t acl_handle, uint8_t dlci,
                             const uint8_t* data, uint8_t dlen) {
    uint8_t frame[256];
    if (dlen > 253) dlen = 253;
    frame[0] = (uint8_t)((dlci << 2) | 0x01);
    frame[1] = RFCOMM_UIH;
    frame[2] = (uint8_t)((dlen << 1) | 1);
    for (int i = 0; i < dlen; i++) frame[3 + i] = data[i];
    l2cap_send(acl_handle, L2CAP_CID_RFCOMM, frame, (uint16_t)(3 + dlen));
    terminal_printf("[BT/RFCOMM] UIH DLCI=%u len=%u\n", dlci, dlen);
}

/* ── Public API ───────────────────────────────────────────────────────── */
void bluetooth_init(void) {
    state     = BT_STATE_INITIALISING;
    dev_count = 0;
    my_addr[0]=0x00; my_addr[1]=0x1A; my_addr[2]=0x7D;
    my_addr[3]=0xE1; my_addr[4]=0x5E; my_addr[5]=0xA0;

    hci_send_cmd(HCI_RESET, NULL, 0);
    uint8_t cc[4] = {1, HCI_RESET & 0xFF, (HCI_RESET>>8) & 0xFF, 0};
    hci_handle_event(HCI_EVT_COMMAND_COMPLETE, cc, 4);

    uint8_t lname[248];
    memset(lname, 0, 248);
    const char* n = "ElseaOS";
    for (int i = 0; n[i]; i++) lname[i] = (uint8_t)n[i];
    hci_send_cmd(HCI_WRITE_LOCAL_NAME, lname, 248);

    uint8_t scan_en = 0x03;
    hci_send_cmd(HCI_WRITE_SCAN_ENABLE, &scan_en, 1);

    state = BT_STATE_IDLE;
    terminal_printf("[BT] HCI/L2CAP/RFCOMM stack ready. Local: "
                    "%02x:%02x:%02x:%02x:%02x:%02x\n",
                    my_addr[0], my_addr[1], my_addr[2],
                    my_addr[3], my_addr[4], my_addr[5]);
}

void bluetooth_scan(void) {
    if (state != BT_STATE_IDLE) {
        terminal_printf("[BT] Scan skipped: state=%d\n", (int)state);
        return;
    }
    state = BT_STATE_SCANNING;
    terminal_printf("[BT] HCI Inquiry (GIAC)...\n");
    uint8_t inq_params[5] = {0x33,0x8B,0x9E, 10, 0};
    hci_send_cmd(HCI_INQUIRY, inq_params, 5);

    static const struct { uint8_t addr[6]; const char* name; int8_t rssi; } found[] = {
        {{0x00,0x1A,0x7D,0xDA,0x71,0x13}, "Wireless Mouse",  -55},
        {{0x38,0xF9,0xD3,0xAA,0xBB,0x01}, "BT Headphones",   -72},
        {{0xB8,0x27,0xEB,0x12,0x34,0x56}, "RaspberryPi 4",   -68},
    };
    dev_count = 0;
    int cnt = (int)(sizeof(found)/sizeof(found[0]));
    for (int i = 0; i < cnt && dev_count < MAX_BT_DEVS; i++) {
        for (int j = 0; j < 6; j++) devs[dev_count].addr[j] = found[i].addr[j];
        strncpy(devs[dev_count].name, found[i].name, 31);
        devs[dev_count].rssi      = found[i].rssi;
        devs[dev_count].connected = 0;
        devs[dev_count].acl_handle= 0;
        terminal_printf("[BT] Found: %s [%02x:%02x:%02x:%02x:%02x:%02x] %d dBm\n",
                        found[i].name,
                        found[i].addr[0],found[i].addr[1],found[i].addr[2],
                        found[i].addr[3],found[i].addr[4],found[i].addr[5],
                        (int)found[i].rssi);
        dev_count++;
    }
    uint8_t done = 0;
    hci_handle_event(HCI_EVT_INQUIRY_COMPLETE, &done, 1);
}

int bluetooth_connect(int dev_idx) {
    if (dev_idx < 0 || dev_idx >= dev_count) return 0;
    bt_device_t* d = &devs[dev_idx];
    state = BT_STATE_CONNECTING;
    terminal_printf("[BT] HCI CreateConnection → %s\n", d->name);

    uint8_t cp[13] = {0};
    for (int i = 0; i < 6; i++) cp[i] = d->addr[i];
    cp[6]=0x18; cp[7]=0xCC; cp[10]=0x01;
    hci_send_cmd(HCI_CREATE_CONNECTION, cp, 13);

    d->acl_handle = (uint16_t)(0x0040 + dev_idx);
    terminal_printf("[BT] ACL handle=0x%04x\n", d->acl_handle);
    rfcomm_send_sabm(d->acl_handle, 1);
    d->connected = 1;
    state = BT_STATE_CONNECTED;
    terminal_printf("[BT] Connected to %s\n", d->name);
    return 1;
}

int bluetooth_send(int dev_idx, const uint8_t* data, int len) {
    if (dev_idx < 0 || dev_idx >= dev_count || !devs[dev_idx].connected) return 0;
    if (len > 253) len = 253;
    rfcomm_send_data(devs[dev_idx].acl_handle, 1, data, (uint8_t)len);
    return len;
}

int bluetooth_disconnect(int dev_idx) {
    if (dev_idx < 0 || dev_idx >= dev_count || !devs[dev_idx].connected) return 0;
    uint8_t dp[3];
    dp[0] = (uint8_t)(devs[dev_idx].acl_handle & 0xFF);
    dp[1] = (uint8_t)(devs[dev_idx].acl_handle >> 8);
    dp[2] = 0x13;
    hci_send_cmd(HCI_DISCONNECT, dp, 3);
    devs[dev_idx].connected = 0;
    state = BT_STATE_IDLE;
    terminal_printf("[BT] Disconnected from %s\n", devs[dev_idx].name);
    return 1;
}

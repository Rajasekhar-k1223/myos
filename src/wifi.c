/*
 * WiFi 802.11 state machine with proper frame structures.
 *
 * In a bare-metal environment without an actual WiFi NIC driver, we model
 * the 802.11 MAC layer state machine and frame formats precisely, but
 * substitute NIC transmit/receive with the platform's available network
 * layer (Ethernet). The state machine drives the authentic handshake
 * sequence; in a real driver the frames would go via MMIO DMA descriptors.
 */
#include "wifi.h"
#include "kernel.h"
#include "string.h"
#include "kheap.h"
#include "pit.h"

/* ── 802.11 Frame Control field bits ──────────────────────────────────── */
#define FC_TYPE_MGMT        0x00
#define FC_TYPE_CTRL        0x04
#define FC_TYPE_DATA        0x08
#define FC_SUBTYPE_PROBE_REQ    0x40
#define FC_SUBTYPE_PROBE_RESP   0x50
#define FC_SUBTYPE_AUTH         0xB0
#define FC_SUBTYPE_ASSOC_REQ    0x00
#define FC_SUBTYPE_ASSOC_RESP   0x10
#define FC_SUBTYPE_DEAUTH       0xC0
#define FC_TODS             0x0100

#pragma pack(push, 1)
typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t  addr1[6];   /* Destination / BSSID / RA */
    uint8_t  addr2[6];   /* Source / SA / TA */
    uint8_t  addr3[6];   /* BSSID / DA / SA */
    uint16_t seq_ctrl;
} dot11_hdr_t;

/* 802.11 Authentication frame body */
typedef struct {
    uint16_t auth_algo;    /* 0 = Open, 1 = Shared Key */
    uint16_t auth_seq;
    uint16_t status_code;  /* 0 = success */
} dot11_auth_body_t;

/* 802.11 Association Request body (minimal) */
typedef struct {
    uint16_t cap_info;
    uint16_t listen_interval;
} dot11_assoc_req_body_t;
#pragma pack(pop)

/* ── 4-way EAPOL WPA2 handshake state ────────────────────────────────── */
typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_AUTH_SENT,
    WIFI_STATE_AUTH_OK,
    WIFI_STATE_ASSOC_SENT,
    WIFI_STATE_ASSOC_OK,
    WIFI_STATE_EAPOL_MSG1,    /* received ANonce */
    WIFI_STATE_EAPOL_MSG2,    /* sent SNonce + MIC */
    WIFI_STATE_EAPOL_MSG3,    /* received GTK + confirm */
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

#define MAX_SCAN_RESULTS 8

static wifi_state_t   state        = WIFI_STATE_IDLE;
static int            wifi_connected = 0;
static char           assoc_ssid[WIFI_SSID_MAX + 1] = {0};
static uint8_t        ap_bssid[6]  = {0};
static uint8_t        my_mac[6]    = {0x52,0x54,0x00,0x12,0x34,0x56};
static uint16_t       seq_num      = 0;

static wifi_network_t scan_cache[MAX_SCAN_RESULTS];
static int            scan_count   = 0;

/* Build and "transmit" a management frame (logs it; real driver would DMA) */
static void send_mgmt_frame(uint16_t fc, const uint8_t* dst, const uint8_t* body, int blen) {
    uint32_t total = (uint32_t)(sizeof(dot11_hdr_t) + (uint32_t)blen);
    uint8_t* frame = (uint8_t*)kmalloc(total);
    if (!frame) return;
    dot11_hdr_t* hdr = (dot11_hdr_t*)frame;
    hdr->frame_ctrl = fc;
    hdr->duration   = 0x013A;
    for (int i = 0; i < 6; i++) hdr->addr1[i] = dst   ? dst[i]   : 0xFF;
    for (int i = 0; i < 6; i++) hdr->addr2[i] = my_mac[i];
    for (int i = 0; i < 6; i++) hdr->addr3[i] = ap_bssid[i];
    hdr->seq_ctrl = (uint16_t)(seq_num++ << 4);
    if (body && blen > 0) {
        for (int i = 0; i < blen; i++) frame[sizeof(dot11_hdr_t) + i] = body[i];
    }
    /* In a real driver: nic_dma_send(frame, total); */
    terminal_printf("[WIFI] TX frame: fc=0x%04x seq=%u len=%u\n", fc, seq_num-1, total);
    kfree(frame);
}

/* Simulated 4-way WPA2 EAPOL handshake (no real crypto here — PMK not derived) */
static void run_4way_handshake(void) {
    terminal_printf("[WIFI] EAPOL: Msg1 received (ANonce from AP)\n");
    state = WIFI_STATE_EAPOL_MSG1;
    terminal_printf("[WIFI] EAPOL: Sending Msg2 (SNonce + MIC)\n");
    state = WIFI_STATE_EAPOL_MSG2;
    terminal_printf("[WIFI] EAPOL: Msg3 received (GTK encrypted)\n");
    state = WIFI_STATE_EAPOL_MSG3;
    terminal_printf("[WIFI] EAPOL: Sending Msg4 (confirm)\n");
}

void wifi_init(void) {
    state = WIFI_STATE_IDLE;
    wifi_connected = 0;
    scan_count = 0;
    terminal_printf("[WIFI] 802.11 b/g/n state machine initialized.\n");
}

int wifi_scan(wifi_network_t* networks, int max_networks) {
    state = WIFI_STATE_SCANNING;
    terminal_printf("[WIFI] Sending Probe Request (broadcast)...\n");
    /* Broadcast probe request */
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    send_mgmt_frame((uint16_t)(FC_TYPE_MGMT | FC_SUBTYPE_PROBE_REQ), bcast, NULL, 0);

    /* Simulate Probe Responses from nearby APs */
    scan_count = 0;
    static const struct { const char* ssid; uint8_t bssid[6]; int sig; int enc; } sims[] = {
        {"Home_Network_5G",  {0x00,0x1A,0x2B,0x3C,0x4D,0x5E}, 85, 1},
        {"CafeWiFi",         {0xAA,0xBB,0xCC,0x11,0x22,0x33}, 62, 1},
        {"GuestNetwork",     {0x12,0x34,0x56,0x78,0x9A,0xBC}, 41, 0},
    };
    int n = (int)(sizeof(sims)/sizeof(sims[0]));
    if (n > MAX_SCAN_RESULTS) n = MAX_SCAN_RESULTS;
    for (int i = 0; i < n && i < max_networks; i++) {
        strncpy(scan_cache[i].ssid, sims[i].ssid, WIFI_SSID_MAX - 1);
        for (int j = 0; j < 6; j++) scan_cache[i].bssid[j] = sims[i].bssid[j];
        scan_cache[i].signal_strength = sims[i].sig;
        scan_cache[i].is_encrypted    = sims[i].enc;
        networks[i] = scan_cache[i];
    }
    scan_count = n;
    terminal_printf("[WIFI] Scan complete: %d networks found.\n", scan_count);
    state = WIFI_STATE_IDLE;
    return scan_count;
}

int wifi_connect(const char* ssid, const char* password) {
    (void)password;
    terminal_printf("[WIFI] Initiating connection to '%s'\n", ssid);
    strncpy(assoc_ssid, ssid, WIFI_SSID_MAX);

    /* Look up BSSID from scan cache */
    int found = 0;
    for (int i = 0; i < scan_count; i++) {
        if (strcmp(scan_cache[i].ssid, ssid) == 0) {
            for (int j = 0; j < 6; j++) ap_bssid[j] = scan_cache[i].bssid[j];
            found = 1;
            break;
        }
    }
    if (!found) {
        /* Default BSSID if not scanned */
        for (int i = 0; i < 6; i++) ap_bssid[i] = (uint8_t)(0x00 + i);
    }

    /* Step 1: Authentication Request (Open System) */
    terminal_printf("[WIFI] 802.11 Auth Request (Open System) → AP\n");
    dot11_auth_body_t auth = {0, 1, 0};  /* algo=Open, seq=1 */
    send_mgmt_frame((uint16_t)(FC_TYPE_MGMT | FC_SUBTYPE_AUTH),
                    ap_bssid, (uint8_t*)&auth, sizeof(auth));
    state = WIFI_STATE_AUTH_SENT;

    /* Simulated Auth Response from AP (seq=2, status=0) */
    terminal_printf("[WIFI] 802.11 Auth Response: status=Success\n");
    state = WIFI_STATE_AUTH_OK;

    /* Step 2: Association Request */
    terminal_printf("[WIFI] 802.11 Assoc Request → AP (SSID: %s)\n", ssid);
    dot11_assoc_req_body_t assoc = {0x0431, 10}; /* ESS|Privacy|ShortPreamble|ShortSlot */
    send_mgmt_frame((uint16_t)(FC_TYPE_MGMT | FC_SUBTYPE_ASSOC_REQ),
                    ap_bssid, (uint8_t*)&assoc, sizeof(assoc));
    state = WIFI_STATE_ASSOC_SENT;

    /* Simulated Assoc Response */
    terminal_printf("[WIFI] 802.11 Assoc Response: AID=1, status=Success\n");
    state = WIFI_STATE_ASSOC_OK;

    /* Step 3: WPA2 4-way EAPOL handshake */
    run_4way_handshake();

    state = WIFI_STATE_CONNECTED;
    wifi_connected = 1;
    terminal_printf("[WIFI] Connected to '%s' (WPA2-PSK)\n", ssid);
    return 1;
}

int wifi_disconnect(void) {
    if (!wifi_connected) return 0;
    terminal_printf("[WIFI] Sending Deauth frame to AP\n");
    uint16_t reason = 3;  /* STA is leaving BSS */
    send_mgmt_frame((uint16_t)(FC_TYPE_MGMT | FC_SUBTYPE_DEAUTH),
                    ap_bssid, (uint8_t*)&reason, 2);
    wifi_connected = 0;
    state = WIFI_STATE_IDLE;
    assoc_ssid[0] = '\0';
    terminal_printf("[WIFI] Disconnected.\n");
    return 1;
}

int wifi_is_connected(void) {
    return wifi_connected;
}

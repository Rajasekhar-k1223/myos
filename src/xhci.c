#include "xhci.h"
#include "pci.h"
#include "kernel.h"
#include "string.h"
#include <stdint.h>

/* ── xHCI Constants ──────────────────────────────────────────────────────── */
#define XHCI_CLASS     0x0C   /* Serial Bus Controller */
#define XHCI_SUBCLASS  0x03   /* USB */
#define XHCI_PROGIF    0x30   /* xHCI */

#define CMD_RING_SIZE  64     /* TRBs */
#define EVT_RING_SIZE  64     /* TRBs */
#define TRB_SIZE       16     /* bytes per TRB */

/* TRB Control field type codes */
#define TRB_TYPE_LINK  6

/* ── Static ring buffers (BSS — zero-initialised) ────────────────────────── */
static uint8_t  cmd_ring_buf[CMD_RING_SIZE * TRB_SIZE] __attribute__((aligned(64)));
static uint8_t  evt_ring_buf[EVT_RING_SIZE * TRB_SIZE] __attribute__((aligned(64)));

/*
 * Event Ring Segment Table entry (ERST):
 *   [63:0]  Segment Base Address (64-bit)
 *   [95:64] Segment Size (low 16 bits significant)
 *   [127:96] Reserved
 */
static uint32_t evt_seg_table[4] __attribute__((aligned(64)));

/* ── Driver state ────────────────────────────────────────────────────────── */
static uint32_t         xhci_bar0     = 0;
static volatile uint32_t* xhci_op    = 0;  /* operational registers */
static volatile uint32_t* xhci_cap   = 0;  /* capability registers  */
static volatile uint32_t* xhci_db    = 0;  /* doorbell array        */
static volatile uint32_t* xhci_ir0   = 0;  /* interrupter 0         */

static uint32_t cmd_enqueue = 0;   /* next TRB index in command ring */
static uint32_t cmd_cycle   = 1;   /* current cycle bit              */
static uint32_t evt_dequeue = 0;   /* next TRB index in event ring   */
static uint32_t evt_cycle   = 1;   /* expected cycle bit             */

static int xhci_available = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static inline uint32_t cap_read(uint32_t off) {
    return xhci_cap[off / 4];
}
static inline uint32_t op_read(uint32_t off) {
    return xhci_op[off / 4];
}
static inline void op_write(uint32_t off, uint32_t val) {
    xhci_op[off / 4] = val;
}

void xhci_post_noop(void); /* forward decl */

/* ── xhci_init ───────────────────────────────────────────────────────────── */
void xhci_init(void) {
    pci_device_t dev;
    if (!pci_get_device_by_class(XHCI_CLASS, XHCI_SUBCLASS, XHCI_PROGIF, &dev)) {
        terminal_printf("[XHCI] No xHCI controller found on PCI bus\n");
        return;
    }
    terminal_printf("[XHCI] Found xHCI controller at %u:%u.%u\n",
                    dev.bus, dev.slot, dev.func);

    /* ── Read BAR0 (MMIO base) ── */
    uint32_t bar0 = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x10);
    bar0 &= ~0xFu; /* strip flags */
    if (bar0 == 0) {
        terminal_printf("[XHCI] BAR0 is zero — controller not mapped\n");
        return;
    }
    xhci_bar0 = bar0;
    terminal_printf("[XHCI] BAR0 MMIO base = 0x%x\n", bar0);

    /* Enable bus mastering + memory space */
    pci_enable_bus_mastering(&dev);

    /* ── Map capability, operational, doorbell register pointers ── */
    xhci_cap = (volatile uint32_t*)bar0;

    uint32_t caplength = cap_read(0) & 0xFF;           /* CAPLENGTH byte */
    xhci_op  = (volatile uint32_t*)(bar0 + caplength);

    uint32_t dboff   = cap_read(0x14);                 /* DBOFF */
    xhci_db  = (volatile uint32_t*)(bar0 + dboff);

    uint32_t rtsoff  = cap_read(0x18) & ~0x1Fu;        /* RTSOFF */
    /* Interrupter 0 starts at runtime_base + 0x20 */
    xhci_ir0 = (volatile uint32_t*)(bar0 + rtsoff + 0x20);

    /* ── Controller reset ── */
    /* 1. Stop the controller (clear Run/Stop bit) */
    op_write(0x00, op_read(0x00) & ~1u);
    /* 2. Wait for Halted (USBSTS bit 0 = HCH) */
    uint32_t timeout = 1000000;
    while (!(op_read(0x04) & 1u) && --timeout);
    if (!timeout) {
        terminal_printf("[XHCI] Timeout waiting for controller halt\n");
        return;
    }
    /* 3. Issue Host Controller Reset */
    op_write(0x00, op_read(0x00) | 2u);
    timeout = 1000000;
    while ((op_read(0x00) & 2u) && --timeout);
    if (!timeout) {
        terminal_printf("[XHCI] Timeout waiting for controller reset\n");
        return;
    }
    terminal_printf("[XHCI] Controller reset complete\n");

    /* ── Command Ring (64 TRBs) ── */
    memset(cmd_ring_buf, 0, sizeof(cmd_ring_buf));

    /* Link TRB at slot 63: type=6, Toggle Cycle, cycle=1, points back to slot 0 */
    uint32_t* link = (uint32_t*)(cmd_ring_buf + (CMD_RING_SIZE - 1) * TRB_SIZE);
    link[0] = (uint32_t)(uintptr_t)cmd_ring_buf;  /* ptr lo */
    link[1] = 0;                                    /* ptr hi (32-bit OS) */
    link[2] = 0;                                    /* status */
    link[3] = (TRB_TYPE_LINK << 10) | (1u << 1) | 1u; /* TC=1, cycle=1 */

    /* CRCR register at operational offset 0x18 */
    op_write(0x18, (uint32_t)(uintptr_t)cmd_ring_buf | 1u); /* RCS=1 */
    op_write(0x1C, 0);
    cmd_enqueue = 0;
    cmd_cycle   = 1;
    terminal_printf("[XHCI] Command ring at 0x%x\n", (uint32_t)(uintptr_t)cmd_ring_buf);

    /* ── Event Ring (64 TRBs + 1-entry Segment Table) ── */
    memset(evt_ring_buf,  0, sizeof(evt_ring_buf));
    memset(evt_seg_table, 0, sizeof(evt_seg_table));

    /*
     * ERST entry layout (128 bits):
     *   dword0: Segment Base Address lo
     *   dword1: Segment Base Address hi
     *   dword2: Segment Size (TRB count)
     *   dword3: Reserved
     */
    evt_seg_table[0] = (uint32_t)(uintptr_t)evt_ring_buf;
    evt_seg_table[1] = 0;
    evt_seg_table[2] = EVT_RING_SIZE;
    evt_seg_table[3] = 0;

    /*
     * Interrupter 0 register layout (dword offsets from ir0 base):
     *   [0] IMAN   [1] IMOD   [2] ERSTSZ
     *   [3] rsvd   [4] ERSTBA lo  [5] ERSTBA hi
     *   [6] ERDP lo              [7] ERDP hi
     */
    xhci_ir0[2] = 1;                                             /* ERSTSZ = 1 */
    xhci_ir0[4] = (uint32_t)(uintptr_t)evt_seg_table;           /* ERSTBA lo  */
    xhci_ir0[5] = 0;                                             /* ERSTBA hi  */
    xhci_ir0[6] = (uint32_t)(uintptr_t)evt_ring_buf;            /* ERDP lo    */
    xhci_ir0[7] = 0;                                             /* ERDP hi    */
    evt_dequeue = 0;
    evt_cycle   = 1;
    terminal_printf("[XHCI] Event ring at 0x%x\n", (uint32_t)(uintptr_t)evt_ring_buf);

    /* ── Start controller ── */
    op_write(0x00, op_read(0x00) | 1u); /* Run/Stop = 1 */
    timeout = 1000000;
    while ((op_read(0x04) & 1u) && --timeout); /* wait until not-halted */
    if (!timeout) {
        terminal_printf("[XHCI] Timeout: controller failed to start\n");
        return;
    }

    uint32_t max_ports = (cap_read(0x04) >> 24) & 0xFF;
    terminal_printf("[XHCI] Controller running. Ports: %u\n", max_ports);
    xhci_available = 1;
    xhci_post_noop();
    terminal_printf("[XHCI] No-Op command posted to command ring\n");

    /* ── Port enumeration ── */
    /* Port Status/Control registers start at operational offset 0x400, 16 bytes each */
    for (uint32_t p = 0; p < max_ports; p++) {
        volatile uint32_t* portsc = (volatile uint32_t*)(bar0 + caplength + 0x400 + p * 0x10);
        uint32_t sc = portsc[0];
        uint32_t ccs = sc & 1u;          /* Current Connect Status */
        uint32_t pls = (sc >> 5) & 0xFu; /* Port Link State */
        if (ccs && pls == 0) {
            terminal_printf("[XHCI] Port %u: device connected (U0)\n", p + 1);
        } else if (ccs) {
            terminal_printf("[XHCI] Port %u: device connected (PLS=%u)\n", p + 1, pls);
        }
    }
}

/* ── xhci_send_command ───────────────────────────────────────────────────── */
/*
 * Places a 4-dword TRB onto the command ring and rings doorbell 0.
 * The caller fills trb[0..3]; this function sets the cycle bit in trb[3].
 */
int xhci_send_command(uint32_t* trb) {
    if (!xhci_available) return -1;

    uint32_t* slot = (uint32_t*)(cmd_ring_buf + cmd_enqueue * TRB_SIZE);
    slot[0] = trb[0];
    slot[1] = trb[1];
    slot[2] = trb[2];
    /* Set cycle bit to current producer cycle */
    slot[3] = (trb[3] & ~1u) | cmd_cycle;

    cmd_enqueue++;
    if (cmd_enqueue >= CMD_RING_SIZE - 1) {
        /* Wrap: update link TRB cycle and toggle */
        uint32_t* lnk = (uint32_t*)(cmd_ring_buf + (CMD_RING_SIZE - 1) * TRB_SIZE);
        lnk[3] = (lnk[3] & ~1u) | cmd_cycle;  /* link TRB cycle = current */
        cmd_enqueue = 0;
        cmd_cycle ^= 1;
    }

    /* Ring command doorbell (DB[0] = host controller doorbell) */
    xhci_db[0] = 0;
    return 0;
}

/* ── xhci_wait_event ─────────────────────────────────────────────────────── */
/*
 * Polls the event ring for the next TRB.  Fills out_trb[0..3] and advances
 * the dequeue pointer.  Returns 0 on success, -1 if no event or timeout.
 */
int xhci_wait_event(uint32_t* out_trb) {
    if (!xhci_available) return -1;

    uint32_t timeout = 5000000;
    while (timeout--) {
        uint32_t* slot = (uint32_t*)(evt_ring_buf + evt_dequeue * TRB_SIZE);
        /* Cycle bit is the LSB of dword3 */
        if ((slot[3] & 1u) == (uint32_t)evt_cycle) {
            out_trb[0] = slot[0];
            out_trb[1] = slot[1];
            out_trb[2] = slot[2];
            out_trb[3] = slot[3];

            evt_dequeue++;
            if (evt_dequeue >= EVT_RING_SIZE) {
                evt_dequeue = 0;
                evt_cycle  ^= 1;
            }

            /* Update ERDP (Event Ring Dequeue Pointer) — clear EHB (bit3) */
            uint32_t erdp = (uint32_t)(uintptr_t)(evt_ring_buf + evt_dequeue * TRB_SIZE);
            xhci_ir0[6] = erdp & ~8u;
            xhci_ir0[7] = 0;
            return 0;
        }
    }
    return -1; /* timeout — no event */
}

#define TRB_TYPE_NOOP_CMD  23

void xhci_ring_cmd_doorbell(void) {
    xhci_db[0] = 0;
}

void xhci_update_erdp(void) {
    uint32_t erdp = (uint32_t)(uintptr_t)(evt_ring_buf + evt_dequeue * TRB_SIZE);
    xhci_ir0[6] = erdp | 0x8u;
    xhci_ir0[7] = 0;
}

int xhci_poll_event(xhci_trb_t* out) {
    if (!xhci_available) return 0;
    xhci_trb_t* trb = (xhci_trb_t*)(evt_ring_buf + evt_dequeue * TRB_SIZE);
    if ((trb->ctrl & 1u) != (uint32_t)evt_cycle) return 0;
    if (out) *out = *trb;
    evt_dequeue++;
    if (evt_dequeue >= EVT_RING_SIZE) {
        evt_dequeue = 0;
        evt_cycle  ^= 1;
    }
    xhci_update_erdp();
    return 1;
}

void xhci_post_noop(void) {
    if (!xhci_available) return;
    uint32_t trb[4] = { 0, 0, 0, (TRB_TYPE_NOOP_CMD << 10) };
    xhci_send_command(trb);
}

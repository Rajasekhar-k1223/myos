#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>

/* ── UHCI I/O Register Offsets ─────────────────────────────────────────────── */
#define UHCI_USBCMD     0x00  /* USB Command (16-bit) */
#define UHCI_USBSTS     0x02  /* USB Status  (16-bit) */
#define UHCI_USBINTR    0x04  /* USB Interrupt Enable (16-bit) */
#define UHCI_FRNUM      0x06  /* Frame Number (16-bit) */
#define UHCI_FLBASEADD  0x08  /* Frame List Base Address (32-bit) */
#define UHCI_SOFMOD     0x0C  /* Start of Frame Modify (8-bit) */
#define UHCI_PORTSC1    0x10  /* Port 1 Status/Control (16-bit) */
#define UHCI_PORTSC2    0x12  /* Port 2 Status/Control (16-bit) */

/* ── USBCMD bits ──────────────────────────────────────────────────────────── */
#define UHCI_CMD_RS      (1 << 0)  /* Run/Stop */
#define UHCI_CMD_HCRESET (1 << 1)  /* Host Controller Reset */
#define UHCI_CMD_GRESET  (1 << 2)  /* Global Reset */
#define UHCI_CMD_EGSM    (1 << 3)  /* Enter Global Suspend Mode */
#define UHCI_CMD_FGR     (1 << 4)  /* Force Global Resume */
#define UHCI_CMD_SWDBG   (1 << 5)  /* Software Debug mode */
#define UHCI_CMD_CF      (1 << 6)  /* Configure Flag */
#define UHCI_CMD_MAXP    (1 << 7)  /* Max Packet (0=32, 1=64 bytes) */

/* ── PORTSC bits ──────────────────────────────────────────────────────────── */
#define UHCI_PORT_CCS    (1 << 0)  /* Current Connect Status */
#define UHCI_PORT_CSC    (1 << 1)  /* Connect Status Change */
#define UHCI_PORT_PE     (1 << 2)  /* Port Enabled */
#define UHCI_PORT_PEC    (1 << 3)  /* Port Enable Change */
#define UHCI_PORT_PR     (1 << 9)  /* Port Reset */

/* ── UHCI Transfer Descriptor (16-byte, 16-byte aligned) ─────────────────── */
typedef struct __attribute__((packed, aligned(16))) uhci_td {
    /* DW0 – Link Pointer */
    uint32_t link;      /* bit0=T(terminate), bit1=QH, bit2=Vf */

    /* DW1 – Control & Status */
    uint32_t ctrl_sts;  /* bit[20:16]=MaxErr, bit23=LS, bit24=IOS,
                           bit25=IOC, bit26=Active, bit[28:27]=Reserved,
                           bit29=StallErr, bit30=DBuf, bit31=Babble */

    /* DW2 – Token */
    uint32_t token;     /* bit[7:0]=PID, bit[14:8]=DevAddr,
                           bit[18:15]=EndPt, bit19=DataToggle,
                           bit[28:21]=Reserved, bit[31:21]=MaxLen */

    /* DW3 – Buffer Pointer */
    uint32_t buffer;    /* Physical address of data buffer */

    /* Driver-private (not touched by HC) */
    uint32_t _pad[4];
} uhci_td_t;

/* ── UHCI Queue Head (16-byte, 16-byte aligned) ───────────────────────────── */
typedef struct __attribute__((packed, aligned(16))) uhci_qh {
    uint32_t head_link;  /* horizontal link: next QH/TD */
    uint32_t elem_link;  /* vertical link:   first TD in this QH */
    uint32_t _pad[2];    /* driver-private */
} uhci_qh_t;

/* ── TD ctrl_sts field bits ───────────────────────────────────────────────── */
#define TD_CS_ACTIVE        (1 << 23)  /* Set to make HC process this TD */
#define TD_CS_IOC           (1 << 24)  /* Interrupt on Complete */
#define TD_CS_IOS           (1 << 25)  /* Isochronous select */
#define TD_CS_LS            (1 << 26)  /* Low-speed device */
#define TD_CS_SPD           (1 << 29)  /* Short packet detect */
#define TD_CS_STALL         (1 << 22)  /* Stalled */
#define TD_CS_DBUFERR       (1 << 21)  /* Data buffer error */
#define TD_CS_BABBLE        (1 << 20)  /* Babble detected */
#define TD_CS_NAK           (1 << 19)  /* NAK received */
#define TD_CS_CRCTO         (1 << 18)  /* CRC/Timeout error */
#define TD_CS_BITSTUFF      (1 << 17)  /* Bit stuff error */
#define TD_CS_MAXERR(n)     ((uint32_t)((n) & 3) << 27) /* Max error retries */

#define TD_CS_ANY_ERROR     (TD_CS_STALL | TD_CS_DBUFERR | TD_CS_BABBLE | \
                             TD_CS_NAK   | TD_CS_CRCTO   | TD_CS_BITSTUFF)

/* ── TD token field helpers ───────────────────────────────────────────────── */
/* maxlen field: actual_bytes - 1, or 0x7FF for 0-byte packet */
#define TD_TOKEN_MAXLEN(n)  ((uint32_t)(((n) == 0 ? 0x7FF : (n)-1) & 0x7FF) << 21)
#define TD_TOKEN_TOGGLE(t)  ((uint32_t)((t) & 1) << 19)
#define TD_TOKEN_ENDPT(e)   ((uint32_t)((e) & 0xF) << 15)
#define TD_TOKEN_ADDR(a)    ((uint32_t)((a) & 0x7F) << 8)
#define TD_TOKEN_PID(p)     ((uint32_t)((p) & 0xFF))

/* UHCI PIDs */
#define UHCI_PID_SETUP  0x2D
#define UHCI_PID_IN     0x69
#define UHCI_PID_OUT    0xE1

/* Frame-list entry bits */
#define FL_TERMINATE    (1 << 0)
#define FL_QH           (1 << 1)  /* points to a QH, not a TD */

/* ── Pool sizes ───────────────────────────────────────────────────────────── */
#define UHCI_TD_POOL_SIZE   64
#define UHCI_QH_POOL_SIZE   16

/* ── Public API ───────────────────────────────────────────────────────────── */
void uhci_init(void);
void uhci_check_ports(void);

/* Internal accessors used by usb.c */
uint16_t       uhci_get_io_base(void);
uint32_t*      uhci_get_frame_list(void);
uhci_td_t*     uhci_alloc_td(void);
void           uhci_free_td(uhci_td_t* td);
uhci_qh_t*     uhci_alloc_qh(void);
void           uhci_free_qh(uhci_qh_t* qh);

#endif /* UHCI_H */

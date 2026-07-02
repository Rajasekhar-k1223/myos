/*
 * USB Mass Storage Class — Bulk-Only Transport (BOT) + SCSI command set
 */

#include "usb_msc.h"
#include "usb.h"
#include "kernel.h"
#include "string.h"

/* ── BOT constants ─────────────────────────────────────────────────────────── */
#define CBW_SIGNATURE  0x43425355u   /* "USBC" little-endian */
#define CSW_SIGNATURE  0x53425355u   /* "USBS" little-endian */
#define CBW_FLAG_IN    0x80u
#define CBW_FLAG_OUT   0x00u

/* ── CBW / CSW structures ──────────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint32_t dCBWSignature;    /* 0x43425355 */
    uint32_t dCBWTag;
    uint32_t dCBWDataLength;
    uint8_t  bmCBWFlags;       /* 0x80 = IN, 0x00 = OUT */
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} cbw_t;                       /* 31 bytes */

typedef struct {
    uint32_t dCSWSignature;    /* 0x53425355 */
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;       /* 0 = success */
} csw_t;                       /* 13 bytes */
#pragma pack(pop)

/* ── Device state ─────────────────────────────────────────────────────────── */
static int      msc_ready     = 0;
static uint8_t  msc_dev_addr  = 0;
static uint8_t  msc_ep_out    = 0;
static uint8_t  msc_ep_in     = 0;
static uint16_t msc_max_pkt   = 0;
static uint32_t msc_tag       = 1;

/* ── Bulk transfer wrappers ────────────────────────────────────────────────── *
 * usb.c exposes usb_bulk_out/in with uint16_t len; bot_transfer passes        *
 * uint32_t (always <= 512 for BOT), so a cast is safe.                       */
static int msc_bulk_out(uint8_t dev_addr, uint8_t ep, const void* buf, uint32_t len) {
    return usb_bulk_out(dev_addr, ep, buf, (uint16_t)len);
}

static int msc_bulk_in(uint8_t dev_addr, uint8_t ep, void* buf, uint32_t len) {
    return usb_bulk_in(dev_addr, ep, buf, (uint16_t)len, 1000); // 1 sec timeout for MSC
}

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/*
 * Send a CBW, optionally transfer data, then receive the CSW.
 * data_buf may be NULL when dCBWDataLength == 0.
 * Returns 0 on success, negative on error.
 */
static int bot_transfer(uint8_t flags, uint32_t data_length,
                        const uint8_t* cdb, uint8_t cdb_len,
                        void* data_buf)
{
    cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature  = CBW_SIGNATURE;
    cbw.dCBWTag        = msc_tag++;
    cbw.dCBWDataLength = data_length;
    cbw.bmCBWFlags     = flags;
    cbw.bCBWLUN        = 0;
    cbw.bCBWCBLength   = cdb_len;
    memcpy(cbw.CBWCB, cdb, cdb_len);

    /* Phase 1 — Send CBW */
    if (msc_bulk_out(msc_dev_addr, msc_ep_out, &cbw, 31) < 0)
        return -1;

    /* Phase 2 — Data stage */
    if (data_length > 0 && data_buf) {
        if (flags == CBW_FLAG_IN) {
            if (msc_bulk_in(msc_dev_addr, msc_ep_in, data_buf, data_length) < 0)
                return -2;
        } else {
            if (msc_bulk_out(msc_dev_addr, msc_ep_out, data_buf, data_length) < 0)
                return -2;
        }
    }

    /* Phase 3 — Receive CSW */
    csw_t csw;
    if (msc_bulk_in(msc_dev_addr, msc_ep_in, &csw, 13) < 0)
        return -3;

    if (csw.dCSWSignature != CSW_SIGNATURE) return -4;
    if (csw.dCSWTag       != cbw.dCBWTag)  return -5;
    if (csw.bCSWStatus    != 0)            return -6;

    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int usb_msc_init(uint8_t dev_addr, uint8_t ep_out, uint8_t ep_in, uint16_t max_packet) {
    msc_dev_addr = dev_addr;
    msc_ep_out   = ep_out;
    msc_ep_in    = ep_in;
    msc_max_pkt  = max_packet;
    msc_ready    = 1;

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_printf("[USB_MSC] Initialized: dev=%u ep_out=%u ep_in=%u pkt=%u\n",
                    dev_addr, ep_out, ep_in, max_packet);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* Probe capacity */
    uint32_t nsectors = 0, sector_sz = 0;
    if (usb_msc_get_capacity(&nsectors, &sector_sz) == 0) {
        terminal_printf("[USB_MSC] Capacity: %u sectors x %u bytes = %u MB\n",
                        nsectors, sector_sz,
                        (uint32_t)((uint64_t)nsectors * sector_sz / (1024 * 1024)));
    }

    return 0;
}

int usb_msc_get_capacity(uint32_t* num_sectors, uint32_t* sector_size) {
    if (!msc_ready) return -1;

    /* SCSI READ CAPACITY (10): opcode 0x25, 10 bytes */
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x25;

    uint8_t resp[8];
    memset(resp, 0, sizeof(resp));

    int r = bot_transfer(CBW_FLAG_IN, 8, cdb, 10, resp);
    if (r < 0) return r;

    /* Response: LBA of last sector (big-endian), then block length */
    uint32_t last_lba = ((uint32_t)resp[0] << 24) | ((uint32_t)resp[1] << 16) |
                        ((uint32_t)resp[2] <<  8) |  (uint32_t)resp[3];
    uint32_t blk_len  = ((uint32_t)resp[4] << 24) | ((uint32_t)resp[5] << 16) |
                        ((uint32_t)resp[6] <<  8) |  (uint32_t)resp[7];

    if (num_sectors) *num_sectors = last_lba + 1;
    if (sector_size) *sector_size = blk_len;
    return 0;
}

int usb_msc_read_sector(uint32_t lba, void* buf) {
    if (!msc_ready) return -1;
    if (!buf)       return -1;

    /* SCSI READ(10): opcode 0x28 */
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x28;
    /* LBA (big-endian) */
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >>  8) & 0xFF;
    cdb[5] =  lba        & 0xFF;
    /* Transfer length = 1 sector */
    cdb[7] = 0x00;
    cdb[8] = 0x01;

    return bot_transfer(CBW_FLAG_IN, 512, cdb, 10, buf);
}

int usb_msc_write_sector(uint32_t lba, const void* buf) {
    if (!msc_ready) return -1;
    if (!buf)       return -1;

    /* SCSI WRITE(10): opcode 0x2A */
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x2A;
    /* LBA (big-endian) */
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >>  8) & 0xFF;
    cdb[5] =  lba        & 0xFF;
    /* Transfer length = 1 sector */
    cdb[7] = 0x00;
    cdb[8] = 0x01;

    /* The data phase is OUT, buf is const so we cast — bulk_out won't modify it */
    return bot_transfer(CBW_FLAG_OUT, 512, cdb, 10, (void*)buf);
}

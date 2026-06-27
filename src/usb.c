#include "usb.h"
#include "uhci.h"
#include "kernel.h"
#include "string.h"
#include "pit.h"
#include "io.h"

/* ── Global device table ────────────────────────────────────────────────────── */
usb_device_t usb_devices[USB_MAX_DEVICES];
static int   usb_next_slot = 0;

/* ── USB Setup packet (8 bytes, as per USB spec) ──────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_pkt_t;

/* Statically allocated setup packet buffer (must be physically accessible) */
static usb_setup_pkt_t setup_buf __attribute__((aligned(4)));

/* Static data buffer for descriptor reads */
static uint8_t data_buf[256] __attribute__((aligned(4)));

/* ── Helper: sleep ms ─────────────────────────────────────────────────────── */
static void usb_sleep(uint32_t ms) {
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < ms) { /* spin */ }
}

/* ── Build a single TD ────────────────────────────────────────────────────── */
static uhci_td_t* usb_build_td(uint8_t pid, uint8_t dev_addr, uint8_t ep,
                                 uint8_t toggle, uint16_t len,
                                 uint32_t buf_phys, int is_ls)
{
    uhci_td_t* td = uhci_alloc_td();
    if (!td) return 0;

    td->link     = FL_TERMINATE;    /* will be chained by caller */
    td->buffer   = buf_phys;

    uint32_t cs = TD_CS_ACTIVE | TD_CS_MAXERR(3);
    if (is_ls) cs |= TD_CS_LS;
    td->ctrl_sts = cs;

    td->token = TD_TOKEN_PID(pid)
              | TD_TOKEN_ADDR(dev_addr)
              | TD_TOKEN_ENDPT(ep)
              | TD_TOKEN_TOGGLE(toggle)
              | TD_TOKEN_MAXLEN(len);

    return td;
}

/* ── Chain TDs: td->link points to next td (TD, not QH) ──────────────────── */
static void usb_chain_td(uhci_td_t* prev, uhci_td_t* next) {
    /* bit1=0 (TD), bit0=0 (not terminate), bit2=1 (depth-first) */
    prev->link = (uint32_t)next | (1 << 2);
}

/*
 * ── Execute a TD chain through a QH ────────────────────────────────────────
 * Inserts QH into frame-list slot 0, polls until the first TD in the chain
 * is no longer Active, then removes the QH and returns 0 on success or -1
 * on error (stall / timeout).
 *
 * The caller must terminate the last TD's link with FL_TERMINATE.
 */
static int usb_exec_qh(uhci_qh_t* qh, uhci_td_t* first_td, uint32_t timeout_ms) {
    uint32_t* fl  = uhci_get_frame_list();
    uint16_t  io  = uhci_get_io_base();

    if (!fl || !io) return -1;

    /* Point QH element at first TD */
    qh->elem_link = (uint32_t)first_td;   /* bit1=0: TD */
    /* QH horizontal link: terminate (we only insert into one frame slot) */
    qh->head_link = FL_TERMINATE | FL_QH;

    /* Insert into frame list slot 0 — QH pointer (bit1=1) */
    uint32_t old_fl0 = fl[0];
    fl[0] = (uint32_t)qh | FL_QH;

    /* Poll for completion or error */
    uint32_t deadline = pit_get_ticks() + timeout_ms;
    int result = 0;

    while (1) {
        /* Check every TD for active/error */
        uhci_td_t* td = first_td;
        int all_done  = 1;
        int any_error = 0;

        while (td) {
            uint32_t cs = td->ctrl_sts;
            if (cs & TD_CS_ACTIVE) { all_done = 0; break; }
            if (cs & TD_CS_ANY_ERROR) { any_error = 1; break; }

            /* Walk chain */
            uint32_t lnk = td->link;
            if (lnk & FL_TERMINATE) break;
            td = (uhci_td_t*)(lnk & ~0xFu);
        }

        if (all_done || any_error) {
            if (any_error) result = -1;
            break;
        }

        if (pit_get_ticks() >= deadline) {
            terminal_printf("[USB] Transfer timeout\n");
            result = -1;
            break;
        }
    }

    /* Remove from frame list */
    fl[0] = old_fl0;

    return result;
}

/* ── Port reset helper ─────────────────────────────────────────────────────── */
static void usb_port_reset(uint8_t port) {
    uint16_t io    = uhci_get_io_base();
    uint16_t portsc = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;

    /* Assert reset */
    outw(io + portsc, UHCI_PORT_PR);
    usb_sleep(50);
    /* De-assert reset */
    outw(io + portsc, 0);
    usb_sleep(10);
    /* Enable port */
    outw(io + portsc, UHCI_PORT_PE);
    usb_sleep(20);
}

/*
 * ── Control transfer helper ─────────────────────────────────────────────────
 * Builds SETUP + (optional DATA IN/OUT) + STATUS stage, executes, returns 0
 * on success. If data_in is non-NULL the DATA stage reads into it.
 */
static int usb_control_transfer(uint8_t dev_addr, uint8_t ep,
                                 uint8_t bm_req_type, uint8_t b_req,
                                 uint16_t w_value, uint16_t w_index,
                                 uint16_t w_length,
                                 uint8_t* data_in, int is_ls)
{
    /* Fill setup packet */
    setup_buf.bmRequestType = bm_req_type;
    setup_buf.bRequest      = b_req;
    setup_buf.wValue        = w_value;
    setup_buf.wIndex        = w_index;
    setup_buf.wLength       = w_length;

    /* SETUP TD (toggle=0, PID=SETUP, 8 bytes) */
    uhci_td_t* td_setup = usb_build_td(UHCI_PID_SETUP, dev_addr, ep,
                                        0, 8, (uint32_t)&setup_buf, is_ls);
    if (!td_setup) return -1;

    uhci_td_t* td_data   = 0;
    uhci_td_t* td_status = 0;

    if (w_length > 0 && data_in) {
        /* DATA IN stage (toggle=1) */
        td_data = usb_build_td(UHCI_PID_IN, dev_addr, ep,
                               1, w_length, (uint32_t)data_in, is_ls);
        if (!td_data) { uhci_free_td(td_setup); return -1; }

        /* STATUS OUT stage (toggle=1, 0 bytes) */
        td_status = usb_build_td(UHCI_PID_OUT, dev_addr, ep,
                                 1, 0, (uint32_t)data_in, is_ls);
        if (!td_status) { uhci_free_td(td_setup); uhci_free_td(td_data); return -1; }

        usb_chain_td(td_setup, td_data);
        usb_chain_td(td_data,  td_status);
        td_status->link = FL_TERMINATE;
    } else {
        /* No data stage — STATUS IN (toggle=1, 0 bytes) */
        td_status = usb_build_td(UHCI_PID_IN, dev_addr, ep,
                                 1, 0, (uint32_t)&setup_buf, is_ls);
        if (!td_status) { uhci_free_td(td_setup); return -1; }

        usb_chain_td(td_setup, td_status);
        td_status->link = FL_TERMINATE;
    }

    uhci_qh_t* qh = uhci_alloc_qh();
    if (!qh) {
        uhci_free_td(td_setup);
        if (td_data)   uhci_free_td(td_data);
        uhci_free_td(td_status);
        return -1;
    }

    int r = usb_exec_qh(qh, td_setup, 200);

    uhci_free_qh(qh);
    uhci_free_td(td_setup);
    if (td_data)   uhci_free_td(td_data);
    uhci_free_td(td_status);

    return r;
}

/* ── usb_enumerate_device ──────────────────────────────────────────────────── */
void usb_enumerate_device(uint8_t port) {
    uint16_t io = uhci_get_io_base();
    if (!io) return;

    /* 1. Reset port */
    usb_port_reset(port);

    /* Check low-speed flag from PORTSC */
    uint16_t portsc_reg = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
    uint16_t portsc_val = inw(io + portsc_reg);
    int is_ls = (portsc_val & (1 << 8)) ? 1 : 0;  /* bit8 = Low-speed device */

    terminal_printf("[USB] Port %d: %s-speed device\n",
                    port + 1, is_ls ? "low" : "full");

    /* Step 1: GET_DESCRIPTOR(Device, 8 bytes) at address 0 */
    memset(data_buf, 0, 18);
    if (usb_control_transfer(0, 0,
                             0x80, USB_REQ_GET_DESCRIPTOR,
                             (uint16_t)(USB_DESC_DEVICE << 8), 0, 8,
                             data_buf, is_ls) != 0) {
        terminal_printf("[USB] Port %d: GET_DESCRIPTOR(8) failed\n", port + 1);
        return;
    }

    uint8_t max_pkt0 = data_buf[USB_DESC_OFFSET_MAX_PACKET0];
    if (max_pkt0 == 0) max_pkt0 = 8;
    terminal_printf("[USB] Port %d: bMaxPacketSize0=%d\n", port + 1, max_pkt0);

    /* Step 2: SET_ADDRESS */
    uint8_t new_addr = (uint8_t)(port + 1);
    if (usb_control_transfer(0, 0,
                             0x00, USB_REQ_SET_ADDRESS,
                             new_addr, 0, 0,
                             0, is_ls) != 0) {
        terminal_printf("[USB] Port %d: SET_ADDRESS failed\n", port + 1);
        return;
    }
    usb_sleep(5);

    /* Step 3: GET_DESCRIPTOR(Device, 18 bytes) at new address */
    memset(data_buf, 0, 18);
    if (usb_control_transfer(new_addr, 0,
                             0x80, USB_REQ_GET_DESCRIPTOR,
                             (uint16_t)(USB_DESC_DEVICE << 8), 0, 18,
                             data_buf, is_ls) != 0) {
        terminal_printf("[USB] Port %d: GET_DESCRIPTOR(18) failed\n", port + 1);
        return;
    }

    uint8_t  dev_class  = data_buf[USB_DESC_OFFSET_CLASS];
    uint8_t  dev_sub    = data_buf[USB_DESC_OFFSET_SUBCLASS];
    uint16_t vendor_id  = (uint16_t)(data_buf[USB_DESC_OFFSET_VENDOR]
                        | (data_buf[USB_DESC_OFFSET_VENDOR + 1] << 8));
    uint16_t product_id = (uint16_t)(data_buf[USB_DESC_OFFSET_PRODUCT]
                        | (data_buf[USB_DESC_OFFSET_PRODUCT + 1] << 8));
    uint8_t  num_cfgs   = data_buf[USB_DESC_OFFSET_NUM_CONFIGS];

    terminal_printf("[USB] Addr %d: Class=0x%x Sub=0x%x VID=%04x PID=%04x cfgs=%d\n",
                    new_addr, dev_class, dev_sub, vendor_id, product_id, num_cfgs);

    /* Step 4: GET_DESCRIPTOR(Configuration, 9 bytes) then full */
    memset(data_buf, 0, 9);
    if (usb_control_transfer(new_addr, 0,
                             0x80, USB_REQ_GET_DESCRIPTOR,
                             (uint16_t)(USB_DESC_CONFIG << 8), 0, 9,
                             data_buf, is_ls) != 0) {
        terminal_printf("[USB] Addr %d: GET_CONFIG_DESC(9) failed\n", new_addr);
        return;
    }

    uint16_t total_len = (uint16_t)(data_buf[2] | (data_buf[3] << 8));
    uint8_t  cfg_val   = data_buf[5];
    if (total_len > 255) total_len = 255;

    memset(data_buf, 0, total_len);
    if (total_len > 9) {
        if (usb_control_transfer(new_addr, 0,
                                 0x80, USB_REQ_GET_DESCRIPTOR,
                                 (uint16_t)(USB_DESC_CONFIG << 8), 0, total_len,
                                 data_buf, is_ls) != 0) {
            terminal_printf("[USB] Addr %d: GET_CONFIG_DESC(full) failed\n", new_addr);
            return;
        }
    }

    /* Parse interface and endpoint descriptors */
    uint8_t ep_in      = 0;
    uint16_t ep_maxpkt = 8;
    uint8_t iface_class = dev_class; /* default if class is in device descriptor */

    uint16_t off = 9; /* skip configuration descriptor */
    while (off + 2 <= total_len) {
        uint8_t desc_len  = data_buf[off];
        uint8_t desc_type = data_buf[off + 1];
        if (desc_len == 0) break;

        if (desc_type == USB_DESC_INTERFACE && off + 9 <= total_len) {
            iface_class = data_buf[off + 5];
        } else if (desc_type == USB_DESC_ENDPOINT && off + 7 <= total_len) {
            uint8_t  ep_addr = data_buf[off + 2];
            uint8_t  ep_attr = data_buf[off + 3];
            uint16_t ep_mps  = (uint16_t)(data_buf[off + 4] | (data_buf[off + 5] << 8));

            /* Interrupt IN endpoint */
            if ((ep_addr & 0x80) && ((ep_attr & 0x03) == 0x03)) {
                ep_in     = ep_addr & 0x0F;
                ep_maxpkt = ep_mps;
            }
        }

        off += desc_len;
    }

    /* Step 5: SET_CONFIGURATION */
    if (usb_control_transfer(new_addr, 0,
                             0x00, USB_REQ_SET_CONFIG,
                             cfg_val, 0, 0,
                             0, is_ls) != 0) {
        terminal_printf("[USB] Addr %d: SET_CONFIGURATION failed\n", new_addr);
        return;
    }

    /* Store in device table */
    if (usb_next_slot < USB_MAX_DEVICES) {
        usb_device_t* dev   = &usb_devices[usb_next_slot++];
        dev->addr           = new_addr;
        dev->class          = iface_class;
        dev->subclass       = dev_sub;
        dev->vendor_id      = vendor_id;
        dev->product_id     = product_id;
        dev->ep_in          = ep_in;
        dev->max_packet     = ep_maxpkt;
        dev->config_value   = cfg_val;
        dev->active         = 1;

        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_printf("[USB] Enumerated: addr=%d class=0x%x %04x:%04x ep_in=%d\n",
                        new_addr, iface_class, vendor_id, product_id, ep_in);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

/* ── Bulk IN ───────────────────────────────────────────────────────────────── */
int usb_bulk_in(uint8_t dev_addr, uint8_t ep, void* buf, uint16_t len) {
    if (!buf || len == 0) return -1;

    uint8_t*   dst    = (uint8_t*)buf;
    uint16_t   remain = len;
    uint8_t    toggle = 1;       /* bulk starts at DATA1 */
    uhci_td_t* first  = 0;
    uhci_td_t* prev   = 0;

    while (remain > 0) {
        uint16_t chunk = (remain > 512) ? 512 : remain;
        uhci_td_t* td  = usb_build_td(UHCI_PID_IN, dev_addr, ep,
                                       toggle, chunk,
                                       (uint32_t)dst, 0 /* full-speed */);
        if (!td) {
            /* Free already-built chain */
            uhci_td_t* t = first;
            while (t) {
                uint32_t lnk = t->link;
                uhci_free_td(t);
                if (lnk & FL_TERMINATE) break;
                t = (uhci_td_t*)(lnk & ~0xFu);
            }
            return -1;
        }
        td->link = FL_TERMINATE;

        if (prev) usb_chain_td(prev, td);
        else       first = td;

        prev    = td;
        dst    += chunk;
        remain -= chunk;
        toggle ^= 1;
    }

    if (!first) return -1;

    uhci_qh_t* qh = uhci_alloc_qh();
    if (!qh) {
        uhci_td_t* t = first;
        while (t) {
            uint32_t lnk = t->link;
            uhci_free_td(t);
            if (lnk & FL_TERMINATE) break;
            t = (uhci_td_t*)(lnk & ~0xFu);
        }
        return -1;
    }

    int r = usb_exec_qh(qh, first, 500);

    uhci_free_qh(qh);
    uhci_td_t* t = first;
    while (t) {
        uint32_t lnk = t->link;
        uhci_free_td(t);
        if (lnk & FL_TERMINATE) break;
        t = (uhci_td_t*)(lnk & ~0xFu);
    }

    return r;
}

/* ── Bulk OUT ──────────────────────────────────────────────────────────────── */
int usb_bulk_out(uint8_t dev_addr, uint8_t ep, const void* buf, uint16_t len) {
    if (!buf || len == 0) return -1;

    const uint8_t* src    = (const uint8_t*)buf;
    uint16_t       remain = len;
    uint8_t        toggle = 1;
    uhci_td_t*     first  = 0;
    uhci_td_t*     prev   = 0;

    while (remain > 0) {
        uint16_t chunk = (remain > 512) ? 512 : remain;
        uhci_td_t* td  = usb_build_td(UHCI_PID_OUT, dev_addr, ep,
                                       toggle, chunk,
                                       (uint32_t)src, 0);
        if (!td) {
            uhci_td_t* t = first;
            while (t) {
                uint32_t lnk = t->link;
                uhci_free_td(t);
                if (lnk & FL_TERMINATE) break;
                t = (uhci_td_t*)(lnk & ~0xFu);
            }
            return -1;
        }
        td->link = FL_TERMINATE;

        if (prev) usb_chain_td(prev, td);
        else       first = td;

        prev    = td;
        src    += chunk;
        remain -= chunk;
        toggle ^= 1;
    }

    if (!first) return -1;

    uhci_qh_t* qh = uhci_alloc_qh();
    if (!qh) {
        uhci_td_t* t = first;
        while (t) {
            uint32_t lnk = t->link;
            uhci_free_td(t);
            if (lnk & FL_TERMINATE) break;
            t = (uhci_td_t*)(lnk & ~0xFu);
        }
        return -1;
    }

    int r = usb_exec_qh(qh, first, 500);

    uhci_free_qh(qh);
    uhci_td_t* t = first;
    while (t) {
        uint32_t lnk = t->link;
        uhci_free_td(t);
        if (lnk & FL_TERMINATE) break;
        t = (uhci_td_t*)(lnk & ~0xFu);
    }

    return r;
}

/* ── usb_init ──────────────────────────────────────────────────────────────── */
void usb_init(void) {
    memset(usb_devices, 0, sizeof(usb_devices));
    usb_next_slot = 0;
}

/* ── usb_list_devices (for lsusb shell command) ───────────────────────────── */
void usb_list_devices(void) {
    int found = 0;
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  USB Devices\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ── addr  class  vendor:product  ep_in  maxpkt\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!usb_devices[i].active) continue;
        found++;
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_printf("     %-4d  0x%02x   %04x:%04x    %-5d  %d\n",
                        usb_devices[i].addr,
                        usb_devices[i].class,
                        usb_devices[i].vendor_id,
                        usb_devices[i].product_id,
                        usb_devices[i].ep_in,
                        usb_devices[i].max_packet);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    if (!found) {
        terminal_writestring("  (no USB devices enumerated)\n");
    }
    terminal_printf("\n  %d device(s)\n\n", found);
}

#include "uhci.h"
#include "pci.h"
#include "kernel.h"
#include "io.h"
#include "pmm.h"
#include "string.h"
#include "pit.h"
#include "usb.h"

/* ── Controller state ──────────────────────────────────────────────────────── */
static uint16_t  uhci_io_base   = 0;
static uint32_t* uhci_frame_list = 0;

/* ── TD / QH pools (statically allocated, 16-byte aligned via attribute) ──── */
static uhci_td_t td_pool[UHCI_TD_POOL_SIZE] __attribute__((aligned(16)));
static uint8_t   td_used[UHCI_TD_POOL_SIZE];

static uhci_qh_t qh_pool[UHCI_QH_POOL_SIZE] __attribute__((aligned(16)));
static uint8_t   qh_used[UHCI_QH_POOL_SIZE];

/* ── Sleep helper ──────────────────────────────────────────────────────────── */
static void uhci_sleep(uint32_t ms) {
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < ms) { /* spin */ }
}

/* ── Pool accessors ────────────────────────────────────────────────────────── */
uint16_t uhci_get_io_base(void)       { return uhci_io_base; }
uint32_t* uhci_get_frame_list(void)   { return uhci_frame_list; }

uhci_td_t* uhci_alloc_td(void) {
    for (int i = 0; i < UHCI_TD_POOL_SIZE; i++) {
        if (!td_used[i]) {
            td_used[i] = 1;
            memset(&td_pool[i], 0, sizeof(uhci_td_t));
            return &td_pool[i];
        }
    }
    return 0;
}

void uhci_free_td(uhci_td_t* td) {
    if (!td) return;
    int idx = (int)(td - td_pool);
    if (idx >= 0 && idx < UHCI_TD_POOL_SIZE) td_used[idx] = 0;
}

uhci_qh_t* uhci_alloc_qh(void) {
    for (int i = 0; i < UHCI_QH_POOL_SIZE; i++) {
        if (!qh_used[i]) {
            qh_used[i] = 1;
            memset(&qh_pool[i], 0, sizeof(uhci_qh_t));
            return &qh_pool[i];
        }
    }
    return 0;
}

void uhci_free_qh(uhci_qh_t* qh) {
    if (!qh) return;
    int idx = (int)(qh - qh_pool);
    if (idx >= 0 && idx < UHCI_QH_POOL_SIZE) qh_used[idx] = 0;
}

/* ── Init ──────────────────────────────────────────────────────────────────── */
void uhci_init(void) {
    memset(td_used, 0, sizeof(td_used));
    memset(qh_used, 0, sizeof(qh_used));

    pci_device_t uhci_dev;

    /* UHCI: Class 0x0C (Serial Bus), Subclass 0x03 (USB), ProgIF 0x00 */
    if (!pci_get_device_by_class(0x0C, 0x03, 0x00, &uhci_dev)) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("[UHCI] Controller not found.\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    /* UHCI uses BAR4 for its I/O Base Address */
    uint32_t bar4 = pci_read_config_32(uhci_dev.bus, uhci_dev.slot, uhci_dev.func, 0x20);
    if ((bar4 & 1) == 0) {
        terminal_printf("[UHCI] BAR4 is not I/O space. Cannot initialize.\n");
        return;
    }

    uhci_io_base = (uint16_t)(bar4 & ~0x3u);
    terminal_printf("[UHCI] Found UHCI Controller at PCI %d:%d:%d (I/O Base 0x%x)\n",
                    uhci_dev.bus, uhci_dev.slot, uhci_dev.func, uhci_io_base);

    pci_enable_bus_mastering(&uhci_dev);

    /* Global reset */
    outw(uhci_io_base + UHCI_USBCMD, UHCI_CMD_GRESET);
    uhci_sleep(20);
    outw(uhci_io_base + UHCI_USBCMD, 0);
    uhci_sleep(10);

    /* Host controller reset */
    outw(uhci_io_base + UHCI_USBCMD, UHCI_CMD_HCRESET);
    uhci_sleep(10);
    while (inw(uhci_io_base + UHCI_USBCMD) & UHCI_CMD_HCRESET) { /* spin */ }

    /* Allocate Frame List (1024 dwords, 4KB aligned) */
    uhci_frame_list = (uint32_t*)pmm_alloc_frame();
    if (!uhci_frame_list) {
        terminal_printf("[UHCI] Failed to allocate Frame List.\n");
        return;
    }
    for (int i = 0; i < 1024; i++) uhci_frame_list[i] = FL_TERMINATE;

    outl(uhci_io_base + UHCI_FLBASEADD, (uint32_t)uhci_frame_list);
    outw(uhci_io_base + UHCI_FRNUM, 0);

    /* Run + Configure Flag */
    outw(uhci_io_base + UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF);
    uhci_sleep(10);

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[UHCI] Controller Initialized and Running.\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    uhci_check_ports();
}

/* ── Port check / enumeration trigger ─────────────────────────────────────── */
void uhci_check_ports(void) {
    if (!uhci_io_base) return;

    uint16_t portsc[2];
    portsc[0] = inw(uhci_io_base + UHCI_PORTSC1);
    portsc[1] = inw(uhci_io_base + UHCI_PORTSC2);

    /* Acknowledge connection status changes */
    if (portsc[0] & UHCI_PORT_CSC)
        outw(uhci_io_base + UHCI_PORTSC1, portsc[0] & ~UHCI_PORT_CSC);
    if (portsc[1] & UHCI_PORT_CSC)
        outw(uhci_io_base + UHCI_PORTSC2, portsc[1] & ~UHCI_PORT_CSC);

    for (int p = 0; p < 2; p++) {
        if (portsc[p] & UHCI_PORT_CCS) {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            terminal_printf("[USB] Device on Port %d (Status: 0x%x) — enumerating...\n",
                            p + 1, portsc[p]);
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            usb_enumerate_device((uint8_t)p);
        }
    }
}

void uhci_poll(void) {
    /* Periodic USB HID polling — called from PIT tick */
    extern void usb_hid_poll(void);
    usb_hid_poll();
}

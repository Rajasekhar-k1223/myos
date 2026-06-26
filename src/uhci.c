#include "uhci.h"
#include "pci.h"
#include "kernel.h"
#include "io.h"
#include "pmm.h"
#include "string.h"
#include "pit.h"

static uint16_t uhci_io_base = 0;
static uint32_t* uhci_frame_list = 0;

static void uhci_sleep(uint32_t ms) {
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < ms) {
        // spin
    }
}

void uhci_init(void) {
    pci_device_t uhci_dev;
    
    // UHCI Class is 0x0C (Serial Bus), Subclass 0x03 (USB), ProgIF 0x00 (UHCI)
    if (!pci_get_device_by_class(0x0C, 0x03, 0x00, &uhci_dev)) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("[UHCI] Controller not found.\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    
    // UHCI uses BAR4 for its I/O Base Address
    uint32_t bar4 = pci_read_config_32(uhci_dev.bus, uhci_dev.slot, uhci_dev.func, 0x20);
    
    // Check if it's actually an I/O space BAR (bit 0 must be 1)
    if ((bar4 & 1) == 0) {
        terminal_printf("[UHCI] BAR4 is not an I/O space. Cannot initialize.\n");
        return;
    }
    
    uhci_io_base = bar4 & ~0x3;
    terminal_printf("[UHCI] Found UHCI Controller at PCI %d:%d:%d (I/O Base 0x%x)\n", uhci_dev.bus, uhci_dev.slot, uhci_dev.func, uhci_io_base);
    
    // Enable Bus Mastering
    pci_enable_bus_mastering(&uhci_dev);
    
    // Issue Global Reset
    outw(uhci_io_base + UHCI_USBCMD, UHCI_CMD_GRESET);
    uhci_sleep(20); // Wait 20ms
    outw(uhci_io_base + UHCI_USBCMD, 0); // Clear GRESET
    uhci_sleep(10);
    
    // Issue Host Controller Reset
    outw(uhci_io_base + UHCI_USBCMD, UHCI_CMD_HCRESET);
    uhci_sleep(10);
    // HCRESET clears itself when done, but wait a bit anyway
    while (inw(uhci_io_base + UHCI_USBCMD) & UHCI_CMD_HCRESET) {
        // Spin
    }
    
    // Allocate Frame List (1024 dwords, aligned to 4KB)
    // pmm_alloc_frame gives us a 4KB aligned physical page.
    uhci_frame_list = (uint32_t*)pmm_alloc_frame();
    if (!uhci_frame_list) {
        terminal_printf("[UHCI] Failed to allocate Frame List.\n");
        return;
    }
    
    // Initialize Frame List to Terminate (bit 0 = 1)
    for (int i = 0; i < 1024; i++) {
        uhci_frame_list[i] = 1; // Terminate bit
    }
    
    // Write Frame List Base Address
    outl(uhci_io_base + UHCI_FLBASEADD, (uint32_t)uhci_frame_list);
    
    // Set Frame Number to 0
    outw(uhci_io_base + UHCI_FRNUM, 0);
    
    // Enable controller and route all ports
    // Set Run/Stop, and CF (Configure Flag) to indicate OS driver has taken over
    outw(uhci_io_base + UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF);
    uhci_sleep(10);
    
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[UHCI] Controller Initialized and Running.\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    // Check initial port statuses
    uhci_check_ports();
}

void uhci_check_ports(void) {
    if (!uhci_io_base) return;
    
    uint16_t port1 = inw(uhci_io_base + UHCI_PORTSC1);
    uint16_t port2 = inw(uhci_io_base + UHCI_PORTSC2);
    
    // Acknowledge connection status changes (write 1 to clear)
    if (port1 & UHCI_PORT_CSC) {
        outw(uhci_io_base + UHCI_PORTSC1, port1);
    }
    if (port2 & UHCI_PORT_CSC) {
        outw(uhci_io_base + UHCI_PORTSC2, port2);
    }
    
    if (port1 & UHCI_PORT_CCS) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_printf("[USB] Device detected on Port 1! (Status: 0x%x)\n", port1);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    if (port2 & UHCI_PORT_CCS) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        terminal_printf("[USB] Device detected on Port 2! (Status: 0x%x)\n", port2);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

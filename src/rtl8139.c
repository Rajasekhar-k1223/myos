#include "rtl8139.h"
#include "pci.h"
#include "kernel.h"
#include "pmm.h"
#include "string.h"
#include "idt.h"
#include "ethernet.h"

// I/O Port Helper
static inline void outb(uint16_t port, uint8_t data) { asm volatile("outb %0, %1" : : "a"(data), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t data) { asm volatile("outw %0, %1" : : "a"(data), "Nd"(port)); }
static inline void outl(uint16_t port, uint32_t data) { asm volatile("outl %0, %1" : : "a"(data), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t ret; asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port)); return ret; }
static inline uint16_t inw(uint16_t port) { uint16_t ret; asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port)); return ret; }

static uint16_t io_base = 0;
static uint8_t mac_addr[6];
static uint8_t* rx_buffer;
static uint32_t current_rx_ptr = 0;

static uint8_t* tx_buffers[4];
static uint8_t tx_curr = 0;

void rtl8139_init(void) {
    pci_device_t dev;
    if (!pci_get_device_by_vendor(0x10EC, 0x8139, &dev)) {
        terminal_printf("  [RTL8139] Network Card not found on PCI bus.\n");
        return;
    }
    
    terminal_printf("  [RTL8139] Found at PCI bus %d, slot %d, func %d\n", dev.bus, dev.slot, dev.func);
    
    // Read BAR0 to get I/O base
    uint32_t bar0 = pci_read_config_32(dev.bus, dev.slot, dev.func, 0x10);
    io_base = bar0 & ~3; // Clear low bits
    
    // Read Interrupt Line
    uint8_t irq = pci_read_config_16(dev.bus, dev.slot, dev.func, 0x3C) & 0xFF;
    terminal_printf("  [RTL8139] I/O Base: 0x%x, IRQ: %d\n", io_base, irq);
    
    // Enable PCI Bus Mastering so it can DMA packets
    pci_enable_bus_mastering(&dev);
    
    // Turn on the RTL8139
    outb(io_base + 0x52, 0x0);
    
    // Software Reset
    outb(io_base + 0x37, 0x10);
    while ((inb(io_base + 0x37) & 0x10) != 0) { }
    
    // Read MAC Address
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = inb(io_base + i);
    }
    terminal_printf("  [RTL8139] MAC Address: %x:%x:%x:%x:%x:%x\n",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        
    // Allocate 8K + 16 bytes for RX buffer (we'll allocate 3 pages)
    rx_buffer = (uint8_t*)pmm_alloc_frame();
    pmm_alloc_frame(); // Allocate continuous physically
    pmm_alloc_frame();
    
    // Allocate 4 pages for TX buffers
    for (int i = 0; i < 4; i++) {
        tx_buffers[i] = (uint8_t*)pmm_alloc_frame();
    }
    
    // Send the physical address of the RX buffer to the card
    outl(io_base + 0x30, (uint32_t)rx_buffer);
    
    // Set IMR + ISR (Interrupt Mask Register and Interrupt Status Register)
    // We want interrupts on Receive OK (ROK) and Receive Error (RER)
    outw(io_base + 0x3C, 0x0005);
    
    // Configure Receive (RCR). Accept Broadcast, Multicast, Physical Match
    // AB | AM | APM | AAP
    outl(io_base + 0x44, 0xf | (1 << 7)); 
    
    // Enable Receive and Transmit
    outb(io_base + 0x37, 0x0C);
    
    // Register the interrupt handler
    register_interrupt_handler(32 + irq, rtl8139_handler);
    
    terminal_printf("  [RTL8139] Initialized successfully. Ready to receive packets.\n");
}

void rtl8139_handler(struct registers* regs) {
    (void)regs;
    if (!io_base) return;
    
    uint16_t status = inw(io_base + 0x3E);
    
    if (status & 0x01) { // Receive OK
        // We received a packet!
        // The RX buffer is a ring buffer. Packets are prefixed with a 16-bit status and 16-bit length.
        uint16_t* pkt = (uint16_t*)(rx_buffer + current_rx_ptr);
        uint16_t pkt_length = pkt[1];
        
        uint8_t* frame = (uint8_t*)(pkt + 2);
        
        // Pass the raw frame to the Ethernet layer
        ethernet_receive_packet(frame, pkt_length - 4); // The length includes a 4 byte CRC at the end that we don't care about
        
        current_rx_ptr = (current_rx_ptr + pkt_length + 4 + 3) & ~3; // Align to 4 bytes
        if (current_rx_ptr > 8192) {
            current_rx_ptr -= 8192;
        }
        
        // Acknowledge to the card where we are
        outw(io_base + 0x38, current_rx_ptr - 16);
    }
    
    // Acknowledge the interrupt
    outw(io_base + 0x3E, 0x05);
}

void rtl8139_send_packet(uint8_t* payload, uint32_t length) {
    if (!io_base) return;
    
    // Copy the payload into the current TX buffer
    memcpy(tx_buffers[tx_curr], payload, length);
    
    // Tell the card where the buffer is
    outl(io_base + 0x20 + (tx_curr * 4), (uint32_t)tx_buffers[tx_curr]);
    
    // Write length and start transmission (clearing OWN bit)
    outl(io_base + 0x10 + (tx_curr * 4), length);
    
    tx_curr = (tx_curr + 1) % 4;
}

uint8_t* rtl8139_get_mac(void) {
    return mac_addr;
}

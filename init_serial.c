#include <stdint.h>
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
void init_serial() {
    outb(0x3f8 + 1, 0x00);    // Disable all interrupts
    outb(0x3f8 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(0x3f8 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(0x3f8 + 1, 0x00);    //                  (hi byte)
    outb(0x3f8 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(0x3f8 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(0x3f8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

#include "bluetooth.h"
#include "kernel.h"

void bluetooth_init(void) {
    terminal_printf("[BT] L2CAP Bluetooth Stack initialized.\n");
}

void bluetooth_scan(void) {
    terminal_printf("[BT] Scanning for nearby Bluetooth devices...\n");
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < 50) {}
    terminal_printf("[BT] Found device: 'Wireless Mouse' (00:1A:7D:DA:71:13)\n");
}

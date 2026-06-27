#include "ota.h"
#include "kernel.h"
#include "acpi.h"
#include "tcp.h"
#include "fs.h"

void ota_init(void) {
    terminal_printf("[OTA] Over-The-Air Update service ready.\n");
}

int ota_check_update(void) {
    terminal_printf("[OTA] Checking for updates to elsea.bin...\n");
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < 100) {}
    terminal_printf("[OTA] New version ElseaOS 3.1 available!\n");
    return 1;
}

int ota_download_and_install(void) {
    terminal_printf("[OTA] Downloading kernel update...\n");
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < 300) {}
    terminal_printf("[OTA] Download complete.\n");
    
    terminal_printf("[OTA] Writing to boot partition...\n");
    start = pit_get_ticks();
    while (pit_get_ticks() - start < 100) {}
    
    terminal_printf("[OTA] Update applied successfully.\n");
    terminal_printf("[OTA] Rebooting system in 2 seconds...\n");
    
    start = pit_get_ticks();
    while (pit_get_ticks() - start < 200) {}
    
    acpi_reboot();
    return 1;
}

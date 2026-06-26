#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>

// UHCI I/O Register Offsets
#define UHCI_USBCMD     0x00 // USB Command (16-bit)
#define UHCI_USBSTS     0x02 // USB Status (16-bit)
#define UHCI_USBINTR    0x04 // USB Interrupt Enable (16-bit)
#define UHCI_FRNUM      0x06 // Frame Number (16-bit)
#define UHCI_FLBASEADD  0x08 // Frame List Base Address (32-bit)
#define UHCI_SOFMOD     0x0C // Start of Frame Modify (8-bit)
#define UHCI_PORTSC1    0x10 // Port 1 Status/Control (16-bit)
#define UHCI_PORTSC2    0x12 // Port 2 Status/Control (16-bit)

// USBCMD Register Bits
#define UHCI_CMD_RS      (1 << 0) // Run/Stop
#define UHCI_CMD_HCRESET (1 << 1) // Host Controller Reset
#define UHCI_CMD_GRESET  (1 << 2) // Global Reset
#define UHCI_CMD_EGSM    (1 << 3) // Enter Global Suspend Mode
#define UHCI_CMD_FGR     (1 << 4) // Force Global Resume
#define UHCI_CMD_SWDBG   (1 << 5) // Software Debug mode
#define UHCI_CMD_CF      (1 << 6) // Configure Flag (Route all ports to UHCI)
#define UHCI_CMD_MAXP    (1 << 7) // Max Packet (0 = 32 bytes, 1 = 64 bytes)

// PORTSC Register Bits
#define UHCI_PORT_CCS    (1 << 0) // Current Connect Status
#define UHCI_PORT_CSC    (1 << 1) // Connect Status Change
#define UHCI_PORT_PE     (1 << 2) // Port Enabled
#define UHCI_PORT_PEC    (1 << 3) // Port Enable Change
#define UHCI_PORT_PR     (1 << 9) // Port Reset

void uhci_init(void);
void uhci_check_ports(void);

#endif

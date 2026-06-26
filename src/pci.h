#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
} pci_device_t;

uint32_t pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint16_t pci_read_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

int pci_get_device_by_vendor(uint16_t vendor, uint16_t device, pci_device_t* out_dev);
int pci_get_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out_dev);
void pci_enable_bus_mastering(pci_device_t* dev);

#endif

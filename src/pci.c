#include "pci.h"
#include "kernel.h"
#include <stdint.h>

static inline void outl(uint16_t port, uint32_t data) {
    asm volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t pci_read_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_write_config_32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, value);
}

uint16_t pci_read_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read_config_32(bus, slot, func, offset);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_write_config_16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t read_val = pci_read_config_32(bus, slot, func, offset);
    uint32_t mask = 0xFFFF0000;
    uint32_t shifted = value;
    if ((offset & 2) != 0) {
        mask = 0x0000FFFF;
        shifted <<= 16;
    }
    uint32_t write_val = (read_val & mask) | shifted;
    pci_write_config_32(bus, slot, func, offset, write_val);
}

int pci_get_device_by_vendor(uint16_t vendor, uint16_t device, pci_device_t* out_dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read_config_16(bus, slot, 0, 0);
            if (v == 0xFFFF) continue; // No device

            for (uint8_t func = 0; func < 8; func++) {
                v = pci_read_config_16(bus, slot, func, 0);
                if (v == 0xFFFF) continue;
                
                uint16_t d = pci_read_config_16(bus, slot, func, 2);
                if (v == vendor && d == device) {
                    out_dev->bus = bus;
                    out_dev->slot = slot;
                    out_dev->func = func;
                    out_dev->vendor = v;
                    out_dev->device = d;
                    return 1;
                }
            }
        }
    }
    return 0;
}

void pci_enable_bus_mastering(pci_device_t* dev) {
    uint16_t cmd = pci_read_config_16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x0004; // Bus Master Enable
    cmd |= 0x0002; // Memory Space Enable
    pci_write_config_16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

int pci_get_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out_dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read_config_16(bus, slot, 0, 0);
            if (v == 0xFFFF) continue; // No device

            for (uint8_t func = 0; func < 8; func++) {
                v = pci_read_config_16(bus, slot, func, 0);
                if (v == 0xFFFF) continue;
                
                uint32_t class_reg = pci_read_config_32(bus, slot, func, 0x08);
                uint8_t c = (class_reg >> 24) & 0xFF;
                uint8_t sc = (class_reg >> 16) & 0xFF;
                uint8_t p = (class_reg >> 8) & 0xFF;

                if (c == class_code && sc == subclass && p == prog_if) {
                    out_dev->bus = bus;
                    out_dev->slot = slot;
                    out_dev->func = func;
                    out_dev->vendor = v;
                    out_dev->device = pci_read_config_16(bus, slot, func, 2);
                    return 1;
                }
            }
        }
    }
    return 0;
}

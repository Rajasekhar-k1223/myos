#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdint.h>

int usb_msc_init(uint8_t dev_addr, uint8_t ep_out, uint8_t ep_in, uint16_t max_packet);
int usb_msc_read_sector(uint32_t lba, void* buf);
int usb_msc_write_sector(uint32_t lba, const void* buf);
int usb_msc_get_capacity(uint32_t* num_sectors, uint32_t* sector_size);

#endif

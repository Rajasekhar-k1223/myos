#ifndef USB_H
#define USB_H

#include <stdint.h>

/* USB device descriptor offsets */
#define USB_DESC_OFFSET_LENGTH          0
#define USB_DESC_OFFSET_TYPE            1
#define USB_DESC_OFFSET_BCD_USB         2
#define USB_DESC_OFFSET_CLASS           4
#define USB_DESC_OFFSET_SUBCLASS        5
#define USB_DESC_OFFSET_PROTOCOL        6
#define USB_DESC_OFFSET_MAX_PACKET0     7
#define USB_DESC_OFFSET_VENDOR          8
#define USB_DESC_OFFSET_PRODUCT         10
#define USB_DESC_OFFSET_BCD_DEVICE      12
#define USB_DESC_OFFSET_NUM_CONFIGS     17

/* USB descriptor types */
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIG         0x02
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05

/* USB class codes */
#define USB_CLASS_HID           0x03
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09

/* USB standard requests */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIG      0x09

/* USB transfer direction */
#define USB_DIR_IN   0x80
#define USB_DIR_OUT  0x00

/* USB table entry */
typedef struct {
    uint8_t  addr;
    uint8_t  class;
    uint8_t  subclass;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  ep_in;        /* interrupt IN endpoint address */
    uint16_t max_packet;
    uint8_t  config_value;
    int      active;
} usb_device_t;

#define USB_MAX_DEVICES  4

extern usb_device_t usb_devices[USB_MAX_DEVICES];

/* Initialise USB subsystem (called from kernel.c) */
void usb_init(void);

/* Enumerate a device on the given UHCI port (0-based port index) */
void usb_enumerate_device(uint8_t port);

/* Bulk transfers */
int usb_bulk_in(uint8_t dev_addr, uint8_t ep, void* buf, uint16_t len);
int usb_bulk_out(uint8_t dev_addr, uint8_t ep, const void* buf, uint16_t len);

/* Print detected USB devices (lsusb shell command) */
void usb_list_devices(void);

#endif /* USB_H */

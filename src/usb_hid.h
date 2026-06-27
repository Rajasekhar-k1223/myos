#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

/* Initialise USB HID polling for keyboard/mouse devices */
void usb_hid_init(void);

/* Poll HID interrupt endpoint — call from main loop or timer */
void usb_hid_poll(void);

#endif /* USB_HID_H */

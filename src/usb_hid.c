#include "usb_hid.h"
#include "usb.h"
#include "kernel.h"
#include "string.h"

/* ── HID Report buffer ─────────────────────────────────────────────────────── */
static uint8_t hid_buf[8] __attribute__((aligned(4)));
static uint8_t prev_buf[8];

/* ── HID boot-protocol keyboard report ────────────────────────────────────── */
static const char hid_keycode_ascii[256] = {
    /* 0x00 */ 0, 0, 0, 0,
    /* 0x04 */ 'a','b','c','d','e','f','g','h','i','j','k','l','m',
    /* 0x11 */ 'n','o','p','q','r','s','t','u','v','w','x','y','z',
    /* 0x1E */ '1','2','3','4','5','6','7','8','9','0',
    /* 0x28 */ '\n','\x1B','\b','\t',' ','-','=','[',']','\\',
    /* 0x32 */ 0,';','\'','`',',','.','/',
};

void usb_hid_init(void) {
    memset(hid_buf,  0, sizeof(hid_buf));
    memset(prev_buf, 0, sizeof(prev_buf));
    terminal_printf("[USB HID] HID driver initialised.\n");
}

void usb_hid_poll(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_device_t* dev = &usb_devices[i];
        if (!dev->active) continue;
        if (dev->class != USB_CLASS_HID) continue;
        if (!dev->ep_in) continue;

        /* Attempt to read 8-byte HID keyboard report */
        memset(hid_buf, 0, sizeof(hid_buf));
        int r = usb_bulk_in(dev->addr, dev->ep_in, hid_buf, 8);
        if (r != 0) continue;

        /* Compare with previous report to find new key-presses */
        for (int k = 2; k < 8; k++) {
            uint8_t kc = hid_buf[k];
            if (kc == 0) continue;

            /* Check if this keycode was already pressed */
            int already = 0;
            for (int p = 2; p < 8; p++) {
                if (prev_buf[p] == kc) { already = 1; break; }
            }
            if (already) continue;

            /* Translate keycode to ASCII */
            if (kc < (int)(sizeof(hid_keycode_ascii))) {
                char c = hid_keycode_ascii[kc];
                if (c) terminal_putchar(c);
            }
        }

        memcpy(prev_buf, hid_buf, sizeof(hid_buf));
    }
}

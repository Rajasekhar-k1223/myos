#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "kernel.h"
#include "shell.h"

#define KBD_DATA 0x60

/* Scancode set 1 → ASCII (unshifted). Index = scancode byte. */
static const char sc_ascii[] = {
      0,   27, '1', '2', '3', '4', '5', '6', '7', '8',  /* 0x00-0x09 */
    '9', '0', '-', '=', '\b', '\t',                       /* 0x0A-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',   /* 0x10-0x19 */
    '[', ']', '\n',   0, 'a', 's', 'd', 'f', 'g', 'h',   /* 0x1A-0x23 */
    'j', 'k', 'l', ';','\'', '`',   0,'\\', 'z', 'x',   /* 0x24-0x2D */
    'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',    /* 0x2E-0x37 */
      0, ' ',                                              /* 0x38-0x39 */
};

/* Scancode set 1 → ASCII (with shift held). Same indices. */
static const char sc_ascii_shift[] = {
      0,   27, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}', '\n',   0, 'A', 'S', 'D', 'F', 'G', 'H',
    'J', 'K', 'L', ':', '"', '~',   0, '|', 'Z', 'X',
    'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0, '*',
      0, ' ',
};

static int shift_held = 0;
static int ctrl_held = 0;

static void keyboard_callback(struct registers* regs) {
    (void)regs;
    uint8_t sc = inb(KBD_DATA);

    /* Shift press/release */
    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_held = 0; return; }

    /* Ctrl press/release */
    if (sc == 0x1D) { ctrl_held = 1; return; }
    if (sc == 0x9D) { ctrl_held = 0; return; }

    if (sc & 0x80) return; /* ignore all other key-release events */

    if (sc < sizeof(sc_ascii)) {
        char c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
        if (c) {
            extern int snake_handle_input(char c);
            extern int wm_handle_keypress(char c);
            extern int wm_handle_shortcut(char c);
            
            if (ctrl_held) {
                wm_handle_shortcut(c);
            } else {
                if (!snake_handle_input(c)) {
                    if (!wm_handle_keypress(c)) {
                        shell_handle_keypress(c);
                    }
                }
            }
        }
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback); /* IRQ1 → vector 33 */
}

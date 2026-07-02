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

/* Polled key ring buffer — used by installer and other non-IRQ consumers */
#define KEY_RING_SIZE 64
static volatile char  key_ring[KEY_RING_SIZE];
static volatile int   key_ring_head = 0;
static volatile int   key_ring_tail = 0;

void keyboard_push_char(char c) {
    int next = (key_ring_head + 1) % KEY_RING_SIZE;
    if (next != key_ring_tail) {
        key_ring[key_ring_head] = c;
        key_ring_head = next;
    }
}

char keyboard_poll_char(void) {
    if (key_ring_head == key_ring_tail) return 0;
    char c = key_ring[key_ring_tail];
    key_ring_tail = (key_ring_tail + 1) % KEY_RING_SIZE;
    return c;
}

void keyboard_handler_inject(char c) {
    if (!c) return;
    keyboard_push_char(c); /* always feed polled buffer */
    extern int snake_handle_input(char c);
    extern int wm_handle_keypress(char c);
    extern int wm_handle_shortcut(char c);
    
    if (ctrl_held) {
        wm_handle_shortcut(c);
    } else {
        if (!snake_handle_input(c)) {
            if (!wm_handle_keypress(c)) {
                extern void shell_handle_keypress(char c);
                shell_handle_keypress(c);
            }
        }
    }
}

static void keyboard_callback(struct registers* regs) {
    (void)regs;
    uint8_t sc = inb(KBD_DATA);

    /* Shift press/release */
    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_held = 0; return; }

    /* Ctrl press/release */
    if (sc == 0x1D) { ctrl_held = 1; return; }
    if (sc == 0x9D) { ctrl_held = 0; return; }

    extern int sdl_app_active;
    if (sdl_app_active) {
        if (ctrl_held && (sc == 0x01 || sc == 0x81)) { // Ctrl+Esc
            extern void sdl_emergency_quit(void);
            sdl_emergency_quit();
            return;
        }
        extern uint32_t sdl_map_key(char c);
        extern void sdl_push_keydown(uint32_t sym);
        extern void sdl_push_keyup(uint32_t sym);
        
        int is_release = (sc & 0x80);
        uint8_t base_sc = sc & 0x7F;
        char c = 0;
        
        if (base_sc == 0x48) c = '\x10'; // up
        else if (base_sc == 0x50) c = '\x11'; // down
        else if (base_sc == 0x4B) c = '\x12'; // left
        else if (base_sc == 0x4D) c = '\x13'; // right
        else if (base_sc < sizeof(sc_ascii)) c = sc_ascii[base_sc];
        
        if (c) {
            uint32_t sym = sdl_map_key(c);
            if (is_release) sdl_push_keyup(sym);
            else sdl_push_keydown(sym);
        }
        return;
    }

    if (sc & 0x80) return; /* ignore all other key-release events */

    if (sc == 0x49) { // Page Up
        extern int wm_handle_shortcut(char c);
        wm_handle_shortcut(17);
        return;
    }
    if (sc == 0x51) { // Page Down
        extern int wm_handle_shortcut(char c);
        wm_handle_shortcut(18);
        return;
    }
    if (sc == 0x48) { // Up arrow — history prev (\x10)
        extern int wm_handle_keypress(char c);
        extern void shell_handle_keypress(char c);
        if (!wm_handle_keypress('\x10')) shell_handle_keypress('\x10');
        return;
    }
    if (sc == 0x50) { // Down arrow — history next (\x11)
        extern int wm_handle_keypress(char c);
        extern void shell_handle_keypress(char c);
        if (!wm_handle_keypress('\x11')) shell_handle_keypress('\x11');
        return;
    }

    if (sc < sizeof(sc_ascii)) {
        char c = shift_held ? sc_ascii_shift[sc] : sc_ascii[sc];
        keyboard_handler_inject(c);
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback); /* IRQ1 → vector 33 */
}

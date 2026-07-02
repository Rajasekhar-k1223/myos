#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "kernel.h"
#include "vesa.h"

int32_t mouse_x = 512;
int32_t mouse_y = 384;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[4];
static uint8_t mouse_buttons = 0;
static uint8_t mouse_has_wheel = 0;

int32_t mouse_get_x(void) { return mouse_x; }
int32_t mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return mouse_buttons; }

void mouse_handler_inject(int rel_x, int rel_y, int z_delta, uint8_t buttons) {
    mouse_buttons = buttons;
    mouse_x += rel_x;
    mouse_y -= rel_y; 
    
    extern uint32_t vesa_width, vesa_height;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > (int)vesa_width - 1) mouse_x = vesa_width - 1;
    if (mouse_y > (int)vesa_height - 1) mouse_y = vesa_height - 1;
    
    extern int sdl_app_active;
    if (sdl_app_active) {
        extern void sdl_push_mousemove(int x, int y, int dx, int dy);
        extern void sdl_push_mousebutton(int down, uint8_t button, int x, int y);
        
        sdl_push_mousemove(mouse_x, mouse_y, rel_x, -rel_y);
        
        static uint8_t last_buttons = 0;
        if ((buttons & 1) && !(last_buttons & 1)) sdl_push_mousebutton(1, 1, mouse_x, mouse_y); // Left down
        if (!(buttons & 1) && (last_buttons & 1)) sdl_push_mousebutton(0, 1, mouse_x, mouse_y); // Left up
        if ((buttons & 2) && !(last_buttons & 2)) sdl_push_mousebutton(1, 3, mouse_x, mouse_y); // Right down
        if (!(buttons & 2) && (last_buttons & 2)) sdl_push_mousebutton(0, 3, mouse_x, mouse_y); // Right up
        if ((buttons & 4) && !(last_buttons & 4)) sdl_push_mousebutton(1, 2, mouse_x, mouse_y); // Middle down
        if (!(buttons & 4) && (last_buttons & 4)) sdl_push_mousebutton(0, 2, mouse_x, mouse_y); // Middle up
        
        last_buttons = buttons;
        return;
    }

    extern void wm_request_redraw(void);
    extern void wm_process_scroll(int delta);
    if (z_delta != 0) wm_process_scroll(z_delta);
    wm_request_redraw();
}

static void mouse_callback(struct registers* regs) {
    (void)regs;
    
    uint8_t status = inb(0x64);
    while (status & 1) { 
        uint8_t mouse_in = inb(0x60);
        
        switch (mouse_cycle) {
            case 0:
                mouse_byte[0] = mouse_in;
                if (mouse_byte[0] & 0x08) mouse_cycle++;
                break;
            case 1:
                mouse_byte[1] = mouse_in;
                mouse_cycle++;
                break;
            case 2:
                mouse_byte[2] = mouse_in;
                if (mouse_has_wheel) {
                    mouse_cycle++;
                } else {
                    mouse_cycle = 0;
                    goto process_packet;
                }
                break;
            case 3:
                mouse_byte[3] = mouse_in;
                mouse_cycle = 0;
                
            process_packet:
                {
                    uint8_t buttons = mouse_byte[0] & 0x07;
                    int rel_x = mouse_byte[1];
                    int rel_y = mouse_byte[2];
                    if (rel_x && (mouse_byte[0] & (1 << 4))) rel_x -= 256;
                    if (rel_y && (mouse_byte[0] & (1 << 5))) rel_y -= 256;
                    
                    // Simple acceleration curve for speed and accuracy
                    int ax = rel_x;
                    int ay = rel_y;
                    if (ax > 3 || ax < -3) ax = (ax * 3) / 2;
                    if (ay > 3 || ay < -3) ay = (ay * 3) / 2;
                    
                    int z_delta = 0;
                    if (mouse_has_wheel) {
                        z_delta = (int8_t)mouse_byte[3];
                    }
                    mouse_handler_inject(ax, ay, z_delta, buttons);
                }
                break;
        }
        status = inb(0x64);
    }
}

static void mouse_wait(uint8_t type) {
    int timeout = 100000;
    if (type == 0) { // Read
        while (timeout--) if ((inb(0x64) & 1) == 1) return;
    } else { // Write
        while (timeout--) if ((inb(0x64) & 2) == 0) return;
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
    mouse_wait(0);
    inb(0x60); // ACK
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init(void) {
    mouse_wait(1);
    outb(0x64, 0xA8); // Enable auxiliary device
    
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60) | 2; // Enable IRQ12
    
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    
    mouse_write(0xF6); // Defaults
    
    // Enable Intellimouse extensions
    mouse_write(0xF3); // Set sample rate
    mouse_write(200);
    mouse_write(0xF3); // Set sample rate
    mouse_write(100);
    mouse_write(0xF3); // Set sample rate
    mouse_write(80);
    
    mouse_write(0xF2); // Get device id
    uint8_t mouse_id = mouse_read();
    if (mouse_id == 3) {
        mouse_has_wheel = 1;
    }
    
    mouse_write(0xF4); // Enable streaming
    
    register_interrupt_handler(44, mouse_callback);
}

void mouse_handler_inject_absolute(int x, int y, uint8_t buttons) {
    extern uint32_t vesa_width, vesa_height;
    int rel_x = x - mouse_x;
    int rel_y = -(y - mouse_y); // Y is inverted in relative
    
    mouse_x = x;
    mouse_y = y;
    mouse_buttons = buttons;
    
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > (int)vesa_width - 1) mouse_x = vesa_width - 1;
    if (mouse_y > (int)vesa_height - 1) mouse_y = vesa_height - 1;
    
    extern int sdl_app_active;
    if (sdl_app_active) {
        extern void sdl_push_mousemove(int x, int y, int dx, int dy);
        extern void sdl_push_mousebutton(int down, uint8_t button, int x, int y);
        
        sdl_push_mousemove(mouse_x, mouse_y, rel_x, -rel_y);
        
        static uint8_t last_buttons = 0;
        if ((buttons & 1) && !(last_buttons & 1)) sdl_push_mousebutton(1, 1, mouse_x, mouse_y);
        if (!(buttons & 1) && (last_buttons & 1)) sdl_push_mousebutton(0, 1, mouse_x, mouse_y);
        if ((buttons & 2) && !(last_buttons & 2)) sdl_push_mousebutton(1, 3, mouse_x, mouse_y);
        if (!(buttons & 2) && (last_buttons & 2)) sdl_push_mousebutton(0, 3, mouse_x, mouse_y);
        if ((buttons & 4) && !(last_buttons & 4)) sdl_push_mousebutton(1, 2, mouse_x, mouse_y);
        if (!(buttons & 4) && (last_buttons & 4)) sdl_push_mousebutton(0, 2, mouse_x, mouse_y);
        
        last_buttons = buttons;
        return;
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

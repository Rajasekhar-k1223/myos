#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "kernel.h"
#include "vesa.h"

int32_t mouse_x = 512;
int32_t mouse_y = 384;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static uint8_t mouse_buttons = 0;

int32_t mouse_get_x(void) { return mouse_x; }
int32_t mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return mouse_buttons; }

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
                mouse_cycle = 0;
                
                mouse_buttons = mouse_byte[0] & 0x07; // 0x01 = Left, 0x02 = Right, 0x04 = Middle
                
                int rel_x = mouse_byte[1];
                int rel_y = mouse_byte[2];
                if (rel_x && (mouse_byte[0] & (1 << 4))) rel_x -= 256;
                if (rel_y && (mouse_byte[0] & (1 << 5))) rel_y -= 256;
                
                mouse_x += rel_x;
                mouse_y -= rel_y; 
                
                extern uint32_t vesa_width, vesa_height;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x > (int)vesa_width - 1) mouse_x = vesa_width - 1;
                if (mouse_y > (int)vesa_height - 1) mouse_y = vesa_height - 1;
                
                // Let the WM know it needs to redraw!
                extern void wm_request_redraw(void);
                wm_request_redraw();
                
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
    mouse_write(0xF4); // Enable streaming
    
    register_interrupt_handler(44, mouse_callback);
}

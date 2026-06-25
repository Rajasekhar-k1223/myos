#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "kernel.h"
#include "vesa.h"

int32_t mouse_x = 512;
int32_t mouse_y = 384;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];

// Save buffer for the 10x15 cursor pixels
static uint32_t cursor_saved_pixels[10 * 15];
static int32_t saved_x = 512;
static int32_t saved_y = 384;
static int cursor_drawn = 0;

static const uint32_t cursor_bitmap[15][10] = {
    {1,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,1,1,1,1},
    {1,2,2,1,2,2,1,0,0,0},
    {1,2,1,0,1,2,2,1,0,0},
    {1,1,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,1,1,0,0,0},
};

void mouse_draw_cursor(void) {
    extern uint32_t vesa_width, vesa_height;
    
    // Clamp to screen bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > (int)vesa_width - 10) mouse_x = vesa_width - 10;
    if (mouse_y > (int)vesa_height - 15) mouse_y = vesa_height - 15;

    // Save pixels under the cursor
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 10; x++) {
            cursor_saved_pixels[y * 10 + x] = vesa_getpixel(mouse_x + x, mouse_y + y);
        }
    }
    saved_x = mouse_x;
    saved_y = mouse_y;
    
    // Draw the cursor
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 10; x++) {
            if (cursor_bitmap[y][x] == 1) vesa_putpixel(mouse_x + x, mouse_y + y, 0x000000); // Black outline
            else if (cursor_bitmap[y][x] == 2) vesa_putpixel(mouse_x + x, mouse_y + y, 0xFFFFFF); // White fill
        }
    }
    cursor_drawn = 1;
}

void mouse_erase_cursor(void) {
    if (!cursor_drawn) return;
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 10; x++) {
            vesa_putpixel(saved_x + x, saved_y + y, cursor_saved_pixels[y * 10 + x]);
        }
    }
    cursor_drawn = 0;
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
                mouse_cycle = 0;
                
                int rel_x = mouse_byte[1];
                int rel_y = mouse_byte[2];
                if (rel_x && (mouse_byte[0] & (1 << 4))) rel_x -= 256;
                if (rel_y && (mouse_byte[0] & (1 << 5))) rel_y -= 256;
                
                mouse_erase_cursor();
                mouse_x += rel_x;
                mouse_y -= rel_y; 
                mouse_draw_cursor();
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
    mouse_draw_cursor(); // Draw initial cursor
}

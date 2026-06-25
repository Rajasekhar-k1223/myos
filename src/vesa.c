#include "vesa.h"
#include "kheap.h"
#include "string.h"

static uint32_t* fb = 0;
uint32_t vesa_width  = 0;
uint32_t vesa_height = 0;
static uint32_t fb_pitch = 0;

static uint32_t* backbuffer = 0;
static int double_buffer_enabled = 0;

/*
 * Initialise from Multiboot2 framebuffer tag parameters.
 * On UEFI:  addr is the GOP linear framebuffer base.
 * On BIOS:  addr is the VBE linear framebuffer base.
 * Both look identical to the kernel — same linear pixel array.
 */
void vesa_init(uint32_t addr, uint32_t width, uint32_t height,
               uint32_t pitch, uint8_t bpp) {
    (void)bpp; /* assume 32bpp — enforced by MB2 header request tag */
    fb          = (uint32_t*)addr;
    vesa_width  = width;
    vesa_height = height;
    fb_pitch    = pitch;
}

void vesa_init_backbuffer(void) {
    if (vesa_width && vesa_height) {
        backbuffer = (uint32_t*)kmalloc(vesa_width * vesa_height * 4);
    }
}

void vesa_set_double_buffer(int enable) {
    double_buffer_enabled = enable;
}

void vesa_swap_buffers(void) {
    if (double_buffer_enabled && backbuffer && fb) {
        memcpy(fb, backbuffer, vesa_width * vesa_height * 4);
    }
}

void vesa_draw_desktop_bg(uint32_t* bg) {
    if (double_buffer_enabled && backbuffer && bg) {
        memcpy(backbuffer, bg, vesa_width * vesa_height * 4);
    }
}

uint32_t vesa_get_fb_addr(void) {
    return (uint32_t)fb;
}

uint32_t vesa_get_fb_size(void) {
    return fb_pitch * vesa_height;
}

void vesa_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= vesa_width || y >= vesa_height) return;
    
    if (double_buffer_enabled && backbuffer) {
        backbuffer[y * vesa_width + x] = color;
    } else if (fb) {
        fb[(y * (fb_pitch / 4)) + x] = color;
    }
}

uint32_t vesa_getpixel(uint32_t x, uint32_t y) {
    if (x >= vesa_width || y >= vesa_height) return 0;
    
    if (double_buffer_enabled && backbuffer) {
        return backbuffer[y * vesa_width + x];
    } else if (fb) {
        return fb[(y * (fb_pitch / 4)) + x];
    }
    return 0;
}

void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            vesa_putpixel(x + j, y + i, color);
        }
    }
}

void vesa_clear(uint32_t color) {
    for (uint32_t y = 0; y < vesa_height; y++) {
        for (uint32_t x = 0; x < vesa_width; x++) {
            vesa_putpixel(x, y, color);
        }
    }
}

void vesa_scroll_by(uint32_t pixels) {
    if (pixels == 0 || pixels >= vesa_height) return;
    if (double_buffer_enabled && backbuffer) {
        for (uint32_t y = 0; y < vesa_height - pixels; y++)
            for (uint32_t x = 0; x < vesa_width; x++)
                backbuffer[y * vesa_width + x] = backbuffer[(y + pixels) * vesa_width + x];
        vesa_draw_rect(0, vesa_height - pixels, vesa_width, pixels, 0x000000);
        vesa_swap_buffers();
    } else if (fb) {
        uint32_t stride = fb_pitch / 4;
        for (uint32_t y = 0; y < vesa_height - pixels; y++)
            for (uint32_t x = 0; x < vesa_width; x++)
                fb[y * stride + x] = fb[(y + pixels) * stride + x];
        vesa_draw_rect(0, vesa_height - pixels, vesa_width, pixels, 0x000000);
    }
}

void vesa_scroll(void) {
    vesa_scroll_by(16);   /* default: one 8×16 font row */
}

#include "vesa.h"

static uint32_t* fb = 0;
uint32_t vesa_width = 0;
uint32_t vesa_height = 0;
static uint32_t fb_pitch = 0;

void vesa_init(struct multiboot_info* mbi) {
    if (mbi->flags & (1 << 12)) {
        fb = (uint32_t*)(uint32_t)mbi->framebuffer_addr;
        vesa_width = mbi->framebuffer_width;
        vesa_height = mbi->framebuffer_height;
        fb_pitch = mbi->framebuffer_pitch;
    }
}

uint32_t vesa_get_fb_addr(void) {
    return (uint32_t)fb;
}

uint32_t vesa_get_fb_size(void) {
    return fb_pitch * vesa_height;
}

void vesa_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb || x >= vesa_width || y >= vesa_height) return;
    fb[(y * (fb_pitch / 4)) + x] = color;
}

uint32_t vesa_getpixel(uint32_t x, uint32_t y) {
    if (!fb || x >= vesa_width || y >= vesa_height) return 0;
    return fb[(y * (fb_pitch / 4)) + x];
}

void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            vesa_putpixel(x + j, y + i, color);
        }
    }
}

void vesa_clear(uint32_t color) {
    if (!fb) return;
    for (uint32_t y = 0; y < vesa_height; y++) {
        for (uint32_t x = 0; x < vesa_width; x++) {
            vesa_putpixel(x, y, color);
        }
    }
}

void vesa_scroll(void) {
    if (!fb) return;
    for (uint32_t y = 0; y < vesa_height - 8; y++) {
        for (uint32_t x = 0; x < vesa_width; x++) {
            fb[y * (fb_pitch / 4) + x] = fb[(y + 8) * (fb_pitch / 4) + x];
        }
    }
    vesa_draw_rect(0, vesa_height - 8, vesa_width, 8, 0x000000);
}

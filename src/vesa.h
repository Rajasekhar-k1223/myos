#pragma once
#include <stdint.h>
#include "multiboot.h"

extern uint32_t vesa_width;
extern uint32_t vesa_height;

/* Primary init: called with GOP/VBE framebuffer params from Multiboot2 tag */
void vesa_init(uint32_t addr, uint32_t width, uint32_t height,
               uint32_t pitch, uint8_t bpp);
void vesa_init_backbuffer(void);
void vesa_set_double_buffer(int enable);
void vesa_swap_buffers(void);
void vesa_draw_desktop_bg(uint32_t* bg);

void vesa_putpixel(uint32_t x, uint32_t y, uint32_t color);
void vesa_putpixel_alpha(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha);
uint32_t vesa_getpixel(uint32_t x, uint32_t y);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vesa_draw_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha);
void vesa_clear(uint32_t color);
void vesa_scroll(void);
void vesa_scroll_by(uint32_t pixels);  /* scroll up by N pixel rows */
uint32_t vesa_get_fb_addr(void);
uint32_t vesa_get_fb_size(void);

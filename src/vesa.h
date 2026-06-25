#pragma once
#include <stdint.h>
#include "multiboot.h"

extern uint32_t vesa_width;
extern uint32_t vesa_height;

void vesa_init(struct multiboot_info* mbi);
void vesa_init_backbuffer(void);
void vesa_set_double_buffer(int enable);
void vesa_swap_buffers(void);

void vesa_putpixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t vesa_getpixel(uint32_t x, uint32_t y);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vesa_clear(uint32_t color);
void vesa_scroll(void);
uint32_t vesa_get_fb_addr(void);
uint32_t vesa_get_fb_size(void);

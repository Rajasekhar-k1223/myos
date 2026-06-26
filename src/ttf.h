#ifndef TTF_H
#define TTF_H

#include <stdint.h>

void ttf_init(uint8_t* font_data);
uint16_t ttf_get_glyph_index(uint16_t charcode);
void ttf_render_glyph(uint16_t glyph_index, float scale, float offset_x, float offset_y, uint32_t* buffer, int width, int height, uint32_t color);

#endif

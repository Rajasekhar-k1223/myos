#ifndef TTF_H
#define TTF_H

#include <stdint.h>

void     ttf_init(uint8_t* font_data);
int      ttf_is_loaded(void);
uint16_t ttf_get_upm(void);
uint16_t ttf_get_glyph_index(uint16_t charcode);
void ttf_render_glyph(uint16_t glyph_index, float scale, float offset_x, float offset_y, uint32_t* buffer, int width, int height, uint32_t color);
void ttf_draw_string(uint32_t* buffer, int width, int height, int x, int y, const char* str, int len, int font_size, uint32_t color);

#endif

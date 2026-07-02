#include "ttf.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"
#include "widget.h" // For widget_blend_color

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x,u)  ((void)(u),kmalloc(x))
#define STBTT_free(x,u)    ((void)(u),kfree(x))
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset
#define STBTT_strlen       strlen
#define STBTT_assert(x)    do { if (!(x)) { terminal_printf("STBTT ASSERT FAILED\n"); } } while(0)

// Provide minimal math functions for stb_truetype
static float custom_sqrtf(float x) {
    if (x <= 0) return 0;
    union {
        float f;
        int32_t i;
    } conv;
    float x2, y;
    const float threehalfs = 1.5F;

    x2 = x * 0.5F;
    conv.f  = x;
    conv.i  = 0x5f3759df - ( conv.i >> 1 );
    y  = conv.f;
    
    y  = y * ( threehalfs - ( x2 * y * y ) );
    y  = y * ( threehalfs - ( x2 * y * y ) );
    y  = y * ( threehalfs - ( x2 * y * y ) );
    
    return x * y;
}

#define STBTT_ifloor(x)    ((int)(x))
#define STBTT_iceil(x)     ((int)((x) + 0.9999f))
#define STBTT_sqrt(x)      custom_sqrtf(x)
#define STBTT_pow(x,y)     (0)
#define STBTT_fmod(x,y)    ((x) - (int)(x))
#define STBTT_cos(x)       (0)
#define STBTT_acos(x)      (0)
#define STBTT_fabs(x)      ((x) < 0 ? -(x) : (x))

#include "stb_truetype.h"

static stbtt_fontinfo font_info;
static int ttf_loaded = 0;

void ttf_init(uint8_t* font_data) {
    if (!font_data) return;
    int offset = stbtt_GetFontOffsetForIndex(font_data, 0);
    if (offset < 0) {
        terminal_printf("[TTF] Error: Invalid TrueType format!\n");
        return;
    }
    int ret;
    asm volatile("cli");
    ret = stbtt_InitFont(&font_info, font_data, offset);
    asm volatile("sti");
    
    if (ret) {
        ttf_loaded = 1;
        terminal_printf("[TTF] stb_truetype initialized successfully!\n");
    } else {
        terminal_printf("[TTF] Error: Failed to initialize font!\n");
    }
}

int ttf_is_loaded(void) {
    return ttf_loaded;
}

void ttf_draw_string(uint32_t* buffer, int width, int height, int x, int y, const char* str, int len, int font_size, uint32_t color) {
    if (!ttf_loaded || !buffer || len <= 0) return;

    float scale;
    asm volatile("cli");
    scale = stbtt_ScaleForPixelHeight(&font_info, (float)font_size);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &lineGap);
    asm volatile("sti");
    
    ascent = (int)(ascent * scale);
    descent = (int)(descent * scale);

    int cur_x = x;
    int cur_y = y; // y is the baseline

    for (int i = 0; i < len; i++) {
        int codepoint = (unsigned char)str[i];
        
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font_info, codepoint, &advance, &lsb);

        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(&font_info, codepoint, scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);

        int bw = c_x2 - c_x1;
        int bh = c_y2 - c_y1;
        
        if (bw > 0 && bh > 0) {
            uint8_t* bitmap = (uint8_t*)kmalloc(bw * bh);
            if (bitmap) {
                asm volatile("cli");
                stbtt_MakeCodepointBitmap(&font_info, bitmap, bw, bh, bw, scale, scale, codepoint);
                asm volatile("sti");
                
                int draw_x = cur_x + c_x1;
                int draw_y = cur_y + c_y1;
                
                for (int row = 0; row < bh; row++) {
                    for (int col = 0; col < bw; col++) {
                        int px = draw_x + col;
                        int py = draw_y + row;
                        
                        if (px >= 0 && px < width && py >= 0 && py < height) {
                            uint8_t alpha = bitmap[row * bw + col];
                            if (alpha > 0) {
                                // Proper alpha blending
                                uint32_t bg = buffer[py * width + px];
                                // Use the widget_blend_color or direct alpha blend
                                uint8_t bg_r = (bg >> 16) & 0xFF;
                                uint8_t bg_g = (bg >> 8) & 0xFF;
                                uint8_t bg_b = bg & 0xFF;
                                
                                uint8_t fg_r = (color >> 16) & 0xFF;
                                uint8_t fg_g = (color >> 8) & 0xFF;
                                uint8_t fg_b = color & 0xFF;
                                
                                uint8_t out_r = ((fg_r * alpha) + (bg_r * (255 - alpha))) / 255;
                                uint8_t out_g = ((fg_g * alpha) + (bg_g * (255 - alpha))) / 255;
                                uint8_t out_b = ((fg_b * alpha) + (bg_b * (255 - alpha))) / 255;
                                
                                buffer[py * width + px] = (out_r << 16) | (out_g << 8) | out_b;
                            }
                        }
                    }
                }
                kfree(bitmap);
            }
        }
        
        cur_x += (int)(advance * scale);
        if (i + 1 < len) {
            cur_x += (int)(stbtt_GetCodepointKernAdvance(&font_info, codepoint, (unsigned char)str[i+1]) * scale);
        }
    }
}

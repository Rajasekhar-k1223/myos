#pragma once
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;      /* bit depth per channel */
    int      color_type; /* PNG color type field */
} png_info_t;

/*
 * Parse IHDR chunk from raw PNG data.
 * Returns 0 on success, -1 if not a valid PNG or IHDR not found.
 */
int  png_get_info(const uint8_t* data, uint32_t len, png_info_t* info);

/*
 * Render a placeholder rectangle into a 32-bpp pixel buffer.
 * Uses a colour derived from hashing `filename` so different images look
 * distinct.  Draws a centred "PNG WxH" label using the kernel 8x8 font.
 */
void png_render_placeholder(uint32_t* buf, int buf_w, int buf_h,
                            const png_info_t* info, const char* filename);

/*
 * Fully decode a PNG file into a 32-bit ARGB pixel buffer.
 * The image is scaled (nearest-neighbour) to out_w x out_h.
 * Supports: color_type 0 (grayscale), 2 (RGB), 4 (gray+alpha), 6 (RGBA),
 *           8-bit depth, DEFLATE stored (BTYPE=00) and fixed Huffman (BTYPE=01).
 * Returns 0 on success, -1 on error.
 */
int png_decode(const uint8_t* data, uint32_t len,
               uint32_t* out_pixels, int out_w, int out_h);

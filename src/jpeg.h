#pragma once
#include <stdint.h>

/*
 * Decode a JPEG file (baseline/progressive, via stb_image) into a 32-bit
 * ARGB pixel buffer (0xAARRGGBB, alpha always 0xFF — JPEG has no alpha
 * channel). The image is nearest-neighbour scaled to out_w x out_h, mirroring
 * png_decode()'s contract so callers (imgview.c) can treat both the same way.
 * Returns 0 on success, -1 on error (corrupt data, unsupported variant, OOM).
 */
int jpeg_decode(const uint8_t* data, uint32_t len,
                 uint32_t* out_pixels, int out_w, int out_h);

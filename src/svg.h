#pragma once
#include <stdint.h>

/*
 * Parse + rasterize an SVG document into a 32-bit ARGB pixel buffer
 * (0xAARRGGBB), mirroring png_decode()/jpeg_decode()'s contract. Unlike
 * those, the source is genuinely vector data: it's rasterized directly at
 * out_w x out_h (uniformly scaled, letterboxed, transparent padding) rather
 * than decoded-then-resampled, so it stays sharp at any requested size.
 * Returns 0 on success, -1 on error (malformed XML, zero-size doc, OOM).
 */
int svg_decode(const uint8_t* data, uint32_t len,
                uint32_t* out_pixels, int out_w, int out_h);

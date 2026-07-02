#include "svg.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"

/* Freestanding-kernel port of nanosvg (parser) + nanosvgrast (rasterizer).
 * Unlike stb_truetype.h/stb_image.h, nanosvg has no STBTT_/STBI_-style
 * override-macro system — it calls malloc/realloc/free and libm functions
 * (sqrtf, sinf, cosf, atan2f, ...) directly. The math side is covered by
 * src/mathf.c (real implementations, not stubs — see that file). Allocation
 * is redirected here via plain macro substitution. */
#define malloc(sz)        kmalloc(sz)
#define realloc(p, sz)    krealloc(p, sz)
#define free(p)           kfree(p)

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

/* nsvgParseFromFile() (part of nanosvg's public surface, compiled in
 * unconditionally above by NANOSVG_IMPLEMENTATION, since nanosvg.h has no
 * NANOSVG_NO_STDIO-style escape hatch the way stb_image.h does) calls libc
 * stdio functions. We never call nsvgParseFromFile ourselves -- svg_decode()
 * always goes through nsvgParse() on an in-memory buffer. See src/nostdio.c
 * for the shared fopen/fclose/fseek/ftell/fread stub definitions this dead
 * code path needs to satisfy the linker. */

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

int svg_decode(const uint8_t* data, uint32_t len,
                uint32_t* out_pixels, int out_w, int out_h) {
    if (!data || !out_pixels || len == 0 || out_w <= 0 || out_h <= 0) return -1;

    /* nsvgParse() tokenizes its input in place, so it needs a mutable,
     * null-terminated copy -- data points into the read-only initrd tar. */
    char* copy = (char*)kmalloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, data, len);
    copy[len] = '\0';

    NSVGimage* image = nsvgParse(copy, "px", 96.0f);
    if (!image || image->width <= 0 || image->height <= 0) {
        if (image) nsvgDelete(image);
        kfree(copy);
        return -1;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(image); kfree(copy); return -1; }

    uint8_t* rgba = (uint8_t*)kmalloc((size_t)out_w * (size_t)out_h * 4);
    if (!rgba) { nsvgDeleteRasterizer(rast); nsvgDelete(image); kfree(copy); return -1; }
    memset(rgba, 0, (size_t)out_w * (size_t)out_h * 4); /* transparent letterbox padding */

    /* Uniform scale to fit the SVG's natural size into out_w x out_h,
     * centred, preserving aspect ratio (unlike png_decode/jpeg_decode's
     * independent-axis stretch -- here it's cheap to do right since we
     * rasterize directly at the destination resolution instead of
     * resampling a decoded bitmap). */
    float scale_x = (float)out_w / image->width;
    float scale_y = (float)out_h / image->height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    float tx = ((float)out_w  - image->width  * scale) * 0.5f;
    float ty = ((float)out_h - image->height * scale) * 0.5f;

    nsvgRasterize(rast, image, tx, ty, scale, rgba, out_w, out_h, out_w * 4);

    for (int i = 0; i < out_w * out_h; i++) {
        const uint8_t* px = rgba + i * 4;
        out_pixels[i] = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) |
                        ((uint32_t)px[1] << 8) | px[2];
    }

    kfree(rgba);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    kfree(copy);
    return 0;
}

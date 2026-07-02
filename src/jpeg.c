#include "jpeg.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"

/* Freestanding-kernel port of stb_image, JPEG decoder only (mirrors the
 * STBTT_* macro override pattern already used for stb_truetype in ttf.c). */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz)       kmalloc(sz)
#define STBI_FREE(p)          kfree(p)

/* JPEG decoding in stb_image never actually grows a buffer (unlike its PNG/
 * GIF paths), so this may go unreferenced depending on which internal code
 * paths get compiled in — kept for STBI_REALLOC_SIZED's "must be defined"
 * contract regardless. */
__attribute__((unused)) static void* jpeg_realloc_sized(void* ptr, size_t oldsz, size_t newsz) {
    void* n = kmalloc(newsz);
    if (!n) return 0;
    if (ptr) {
        size_t copy = oldsz < newsz ? oldsz : newsz;
        memcpy(n, ptr, copy);
        kfree(ptr);
    }
    return n;
}
#define STBI_REALLOC_SIZED(p, oldsz, newsz) jpeg_realloc_sized(p, oldsz, newsz)

#include "stb_image.h"

int jpeg_decode(const uint8_t* data, uint32_t len,
                 uint32_t* out_pixels, int out_w, int out_h) {
    if (!data || !out_pixels || out_w <= 0 || out_h <= 0) return -1;

    int img_w, img_h, channels;
    stbi_uc* rgba = stbi_load_from_memory(data, (int)len, &img_w, &img_h, &channels, 4);
    if (!rgba) return -1;
    if (img_w <= 0 || img_h <= 0) { stbi_image_free(rgba); return -1; }

    /* Nearest-neighbour scale into the caller's buffer, packed as
     * 0xAARRGGBB to match png_decode()'s output format. */
    for (int dy = 0; dy < out_h; dy++) {
        uint32_t sy = (uint32_t)dy * (uint32_t)img_h / (uint32_t)out_h;
        if ((int)sy >= img_h) sy = (uint32_t)img_h - 1;
        for (int dx = 0; dx < out_w; dx++) {
            uint32_t sx = (uint32_t)dx * (uint32_t)img_w / (uint32_t)out_w;
            if ((int)sx >= img_w) sx = (uint32_t)img_w - 1;
            const stbi_uc* px = rgba + (sy * (uint32_t)img_w + sx) * 4;
            out_pixels[dy * out_w + dx] =
                0xFF000000u | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
        }
    }

    stbi_image_free(rgba);
    return 0;
}

#include "bmp.h"
#include "tar.h"
#include "vesa.h"
#include "kernel.h"

#pragma pack(push, 1)
struct bmp_header {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
    uint32_t dib_header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_res;
    int32_t  y_res;
    uint32_t colors;
    uint32_t imp_colors;
};
#pragma pack(pop)

void bmp_draw_file(const char* filename, uint32_t screen_x, uint32_t screen_y) {
    size_t file_size;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!data) {
        terminal_printf("  [WARN] BMP '%s' not found in RAM disk\n", filename);
        return;
    }

    struct bmp_header* bmp = (struct bmp_header*)data;
    if (bmp->type != 0x4D42) { // 'BM'
        terminal_printf("  [WARN] '%s' is not a valid BMP\n", filename);
        return;
    }

    if (bmp->bpp != 24 || bmp->compression != 0) {
        terminal_printf("  [WARN] Only 24-bit uncompressed BMPs supported\n");
        return;
    }

    int width = bmp->width;
    int height = bmp->height;
    const uint8_t* pixels = data + bmp->offset;
    
    // BMPs are padded to 4-byte boundaries per row
    int row_bytes = (width * 3 + 3) & ~3;

    // Standard BMPs are stored bottom-up
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_idx = (height - 1 - y) * row_bytes + x * 3;
            uint8_t b = pixels[src_idx];
            uint8_t g = pixels[src_idx + 1];
            uint8_t r = pixels[src_idx + 2];
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            
            // Only draw non-black pixels (treating pure black as transparent)
            if (color != 0x000000) {
                vesa_putpixel(screen_x + x, screen_y + y, color);
            }
        }
    }
}

void bmp_load_to_window(const char* filename, window_t* win) {
    if (!win || !win->buffer) return;

    size_t file_size;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!data) return;

    struct bmp_header* bmp = (struct bmp_header*)data;
    if (bmp->type != 0x4D42) return;
    if (bmp->bpp != 24 || bmp->compression != 0) return;

    int width = bmp->width;
    int height = bmp->height;
    const uint8_t* pixels = data + bmp->offset;
    
    int row_bytes = (width * 3 + 3) & ~3;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (x >= (int)win->w || y >= (int)win->h) continue;

            int src_idx = (height - 1 - y) * row_bytes + x * 3;
            uint8_t b = pixels[src_idx];
            uint8_t g = pixels[src_idx + 1];
            uint8_t r = pixels[src_idx + 2];
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            
            if (color != 0x000000) {
                win->buffer[y * win->w + x] = color;
            }
        }
    }
}

void bmp_load_to_buffer(const char* filename, uint32_t* buffer, int buf_w, int buf_h, int offset_x, int offset_y) {
    if (!buffer) return;

    size_t file_size;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!data) return;

    struct bmp_header* bmp = (struct bmp_header*)data;
    if (bmp->type != 0x4D42) return;
    if (bmp->bpp != 24 || bmp->compression != 0) return;

    int width = bmp->width;
    int height = bmp->height;
    const uint8_t* pixels = data + bmp->offset;
    
    int row_bytes = (width * 3 + 3) & ~3;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dest_x = offset_x + x;
            int dest_y = offset_y + y;
            if (dest_x >= buf_w || dest_y >= buf_h || dest_x < 0 || dest_y < 0) continue;

            int src_idx = (height - 1 - y) * row_bytes + x * 3;
            uint8_t b = pixels[src_idx];
            uint8_t g = pixels[src_idx + 1];
            uint8_t r = pixels[src_idx + 2];
            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            
            if (color != 0x000000) {
                buffer[dest_y * buf_w + dest_x] = color;
            }
        }
    }
}

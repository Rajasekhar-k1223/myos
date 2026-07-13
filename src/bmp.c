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

#include "stb_image.h"

void bmp_load_to_buffer(const char* filename, uint32_t* buffer, int buf_w, int buf_h, int offset_x, int offset_y) {
    if (!buffer) return;

    size_t file_size;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!data) return;

    int img_w, img_h, channels;
    uint8_t* img_data = stbi_load_from_memory(data, file_size, &img_w, &img_h, &channels, 4);
    if (!img_data) return;

    int width = img_w;
    int height = img_h;
    const uint8_t* pixels = img_data;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dest_x = offset_x + x;
            int dest_y = offset_y + y;
            if (dest_x >= buf_w || dest_y >= buf_h || dest_x < 0 || dest_y < 0) continue;

            const uint8_t* px = &pixels[(y * width + x) * 4];
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];
            uint8_t a = px[3];
            
            uint32_t color = (255 << 24) | (r << 16) | (g << 8) | b;
            
            if (r == 255 && g == 0 && b == 255) {
                buffer[dest_y * buf_w + dest_x] = 0; // Magenta mask
            } else if (a > 128) {
                buffer[dest_y * buf_w + dest_x] = color;
            }
        }
    }
    stbi_image_free(img_data);
}

void bmp_load_to_buffer_scaled(const char* filename, uint32_t* buffer, int buf_w, int buf_h, int offset_x, int offset_y, int scale_w, int scale_h) {
    size_t file_size;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!data) {
        terminal_printf("[BMP] tar_get_file failed for %s\n", filename);
        return;
    }

    int img_w, img_h, channels;
    uint8_t* img_data = stbi_load_from_memory(data, file_size, &img_w, &img_h, &channels, 4);
    if (!img_data) {
        terminal_printf("[BMP] stbi_load failed for %s (size %d): %s\n", filename, (int)file_size, stbi_failure_reason());
        return;
    }

    if (scale_w <= 0 || scale_h <= 0) {
        stbi_image_free(img_data);
        return;
    }

    int orig_width = img_w;
    int orig_height = img_h;
    const uint8_t* pixels = img_data;

    for (int y = 0; y < scale_h; y++) {
        for (int x = 0; x < scale_w; x++) {
            int src_x = (x * orig_width) / scale_w;
            int src_y = (y * orig_height) / scale_h;
            
            const uint8_t* px = &pixels[(src_y * orig_width + src_x) * 4];
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];
            uint8_t a = px[3];

            if (a > 128) {
                int dst_x = offset_x + x;
                int dst_y = offset_y + y;
                if (dst_x >= 0 && dst_x < buf_w && dst_y >= 0 && dst_y < buf_h) {
                    buffer[dst_y * buf_w + dst_x] = (255 << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
    stbi_image_free(img_data);
}

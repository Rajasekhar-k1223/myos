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
            
            if (color != 0xFF00FF) {
                buffer[dest_y * buf_w + dest_x] = color;
            } else {
                buffer[dest_y * buf_w + dest_x] = 0; // Ensure it writes 0 for magenta so draw_dock_icon skips it
            }
        }
    }
}

void bmp_load_to_buffer_scaled(const char* filename, uint32_t* buffer, int buf_w, int buf_h, int offset_x, int offset_y, int scale_w, int scale_h) {
    size_t file_size;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!data) return;

    struct bmp_header* bmp = (struct bmp_header*)data;
    if (bmp->type != 0x4D42) return;
    if (bmp->bpp != 24 || bmp->compression != 0) return;

    if (scale_w <= 0 || scale_h <= 0) return;
    int orig_width = bmp->width;
    int orig_height = bmp->height;
    int top_down = 0;
    if (orig_height < 0) {
        orig_height = -orig_height;
        top_down = 1;
    }
    const uint8_t* pixels = data + bmp->offset;
    
    int row_bytes = (orig_width * 3 + 3) & ~3;

    for (int dst_y = 0; dst_y < scale_h; dst_y++) {
        for (int dst_x = 0; dst_x < scale_w; dst_x++) {
            int px = offset_x + dst_x;
            int py = offset_y + dst_y;
            if (px < 0 || px >= buf_w || py < 0 || py >= buf_h) continue;

            int64_t gx = ((int64_t)dst_x * (orig_width - 1) * 256) / scale_w;
            int64_t gy = ((int64_t)dst_y * (orig_height - 1) * 256) / scale_h;
            int gxi = gx / 256;
            int gyi = gy / 256;
            
            int src_gyi = top_down ? gyi : (orig_height - 1 - gyi);
            int src_gyi1 = top_down ? (gyi + 1 < orig_height ? gyi + 1 : gyi) : (orig_height - 1 - (gyi + 1 < orig_height ? gyi + 1 : gyi));
            
            int c00 = src_gyi * row_bytes + gxi * 3;
            int c10 = src_gyi * row_bytes + (gxi + 1 < orig_width ? gxi + 1 : gxi) * 3;
            int c01 = src_gyi1 * row_bytes + gxi * 3;
            int c11 = src_gyi1 * row_bytes + (gxi + 1 < orig_width ? gxi + 1 : gxi) * 3;
            
            int tx = gx % 256;
            int ty = gy % 256;
            
            uint8_t r00 = pixels[c00+2], g00 = pixels[c00+1], b00 = pixels[c00];
            uint8_t r10 = pixels[c10+2], g10 = pixels[c10+1], b10 = pixels[c10];
            uint8_t r01 = pixels[c01+2], g01 = pixels[c01+1], b01 = pixels[c01];
            uint8_t r11 = pixels[c11+2], g11 = pixels[c11+1], b11 = pixels[c11];
            
            int w00 = (256 - tx) * (256 - ty);
            int w10 = tx * (256 - ty);
            int w01 = (256 - tx) * ty;
            int w11 = tx * ty;
            
            uint32_t r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) / 65536;
            uint32_t g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) / 65536;
            uint32_t b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) / 65536;
            
            if (r00 == 0 && g00 == 0 && b00 == 0) continue; // Transparency heuristic
            
            buffer[py * buf_w + px] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            
        }
    }
}

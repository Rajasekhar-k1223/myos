#include "screenshot.h"
#include "vesa.h"
#include "fat16.h"
#include "kheap.h"
#include "string.h"
#include "pit.h"

static window_t* my_win = 0;

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} bmp_file_header_t;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} bmp_info_header_t;
#pragma pack(pop)

static void screenshot_capture(void) {
    if (!my_win) return;
    
    uint32_t bw = vesa_width;
    uint32_t bh = vesa_height;
    uint32_t row_size = ((bw * 24 + 31) / 32) * 4;
    uint32_t img_size = row_size * bh;
    
    uint32_t file_size = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t) + img_size;
    uint8_t* buf = (uint8_t*)kmalloc(file_size);
    if (!buf) {
        wm_draw_string_window(my_win, 10, 70, "OOM error!", 0xFF0000);
        return;
    }
    
    bmp_file_header_t* fh = (bmp_file_header_t*)buf;
    fh->bfType = 0x4D42;
    fh->bfSize = file_size;
    fh->bfReserved1 = 0;
    fh->bfReserved2 = 0;
    fh->bfOffBits = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t);
    
    bmp_info_header_t* ih = (bmp_info_header_t*)(buf + sizeof(bmp_file_header_t));
    ih->biSize = sizeof(bmp_info_header_t);
    ih->biWidth = bw;
    ih->biHeight = -bh; // top-down
    ih->biPlanes = 1;
    ih->biBitCount = 24;
    ih->biCompression = 0;
    ih->biSizeImage = img_size;
    ih->biXPelsPerMeter = 0;
    ih->biYPelsPerMeter = 0;
    ih->biClrUsed = 0;
    ih->biClrImportant = 0;
    
    uint8_t* pix = buf + fh->bfOffBits;
    uint8_t* fb = (uint8_t*)vesa_get_fb_addr();
    
    for (uint32_t y = 0; y < bh; y++) {
        for (uint32_t x = 0; x < bw; x++) {
            uint32_t c = ((uint32_t*)fb)[y * vesa_width + x];
            pix[y * row_size + x * 3 + 0] = (c & 0xFF);         // B
            pix[y * row_size + x * 3 + 1] = ((c >> 8) & 0xFF);  // G
            pix[y * row_size + x * 3 + 2] = ((c >> 16) & 0xFF); // R
        }
    }
    
    fat16_write_file("SCRN.BMP", buf, file_size);
    kfree(buf);
    
    wm_draw_string_window(my_win, 10, 70, "Saved SCRN.BMP!   ", 0x4ade80);
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void screenshot_init(window_t* win) {
    my_win = win;
    for (uint32_t i = 0; i < win->w * win->h; i++)
        win->buffer[i] = 0x1E1E2E;
        
    wm_draw_string_window(my_win, 10, 10, "Screenshot Tool", 0xFFFFFF);
    
    for (int y=30; y<50; y++) {
        for (int x=10; x<150; x++) {
            win->buffer[y*win->w+x] = 0x3B82F6;
        }
    }
    wm_draw_string_window(my_win, 20, 36, "Capture Now", 0xFFFFFF);
}

void screenshot_handle_click(window_t* win, int mx, int my) {
    (void)win;
    if (mx >= 10 && mx <= 150 && my >= 30 && my <= 50) {
        screenshot_capture();
    }
}

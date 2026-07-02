#include "recorder.h"
#include "string.h"
#include "fat16.h"
#include "vesa.h"
#include "kheap.h"
#include "pit.h"

int sprintf(char *str, const char *format, ...);

static window_t* my_win = 0;
static int is_recording = 0;
static int frame_count  = 0;
static uint32_t last_frame_tick = 0;
#define FRAME_INTERVAL 50   /* ~2 fps at 100 Hz PIT */
#define MAX_FRAMES     30

#pragma pack(push, 1)
typedef struct { uint16_t bfType; uint32_t bfSize; uint16_t res1, res2; uint32_t bfOff; } bmp_fh_t;
typedef struct { uint32_t biSize; int32_t w, h; uint16_t planes, bpp;
                 uint32_t comp, imgSz; int32_t xppm, yppm; uint32_t clrUsed, clrImp; } bmp_ih_t;
#pragma pack(pop)

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y+h; yy++)
        for (int xx = x; xx < x+w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

static void capture_frame(void) {
    uint32_t bw = vesa_width, bh = vesa_height;
    uint32_t row_bytes = ((bw * 24 + 31) / 32) * 4;
    uint32_t img_sz    = row_bytes * bh;
    uint32_t file_sz   = sizeof(bmp_fh_t) + sizeof(bmp_ih_t) + img_sz;

    uint8_t* buf = (uint8_t*)kmalloc(file_sz);
    if (!buf) return;

    bmp_fh_t* fh = (bmp_fh_t*)buf;
    fh->bfType = 0x4D42; fh->bfSize = file_sz; fh->res1 = fh->res2 = 0;
    fh->bfOff  = sizeof(bmp_fh_t) + sizeof(bmp_ih_t);

    bmp_ih_t* ih = (bmp_ih_t*)(buf + sizeof(bmp_fh_t));
    ih->biSize = sizeof(bmp_ih_t); ih->w = (int32_t)bw; ih->h = -(int32_t)bh;
    ih->planes = 1; ih->bpp = 24; ih->comp = 0; ih->imgSz = img_sz;
    ih->xppm = ih->yppm = 2835; ih->clrUsed = ih->clrImp = 0;

    uint8_t* px = buf + fh->bfOff;
    /* Read from current front framebuffer */
    extern uint32_t* fb;          /* declared in vesa.c */
    uint32_t* src = fb;
    if (!src) { kfree(buf); return; }

    for (uint32_t y = 0; y < bh; y++) {
        uint8_t* row = px + y * row_bytes;
        for (uint32_t x = 0; x < bw; x++) {
            uint32_t c = src[y * bw + x];
            row[x*3+0] = (uint8_t)(c & 0xFF);
            row[x*3+1] = (uint8_t)((c >> 8) & 0xFF);
            row[x*3+2] = (uint8_t)((c >> 16) & 0xFF);
        }
    }

    char fname[32];
    sprintf(fname, "rec%04d.bmp", frame_count++);
    fat16_write_file(fname, buf, file_sz);
    kfree(buf);
}

static void recorder_render(void) {
    if (!my_win) return;
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0x111827;

    wm_draw_string_window(my_win, 10, 8, "Screen Recorder", 0xF9FAFB);
    draw_rect(10, 22, (int)my_win->w - 20, 1, 0x374151);

    if (is_recording)
        draw_rect(10, 32, 10, 10, 0xEF4444);

    char status[64];
    sprintf(status, "%s  Frames: %d / %d",
            is_recording ? "REC" : "IDLE", frame_count, MAX_FRAMES);
    wm_draw_string_window(my_win, 26, 30, status, is_recording ? 0xEF4444 : 0x9CA3AF);

    draw_rect(10, 50, 90, 26, is_recording ? 0x7F1D1D : 0x064E3B);
    wm_draw_string_window(my_win, 22, 58, is_recording ? "Stop REC" : "Start REC", 0xFFFFFF);

    draw_rect(108, 50, 70, 26, 0x1E3A5F);
    wm_draw_string_window(my_win, 116, 58, "Clear", 0xFFFFFF);

    if (frame_count > 0) {
        char msg[48];
        sprintf(msg, "rec0000.bmp .. rec%04d.bmp", frame_count - 1);
        wm_draw_string_window(my_win, 10, 86, msg, 0x6B7280);
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void recorder_init(window_t* win) {
    my_win = win;
    is_recording = 0;
    frame_count  = 0;
    last_frame_tick = 0;
    recorder_render();
}

void recorder_tick(void) {
    if (!is_recording || frame_count >= MAX_FRAMES) return;
    uint32_t now = pit_get_ticks();
    if (now - last_frame_tick >= FRAME_INTERVAL) {
        capture_frame();
        last_frame_tick = now;
        recorder_render();
        if (frame_count >= MAX_FRAMES) {
            is_recording = 0;
            extern void wm_toast(const char*, uint32_t);
            wm_toast("Recording complete (30 frames)", 200);
        }
    }
}

void recorder_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;

    if (lx >= 10 && lx <= 100 && ly >= 50 && ly <= 76) {
        is_recording = !is_recording;
        if (is_recording) { frame_count = 0; last_frame_tick = pit_get_ticks(); }
        recorder_render();
    }
    if (lx >= 108 && lx <= 178 && ly >= 50 && ly <= 76) {
        is_recording = 0;
        frame_count  = 0;
        recorder_render();
    }
}

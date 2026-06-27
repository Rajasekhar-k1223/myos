#include "imgview.h"
#include "bmp.h"
#include "png.h"
#include "tar.h"
#include "wm.h"
#include "string.h"
#include "kheap.h"

#define IV_SRC_W 256
#define IV_SRC_H 256

static window_t* iv_win    = NULL;
static uint32_t  iv_src[IV_SRC_W * IV_SRC_H]; /* original pixels */
static int       iv_src_w  = 0;
static int       iv_src_h  = 0;
static int       iv_zoom   = 4;  /* 1..8 — scale = zoom/4 (so 4=1x, 8=2x, 2=0.5x) */
static int       iv_pan_x  = 0;
static int       iv_pan_y  = 0;
static char      iv_name[64] = "";

static void iv_render(void) {
    if (!iv_win) return;
    int ww = (int)iv_win->w, wh = (int)iv_win->h;

    /* background */
    for (int i = 0; i < ww * wh; i++) iv_win->buffer[i] = 0x1A1A2E;

    if (iv_src_w == 0) {
        wm_draw_string_window(iv_win, 10, 10, "No image loaded.", 0x888888);
        wm_draw_string_window(iv_win, 10, 28, "Click a BMP in File Explorer", 0x666666);
        return;
    }

    /* scale = iv_zoom / 4.0  (integer math: multiply src coord by 4, divide by iv_zoom) */
    for (int dy = 0; dy < wh; dy++) {
        for (int dx = 0; dx < ww; dx++) {
            int sx = (dx * 4 / iv_zoom) + iv_pan_x;
            int sy = (dy * 4 / iv_zoom) + iv_pan_y;
            uint32_t col;
            if (sx < 0 || sx >= iv_src_w || sy < 0 || sy >= iv_src_h) {
                /* checkerboard transparent background */
                col = ((dx / 8 + dy / 8) & 1) ? 0x444444 : 0x333333;
            } else {
                col = iv_src[sy * iv_src_w + sx];
            }
            iv_win->buffer[dy * ww + dx] = col;
        }
    }

    /* HUD overlay */
    char _hud[64];
    int _pct = (iv_zoom * 100) / 4;
    sprintf(_hud, "%s  %dx%d  zoom:%d%%  +/- to zoom  arrows to pan",
            iv_name, iv_src_w, iv_src_h, _pct);
    /* dim strip at top */
    for (int xx = 0; xx < ww && xx < (int)strlen(_hud) * 8 + 8; xx++)
        for (int yy = 0; yy < 14; yy++)
            if (xx < ww && yy < wh)
                iv_win->buffer[yy * ww + xx] =
                    (iv_win->buffer[yy * ww + xx] & 0xFEFEFE) >> 1;
    wm_draw_string_window(iv_win, 4, 1, _hud, 0xFFFFFF);

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void imgview_init(window_t* win, const char* filename) {
    iv_win   = win;
    iv_zoom  = 4;
    iv_pan_x = 0;
    iv_pan_y = 0;
    iv_src_w = 0;
    iv_src_h = 0;

    if (filename && filename[0]) {
        strncpy(iv_name, filename, 63);
        iv_name[63] = '\0';
        memset(iv_src, 0, sizeof(iv_src));

        /* Detect PNG by extension */
        int is_png = 0;
        int fnlen = (int)strlen(filename);
        if (fnlen > 4 &&
            filename[fnlen-4] == '.' &&
            (filename[fnlen-3] == 'p' || filename[fnlen-3] == 'P') &&
            (filename[fnlen-2] == 'n' || filename[fnlen-2] == 'N') &&
            (filename[fnlen-1] == 'g' || filename[fnlen-1] == 'G'))
            is_png = 1;

        if (is_png) {
            size_t fsz = 0;
            const uint8_t* fdata = (const uint8_t*)tar_get_file(filename, &fsz);
            if (fdata && fsz > 8) {
                png_info_t info;
                if (png_get_info(fdata, (uint32_t)fsz, &info) == 0) {
                    iv_src_w = (int)info.width  > IV_SRC_W ? IV_SRC_W : (int)info.width;
                    iv_src_h = (int)info.height > IV_SRC_H ? IV_SRC_H : (int)info.height;
                    if (png_decode(fdata, (uint32_t)fsz, iv_src, iv_src_w, iv_src_h) != 0) {
                        png_render_placeholder(iv_src, iv_src_w, iv_src_h, &info, filename);
                    }
                }
            }
            if (iv_src_w == 0) { iv_src_w = 64; iv_src_h = 64; }
        } else {
            bmp_load_to_buffer(filename, iv_src, IV_SRC_W, IV_SRC_H, 0, 0);
            iv_src_w = IV_SRC_W;
            iv_src_h = IV_SRC_H;
        }
        /* auto-fit zoom so the image fills the window */
        int fit_x = (iv_win->w * 4) / iv_src_w;
        int fit_y = (iv_win->h * 4) / iv_src_h;
        iv_zoom = fit_x < fit_y ? fit_x : fit_y;
        if (iv_zoom < 1)  iv_zoom = 1;
        if (iv_zoom > 32) iv_zoom = 32;
    } else {
        strcpy(iv_name, "(none)");
    }
    iv_render();
}

void imgview_handle_key(window_t* win, char c) {
    if (win != iv_win) return;
    int changed = 1;
    if (c == '+' || c == '=') {
        if (iv_zoom < 32) iv_zoom++;
    } else if (c == '-') {
        if (iv_zoom > 1) iv_zoom--;
    } else if (c == '\x10') { /* up arrow */
        iv_pan_y -= 8;
        if (iv_pan_y < 0) iv_pan_y = 0;
    } else if (c == '\x11') { /* down arrow */
        iv_pan_y += 8;
        if (iv_pan_y > iv_src_h - 1) iv_pan_y = iv_src_h - 1;
    } else if (c == 'h' || c == 'H') {
        iv_pan_x -= 8;
        if (iv_pan_x < 0) iv_pan_x = 0;
    } else if (c == 'l' || c == 'L') {
        iv_pan_x += 8;
        if (iv_pan_x > iv_src_w - 1) iv_pan_x = iv_src_w - 1;
    } else if (c == 'r' || c == 'R') { /* reset */
        iv_zoom = 4; iv_pan_x = 0; iv_pan_y = 0;
    } else {
        changed = 0;
    }
    if (changed) iv_render();
}

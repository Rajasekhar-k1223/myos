#include "fontview.h"
#include "ttf.h"
#include "string.h"

int sprintf(char *str, const char *format, ...);

static window_t* my_win = 0;

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y+h; yy++)
        for (int xx = x; xx < x+w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

static void fontview_render(void) {
    if (!my_win) return;
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0xFFFFFF;

    draw_rect(0, 0, (int)my_win->w, 22, 0xF1F5F9);
    wm_draw_string_window(my_win, 8, 5, "Font Viewer — ElseaOS System Font", 0x1E293B);
    draw_rect(0, 22, (int)my_win->w, 1, 0xCBD5E1);

    static const char* sample = "The quick brown fox jumps over the lazy dog.";
    static const int   sizes[] = {8, 10, 12, 16, 20, 24, 32};
    int y = 30;

    for (int si = 0; si < 7 && y < (int)my_win->h - 40; si++) {
        int sz = sizes[si];
        char label[16]; sprintf(label, "%dpx", sz);
        wm_draw_string_window(my_win, 6, y + 4, label, 0x94A3B8);
        /* Use real TTF rendering into window buffer */
        ttf_draw_string(my_win->buffer, (int)my_win->w, (int)my_win->h,
                        46, y, sample, (int)strlen(sample), sz, 0x1E293B);
        y += sz + 12;
    }

    /* Show character set preview at 14px */
    draw_rect(0, y, (int)my_win->w, 1, 0xCBD5E1);
    y += 4;
    wm_draw_string_window(my_win, 6, y, "A-Z a-z 0-9 !@#$%^&*()", 0x64748B);
    y += 14;
    static const char* charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789";
    ttf_draw_string(my_win->buffer, (int)my_win->w, (int)my_win->h,
                    6, y, charset, (int)strlen(charset), 12, 0x334155);

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void fontview_init(window_t* win) {
    my_win = win;
    fontview_render();
}

void fontview_handle_click(window_t* win, int mx, int my) {
    (void)win; (void)mx; (void)my;
}

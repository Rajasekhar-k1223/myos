#include "charmap.h"
#include "string.h"

int sprintf(char *str, const char *format, ...);

static window_t* my_win = 0;
static int selected_char = 0;
static int page = 0;   /* 0 = ASCII 32-127, 1 = Latin-1 128-255 */

/* wm clipboard access */
extern char clipboard_buf[8192];
extern uint32_t clipboard_len;

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y+h; yy++)
        for (int xx = x; xx < x+w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

#define COLS  16
#define ROWS  6
#define CELL  20

static void charmap_render(void) {
    if (!my_win) return;
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0xF8FAFC;

    draw_rect(0, 0, (int)my_win->w, 22, 0xE2E8F0);
    wm_draw_string_window(my_win, 8, 5, page ? "Character Map — Latin-1 (128-255)" : "Character Map — ASCII (32-127)", 0x1E293B);
    draw_rect(0, 22, (int)my_win->w, 1, 0xCBD5E1);

    int base = page ? 128 : 32;
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int ci = base + row * COLS + col;
            if (ci > (page ? 255 : 127)) continue;
            int cx = 10 + col * CELL;
            int cy = 28 + row * CELL;
            int sel = (ci == selected_char);
            draw_rect(cx-1, cy-1, CELL, CELL, sel ? 0x2563EB : 0xE8EEF4);
            if (sel) draw_rect(cx, cy, CELL-2, CELL-2, 0xDBEAFE);
            char ch[2] = {(char)ci, 0};
            wm_draw_string_window(my_win, cx+4, cy+4, ch, sel ? 0x1D4ED8 : 0x1E293B);
        }
    }

    /* Info panel */
    int iy = 28 + ROWS * CELL + 6;
    draw_rect(0, iy, (int)my_win->w, 1, 0xCBD5E1);
    iy += 4;

    if (selected_char > 0) {
        char info[64];
        sprintf(info, "U+%04X   Dec: %d   '%c'", selected_char, selected_char,
                (selected_char >= 32 && selected_char < 127) ? (char)selected_char : '?');
        wm_draw_string_window(my_win, 10, iy, info, 0x334155);
        wm_draw_string_window(my_win, 10, iy+16, "Click to copy. Ctrl+C to paste.", 0x64748B);
    } else {
        wm_draw_string_window(my_win, 10, iy, "Click a character to copy it.", 0x64748B);
    }

    /* Page toggle button */
    int bx = (int)my_win->w - 90, by = 4;
    draw_rect(bx, by, 82, 16, 0x3B82F6);
    wm_draw_string_window(my_win, bx+4, by+3, page ? "ASCII (32-127)" : "Latin-1 (128+)", 0xFFFFFF);

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void charmap_init(window_t* win) {
    my_win = win;
    selected_char = 0;
    page = 0;
    charmap_render();
}

void charmap_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;

    /* Page toggle button */
    int bx = (int)win->w - 90, by = 4;
    if (lx >= bx && lx <= bx+82 && ly >= by && ly <= by+16) {
        page = !page;
        selected_char = 0;
        charmap_render();
        return;
    }

    /* Grid click */
    int base = page ? 128 : 32;
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int ci = base + row * COLS + col;
            if (ci > (page ? 255 : 127)) continue;
            int cx = 10 + col * CELL;
            int cy = 28 + row * CELL;
            if (lx >= cx-1 && lx < cx+CELL-1 && ly >= cy-1 && ly < cy+CELL-1) {
                selected_char = ci;
                /* Copy to clipboard */
                clipboard_buf[0] = (char)ci;
                clipboard_buf[1] = '\0';
                clipboard_len = 1;
                charmap_render();
                extern void wm_toast(const char*, uint32_t);
                char tmsg[32];
                sprintf(tmsg, "Copied: '%c' (U+%04X)", ci >= 32 ? (char)ci : '?', ci);
                wm_toast(tmsg, 120);
                return;
            }
        }
    }
}

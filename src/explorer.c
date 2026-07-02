#include "explorer.h"
#include "fs.h"
#include "fat16.h"
#include "string.h"
#include "imgview.h"
#include "textedit.h"

/* 0 = RAM disk (initrd), 1 = FAT16 disk */
static int fat_mode = 0;

static void draw_tab(window_t* win, int x, int y, const char* label, int active) {
    uint32_t bg  = active ? 0x2A5298 : 0x1A1A2E;
    uint32_t fg  = active ? 0xFFFFFF : 0xAAAACC;
    int tw = 100, th = 20;
    for (int yy = y; yy < y + th && yy < (int)win->h; yy++)
        for (int xx = x; xx < x + tw && xx < (int)win->w; xx++)
            win->buffer[yy * win->w + xx] = bg;
    wm_draw_string_window(win, x + 6, y + 4, label, fg);
}

static void draw_file_icon(window_t* win, int px, int py, uint32_t icon_col) {
    for (int y = 0; y < 36 && (py + y) < (int)win->h; y++) {
        for (int x = 0; x < 28 && (px + x) < (int)win->w; x++) {
            uint32_t c = 0xF0F0F0;
            if (x == 0 || y == 0 || x == 27 || y == 35)      c = icon_col;
            else if (y == 1 && x >= 18)                        c = icon_col;
            else if (x == 18 && y <= 8)                        c = icon_col;
            else if (y == 8 && x >= 18)                        c = icon_col;
            else if (y > 12 && y < 16 && x > 4 && x < 22)    c = 0xCCCCCC;
            else if (y > 18 && y < 22 && x > 4 && x < 22)    c = 0xCCCCCC;
            else if (y > 24 && y < 28 && x > 4 && x < 14)    c = 0xCCCCCC;
            win->buffer[(py + y) * win->w + (px + x)] = c;
        }
    }
}

static void explorer_render(window_t* win) {
    if (!win) return;

    /* background */
    for (uint32_t i = 0; i < win->w * win->h; i++)
        win->buffer[i] = 0x1A1A2E;

    /* tab bar */
    draw_tab(win, 0,   0, "RAM Disk",   fat_mode == 0);
    draw_tab(win, 102, 0, "FAT16 Disk", fat_mode == 1);

    /* separator line */
    for (int xx = 0; xx < (int)win->w; xx++)
        win->buffer[20 * win->w + xx] = 0x2A5298;

    if (fat_mode == 0) {
        /* ── initrd files ─────────────────────────────────────────── */
        wm_draw_string_window(win, 4, 24, "RAM Disk (initrd.tar)", 0x79C0FF);

        fs_file_info_t files[20];
        int num = fs_list_files(files);

        for (int i = 0; i < num; i++) {
            int col = i % 4;
            int row = i / 4;
            int px  = 8  + col * 90;
            int py  = 42 + row * 72;
            draw_file_icon(win, px, py, 0x2A5298);
            wm_draw_string_window(win, px, py + 40, files[i].name, 0xC9D1D9);
        }
        if (num == 0)
            wm_draw_string_window(win, 10, 44, "(empty)", 0x484F58);
    } else {
        /* ── FAT16 files ──────────────────────────────────────────── */
        wm_draw_string_window(win, 4, 24, "FAT16 Disk (disk.img)", 0x79C0FF);

        fat16_file_info_t fnames[64];
        int num = fat16_list_files(fnames, 64);

        for (int i = 0; i < num; i++) {
            int col = i % 4;
            int row = i / 4;
            int px  = 8  + col * 90;
            int py  = 42 + row * 72;
            draw_file_icon(win, px, py, 0x238636);
            wm_draw_string_window(win, px, py + 40, fnames[i].name, 0xC9D1D9);
        }
        if (num == 0)
            wm_draw_string_window(win, 10, 44, "(no files — format with 'fat ls')", 0x484F58);
    }
}

void explorer_init(window_t* win) {
    if (!win) return;
    fat_mode    = 0;
    win->bg_color = 0x1A1A2E;
    explorer_render(win);
}

void explorer_handle_click(window_t* win, int mx, int my) {
    if (!win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20; /* title bar */

    /* tab clicks */
    if (ly >= 0 && ly <= 20) {
        int new_mode = (lx < 102) ? 0 : 1;
        if (new_mode != fat_mode) {
            fat_mode = new_mode;
            explorer_render(win);
            extern void wm_request_redraw(void);
            wm_request_redraw();
        }
        return;
    }

    if (fat_mode == 0) {
        /* initrd file click → open in Notepad */
        fs_file_info_t files[20];
        int num = fs_list_files(files);
        for (int i = 0; i < num; i++) {
            int col = i % 4, row = i / 4;
            int px = 8  + col * 90;
            int py = 42 + row * 72;
            if (lx >= px && lx <= px + 28 && ly >= py && ly <= py + 36) {
                // Determine file extension
                const char* ext = strrchr(files[i].name, '.');
                if (ext && (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".png") == 0)) {
                    // Open in ImageView
                    char title[80]; strcpy(title, "ImageView - ");
                    strncat(title, files[i].name, 63);
                    window_t* tw = wm_create_window(150, 100, 600, 450, title);
                    if (tw) imgview_init(tw, files[i].name);
                } else {
                    // Default to TextEdit
                    char title[80]; strcpy(title, "TextEdit - ");
                    strncat(title, files[i].name, 63);
                    window_t* tw = wm_create_window(220, 160, 600, 400, title);
                    if (tw) textedit_init(tw, files[i].name);
                }
                return;
            }
        }
    } else {
        /* FAT16 file click → open according to extension */
        fat16_file_info_t fnames[64];
        int num = fat16_list_files(fnames, 64);
        for (int i = 0; i < num; i++) {
            int col = i % 4, row = i / 4;
            int px = 8  + col * 90;
            int py = 42 + row * 72;
            if (lx >= px && lx <= px + 28 && ly >= py && ly <= py + 36) {
                const char* ext = strrchr(fnames[i].name, '.');
                if (ext && (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".png") == 0)) {
                    char title[80]; strcpy(title, "ImageView - ");
                    strncat(title, fnames[i].name, 63);
                    window_t* tw = wm_create_window(150, 100, 600, 450, title);
                    if (tw) imgview_init(tw, fnames[i].name);
                } else {
                    char title[80]; strcpy(title, "TextEdit - ");
                    strncat(title, fnames[i].name, 63);
                    window_t* tw = wm_create_window(220, 160, 600, 400, title);
                    if (tw) textedit_init(tw, fnames[i].name);
                }
                return;
            }
        }
    }
}

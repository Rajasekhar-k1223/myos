#include "wallpaper.h"
#include "tar.h"
#include "string.h"

extern void wm_draw_string_window(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t fg);
extern void wm_set_wallpaper(const char* filename);

void wallpaper_init(window_t* win) {
    if (!win) return;
    win->bg_color = 0x222222;
    for (uint32_t i = 0; i < win->w * win->h; i++) {
        win->buffer[i] = win->bg_color;
    }
    
    wm_draw_string_window(win, 10, 10, "Select Desktop Wallpaper:", 0x00FF00);
    
    int y_off = 30;
    char name[100];
    for (int i = 0; tar_get_file_at_index(i, name); i++) {
        int len = strlen(name);
        if (len >= 4 && strcmp(&name[len-4], ".bmp") == 0) {
            // Draw button rect
            for (int r = 0; r < 24; r++) {
                for (int c = 0; c < 200; c++) {
                    if (y_off + r < (int)win->h && 10 + c < (int)win->w) {
                        win->buffer[(y_off + r) * win->w + (10 + c)] = 0x555555;
                    }
                }
            }
            wm_draw_string_window(win, 20, y_off + 8, name, 0xFFFFFF);
            y_off += 30;
        }
    }
}

void wallpaper_handle_click(window_t* win, int mx, int my) {
    if (!win) return;
    int local_x = mx - win->x;
    int local_y = my - win->y - 20; // 20px titlebar
    
    if (local_x >= 10 && local_x <= 210 && local_y >= 30) {
        int clicked_row = (local_y - 30) / 30;
        if (clicked_row >= 0) {
            int bmp_idx = 0;
            char name[100];
            for (int i = 0; tar_get_file_at_index(i, name); i++) {
                int len = strlen(name);
                if (len >= 4 && strcmp(&name[len-4], ".bmp") == 0) {
                    if (bmp_idx == clicked_row) {
                        wm_set_wallpaper(name);
                        return;
                    }
                    bmp_idx++;
                }
            }
        }
    }
}

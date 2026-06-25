#include "explorer.h"
#include "fs.h"
#include "string.h"

static void explorer_render(window_t* win) {
    if (!win) return;
    
    // Clear background
    for (uint32_t i = 0; i < win->w * win->h; i++) {
        win->buffer[i] = 0xEEEEEE;
    }
    
    fs_file_info_t files[20];
    int num = fs_list_files(files);
    
    wm_draw_string_window(win, 10, 10, "MyFS - Hard Drive /", 0x000000);
    
    for (int i = 0; i < num; i++) {
        int r = i / 4;
        int c = i % 4;
        int px = 20 + c * 80;
        int py = 40 + r * 80;
        
        // Draw file icon (document)
        for(int y=0; y<40; y++) {
            for(int x=0; x<30; x++) {
                if (x == 0 || y == 0 || x == 29 || y == 39 || (y == 5 && x >= 5 && x <= 24) || (y == 15 && x >= 5 && x <= 24) || (y == 25 && x >= 5 && x <= 24)) {
                    win->buffer[(py+y) * win->w + (px+x)] = 0x000000;
                } else {
                    win->buffer[(py+y) * win->w + (px+x)] = 0xFFFFFF;
                }
            }
        }
        
        // Draw filename
        wm_draw_string_window(win, px, py + 45, files[i].name, 0x000000);
    }
}

void explorer_init(window_t* win) {
    if (!win) return;
    win->bg_color = 0xEEEEEE;
    explorer_render(win);
}

void explorer_handle_click(window_t* win, int mx, int my) {
    if (!win) return;
    int lx = mx - win->x;
    int ly = my - win->y - 20; // Title bar offset
    
    fs_file_info_t files[20];
    int num = fs_list_files(files);
    
    for (int i = 0; i < num; i++) {
        int r = i / 4;
        int c = i % 4;
        int px = 20 + c * 80;
        int py = 40 + r * 80;
        
        if (lx >= px && lx <= px + 30 && ly >= py && ly <= py + 40) {
            // Clicked this file!
            char data[512] = {0};
            if (fs_read_file(files[i].name, data) == 0) {
                // Spawn a Notepad
                char title[100];
                strcpy(title, "Notepad - ");
                strncat(title, files[i].name, 63);
                window_t* txt_win = wm_create_window(250, 200, 400, 300, title);
                if (txt_win) {
                    for(size_t j=0; j<strlen(data); j++) {
                        wm_putchar(txt_win, data[j]);
                    }
                }
            }
            return;
        }
    }
}

#include "paint.h"

static uint32_t current_color = 0x000000; // Black default

// Colors: 0: Black, 1: Red, 2: Green, 3: Blue, 4: Yellow, 5: White (Eraser)
static uint32_t palette[] = {0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFFFFFF};

void paint_init(window_t* win) {
    if (!win) return;
    
    win->bg_color = 0xFFFFFF; // White background
    
    // Fill canvas with white
    for (uint32_t i = 0; i < win->w * win->h; i++) {
        win->buffer[i] = 0xFFFFFF;
    }
    
    // Draw palette bar on the left (40px wide)
    for (int y = 0; y < (int)win->h; y++) {
        for (int x = 0; x < 40; x++) {
            win->buffer[y * win->w + x] = 0xCCCCCC;
        }
    }
    
    // Draw color boxes (30x30, 5px padding)
    for (int i = 0; i < 6; i++) {
        int by = 5 + (i * 35);
        for (int r = 0; r < 30; r++) {
            for (int c = 0; c < 30; c++) {
                if (by + r < (int)win->h && 5 + c < 40) {
                    win->buffer[(by + r) * win->w + (5 + c)] = palette[i];
                }
            }
        }
    }
}

void paint_handle_click(window_t* win, int mx, int my) {
    if (!win) return;
    int lx = mx - win->x;
    int ly = my - win->y - 20; // 20px Title bar offset
    
    if (lx < 0 || ly < 0 || lx >= (int)win->w || ly >= (int)win->h) return;
    
    // Check palette click
    if (lx < 40) {
        for (int i = 0; i < 6; i++) {
            int by = 5 + (i * 35);
            if (ly >= by && ly <= by + 30 && lx >= 5 && lx <= 35) {
                current_color = palette[i];
                return;
            }
        }
    } else {
        // Draw on canvas! 5x5 brush
        for (int r = -2; r <= 2; r++) {
            for (int c = -2; c <= 2; c++) {
                int px = lx + c;
                int py = ly + r;
                if (px >= 40 && px < (int)win->w && py >= 0 && py < (int)win->h) {
                    win->buffer[py * win->w + px] = current_color;
                }
            }
        }
    }
}

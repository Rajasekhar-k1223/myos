#include "settings.h"
#include "kheap.h"
#include "string.h"
#include "speaker.h"

static window_t* my_win = 0;

static uint32_t theme_colors[] = {
    0x008080, // Teal
    0x800000, // Maroon
    0x000080, // Navy
    0x404040, // Dark Gray
    0x808080, // Gray
    0xC0C0C0, // Silver
    0x000000, // Black
    0xFFFFFF  // White
};

#define NUM_COLORS (sizeof(theme_colors) / sizeof(uint32_t))

static void settings_render(void) {
    if (!my_win) return;
    
    // Fill background
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++) {
        my_win->buffer[i] = 0xEEEEEE;
    }
    
    extern void wm_draw_string_window(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t color);
    
    // Draw sections
    int start_y = 20;
    
    wm_draw_string_window(my_win, 20, start_y, "Title Bar Color:", 0x000000);
    for (uint32_t i = 0; i < NUM_COLORS; i++) {
        int cx = 20 + i * 30;
        int cy = start_y + 20;
        for (int yy = 0; yy < 20; yy++) {
            for (int xx = 0; xx < 20; xx++) {
                my_win->buffer[(cy + yy) * my_win->w + (cx + xx)] = theme_colors[i];
            }
        }
    }
    
    start_y += 60;
    wm_draw_string_window(my_win, 20, start_y, "Window Border:", 0x000000);
    for (uint32_t i = 0; i < NUM_COLORS; i++) {
        int cx = 20 + i * 30;
        int cy = start_y + 20;
        for (int yy = 0; yy < 20; yy++) {
            for (int xx = 0; xx < 20; xx++) {
                my_win->buffer[(cy + yy) * my_win->w + (cx + xx)] = theme_colors[i];
            }
        }
    }
    
    start_y += 60;
    wm_draw_string_window(my_win, 20, start_y, "Window Background:", 0x000000);
    for (uint32_t i = 0; i < NUM_COLORS; i++) {
        int cx = 20 + i * 30;
        int cy = start_y + 20;
        for (int yy = 0; yy < 20; yy++) {
            for (int xx = 0; xx < 20; xx++) {
                my_win->buffer[(cy + yy) * my_win->w + (cx + xx)] = theme_colors[i];
            }
        }
    }
    
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void settings_init(window_t* win) {
    my_win = win;
    settings_render();
}

void settings_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    
    int lx = mx - win->x;
    int ly = my - win->y - 20; // Title bar offset
    
    if (lx < 20 || lx > 20 + (int)NUM_COLORS * 30) return;
    
    int color_idx = (lx - 20) / 30;
    if (color_idx < 0 || color_idx >= (int)NUM_COLORS) return;
    
    // Check which row was clicked
    int start_y = 20;
    
    // Row 1: Title Bar
    if (ly >= start_y + 20 && ly <= start_y + 40) {
        current_theme.title_bg = theme_colors[color_idx];
        current_theme.title_inactive_bg = (theme_colors[color_idx] & 0xFEFEFE) >> 1; // Darker for inactive
        speaker_beep(2000, 5);
    }
    
    start_y += 60;
    // Row 2: Border
    if (ly >= start_y + 20 && ly <= start_y + 40) {
        current_theme.window_border = theme_colors[color_idx];
        speaker_beep(2000, 5);
    }
    
    start_y += 60;
    // Row 3: Window Background
    if (ly >= start_y + 20 && ly <= start_y + 40) {
        current_theme.window_bg = theme_colors[color_idx];
        
        // Update background of terminal if we wanted, but let's just let it be.
        // Or update default window colors.
        speaker_beep(2000, 5);
    }
    
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

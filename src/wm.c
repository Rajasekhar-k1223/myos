#include "wm.h"
#include "mouse.h"
#include "vesa.h"
#include "kheap.h"
#include "string.h"
#include "font.h"
#include "rtc.h"
#include "pit.h"

#define MAX_WINDOWS 10

static window_t windows[MAX_WINDOWS];
static int num_windows = 0;
static int redraw_needed = 1;

static int drag_win_idx = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static char clock_str[20] = "";
static uint32_t last_clock_ticks = 0;

static const uint32_t cursor_bitmap[15][10] = {
    {1,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,1,1,1,1},
    {1,2,2,1,2,2,1,0,0,0},
    {1,2,1,0,1,2,2,1,0,0},
    {1,1,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,1,1,0,0,0},
};

void wm_init(void) {
    vesa_init_backbuffer();
    vesa_set_double_buffer(1);
}

void wm_request_redraw(void) {
    redraw_needed = 1;
}

window_t* wm_create_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title) {
    if (num_windows >= MAX_WINDOWS) return NULL;
    window_t* win = &windows[num_windows++];
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    strncpy(win->title, title, 63);
    win->active = 1;
    win->cursor_x = 0;
    win->cursor_y = 0;
    win->fg_color = 0xAAAAAA; // Default Light Gray Text
    win->bg_color = 0x000000; // Default Black Background
    
    win->buffer = (uint32_t*)kmalloc(w * h * 4);
    
    // Fill window with background color
    for (uint32_t i = 0; i < w * h; i++) {
        win->buffer[i] = win->bg_color; 
    }
    redraw_needed = 1;
    return win;
}

static void wm_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg) {
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        for (int row = 0; row < 8; row++) {
            uint8_t row_data = font8x8[c][row];
            for (int col = 0; col < 8; col++) {
                if (row_data & (1 << col)) {
                    vesa_putpixel(x + (i * 8) + col, y + row, fg);
                }
            }
        }
    }
}

void wm_putchar(window_t* win, char c) {
    if (!win || !win->buffer) return;
    
    if (c == '\n') {
        win->cursor_x = 0;
        win->cursor_y += 8;
    } else if (c == '\r') {
        win->cursor_x = 0;
    } else if (c == '\b') {
        if (win->cursor_x >= 8) {
            win->cursor_x -= 8;
            // Clear the 8x8 character cell
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    if (win->cursor_y + y < win->h && win->cursor_x + x < win->w) {
                        win->buffer[(win->cursor_y + y) * win->w + (win->cursor_x + x)] = win->bg_color;
                    }
                }
            }
        }
    } else if (c >= ' ') {
        unsigned char uc = (unsigned char)c;
        for (int row = 0; row < 8; row++) {
            uint8_t row_data = font8x8[uc][row];
            for (int col = 0; col < 8; col++) {
                if ((row_data & (1 << col)) && (win->cursor_y + row < win->h) && (win->cursor_x + col < win->w)) {
                    win->buffer[(win->cursor_y + row) * win->w + (win->cursor_x + col)] = win->fg_color;
                }
            }
        }
        win->cursor_x += 8;
    }

    // Wrap horizontally
    if (win->cursor_x >= win->w) {
        win->cursor_x = 0;
        win->cursor_y += 8;
    }
    
    // Scroll vertically if needed
    if (win->cursor_y + 8 > win->h) {
        // Shift everything up by 8 lines
        memcpy(win->buffer, win->buffer + (win->w * 8), (win->h - 8) * win->w * 4);
        // Clear bottom 8 lines
        for (uint32_t i = (win->h - 8) * win->w; i < win->h * win->w; i++) {
            win->buffer[i] = win->bg_color;
        }
        win->cursor_y -= 8;
    }
    redraw_needed = 1;
}

static void wm_render(void) {
    extern uint32_t vesa_width, vesa_height;
    
    // 1. Draw Desktop Background (Teal)
    vesa_clear(0x008080);
    
    // 2. Draw Windows
    for (int i = 0; i < num_windows; i++) {
        window_t* w = &windows[i];
        if (!w->active) continue;
        
        // Window Border (2px)
        vesa_draw_rect(w->x - 2, w->y - 2, w->w + 4, w->h + 24, 0xC0C0C0);
        // Window Title bar (20px high)
        vesa_draw_rect(w->x, w->y, w->w, 20, 0x0000A0); // Dark Blue
        wm_draw_string(w->x + 5, w->y + 6, w->title, 0xFFFFFF); // White Text
        
        // Draw the inner buffer
        for (uint32_t yy = 0; yy < w->h; yy++) {
            for (uint32_t xx = 0; xx < w->w; xx++) {
                vesa_putpixel(w->x + xx, w->y + 20 + yy, w->buffer[yy * w->w + xx]);
            }
        }
    }
    
    // 3. Draw Taskbar
    vesa_draw_rect(0, vesa_height - 30, vesa_width, 30, 0xC0C0C0);
    vesa_draw_rect(0, vesa_height - 30, vesa_width, 2, 0xFFFFFF); // 3D highlight top edge
    
    // Start Button
    vesa_draw_rect(5, vesa_height - 25, 60, 20, 0x008000); // Green
    vesa_draw_rect(5, vesa_height - 25, 60, 2, 0x00FF00); // Highlight
    vesa_draw_rect(5, vesa_height - 25, 2, 20, 0x00FF00); // Highlight
    wm_draw_string(15, vesa_height - 19, "START", 0xFFFFFF);
    
    // Clock Box (Inset 3D)
    vesa_draw_rect(vesa_width - 160, vesa_height - 25, 150, 20, 0xA0A0A0);
    vesa_draw_rect(vesa_width - 160, vesa_height - 25, 150, 2, 0x808080); // inner shadow top
    vesa_draw_rect(vesa_width - 160, vesa_height - 25, 2, 20, 0x808080); // inner shadow left
    vesa_draw_rect(vesa_width - 160, vesa_height - 7, 150, 2, 0xFFFFFF); // inner highlight bottom
    vesa_draw_rect(vesa_width - 12, vesa_height - 25, 2, 20, 0xFFFFFF); // inner highlight right
    wm_draw_string(vesa_width - 150, vesa_height - 19, clock_str, 0x000000);
    
    // 4. Draw Mouse
    int mx = mouse_get_x();
    int my = mouse_get_y();
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 10; x++) {
            if (cursor_bitmap[y][x] == 1) vesa_putpixel(mx + x, my + y, 0x000000);
            else if (cursor_bitmap[y][x] == 2) vesa_putpixel(mx + x, my + y, 0xFFFFFF);
        }
    }
    
    // 5. Swap!
    vesa_swap_buffers();
}

void wm_process_events(void) {
    // 0. Update Clock
    uint32_t current_ticks = pit_get_ticks();
    if (current_ticks - last_clock_ticks >= 100 || clock_str[0] == '\0') {
        rtc_datetime_str(clock_str);
        last_clock_ticks = current_ticks;
        redraw_needed = 1;
    }

    int mx = mouse_get_x();
    int my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();
    
    static uint8_t last_btns = 0;
    int left_click_just_pressed = (btns & 1) && !(last_btns & 1);
    int left_click_held = (btns & 1);
    
    if (left_click_just_pressed) {
        // Iterate backwards (top-most first)
        for (int i = num_windows - 1; i >= 0; i--) {
            window_t* w = &windows[i];
            if (w->active) {
                // Check if click is inside the title bar
                if (mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                    my >= (int)w->y && my <= (int)(w->y + 20)) {
                    drag_win_idx = i;
                    drag_off_x = mx - w->x;
                    drag_off_y = my - w->y;
                    break;
                }
            }
        }
    } else if (left_click_held && drag_win_idx >= 0) {
        windows[drag_win_idx].x = mx - drag_off_x;
        windows[drag_win_idx].y = my - drag_off_y;
        redraw_needed = 1;
    } else if (!left_click_held) {
        drag_win_idx = -1;
    }
    
    last_btns = btns;

    if (redraw_needed) {
        wm_render();
        redraw_needed = 0;
    }
}

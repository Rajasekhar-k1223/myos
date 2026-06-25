#pragma once
#include <stdint.h>

typedef struct {
    uint32_t x, y;
    uint32_t w, h;
    char title[64];
    uint32_t* buffer;
    int active;
    uint32_t cursor_x, cursor_y;
    uint32_t bg_color;
    uint32_t fg_color;
    uint8_t alpha;
    /* Text buffer — non-NULL for Notepad windows; tracks raw typed text */
    char*    text_buf;
    uint32_t text_len;
} window_t;

typedef struct {
    uint32_t window_bg;
    uint32_t window_border;
    uint32_t title_bg;
    uint32_t title_fg;
    uint32_t taskbar_bg;
    uint32_t start_btn_bg;
    uint32_t start_btn_fg;
    uint32_t menu_fg;
    uint32_t title_inactive_bg;
} theme_t;

extern theme_t current_theme;

int wm_handle_keypress(char c);
int wm_handle_shortcut(char key);

void wm_init(void);
void wm_request_redraw(void);
window_t* wm_create_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title);
void wm_draw_string_window(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t fg);
void wm_putchar(window_t* win, char c);
void wm_process_events(void);

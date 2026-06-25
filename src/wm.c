#include "wm.h"
#include "mouse.h"
#include "vesa.h"
#include "kheap.h"
#include "string.h"
#include "font.h"
#include "rtc.h"
#include "pit.h"
#include "io.h"
#include "tar.h"
#include "snake.h"
#include "calc.h"
#include "clock.h"
#include "wallpaper.h"
#include "paint.h"

#define MAX_WINDOWS 10

static window_t windows[MAX_WINDOWS];
static int num_windows = 0;
static int redraw_needed = 1;

typedef struct {
    char name[64];
    int x, y, w, h;
} desktop_icon_t;

#define MAX_ICONS 32
static desktop_icon_t icons[MAX_ICONS];
static int num_icons = 0;

static uint32_t* desktop_bg_buffer = 0;
static window_t* focused_window = 0;

static int drag_win_idx = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static char clock_str[20] = "";
static uint32_t last_clock_ticks = 0;

static int start_menu_open = 0;
static int start_btn_pressed = 0;

theme_t theme_win95 = {
    .window_bg = 0xC0C0C0,
    .window_border = 0xC0C0C0,
    .title_bg = 0x0000A0,
    .title_fg = 0xFFFFFF,
    .taskbar_bg = 0xC0C0C0,
    .start_btn_bg = 0x008000,
    .start_btn_fg = 0xFFFFFF,
    .menu_fg = 0x000000,
    .title_inactive_bg = 0x808080
};

theme_t theme_ubuntu = {
    .window_bg = 0x303030,
    .window_border = 0x202020,
    .title_bg = 0x300A24,
    .title_fg = 0xFFFFFF,
    .taskbar_bg = 0x222222,
    .start_btn_bg = 0xE95420,
    .start_btn_fg = 0xFFFFFF,
    .menu_fg = 0xFFFFFF,
    .title_inactive_bg = 0x505050
};

theme_t current_theme;

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
    extern uint32_t vesa_width, vesa_height;
    vesa_init_backbuffer();
    vesa_set_double_buffer(1);
    
    current_theme = theme_win95;
    
    desktop_bg_buffer = (uint32_t*)kmalloc(vesa_width * vesa_height * 4);
    for (uint32_t i = 0; i < vesa_width * vesa_height; i++) {
        desktop_bg_buffer[i] = 0x008080;
    }
    
    extern void bmp_load_to_buffer(const char*, uint32_t*, int, int, int, int);
    // Tile logo.bmp across the desktop
    for (int y = 0; y < (int)vesa_height; y += 150) {
        for (int x = 0; x < (int)vesa_width; x += 250) {
            bmp_load_to_buffer("logo.bmp", desktop_bg_buffer, vesa_width, vesa_height, x, y);
        }
    }
    
    // Cache desktop icons
    char name[100];
    int icon_x = 20;
    int icon_y = 20;
    for (int i = 0; tar_get_file_at_index(i, name); i++) {
        if (num_icons < MAX_ICONS) {
            strncpy(icons[num_icons].name, name, 63);
            icons[num_icons].name[63] = '\0';
            icons[num_icons].x = icon_x;
            icons[num_icons].y = icon_y;
            icons[num_icons].w = 32;
            icons[num_icons].h = 32;
            num_icons++;
            
            icon_y += 80;
            if (icon_y > (int)vesa_height - 120) {
                icon_y = 20;
                icon_x += 100;
            }
        }
    }
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
    
    focused_window = win;
    
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

void wm_draw_string_window(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t fg) {
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        for (int row = 0; row < 8; row++) {
            uint8_t row_data = font8x8[c][row];
            for (int col = 0; col < 8; col++) {
                if (row_data & (1 << col)) {
                    if (x + (i*8) + col < win->w && y + row < win->h) {
                        win->buffer[(y + row) * win->w + (x + (i*8) + col)] = fg;
                    }
                }
            }
        }
    }
}

void wm_set_wallpaper(const char* filename) {
    extern uint32_t vesa_width, vesa_height;
    if (!desktop_bg_buffer) return;
    for (uint32_t i = 0; i < vesa_width * vesa_height; i++) {
        desktop_bg_buffer[i] = 0x008080;
    }
    extern void bmp_load_to_buffer(const char*, uint32_t*, int, int, int, int);
    for (int y = 0; y < (int)vesa_height; y += 150) {
        for (int x = 0; x < (int)vesa_width; x += 250) {
            bmp_load_to_buffer(filename, desktop_bg_buffer, vesa_width, vesa_height, x, y);
        }
    }
    wm_request_redraw();
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
    
    // 1. Draw Desktop Background
    extern void vesa_draw_desktop_bg(uint32_t*);
    if (desktop_bg_buffer) {
        vesa_draw_desktop_bg(desktop_bg_buffer);
    } else {
        vesa_clear(0x008080);
    }
    
    // 1.5 Draw Desktop Icons (Removed for Modern Dock)
    
    // 2. Draw Windows
    for (int i = 0; i < num_windows; i++) {
        window_t* w = &windows[i];
        if (!w->active) continue;
        
        // Window Border (1px flat)
        vesa_draw_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 22, current_theme.window_border);
        // Window Title bar (20px high)
        vesa_draw_rect(w->x, w->y, w->w, 20, (w == focused_window) ? current_theme.title_bg : current_theme.title_inactive_bg);
        wm_draw_string(w->x + 5, w->y + 6, w->title, current_theme.title_fg);
        
        // Close Button (Modern Flat)
        vesa_draw_rect(w->x + w->w - 18, w->y + 2, 16, 16, 0xC00000);
        wm_draw_string(w->x + w->w - 14, w->y + 6, "x", 0xFFFFFF);
        
        // Draw the inner buffer with Alpha Transparency for Terminal
        if (strncmp(w->title, "Terminal", 8) == 0) {
            for (uint32_t yy = 0; yy < w->h; yy++) {
                for (uint32_t xx = 0; xx < w->w; xx++) {
                    uint32_t px = w->buffer[yy * w->w + xx];
                    if (px == w->bg_color && desktop_bg_buffer) {
                        uint32_t bg = desktop_bg_buffer[(w->y + 20 + yy) * vesa_width + (w->x + xx)];
                        uint32_t r = (((bg >> 16) & 0xFF) + ((px >> 16) & 0xFF)) >> 1;
                        uint32_t g = (((bg >> 8) & 0xFF) + ((px >> 8) & 0xFF)) >> 1;
                        uint32_t b = (((bg >> 0) & 0xFF) + ((px >> 0) & 0xFF)) >> 1;
                        vesa_putpixel(w->x + xx, w->y + 20 + yy, (r << 16) | (g << 8) | b);
                    } else {
                        vesa_putpixel(w->x + xx, w->y + 20 + yy, px);
                    }
                }
            }
        } else {
            for (uint32_t yy = 0; yy < w->h; yy++) {
                for (uint32_t xx = 0; xx < w->w; xx++) {
                    vesa_putpixel(w->x + xx, w->y + 20 + yy, w->buffer[yy * w->w + xx]);
                }
            }
        }
        
        // Blinking Cursor
        if (w == focused_window && (strncmp(w->title, "Notepad", 7) == 0 || strncmp(w->title, "Terminal", 8) == 0 || strncmp(w->title, "Calculator", 10) == 0)) {
            uint32_t ticks = pit_get_ticks();
            if ((ticks / 50) % 2 == 0) {
                vesa_draw_rect(w->x + w->cursor_x, w->y + 20 + w->cursor_y, 8, 8, w->fg_color);
            }
        }
    }
    
    // 3. Draw Top Bar (Modern Linux Style)
    vesa_draw_rect(0, 0, vesa_width, 24, current_theme.taskbar_bg);
    
    // Activities Button
    if (start_btn_pressed) {
        vesa_draw_rect(0, 0, 80, 24, 0x000000); // Pressed shadow
    }
    wm_draw_string(8, 8, "Activities", current_theme.title_fg);
    
    // Centered Clock
    wm_draw_string(vesa_width / 2 - (strlen(clock_str) * 4), 8, clock_str, current_theme.title_fg);
    
    // Bottom Dock (Modern floating launcher)
    int dock_w = 560; // Expanded for Paint
    int dock_h = 50;
    int dock_x = (vesa_width - dock_w) / 2;
    int dock_y = vesa_height - dock_h - 10;
    
    // Draw rounded dock base
    vesa_draw_rect(dock_x, dock_y, dock_w, dock_h, current_theme.taskbar_bg);
    
    // Dock Icons
    // Terminal
    vesa_draw_rect(dock_x + 10, dock_y + 5, 40, 40, 0x111111);
    wm_draw_string(dock_x + 20, dock_y + 20, ">_", 0x00FF00);
    // Files
    vesa_draw_rect(dock_x + 70, dock_y + 5, 40, 40, 0x0000AA);
    wm_draw_string(dock_x + 75, dock_y + 20, "Dir", 0xFFFFFF);
    // Snake
    vesa_draw_rect(dock_x + 130, dock_y + 5, 40, 40, 0x00AA00);
    wm_draw_string(dock_x + 135, dock_y + 20, "Snk", 0xFFFFFF);
    // Reverser
    vesa_draw_rect(dock_x + 190, dock_y + 5, 40, 40, 0xAA5500);
    wm_draw_string(dock_x + 195, dock_y + 20, "Rev", 0xFFFFFF);
    // Theme
    vesa_draw_rect(dock_x + 250, dock_y + 5, 40, 40, 0xAA00AA);
    wm_draw_string(dock_x + 255, dock_y + 20, "Thm", 0xFFFFFF);
    // Calculator
    vesa_draw_rect(dock_x + 310, dock_y + 5, 40, 40, 0x008080);
    wm_draw_string(dock_x + 315, dock_y + 20, "Calc", 0xFFFFFF);
    // Clock
    vesa_draw_rect(dock_x + 370, dock_y + 5, 40, 40, 0x000000);
    wm_draw_string(dock_x + 375, dock_y + 20, "Time", 0xFFFFFF);
    // Wallpaper
    vesa_draw_rect(dock_x + 430, dock_y + 5, 40, 40, 0x808000);
    wm_draw_string(dock_x + 435, dock_y + 20, "Wall", 0xFFFFFF);
    // Paint
    vesa_draw_rect(dock_x + 490, dock_y + 5, 40, 40, 0xFF00FF);
    wm_draw_string(dock_x + 495, dock_y + 20, "Paint", 0xFFFFFF);
    
    // 4. Draw Start Menu
    if (start_menu_open) {
        uint32_t m_w = 150;
        uint32_t m_h = 240;
        uint32_t m_x = 0;
        uint32_t m_y = 24; // Dropdown from top bar
        
        // Menu Background
        vesa_draw_rect(m_x, m_y, m_w, m_h, current_theme.window_bg);
        // 3D Borders
        vesa_draw_rect(m_x, m_y, m_w, 2, 0xFFFFFF); // top
        vesa_draw_rect(m_x, m_y, 2, m_h, 0xFFFFFF); // left
        vesa_draw_rect(m_x + m_w - 2, m_y, 2, m_h, 0x808080); // right
        vesa_draw_rect(m_x, m_y + m_h - 2, m_w, 2, 0x808080); // bottom
        
        // Side banner
        vesa_draw_rect(m_x + 2, m_y + 2, 25, m_h - 4, current_theme.title_bg);
        wm_draw_string(m_x + 6, m_y + m_h - 50, "my", current_theme.title_fg);
        wm_draw_string(m_x + 6, m_y + m_h - 40, "OS", current_theme.title_fg);
        
        // Menu Items
        wm_draw_string(m_x + 35, m_y + 20, "New Terminal", current_theme.menu_fg);
        wm_draw_string(m_x + 35, m_y + 45, "New Window", current_theme.menu_fg);
        wm_draw_string(m_x + 35, m_y + 70, "Image Viewer", current_theme.menu_fg);
        wm_draw_string(m_x + 35, m_y + 95, "File Explorer", current_theme.menu_fg);
        wm_draw_string(m_x + 35, m_y + 120, "Play Snake", current_theme.menu_fg);
        wm_draw_string(m_x + 35, m_y + 145, "Text Reverser", current_theme.menu_fg);
        wm_draw_string(m_x + 35, m_y + 170, "Switch Theme", current_theme.menu_fg);
        
        vesa_draw_rect(m_x + 35, m_y + 195, m_w - 45, 1, 0x808080); // Separator
        
        wm_draw_string(m_x + 35, m_y + 210, "Reboot", current_theme.menu_fg);
    }
    
    // 5. Draw Mouse
    int mx = mouse_get_x();
    int my = mouse_get_y();
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 10; x++) {
            if (cursor_bitmap[y][x] == 1) vesa_putpixel(mx + x, my + y, 0x000000);
            else if (cursor_bitmap[y][x] == 2) vesa_putpixel(mx + x, my + y, 0xFFFFFF);
        }
    }
    
    // 6. Swap!
    vesa_swap_buffers();
}

void wm_process_events(void) {
    extern uint32_t vesa_height;
    
    // 0. Update Clock
    uint32_t current_ticks = pit_get_ticks();
    if (current_ticks - last_clock_ticks >= 100 || clock_str[0] == '\0') {
        rtc_datetime_str(clock_str);
        last_clock_ticks = current_ticks;
        redraw_needed = 1;
        
        // Update Analog Clocks
        for (int i = 0; i < num_windows; i++) {
            if (windows[i].active && strcmp(windows[i].title, "Analog Clock") == 0) {
                clock_update(&windows[i]);
            }
        }
    }

    int mx = mouse_get_x();
    int my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();
    
    static uint8_t last_btns = 0;
    int left_click_just_pressed = (btns & 1) && !(last_btns & 1);
    int left_click_just_released = !(btns & 1) && (last_btns & 1);
    int left_click_held = (btns & 1);
    
    if (left_click_just_pressed) {
        int clicked_on_something = 0;
        
        // Check Top Bar "Activities" click
        if (mx >= 0 && mx <= 80 && my >= 0 && my <= 24) {
            start_btn_pressed = 1;
            clicked_on_something = 1;
            redraw_needed = 1;
        } 
        // Check Activities Menu items click
        else if (start_menu_open && mx >= 0 && mx <= 150 && my >= 24 && my <= 24 + 240) {
            clicked_on_something = 1;
            uint32_t m_y = 24;
            
            if (my >= (int)(m_y + 15) && my <= (int)(m_y + 35)) {
                // New Terminal
                extern window_t* shell_window;
                shell_window = wm_create_window(50, 50, 600, 400, "Terminal");
                start_menu_open = 0;
                redraw_needed = 1;
            } else if (my >= (int)(m_y + 40) && my <= (int)(m_y + 60)) {
                // New Window
                wm_create_window(200, 200, 300, 200, "Window");
                start_menu_open = 0;
                redraw_needed = 1;
            } else if (my >= (int)(m_y + 65) && my <= (int)(m_y + 85)) {
                // Image Viewer
                window_t* img_win = wm_create_window(250, 150, 500, 400, "Image Viewer - logo.bmp");
                extern void bmp_load_to_window(const char* filename, window_t* win);
                bmp_load_to_window("logo.bmp", img_win);
                start_menu_open = 0;
                redraw_needed = 1;
            } else if (my >= (int)(m_y + 205) && my <= (int)(m_y + 225)) {
                // Reboot
                outb(0x64, 0xFE);
                for (;;) __asm__ volatile("hlt");
            }
        }
        
        // Check Dock Clicks
        if (!clicked_on_something && !start_menu_open) {
            int dock_w = 560;
            int dock_h = 50;
            int dock_x = (vesa_width - dock_w) / 2;
            int dock_y = vesa_height - dock_h - 10;
            
            if (my >= dock_y && my <= dock_y + dock_h && mx >= dock_x && mx <= dock_x + dock_w) {
                clicked_on_something = 1;
                redraw_needed = 1;
                
                if (mx >= dock_x + 10 && mx <= dock_x + 50) {
                    // Terminal
                    extern window_t* shell_window;
                    shell_window = wm_create_window(50, 50, 600, 400, "Terminal");
                } else if (mx >= dock_x + 70 && mx <= dock_x + 110) {
                    // File Explorer
                    window_t* fe_win = wm_create_window(100, 100, 400, 300, "File Explorer");
                    char name[100];
                    for (int i = 0; tar_get_file_at_index(i, name); i++) {
                        for(int j = 0; name[j]; j++) wm_putchar(fe_win, name[j]);
                        wm_putchar(fe_win, '\n');
                    }
                } else if (mx >= dock_x + 130 && mx <= dock_x + 170) {
                    // Snake
                    window_t* snake_win = wm_create_window(200, 150, 400, 400, "Snake");
                    snake_init(snake_win);
                } else if (mx >= dock_x + 190 && mx <= dock_x + 230) {
                    // Reverser
                    window_t* txt_win = wm_create_window(150, 150, 400, 300, "Text Reverser");
                    size_t file_size;
                    char* data = (char*)tar_get_file("readme.txt", &file_size);
                    if (data) {
                        for(int j = (int)file_size - 1; j >= 0; j--) {
                            wm_putchar(txt_win, data[j]);
                        }
                    }
                } else if (mx >= dock_x + 250 && mx <= dock_x + 290) {
                    // Switch Theme
                    static int theme_idx = 0;
                    theme_idx = !theme_idx;
                    extern theme_t theme_win95, theme_ubuntu;
                    current_theme = theme_idx ? theme_ubuntu : theme_win95;
                } else if (mx >= dock_x + 310 && mx <= dock_x + 350) {
                    // Calculator
                    window_t* calc_win = wm_create_window(200, 200, 300, 250, "Calculator");
                    calc_init(calc_win);
                } else if (mx >= dock_x + 370 && mx <= dock_x + 410) {
                    // Clock
                    window_t* clk_win = wm_create_window(300, 100, 250, 250, "Analog Clock");
                    clock_init(clk_win);
                } else if (mx >= dock_x + 430 && mx <= dock_x + 470) {
                    // Wallpaper
                    window_t* wall_win = wm_create_window(100, 100, 220, 250, "Wallpaper Selector");
                    wallpaper_init(wall_win);
                } else if (mx >= dock_x + 490 && mx <= dock_x + 530) {
                    // Paint
                    window_t* paint_win = wm_create_window(150, 100, 400, 300, "Paint");
                    paint_init(paint_win);
                }
            }
        }
        
        // Check Windows (Iterate backwards / top-most first)
        if (!clicked_on_something && !start_menu_open) {
            for (int i = num_windows - 1; i >= 0; i--) {
                window_t* w = &windows[i];
                if (w->active) {
                    // Check if click is inside window bounds
                    if (mx >= (int)(w->x - 2) && mx <= (int)(w->x + w->w + 2) &&
                        my >= (int)(w->y - 2) && my <= (int)(w->y + w->h + 20)) {
                        
                        focused_window = w;
                        
                        // Bring window to top by shifting it to end of array
                        if (i != num_windows - 1) {
                            window_t temp = *w;
                            for (int j = i; j < num_windows - 1; j++) {
                                windows[j] = windows[j + 1];
                            }
                            windows[num_windows - 1] = temp;
                            w = &windows[num_windows - 1];
                            i = num_windows - 1; // update i so drag_win_idx gets right value
                        }
                        
                        // Check if click is inside the Close Button (X)
                        if (mx >= (int)(w->x + w->w - 18) && mx <= (int)(w->x + w->w - 2) &&
                            my >= (int)(w->y + 2) && my <= (int)(w->y + 18)) {
                        w->active = 0; // Close the window
                        extern window_t* shell_window;
                        if (shell_window == w) {
                            shell_window = 0; // Safely disconnect shell output
                        }
                        clicked_on_something = 1;
                        redraw_needed = 1;
                        break;
                    }
                    
                    // Check if click is inside the title bar (for dragging)
                    if (mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my >= (int)w->y && my <= (int)(w->y + 20)) {
                        drag_win_idx = i;
                        drag_off_x = mx - w->x;
                        drag_off_y = my - w->y;
                        clicked_on_something = 1;
                        break;
                    }
                    
                    // Check if click is inside the File Explorer content
                    if (strcmp(w->title, "File Explorer") == 0 &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my > (int)(w->y + 20) && my <= (int)(w->y + w->h + 20)) {
                        
                        int clicked_row = (my - (w->y + 20)) / 8;
                        char name[100];
                        if (tar_get_file_at_index(clicked_row, name)) {
                            int len = strlen(name);
                            if (len >= 4 && strcmp(&name[len-4], ".bmp") == 0) {
                                char title[100];
                                strcpy(title, "Image Viewer - ");
                                strncat(title, name, 63);
                                window_t* img_win = wm_create_window(250, 150, 500, 400, title);
                                extern void bmp_load_to_window(const char*, window_t*);
                                bmp_load_to_window(name, img_win);
                            } else if (len >= 4 && strcmp(&name[len-4], ".txt") == 0) {
                                char title[100];
                                strcpy(title, "Notepad - ");
                                strncat(title, name, 63);
                                window_t* txt_win = wm_create_window(200, 200, 400, 300, title);
                                size_t file_size;
                                char* data = (char*)tar_get_file(name, &file_size);
                                if (data) {
                                    for(size_t j=0; j<file_size; j++) {
                                        wm_putchar(txt_win, data[j]);
                                    }
                                }
                            }
                        }
                        clicked_on_something = 1;
                        break;
                    }
                    
                    // Check if click is inside Wallpaper Selector
                    if (strcmp(w->title, "Wallpaper Selector") == 0 &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my > (int)(w->y + 20) && my <= (int)(w->y + w->h + 20)) {
                        wallpaper_handle_click(w, mx, my);
                        clicked_on_something = 1;
                        break;
                    }
                    
                    // Check if click is inside Paint
                    if (strcmp(w->title, "Paint") == 0 &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my > (int)(w->y + 20) && my <= (int)(w->y + w->h + 20)) {
                        paint_handle_click(w, mx, my);
                        clicked_on_something = 1;
                        break;
                    }
                    
                    // Clicked inside the window body (not title/close/explorer)
                    clicked_on_something = 1;
                    redraw_needed = 1;
                    break;
                    }
                }
            }
        }
        
        // Close menu if clicked outside
        if (!clicked_on_something && start_menu_open) {
            start_menu_open = 0;
            redraw_needed = 1;
        }
        
        // (Desktop icons logic removed for modern dock)
        
    } else if (left_click_just_released) {
        if (start_btn_pressed) {
            start_btn_pressed = 0;
            // Check if we released while still over the Activities button
            if (mx >= 0 && mx <= 80 && my >= 0 && my <= 24) {
                start_menu_open = !start_menu_open;
            }
            redraw_needed = 1;
        }
        drag_win_idx = -1;
    } else if (left_click_held && drag_win_idx >= 0) {
        windows[drag_win_idx].x = mx - drag_off_x;
        windows[drag_win_idx].y = my - drag_off_y;
        redraw_needed = 1;
    } else if (left_click_held && drag_win_idx == -1) {
        if (focused_window && strcmp(focused_window->title, "Paint") == 0) {
            paint_handle_click(focused_window, mx, my);
            redraw_needed = 1;
        }
    }
    
    last_btns = btns;

    // Force redraw for blinking cursor if focused window is text-based
    if (focused_window && (strncmp(focused_window->title, "Notepad", 7) == 0 || strncmp(focused_window->title, "Terminal", 8) == 0 || strncmp(focused_window->title, "Calculator", 10) == 0)) {
        if (pit_get_ticks() % 50 == 0) redraw_needed = 1;
    }

    if (redraw_needed) {
        wm_render();
        redraw_needed = 0;
    }
}

int wm_handle_keypress(char c) {
    if (focused_window && (strncmp(focused_window->title, "Notepad", 7) == 0 || strncmp(focused_window->title, "Terminal", 8) == 0)) {
        wm_putchar(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Calculator", 10) == 0) {
        calc_handle_input(focused_window, c);
        return 1;
    }
    return 0;
}

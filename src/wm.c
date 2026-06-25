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

static int drag_win_idx = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static char clock_str[20] = "";
static uint32_t last_clock_ticks = 0;

static int start_menu_open = 0;
static int start_btn_pressed = 0;

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
    
    // 1. Draw Desktop Background
    extern void vesa_draw_desktop_bg(uint32_t*);
    if (desktop_bg_buffer) {
        vesa_draw_desktop_bg(desktop_bg_buffer);
    } else {
        vesa_clear(0x008080);
    }
    
    // 1.5 Draw Desktop Icons
    for (int i = 0; i < num_icons; i++) {
        // Icon graphic (White square with inner shadow)
        vesa_draw_rect(icons[i].x, icons[i].y, icons[i].w, icons[i].h, 0xFFFFFF);
        vesa_draw_rect(icons[i].x, icons[i].y, icons[i].w, 2, 0xC0C0C0);
        vesa_draw_rect(icons[i].x, icons[i].y, 2, icons[i].h, 0xC0C0C0);
        
        // Text label with dark background
        int text_len = strlen(icons[i].name);
        int text_x = icons[i].x + (icons[i].w / 2) - ((text_len * 8) / 2);
        vesa_draw_rect(text_x - 2, icons[i].y + icons[i].h + 5, text_len * 8 + 4, 10, 0x000000);
        wm_draw_string(text_x, icons[i].y + icons[i].h + 6, icons[i].name, 0xFFFFFF);
    }
    
    // 2. Draw Windows
    for (int i = 0; i < num_windows; i++) {
        window_t* w = &windows[i];
        if (!w->active) continue;
        
        // Window Border (2px)
        vesa_draw_rect(w->x - 2, w->y - 2, w->w + 4, w->h + 24, 0xC0C0C0);
        // Window Title bar (20px high)
        vesa_draw_rect(w->x, w->y, w->w, 20, 0x0000A0); // Dark Blue
        wm_draw_string(w->x + 5, w->y + 6, w->title, 0xFFFFFF); // White Text
        
        // Close Button (Red 'X')
        vesa_draw_rect(w->x + w->w - 18, w->y + 2, 16, 16, 0xC00000);
        vesa_draw_rect(w->x + w->w - 18, w->y + 2, 16, 1, 0xFF8080); // top highlight
        vesa_draw_rect(w->x + w->w - 18, w->y + 2, 1, 16, 0xFF8080); // left highlight
        wm_draw_string(w->x + w->w - 14, w->y + 6, "x", 0xFFFFFF);
        
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
    if (start_btn_pressed) {
        vesa_draw_rect(5, vesa_height - 25, 60, 2, 0x004000); // Pressed shadow
        vesa_draw_rect(5, vesa_height - 25, 2, 20, 0x004000); 
    } else {
        vesa_draw_rect(5, vesa_height - 25, 60, 2, 0x00FF00); // Highlight
        vesa_draw_rect(5, vesa_height - 25, 2, 20, 0x00FF00); 
    }
    wm_draw_string(15, vesa_height - 19, "START", 0xFFFFFF);
    
    // Clock Box (Inset 3D)
    vesa_draw_rect(vesa_width - 160, vesa_height - 25, 150, 20, 0xA0A0A0);
    vesa_draw_rect(vesa_width - 160, vesa_height - 25, 150, 2, 0x808080); // inner shadow top
    vesa_draw_rect(vesa_width - 160, vesa_height - 25, 2, 20, 0x808080); // inner shadow left
    vesa_draw_rect(vesa_width - 160, vesa_height - 7, 150, 2, 0xFFFFFF); // inner highlight bottom
    vesa_draw_rect(vesa_width - 12, vesa_height - 25, 2, 20, 0xFFFFFF); // inner highlight right
    wm_draw_string(vesa_width - 150, vesa_height - 19, clock_str, 0x000000);
    
    // 4. Draw Start Menu
    if (start_menu_open) {
        uint32_t m_w = 150;
        uint32_t m_h = 200;
        uint32_t m_x = 0;
        uint32_t m_y = vesa_height - 30 - m_h;
        
        // Menu Background
        vesa_draw_rect(m_x, m_y, m_w, m_h, 0xC0C0C0);
        // 3D Borders
        vesa_draw_rect(m_x, m_y, m_w, 2, 0xFFFFFF); // top
        vesa_draw_rect(m_x, m_y, 2, m_h, 0xFFFFFF); // left
        vesa_draw_rect(m_x + m_w - 2, m_y, 2, m_h, 0x808080); // right
        vesa_draw_rect(m_x, m_y + m_h - 2, m_w, 2, 0x808080); // bottom
        
        // Side banner
        vesa_draw_rect(m_x + 2, m_y + 2, 25, m_h - 4, 0x0000A0);
        wm_draw_string(m_x + 6, m_y + m_h - 50, "my", 0xFFFFFF);
        wm_draw_string(m_x + 6, m_y + m_h - 40, "OS", 0xFFFFFF);
        
        // Menu Items
        wm_draw_string(m_x + 35, m_y + 20, "New Terminal", 0x000000);
        wm_draw_string(m_x + 35, m_y + 45, "New Window", 0x000000);
        wm_draw_string(m_x + 35, m_y + 70, "Image Viewer", 0x000000);
        wm_draw_string(m_x + 35, m_y + 95, "File Explorer", 0x000000);
        wm_draw_string(m_x + 35, m_y + 120, "Play Snake", 0x000000);
        
        vesa_draw_rect(m_x + 35, m_y + 160, m_w - 45, 1, 0x808080); // Separator
        
        wm_draw_string(m_x + 35, m_y + 175, "Reboot", 0x000000);
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
        
        // Check Start Button click
        if (mx >= 5 && mx <= 65 && my >= (int)(vesa_height - 25) && my <= (int)(vesa_height - 5)) {
            start_btn_pressed = 1;
            clicked_on_something = 1;
            redraw_needed = 1;
        } 
        // Check Start Menu items click
        else if (start_menu_open && mx >= 0 && mx <= 150 && my >= (int)(vesa_height - 30 - 200) && my <= (int)(vesa_height - 30)) {
            clicked_on_something = 1;
            uint32_t m_y = vesa_height - 30 - 200;
            
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
            } else if (my >= (int)(m_y + 90) && my <= (int)(m_y + 110)) {
                // File Explorer
                window_t* fe_win = wm_create_window(100, 100, 400, 300, "File Explorer");
                char name[100];
                for (int i = 0; tar_get_file_at_index(i, name); i++) {
                    for(int j = 0; name[j]; j++) wm_putchar(fe_win, name[j]);
                    wm_putchar(fe_win, '\n');
                }
                start_menu_open = 0;
                redraw_needed = 1;
            } else if (my >= (int)(m_y + 115) && my <= (int)(m_y + 135)) {
                // Snake
                window_t* snake_win = wm_create_window(200, 150, 400, 400, "Snake");
                snake_init(snake_win);
                start_menu_open = 0;
                redraw_needed = 1;
            } else if (my >= (int)(m_y + 170) && my <= (int)(m_y + 190)) {
                // Reboot
                outb(0x64, 0xFE);
                for (;;) __asm__ volatile("hlt");
            }
        }
        
        // Check Windows (Iterate backwards / top-most first)
        if (!clicked_on_something && !start_menu_open) {
            for (int i = num_windows - 1; i >= 0; i--) {
                window_t* w = &windows[i];
                if (w->active) {
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
                }
            }
        }
        
        // Close menu if clicked outside
        if (!clicked_on_something && start_menu_open) {
            start_menu_open = 0;
            redraw_needed = 1;
        }
        
        // Check Desktop Icons
        if (!clicked_on_something && !start_menu_open) {
            for (int i = 0; i < num_icons; i++) {
                int text_len = strlen(icons[i].name);
                int text_x = icons[i].x + (icons[i].w / 2) - ((text_len * 8) / 2);
                
                // Bounding box includes icon and text
                if (mx >= text_x - 5 && mx <= text_x + (text_len * 8) + 5 &&
                    my >= icons[i].y && my <= icons[i].y + icons[i].h + 20) {
                    
                    char* name = icons[i].name;
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
                    clicked_on_something = 1;
                    redraw_needed = 1;
                    break;
                }
            }
        }
        
    } else if (left_click_just_released) {
        if (start_btn_pressed) {
            start_btn_pressed = 0;
            // Check if we released while still over the button
            if (mx >= 5 && mx <= 65 && my >= (int)(vesa_height - 25) && my <= (int)(vesa_height - 5)) {
                start_menu_open = !start_menu_open;
            }
            redraw_needed = 1;
        }
        drag_win_idx = -1;
    } else if (left_click_held && drag_win_idx >= 0) {
        windows[drag_win_idx].x = mx - drag_off_x;
        windows[drag_win_idx].y = my - drag_off_y;
        redraw_needed = 1;
    }
    
    last_btns = btns;

    if (redraw_needed) {
        wm_render();
        redraw_needed = 0;
    }
}

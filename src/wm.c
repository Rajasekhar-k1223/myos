#include "wm.h"
#include "mouse.h"
#include "vesa.h"
#include "kheap.h"
#include "string.h"
#include "font16.h"
#include "fat16.h"
#include "rtc.h"
#include "pit.h"
#include "io.h"
#include "tar.h"
#include "snake.h"
#include "calc.h"
#include "clock.h"
#include "wallpaper.h"
#include "paint.h"
#include "explorer.h"
#include "speaker.h"
#include "kheap.h"
#include "minesweeper.h"
#include "settings.h"

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

// Window Dragging and Resizing State
static int drag_win_idx = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static window_t* resizing_window = 0;
static int resize_off_x = 0;
static int resize_off_y = 0;

static window_t* scrollbar_win    = NULL; /* window whose scrollbar is being dragged */
static int       scrollbar_track_y = 0;   /* screen-y of the scrollbar track top     */
static int       scrollbar_track_h = 0;   /* height of the track in pixels           */

#define SB_W 10  /* scrollbar width in pixels */

/* Window snap */
#define SNAP_ZONE 24          /* pixels from edge that triggers snap  */
static int snap_zone = 0;    /* 0=none 1=left 2=right 3=top(maximize) */

static char clock_str[20] = "";
static uint32_t last_clock_ticks = 0;

static int start_menu_open = 0;
static int start_btn_pressed = 0;

/* System clipboard */
static char     clipboard_buf[8192];
static uint32_t clipboard_len = 0;

/* Virtual desktops */
#define NUM_DESKTOPS 4
static int current_desktop = 0;

/* Right-click context menu */
static int  ctx_menu_open = 0;
static int  ctx_menu_x    = 0;
static int  ctx_menu_y    = 0;

#define CTX_ITEM_H  20
#define CTX_MENU_W  164

static const char* ctx_items[] = {
    "New Terminal",
    "New Notepad",
    "File Explorer",
    "Minesweeper",
    "Theme Settings",
    "-",
    "Refresh Desktop",
};
#define CTX_NUM_ITEMS 7

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

static uint32_t icon_term_buf[32 * 32];
static uint32_t icon_expl_buf[32 * 32];
static uint32_t icon_sett_buf[32 * 32];
static uint32_t icon_pnt_buf[32 * 32];
static uint32_t cursor_buf[16 * 16];

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
    
    memset(icon_term_buf, 0, sizeof(icon_term_buf));
    memset(icon_expl_buf, 0, sizeof(icon_expl_buf));
    memset(icon_sett_buf, 0, sizeof(icon_sett_buf));
    memset(icon_pnt_buf,  0, sizeof(icon_pnt_buf));
    memset(cursor_buf,    0, sizeof(cursor_buf));
    bmp_load_to_buffer("icon_term.bmp", icon_term_buf, 32, 32, 0, 0);
    bmp_load_to_buffer("icon_expl.bmp", icon_expl_buf, 32, 32, 0, 0);
    bmp_load_to_buffer("icon_sett.bmp", icon_sett_buf, 32, 32, 0, 0);
    bmp_load_to_buffer("icon_pnt.bmp",  icon_pnt_buf,  32, 32, 0, 0);
    bmp_load_to_buffer("cursor.bmp",    cursor_buf,    16, 16, 0, 0);
    
    // Create initial terminal
    extern window_t* shell_window;
    shell_window = wm_create_window(50, 50, 600, 400, "Terminal");
    
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
    win->fg_color = 0xAAAAAA;
    win->bg_color = 0x000000;
    win->alpha = (strncmp(title, "Terminal", 8) == 0) ? 210 : 255;
    win->text_buf   = NULL;
    win->text_len   = 0;
    win->minimized  = 0;
    win->maximized  = 0;
    win->orig_x = x; win->orig_y = y;
    win->orig_w = w; win->orig_h = h;
    win->desktop_id = current_desktop;
    
    if (strncmp(title, "Terminal", 8) == 0) {
        win->term_cols = w / 8;
        win->term_rows = 256;
        win->term_line = 0;
        win->term_scroll = 0;
        win->ansi_state = 0;
        win->ansi_param = 0;
        win->term_grid = (uint32_t*)kmalloc(win->term_rows * win->term_cols * 4);
        for (uint32_t i = 0; i < win->term_rows * win->term_cols; i++) {
            win->term_grid[i] = (' ') | (0xAAAAAA << 8);
        }
    } else {
        win->term_grid = NULL;
    }

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
        for (int row = 0; row < 16; row++) {
            uint8_t row_data = font8x16[c][row];
            for (int col = 0; col < 8; col++) {
                if (row_data & (1 << (7 - col))) {
                    vesa_putpixel(x + (i * 8) + col, y + row, fg);
                }
            }
        }
    }
}

void wm_draw_string_window(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t fg) {
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        for (int row = 0; row < 16; row++) {
            uint8_t row_data = font8x16[c][row];
            for (int col = 0; col < 8; col++) {
                if (row_data & (1 << (7 - col))) {
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

static window_t* wm_notepad_open(const char* title, uint32_t x, uint32_t y) {
    window_t* win = wm_create_window(x, y, 600, 400, title);
    if (!win) return NULL;
    win->text_buf = (char*)kmalloc(8192);
    win->text_len = 0;
    win->text_buf[0] = '\0';
    win->fg_color = 0xE0E0E0;
    win->bg_color = 0x1A1A2E;
    for (uint32_t i = 0; i < win->w * win->h; i++)
        win->buffer[i] = win->bg_color;
    return win;
}

static void wm_notepad_reload(window_t* win, const uint8_t* data, uint32_t len) {
    /* Clear pixel buffer */
    for (uint32_t i = 0; i < win->w * win->h; i++)
        win->buffer[i] = win->bg_color;
    win->cursor_x = 0;
    win->cursor_y = 0;
    /* Temporarily disable text_buf tracking so we don't double-record */
    char* saved_buf  = win->text_buf;
    uint32_t saved_len = win->text_len;
    win->text_buf = NULL;
    for (uint32_t i = 0; i < len; i++) wm_putchar(win, (char)data[i]);
    win->text_buf = saved_buf;
    win->text_len = saved_len;
}

static void wm_redraw_term(window_t* win);

int wm_handle_shortcut(char key) {
    extern window_t* shell_window;
    if (key == 't' || key == 'T') {
        shell_window = wm_create_window(100, 100, 500, 350, "Terminal");
        redraw_needed = 1;
        return 1;
    }
    if (key == 'e' || key == 'E') {
        window_t* exp_win = wm_create_window(100, 100, 400, 300, "File Explorer");
        explorer_init(exp_win);
        redraw_needed = 1;
        return 1;
    }
    if (key == 'n' || key == 'N') {
        wm_notepad_open("Notepad", 120, 80);
        redraw_needed = 1;
        return 1;
    }
    if (key == 's' || key == 'S') {
        /* Save focused text-buffered window to FAT16 */
        if (focused_window && focused_window->text_buf) {
            /* Extract filename from title "Notepad - foo.txt" or use "notes.txt" */
            const char* fname = "notes.txt";
            const char* dash = strstr(focused_window->title, " - ");
            if (dash) fname = dash + 3;
            fat16_write_file(fname,
                             (const uint8_t*)focused_window->text_buf,
                             focused_window->text_len);
            speaker_beep(880, 80);
            /* Show toast: "Saved: <fname>" */
            char tmsg[64];
            int ti = 0;
            const char* pfx = "Saved: ";
            while (*pfx) tmsg[ti++] = *pfx++;
            const char* fn = fname;
            while (*fn && ti < 62) tmsg[ti++] = *fn++;
            tmsg[ti] = '\0';
            wm_toast(tmsg, 200);
        }
        return 1;
    }
    if (key == 'o' || key == 'O') {
        /* Load "notes.txt" from FAT16 into a new Notepad */
        static uint8_t load_buf[8192];
        int r = fat16_read_file("notes.txt", load_buf, sizeof(load_buf));
        if (r > 0) {
            window_t* np = wm_notepad_open("Notepad - notes.txt", 130, 90);
            if (np) {
                np->text_len = (uint32_t)r;
                memcpy(np->text_buf, load_buf, (uint32_t)r + 1);
                wm_notepad_reload(np, load_buf, (uint32_t)r);
            }
            wm_toast("Loaded: notes.txt", 200);
        } else {
            wm_toast("File not found: notes.txt", 150);
        }
        redraw_needed = 1;
        return 1;
    }
    if (key == 'w' || key == 'W') {
        if (focused_window) {
            focused_window->active = 0;
            if (shell_window == focused_window) shell_window = 0;
            focused_window = 0;
            redraw_needed = 1;
            speaker_beep(2000, 10);
        }
        return 1;
    }
    if (key == 'm' || key == 'M') {
        window_t* ms_win = wm_create_window(150, 150, 220, 240, "Minesweeper");
        minesweeper_init(ms_win);
        redraw_needed = 1;
        return 1;
    }
    if (key == 'i' || key == 'I') {
        window_t* set_win = wm_create_window(200, 150, 300, 250, "Theme Settings");
        settings_init(set_win);
        redraw_needed = 1;
        return 1;
    }
    if (key == 17) { // Page Up
        if (focused_window && focused_window->term_grid) {
            focused_window->term_scroll += 5;
            wm_redraw_term(focused_window);
            return 1;
        }
    }
    if (key == 18) { // Page Down
        if (focused_window && focused_window->term_grid) {
            focused_window->term_scroll -= 5;
            if (focused_window->term_scroll < 0) focused_window->term_scroll = 0;
            wm_redraw_term(focused_window);
            return 1;
        }
    }
    if (key == 'c' || key == 'C') {
        /* Copy focused Notepad text_buf → clipboard */
        if (focused_window && focused_window->text_buf && focused_window->text_len > 0) {
            clipboard_len = focused_window->text_len;
            if (clipboard_len > 8191) clipboard_len = 8191;
            memcpy(clipboard_buf, focused_window->text_buf, clipboard_len);
            clipboard_buf[clipboard_len] = '\0';
            char tmsg[48];
            sprintf(tmsg, "Copied: %u chars", (unsigned)clipboard_len);
            wm_toast(tmsg, 150);
        }
        return 1;
    }
    if (key == 'v' || key == 'V') {
        /* Paste clipboard into focused window */
        if (clipboard_len > 0 && focused_window) {
            for (uint32_t i = 0; i < clipboard_len; i++)
                wm_putchar(focused_window, clipboard_buf[i]);
            wm_toast("Pasted", 100);
            redraw_needed = 1;
        }
        return 1;
    }
    if (key == 'a' || key == 'A') {
        /* Select-all: copy entire text_buf to clipboard (Notepad only) */
        if (focused_window && focused_window->text_buf && focused_window->text_len > 0) {
            clipboard_len = focused_window->text_len;
            if (clipboard_len > 8191) clipboard_len = 8191;
            memcpy(clipboard_buf, focused_window->text_buf, clipboard_len);
            clipboard_buf[clipboard_len] = '\0';
            char tmsg[48];
            sprintf(tmsg, "Selected: %u chars", (unsigned)clipboard_len);
            wm_toast(tmsg, 120);
        }
        return 1;
    }
    /* Ctrl+1..4 — switch virtual desktop */
    if (key >= '1' && key <= '0' + NUM_DESKTOPS) {
        int target = key - '1';
        if (target != current_desktop) {
            current_desktop = target;
            /* Deselect focused window if it lives on another desktop */
            if (focused_window && focused_window->desktop_id != current_desktop)
                focused_window = NULL;
            /* Re-focus top-most window on new desktop */
            if (!focused_window) {
                for (int i = num_windows - 1; i >= 0; i--) {
                    if (windows[i].active && !windows[i].minimized &&
                        windows[i].desktop_id == current_desktop) {
                        focused_window = &windows[i];
                        break;
                    }
                }
            }
            char tmsg[24];
            sprintf(tmsg, "Desktop %d", current_desktop + 1);
            wm_toast(tmsg, 100);
            redraw_needed = 1;
        }
        return 1;
    }
    /* Ctrl+Shift equivalent: move focused window to desktop (use F1-F4 via key codes) */
    return 0;
}

void wm_redraw_term(window_t* win) {
    if (!win->term_grid) return;
    // Clear buffer
    for (uint32_t i = 0; i < win->w * win->h; i++) win->buffer[i] = win->bg_color;
    
    int lines_visible = win->h / 16;
    int start_line = win->term_line - lines_visible + 1 - win->term_scroll;
    if (start_line < 0) start_line = 0;
    
    int draw_y = 0;
    for (int y = start_line; y <= (int)win->term_line && draw_y < lines_visible; y++) {
        for (uint32_t x = 0; x < win->term_cols; x++) {
            uint32_t cell = win->term_grid[y * win->term_cols + x];
            char c = (char)(cell & 0xFF);
            uint32_t cell_fg = cell >> 8;
            if (c >= ' ') {
                unsigned char uc = (unsigned char)c;
                for (int row = 0; row < 16; row++) {
                    uint8_t row_data = font8x16[uc][row];
                    for (int col = 0; col < 8; col++) {
                        if (row_data & (1 << (7 - col))) {
                            if ((uint32_t)(draw_y * 16 + row) < win->h && (uint32_t)(x * 8 + col) < win->w) {
                                win->buffer[(draw_y * 16 + row) * win->w + (x * 8 + col)] = cell_fg;
                            }
                        }
                    }
                }
            }
        }
        draw_y++;
    }
    redraw_needed = 1;
}

void wm_putchar(window_t* win, char c) {
    if (!win || !win->buffer) return;

    /* Maintain raw text buffer for Notepad save/load */
    if (win->text_buf) {
        if (c == '\b') {
            if (win->text_len > 0) win->text_buf[--win->text_len] = '\0';
        } else if (c != '\r' && win->text_len < 8190) {
            win->text_buf[win->text_len++] = c;
            win->text_buf[win->text_len]   = '\0';
        }
    }

    if (win->term_grid) {
        /* ANSI Escape Parsing */
        if (win->ansi_state == 1) {
            if (c == '[') win->ansi_state = 2;
            else win->ansi_state = 0;
            return;
        } else if (win->ansi_state == 2) {
            if (c >= '0' && c <= '9') {
                win->ansi_param = win->ansi_param * 10 + (c - '0');
                return;
            } else if (c == 'm') {
                uint32_t ansi_colors[8] = {0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA};
                if (win->ansi_param >= 30 && win->ansi_param <= 37) {
                    win->fg_color = ansi_colors[win->ansi_param - 30];
                } else if (win->ansi_param == 0) {
                    win->fg_color = 0xAAAAAA;
                }
                win->ansi_state = 0;
                win->ansi_param = 0;
                return;
            } else if (c == ';') {
                /* Ignore complex codes for now */
                win->ansi_state = 0;
                return;
            } else {
                win->ansi_state = 0;
                return;
            }
        }
        if (c == '\033') {
            win->ansi_state = 1;
            win->ansi_param = 0;
            return;
        }
        if (c == '\b') {
            if (win->cursor_x >= 8) {
                win->cursor_x -= 8;
                int term_x = win->cursor_x / 8;
                win->term_grid[win->term_line * win->term_cols + term_x] = (' ') | (win->fg_color << 8);
            }
        } else if (c == '\n') {
            win->cursor_x = 0;
            win->term_line++;
            if (win->term_line >= win->term_rows) {
                memmove(win->term_grid, win->term_grid + win->term_cols, (win->term_rows - 1) * win->term_cols * 4);
                for (uint32_t i = 0; i < win->term_cols; i++) {
                    win->term_grid[(win->term_rows - 1) * win->term_cols + i] = (' ') | (win->fg_color << 8);
                }
                win->term_line--;
            }
        } else if (c == '\r') {
            win->cursor_x = 0;
        } else if (c >= ' ') {
            int term_x = win->cursor_x / 8;
            if (term_x < (int)win->term_cols) {
                win->term_grid[win->term_line * win->term_cols + term_x] = ((uint32_t)c) | (win->fg_color << 8);
            }
            win->cursor_x += 8;
        }
        
        if ((uint32_t)win->cursor_x >= win->w) {
            win->cursor_x = 0;
            win->term_line++;
            if (win->term_line >= win->term_rows) {
                memmove(win->term_grid, win->term_grid + win->term_cols, (win->term_rows - 1) * win->term_cols * 4);
                for (uint32_t i = 0; i < win->term_cols; i++) {
                    win->term_grid[(win->term_rows - 1) * win->term_cols + i] = (' ') | (win->fg_color << 8);
                }
                win->term_line--;
            }
        }
        
        // If we are at the bottom, reset scroll_y so it tracks
        win->term_scroll = 0;
        wm_redraw_term(win);
        return;
    }

    if (c == '\n') {
        win->cursor_x = 0;
        win->cursor_y += 16;
    } else if (c == '\r') {
        win->cursor_x = 0;
    } else if (c == '\b') {
        if (win->cursor_x >= 8) {
            win->cursor_x -= 8;
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 8; x++) {
                    if ((uint32_t)(win->cursor_y + y) < win->h && (uint32_t)(win->cursor_x + x) < win->w) {
                        win->buffer[(win->cursor_y + y) * win->w + (win->cursor_x + x)] = win->bg_color;
                    }
                }
            }
        }
    } else if (c >= ' ') {
        unsigned char uc = (unsigned char)c;
        for (int row = 0; row < 16; row++) {
            uint8_t row_data = font8x16[uc][row];
            for (int col = 0; col < 8; col++) {
                if ((row_data & (1 << (7 - col))) && ((uint32_t)(win->cursor_y + row) < win->h) && ((uint32_t)(win->cursor_x + col) < win->w)) {
                    win->buffer[(win->cursor_y + row) * win->w + (win->cursor_x + col)] = win->fg_color;
                }
            }
        }
        win->cursor_x += 8;
    }

    // Wrap horizontally
    if ((uint32_t)win->cursor_x >= win->w) {
        win->cursor_x = 0;
        win->cursor_y += 16;
    }

    // Scroll vertically if needed
    if ((uint32_t)(win->cursor_y + 16) > win->h) {
        memcpy(win->buffer, win->buffer + (win->w * 16), (win->h - 16) * win->w * 4);
        for (uint32_t i = (win->h - 16) * win->w; i < win->h * win->w; i++) {
            win->buffer[i] = win->bg_color;
        }
        win->cursor_y -= 16;
    }
    redraw_needed = 1;
}

/* ─── Toast notification ─────────────────────────────────────────────── */
static char     toast_msg[80]  = "";
static uint32_t toast_until    = 0;

void wm_toast(const char* msg, uint32_t duration_ticks) {
    strncpy(toast_msg, msg, 79);
    toast_msg[79] = '\0';
    toast_until = pit_get_ticks() + duration_ticks;
    redraw_needed = 1;
}

/* ─── Resize / Maximize helpers ──────────────────────────────────────── */
static void wm_do_resize(window_t* w, uint32_t new_w, uint32_t new_h) {
    uint32_t* nb = (uint32_t*)kmalloc(new_w * new_h * 4);
    if (!nb) return;
    for (uint32_t yy = 0; yy < new_h; yy++)
        for (uint32_t xx = 0; xx < new_w; xx++)
            nb[yy * new_w + xx] = (xx < w->w && yy < w->h)
                                   ? w->buffer[yy * w->w + xx]
                                   : w->bg_color;
    kfree(w->buffer);
    w->buffer = nb;
    w->w = new_w;
    w->h = new_h;
}

static void wm_maximize_toggle(window_t* w) {
    extern uint32_t vesa_width, vesa_height;
    if (!w->maximized) {
        w->orig_x = w->x; w->orig_y = w->y;
        w->orig_w = w->w; w->orig_h = w->h;
        w->x = 0;
        w->y = 24;
        wm_do_resize(w, vesa_width, vesa_height - 84);
        w->maximized = 1;
    } else {
        w->x = w->orig_x;
        w->y = w->orig_y;
        wm_do_resize(w, w->orig_w, w->orig_h);
        w->maximized = 0;
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
    
    // 2. Draw Windows (current desktop only)
    for (int i = 0; i < num_windows; i++) {
        window_t* w = &windows[i];
        if (!w->active || w->minimized) continue;
        if (w->desktop_id != current_desktop) continue;

        // Drop Shadow (8px offset)
        if (w->alpha == 255) { // Only draw shadow for fully opaque windows (e.g. not Terminal)
            vesa_draw_rect_alpha(w->x + 6, w->y + 6, w->w + 2, w->h + 22, 0x000000, 80);
        }

        // Window Border (1px flat)
        vesa_draw_rect_alpha(w->x - 1, w->y - 1, w->w + 2, w->h + 22, current_theme.window_border, w->alpha);
        // Window Title bar (20px high)
        vesa_draw_rect_alpha(w->x, w->y, w->w, 20, (w == focused_window) ? current_theme.title_bg : current_theme.title_inactive_bg, w->alpha);
        wm_draw_string(w->x + 5, w->y + 2, w->title, current_theme.title_fg);

        // Maximize button (green □ — or ❐ when already maximized)
        vesa_draw_rect_alpha(w->x + w->w - 58, w->y + 2, 16, 16, 0x007700, w->alpha);
        wm_draw_string(w->x + w->w - 55, w->y + 6, w->maximized ? "v" : "^", 0xFFFFFF);

        // Minimize button (yellow _ )
        vesa_draw_rect_alpha(w->x + w->w - 38, w->y + 2, 16, 16, 0xC09000, w->alpha);
        wm_draw_string(w->x + w->w - 35, w->y + 6, "_", 0xFFFFFF);

        // Close button (red x)
        vesa_draw_rect_alpha(w->x + w->w - 18, w->y + 2, 16, 16, 0xC00000, w->alpha);
        wm_draw_string(w->x + w->w - 14, w->y + 6, "x", 0xFFFFFF);

        // Draw the inner buffer with Alpha Transparency
        for (uint32_t yy = 0; yy < w->h; yy++) {
            for (uint32_t xx = 0; xx < w->w; xx++) {
                vesa_putpixel_alpha(w->x + xx, w->y + 20 + yy, w->buffer[yy * w->w + xx], w->alpha);
            }
        }
        
        // Draw Scrollbar for Terminal windows with scrollable content
        if (w->term_grid) {
            int total_lines   = (int)w->term_line + 1;
            int visible_lines = (int)w->h / 16;
            if (total_lines > visible_lines) {
                int sb_x  = (int)(w->x + w->w) - SB_W;
                int sb_y  = (int)(w->y) + 20;
                int sb_h  = (int)w->h;
                /* Track */
                vesa_draw_rect((uint32_t)sb_x, (uint32_t)sb_y, SB_W, (uint32_t)sb_h, 0x222233);
                /* Thumb */
                int max_scroll = total_lines - visible_lines;
                int thumb_h = sb_h * visible_lines / total_lines;
                if (thumb_h < 12) thumb_h = 12;
                int scroll_clamped = w->term_scroll;
                if (scroll_clamped > max_scroll) scroll_clamped = max_scroll;
                /* scroll=0→ bottom, scroll=max→ top; invert for thumb_y */
                int thumb_y = sb_y;
                if (max_scroll > 0)
                    thumb_y = sb_y + (sb_h - thumb_h) * (max_scroll - scroll_clamped) / max_scroll;
                vesa_draw_rect((uint32_t)(sb_x + 1), (uint32_t)(thumb_y + 1),
                               SB_W - 2, (uint32_t)(thumb_h - 2), 0x7799BB);
            }
        }

        // Draw Resize Handle
        vesa_draw_rect(w->x + w->w - 12, w->y + 20 + w->h - 12, 12, 12, current_theme.title_bg);
        
        // Blinking Cursor
        if (w == focused_window && (strncmp(w->title, "Notepad", 7) == 0 || strncmp(w->title, "Terminal", 8) == 0 || strncmp(w->title, "Calculator", 10) == 0)) {
            uint32_t ticks = pit_get_ticks();
            if ((ticks / 50) % 2 == 0) {
                vesa_draw_rect(w->x + w->cursor_x, w->y + 20 + w->cursor_y, 8, 16, w->fg_color);
            }
        }
    }

    // 2.5 Window Taskbar (all open windows, above dock)
    {
        int strip_h = 26;
        int strip_y = (int)vesa_height - 50 - 10 - strip_h - 2;
        int btn_w   = 140;
        int max_btns = ((int)vesa_width - 20) / (btn_w + 4);
        int count = 0;
        /* Count active windows on this desktop */
        for (int i = 0; i < num_windows; i++)
            if (windows[i].active && windows[i].desktop_id == current_desktop) count++;
        if (count > 0) {
            /* Dark background bar */
            vesa_draw_rect(0, (uint32_t)strip_y, vesa_width, (uint32_t)strip_h,
                           0x111122);
            int strip_x = 10;
            for (int i = 0; i < num_windows && i < max_btns; i++) {
                window_t* w = &windows[i];
                if (!w->active || w->desktop_id != current_desktop) continue;
                uint32_t bg;
                if (w == focused_window && !w->minimized)
                    bg = current_theme.title_bg;          /* focused */
                else if (w->minimized)
                    bg = 0x334455;                        /* minimized */
                else
                    bg = current_theme.title_inactive_bg; /* open, not focused */
                vesa_draw_rect((uint32_t)strip_x, (uint32_t)(strip_y + 2),
                               (uint32_t)btn_w, (uint32_t)(strip_h - 4), bg);
                /* Truncate title to 16 chars */
                char short_title[17];
                strncpy(short_title, w->title, 16);
                short_title[16] = '\0';
                /* Prefix minimised windows with "_" */
                if (w->minimized) {
                    char tmp[18];
                    tmp[0] = '_'; tmp[1] = ' ';
                    strncpy(tmp + 2, short_title, 15);
                    tmp[17] = '\0';
                    wm_draw_string((uint32_t)(strip_x + 4),
                                   (uint32_t)(strip_y + 5), tmp, 0xAAAACC);
                } else {
                    wm_draw_string((uint32_t)(strip_x + 4),
                                   (uint32_t)(strip_y + 5), short_title,
                                   current_theme.title_fg);
                }
                strip_x += btn_w + 4;
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

    // Desktop indicator dots (top-right)
    {
        int dot_size = 12;
        int dot_gap  = 4;
        int total_w  = NUM_DESKTOPS * (dot_size + dot_gap) - dot_gap;
        int dx = (int)vesa_width - total_w - 8;
        int dy = 6;
        for (int d = 0; d < NUM_DESKTOPS; d++) {
            uint32_t col = (d == current_desktop) ? 0xFFFFFF : 0x556677;
            /* Count windows on this desktop */
            int has_wins = 0;
            for (int wi = 0; wi < num_windows; wi++)
                if (windows[wi].active && windows[wi].desktop_id == d) { has_wins = 1; break; }
            if (has_wins && d != current_desktop) col = 0x88AACC;
            vesa_draw_rect((uint32_t)(dx + d * (dot_size + dot_gap)),
                           (uint32_t)dy, (uint32_t)dot_size, (uint32_t)dot_size, col);
        }
    }
    
    // Bottom Dock (Modern floating launcher)
    int dock_w = 800;
    int dock_h = 50;
    int dock_x = (vesa_width - dock_w) / 2;
    int dock_y = vesa_height - dock_h - 10;
    
    // Draw rounded dock base
    vesa_draw_rect(dock_x, dock_y, dock_w, dock_h, current_theme.taskbar_bg);
    
    // Helper to draw icon (with transparency treated as black=0)
    void draw_dock_icon(int bx, int by, uint32_t* icon_buf) {
        if (!icon_buf) return;
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                uint32_t col = icon_buf[y * 32 + x];
                if (col != 0) {
                    vesa_putpixel_alpha(bx + x, by + y, col, 255);
                }
            }
        }
    }
    
    // Dock Icons
    // Terminal (using new 32x32 BMP icon)
    draw_dock_icon(dock_x + 14, dock_y + 9, icon_term_buf);
    
    // Files (Explorer) (using new 32x32 BMP icon)
    draw_dock_icon(dock_x + 74, dock_y + 9, icon_expl_buf);
    
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
    // Paint (using new 32x32 BMP icon)
    draw_dock_icon(dock_x + 494, dock_y + 9, icon_pnt_buf);
    
    // Explorer text button placeholder
    vesa_draw_rect(dock_x + 550, dock_y + 5, 40, 40, 0xDAA520);
    wm_draw_string(dock_x + 555, dock_y + 20, "Files", 0xFFFFFF);
    // Notepad
    vesa_draw_rect(dock_x + 610, dock_y + 5, 40, 40, 0x1A1A2E);
    wm_draw_string(dock_x + 615, dock_y + 20, "Note", 0xE0E0E0);
    // Minesweeper
    vesa_draw_rect(dock_x + 670, dock_y + 5, 40, 40, 0x2D5A27);
    wm_draw_string(dock_x + 675, dock_y + 20, "Mine", 0xFFFFFF);
    // Theme Settings (using new 32x32 BMP icon)
    draw_dock_icon(dock_x + 734, dock_y + 9, icon_sett_buf);
    
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
    
    // 5. Draw Toast Notification
    if (toast_msg[0] && pit_get_ticks() < toast_until) {
        int tw = (int)strlen(toast_msg) * 8 + 20;
        int tx = ((int)vesa_width  - tw) / 2;
        int ty = (int)vesa_height - 50 - 10 - 50;   /* above dock */
        vesa_draw_rect((uint32_t)tx - 2, (uint32_t)ty - 2,
                       (uint32_t)tw + 4, 24, 0x111111);
        vesa_draw_rect((uint32_t)tx, (uint32_t)ty,
                       (uint32_t)tw, 20, 0x1E3A5F);
        wm_draw_string((uint32_t)(tx + 10), (uint32_t)(ty + 2),
                       toast_msg, 0xFFFFFF);
        redraw_needed = 1; /* keep redrawing until expired */
    } else if (toast_msg[0] && pit_get_ticks() >= toast_until) {
        toast_msg[0] = '\0';
        redraw_needed = 1;
    }

    // 5.5 Draw Snap Preview
    if (drag_win_idx >= 0 && snap_zone != 0) {
        uint32_t snap_y = 24;
        uint32_t snap_h = (vesa_height > 120) ? vesa_height - 24 - 88 : vesa_height / 2;
        uint32_t half_w = vesa_width / 2;
        uint32_t px = 0, py = snap_y, pw = 0, ph = snap_h;
        if (snap_zone == 1)      { px = 0;      pw = half_w; }
        else if (snap_zone == 2) { px = half_w; pw = half_w; }
        else if (snap_zone == 3) { px = 0; py = snap_y; pw = vesa_width; ph = vesa_height - snap_y - 88; }
        /* Draw semi-transparent blue preview rectangle */
        for (uint32_t yy = py; yy < py + ph && yy < vesa_height; yy++)
            for (uint32_t xx = px; xx < px + pw && xx < vesa_width; xx++)
                vesa_putpixel_alpha(xx, yy, 0x4488FF, 100);
        /* Border */
        vesa_draw_rect(px, py, pw, 2, 0x88AAFF);
        vesa_draw_rect(px, py + ph - 2, pw, 2, 0x88AAFF);
        vesa_draw_rect(px, py, 2, ph, 0x88AAFF);
        vesa_draw_rect(px + pw - 2, py, 2, ph, 0x88AAFF);
    }

    // 6. Draw Context Menu
    if (ctx_menu_open) {
        int cw = CTX_MENU_W;
        int ch = CTX_NUM_ITEMS * CTX_ITEM_H + 4;
        int cx = ctx_menu_x;
        int cy = ctx_menu_y;
        /* Clamp to screen */
        if (cx + cw > (int)vesa_width)  cx = (int)vesa_width  - cw - 2;
        if (cy + ch > (int)vesa_height) cy = (int)vesa_height - ch - 2;
        /* Background */
        for (int yy = 0; yy < ch; yy++)
            for (int xx = 0; xx < cw; xx++)
                vesa_putpixel(cx + xx, cy + yy, 0x1E1E2E);
        /* Border */
        for (int xx = 0; xx < cw; xx++) {
            vesa_putpixel(cx + xx, cy,        0x5555AA);
            vesa_putpixel(cx + xx, cy + ch-1, 0x5555AA);
        }
        for (int yy = 0; yy < ch; yy++) {
            vesa_putpixel(cx,      cy + yy, 0x5555AA);
            vesa_putpixel(cx+cw-1, cy + yy, 0x5555AA);
        }
        /* Items */
        for (int i = 0; i < CTX_NUM_ITEMS; i++) {
            int iy = cy + 2 + i * CTX_ITEM_H;
            if (ctx_items[i][0] == '-') {
                for (int xx = 4; xx < cw - 4; xx++)
                    vesa_putpixel(cx + xx, iy + CTX_ITEM_H/2, 0x5555AA);
            } else {
                wm_draw_string(cx + 8, iy + 2, ctx_items[i], 0xE0E0E0);
            }
        }
    }

    // 7. Draw Mouse
    int mx = mouse_get_x();
    int my = mouse_get_y();
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint32_t col = cursor_buf[y * 16 + x];
            if (col != 0) { // Black is treated as transparent in our simple implementation
                vesa_putpixel_alpha(mx + x, my + y, col, 255);
            }
        }
    }

    // 7. Swap!
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

        /* Context menu click */
        if (ctx_menu_open) {
            ctx_menu_open = 0;
            redraw_needed = 1;
            int cw = CTX_MENU_W;
            int ch = CTX_NUM_ITEMS * CTX_ITEM_H + 4;
            int cx = ctx_menu_x;
            int cy = ctx_menu_y;
            if (cx + cw > (int)vesa_width)  cx = (int)vesa_width  - cw - 2;
            if (cy + ch > (int)vesa_height) cy = (int)vesa_height - ch - 2;
            if (mx >= cx && mx <= cx + cw && my >= cy && my <= cy + ch) {
                int item = (my - cy - 2) / CTX_ITEM_H;
                if (item >= 0 && item < CTX_NUM_ITEMS && ctx_items[item][0] != '-') {
                    extern window_t* shell_window;
                    if (item == 0) { /* New Terminal */
                        shell_window = wm_create_window(100, 100, 500, 350, "Terminal");
                    } else if (item == 1) { /* New Notepad */
                        wm_notepad_open("Notepad", 120, 80);
                    } else if (item == 2) { /* File Explorer */
                        window_t* exp_win = wm_create_window(100, 100, 400, 300, "File Explorer");
                        explorer_init(exp_win);
                    } else if (item == 3) { /* Minesweeper */
                        window_t* ms_win = wm_create_window(150, 150, 220, 240, "Minesweeper");
                        minesweeper_init(ms_win);
                    } else if (item == 4) { /* Theme Settings */
                        window_t* set_win = wm_create_window(200, 150, 300, 250, "Theme Settings");
                        settings_init(set_win);
                    } else if (item == 6) { /* Refresh Desktop */
                        /* nothing extra needed — redraw already set */
                    }
                    redraw_needed = 1;
                }
                clicked_on_something = 1;
            }
            /* clicking outside the menu just closes it */
            clicked_on_something = 1;
        }

        // Check desktop indicator dot clicks (top-right of top bar)
        if (!clicked_on_something && my >= 0 && my <= 24) {
            int dot_size = 12;
            int dot_gap  = 4;
            int total_w  = NUM_DESKTOPS * (dot_size + dot_gap) - dot_gap;
            int dot_base_x = (int)vesa_width - total_w - 8;
            for (int d = 0; d < NUM_DESKTOPS; d++) {
                int dx = dot_base_x + d * (dot_size + dot_gap);
                if (mx >= dx && mx <= dx + dot_size) {
                    if (d != current_desktop) {
                        current_desktop = d;
                        if (focused_window && focused_window->desktop_id != current_desktop)
                            focused_window = NULL;
                        /* Re-focus top window on new desktop */
                        if (!focused_window) {
                            for (int i = num_windows - 1; i >= 0; i--) {
                                if (windows[i].active && !windows[i].minimized &&
                                    windows[i].desktop_id == current_desktop) {
                                    focused_window = &windows[i];
                                    break;
                                }
                            }
                        }
                        char tmsg[24];
                        sprintf(tmsg, "Desktop %d", current_desktop + 1);
                        wm_toast(tmsg, 100);
                        redraw_needed = 1;
                    }
                    clicked_on_something = 1;
                    break;
                }
            }
        }

        // Check Top Bar "Activities" click
        if (!clicked_on_something && mx >= 0 && mx <= 80 && my >= 0 && my <= 24) {
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
            int dock_w = 800;
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
                } else if (mx >= dock_x + 550 && mx <= dock_x + 590) {
                    // Explorer
                    window_t* exp_win = wm_create_window(100, 100, 400, 300, "File Explorer");
                    explorer_init(exp_win);
                } else if (mx >= dock_x + 610 && mx <= dock_x + 650) {
                    // Notepad
                    wm_notepad_open("Notepad", 120, 80);
                } else if (mx >= dock_x + 670 && mx <= dock_x + 710) {
                    // Minesweeper
                    window_t* ms_win = wm_create_window(150, 150, 220, 240, "Minesweeper");
                    minesweeper_init(ms_win);
                } else if (mx >= dock_x + 730 && mx <= dock_x + 770) {
                    // Theme Settings
                    window_t* set_win = wm_create_window(200, 150, 300, 250, "Theme Settings");
                    settings_init(set_win);
                }
            }
        }
        
        // Check Window Taskbar buttons (all open windows)
        if (!clicked_on_something) {
            int strip_h = 26;
            int strip_y = (int)vesa_height - 50 - 10 - strip_h - 2;
            int btn_w   = 140;
            int strip_x = 10;
            for (int i = 0; i < num_windows; i++) {
                window_t* w = &windows[i];
                if (!w->active) continue;
                if (mx >= strip_x && mx <= strip_x + btn_w &&
                    my >= strip_y && my <= strip_y + strip_h) {
                    if (w->minimized) {
                        /* Restore minimized window */
                        w->minimized = 0;
                        focused_window = w;
                        speaker_beep(600, 60);
                    } else if (w == focused_window) {
                        /* Click focused window → minimize it */
                        w->minimized = 1;
                        focused_window = (num_windows > 1)
                                         ? &windows[num_windows - 2] : 0;
                        speaker_beep(400, 60);
                    } else {
                        /* Bring non-focused window to front */
                        focused_window = w;
                        /* Shift window to top of z-order */
                        window_t tmp = *w;
                        for (int j = i; j < num_windows - 1; j++)
                            windows[j] = windows[j + 1];
                        windows[num_windows - 1] = tmp;
                        focused_window = &windows[num_windows - 1];
                        speaker_beep(550, 40);
                    }
                    clicked_on_something = 1;
                    redraw_needed = 1;
                    break;
                }
                strip_x += btn_w + 4;
            }
        }

        // Check Windows (Iterate backwards / top-most first)
        if (!clicked_on_something && !start_menu_open) {
            for (int i = num_windows - 1; i >= 0; i--) {
                window_t* w = &windows[i];
                if (w->active && !w->minimized && w->desktop_id == current_desktop) {
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
                        
                        // Check if click is on Terminal scrollbar track
                        if (w->term_grid) {
                            int total_l   = (int)w->term_line + 1;
                            int visible_l = (int)w->h / 16;
                            if (total_l > visible_l) {
                                int sb_x = (int)(w->x + w->w) - SB_W;
                                int sb_y = (int)(w->y) + 20;
                                int sb_h = (int)w->h;
                                if (mx >= sb_x && mx <= sb_x + SB_W &&
                                    my >= sb_y && my <= sb_y + sb_h) {
                                    int max_s = total_l - visible_l;
                                    int rel   = my - sb_y;
                                    int ns    = max_s - rel * max_s / sb_h;
                                    if (ns < 0) ns = 0;
                                    if (ns > max_s) ns = max_s;
                                    w->term_scroll    = ns;
                                    scrollbar_win     = w;
                                    scrollbar_track_y = sb_y;
                                    scrollbar_track_h = sb_h;
                                    wm_redraw_term(w);
                                    clicked_on_something = 1;
                                    redraw_needed = 1;
                                    break;
                                }
                            }
                        }

                        // Check if click is inside the Close Button (X)
                        if (mx >= (int)(w->x + w->w - 18) && mx <= (int)(w->x + w->w - 2) &&
                            my >= (int)(w->y + 2) && my <= (int)(w->y + 18)) {
                            speaker_beep(2000, 10);
                            w->active = 0;
                            extern window_t* shell_window;
                            if (shell_window == w) shell_window = 0;
                            clicked_on_something = 1;
                            redraw_needed = 1;
                            break;
                        }

                        // Check if click is inside the Maximize Button (^/v)
                        if (mx >= (int)(w->x + w->w - 58) && mx <= (int)(w->x + w->w - 42) &&
                            my >= (int)(w->y + 2) && my <= (int)(w->y + 18)) {
                            wm_maximize_toggle(w);
                            speaker_beep(700, 60);
                            clicked_on_something = 1;
                            break;
                        }

                        // Check if click is inside the Minimize Button (_)
                        if (mx >= (int)(w->x + w->w - 38) && mx <= (int)(w->x + w->w - 22) &&
                            my >= (int)(w->y + 2) && my <= (int)(w->y + 18)) {
                            speaker_beep(600, 60);
                            w->minimized = 1;
                            if (focused_window == w) focused_window = NULL;
                            clicked_on_something = 1;
                            redraw_needed = 1;
                            break;
                        }

                        // Check if click is inside the Resize Handle
                        if (mx >= (int)(w->x + w->w - 12) && mx <= (int)(w->x + w->w) &&
                            my >= (int)(w->y + 20 + w->h - 12) && my <= (int)(w->y + 20 + w->h)) {
                            resizing_window = w;
                            resize_off_x = mx - w->w;
                            resize_off_y = my - w->h;
                            clicked_on_something = 1;
                            break;
                        }
                    
                    // Check if click is inside the title bar (for dragging, skip when maximized)
                    if (!w->maximized &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
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
                        
                        int clicked_row = (my - (w->y + 20)) / 16;
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
                    
                    // Check if click is inside Minesweeper
                    if (strcmp(w->title, "Minesweeper") == 0 &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my > (int)(w->y + 20) && my <= (int)(w->y + w->h + 20)) {
                        minesweeper_handle_click(w, mx, my, 0);
                        clicked_on_something = 1;
                        break;
                    }
                    
                    // Check if click is inside Theme Settings
                    if (strcmp(w->title, "Theme Settings") == 0 &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my > (int)(w->y + 20) && my <= (int)(w->y + w->h + 20)) {
                        settings_handle_click(w, mx, my);
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
                    
                    // Check if click is inside File Explorer
                    if (strcmp(w->title, "File Explorer") == 0 &&
                        mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my > (int)(w->y + 20) && my <= (int)(w->y + w->h + 20)) {
                        explorer_handle_click(w, mx, my);
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
        /* Apply window snap on drag release */
        if (drag_win_idx >= 0 && snap_zone != 0) {
            window_t* sw = &windows[drag_win_idx];
            /* Usable content area: below top bar, above taskbar strip+dock */
            uint32_t snap_y = 24;
            uint32_t snap_h = (vesa_height > 120) ? vesa_height - 24 - 88 : vesa_height / 2;
            uint32_t half_w = vesa_width / 2;
            if (snap_zone == 1) {          /* left half */
                sw->x = 0; sw->y = snap_y;
                wm_do_resize(sw, half_w, snap_h);
                wm_toast("Snapped: left", 80);
            } else if (snap_zone == 2) {   /* right half */
                sw->x = half_w; sw->y = snap_y;
                wm_do_resize(sw, half_w, snap_h);
                wm_toast("Snapped: right", 80);
            } else if (snap_zone == 3) {   /* maximize via top edge */
                if (!sw->maximized) wm_maximize_toggle(sw);
                wm_toast("Maximized", 80);
            }
            snap_zone = 0;
            redraw_needed = 1;
        } else {
            snap_zone = 0;
        }
        drag_win_idx = -1;
    } else if (left_click_held && drag_win_idx >= 0) {
        windows[drag_win_idx].x = mx - drag_off_x;
        windows[drag_win_idx].y = my - drag_off_y;
        /* Detect snap zone from mouse position */
        int new_snap = 0;
        if (mx < SNAP_ZONE)                        new_snap = 1; /* left  */
        else if (mx > (int)vesa_width - SNAP_ZONE) new_snap = 2; /* right */
        else if (my < SNAP_ZONE + 4)               new_snap = 3; /* top   */
        if (new_snap != snap_zone) { snap_zone = new_snap; redraw_needed = 1; }
        else redraw_needed = 1;
    } else if (left_click_held && drag_win_idx == -1) {
        if (focused_window && strcmp(focused_window->title, "Paint") == 0) {
            paint_handle_click(focused_window, mx, my);
            redraw_needed = 1;
        }
    }
    
    int right_click_just_pressed = (btns & 2) && !(last_btns & 2);
    if (right_click_just_pressed) {
        /* Close context menu if already open */
        if (ctx_menu_open) {
            ctx_menu_open = 0;
            redraw_needed = 1;
        }
        /* Check if right-click hit a window */
        int hit_window = 0;
        if (!start_menu_open) {
            for (int i = num_windows - 1; i >= 0; i--) {
                window_t* w = &windows[i];
                if (w->active && !w->minimized && w->desktop_id == current_desktop) {
                    if (mx >= (int)w->x && mx <= (int)(w->x + w->w) &&
                        my >= (int)(w->y) && my <= (int)(w->y + w->h + 20)) {
                        focused_window = w;
                        if (strcmp(w->title, "Minesweeper") == 0 &&
                            my > (int)(w->y + 20)) {
                            minesweeper_handle_click(w, mx, my, 1);
                        }
                        hit_window = 1;
                        break;
                    }
                }
            }
        }
        /* Right-click on desktop → open context menu */
        if (!hit_window && !start_menu_open) {
            ctx_menu_x = mx;
            ctx_menu_y = my;
            ctx_menu_open = 1;
            redraw_needed = 1;
        }
    }

    // Process Window Dragging
    if (left_click_held && drag_win_idx != -1 && resizing_window == 0) {
        windows[drag_win_idx].x = mx - drag_off_x;
        windows[drag_win_idx].y = my - drag_off_y;
        redraw_needed = 1;
    }
    
    // Process Window Resizing
    if (left_click_held && resizing_window != 0) {
        int new_w = mx - resize_off_x;
        int new_h = my - resize_off_y;
        
        if (new_w < 150) new_w = 150;
        if (new_h < 100) new_h = 100;
        if (new_w > (int)vesa_width) new_w = vesa_width;
        if (new_h > (int)vesa_height - 50) new_h = vesa_height - 50;
        
        if ((uint32_t)new_w != resizing_window->w || (uint32_t)new_h != resizing_window->h) {
            uint32_t* new_buf = (uint32_t*)kmalloc(new_w * new_h * 4);
            if (new_buf) {
                for (int yy = 0; yy < new_h; yy++) {
                    for (int xx = 0; xx < new_w; xx++) {
                        if (xx < (int)resizing_window->w && yy < (int)resizing_window->h) {
                            new_buf[yy * new_w + xx] = resizing_window->buffer[yy * resizing_window->w + xx];
                        } else {
                            new_buf[yy * new_w + xx] = resizing_window->bg_color;
                        }
                    }
                }
                kfree(resizing_window->buffer);
                resizing_window->buffer = new_buf;
                resizing_window->w = new_w;
                resizing_window->h = new_h;
                redraw_needed = 1;
            }
        }
    }

    /* Scrollbar drag: update while held, clear on release */
    if (left_click_held && scrollbar_win && scrollbar_track_h > 0) {
        int total_l   = (int)scrollbar_win->term_line + 1;
        int visible_l = (int)scrollbar_win->h / 16;
        int max_s     = total_l - visible_l;
        if (max_s > 0) {
            int rel = my - scrollbar_track_y;
            int ns  = max_s - rel * max_s / scrollbar_track_h;
            if (ns < 0) ns = 0;
            if (ns > max_s) ns = max_s;
            if (ns != scrollbar_win->term_scroll) {
                scrollbar_win->term_scroll = ns;
                wm_redraw_term(scrollbar_win);
                redraw_needed = 1;
            }
        }
    }

    if (!left_click_held) {
        drag_win_idx = -1;
        resizing_window = 0;
        scrollbar_win = NULL;
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
    extern window_t* shell_window;
    if (focused_window && strncmp(focused_window->title, "Terminal", 8) == 0) {
        if (focused_window == shell_window) {
            /* Route to shell — it echoes via terminal_putchar → wm_putchar */
            extern void shell_handle_keypress(char c);
            shell_handle_keypress(c);
        } else {
            wm_putchar(focused_window, c);
        }
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Notepad", 7) == 0) {
        wm_putchar(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Calculator", 10) == 0) {
        calc_handle_input(focused_window, c);
        return 1;
    }
    return 0;
}

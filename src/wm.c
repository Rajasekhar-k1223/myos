#include "wm.h"
#include "sdl_shim.h"
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
#include "ttf.h"
#include "widget.h"

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

void wm_draw_desktop_text(const char* str, float scale, int start_x, int start_y, uint32_t color) {
    extern uint32_t vesa_width, vesa_height;
    if (!desktop_bg_buffer) return;
    float cur_x = start_x;
    for (int i = 0; str[i] != '\0'; i++) {
        uint16_t gid = ttf_get_glyph_index(str[i]);
        if (gid) {
            ttf_render_glyph(gid, scale, cur_x, start_y, desktop_bg_buffer, vesa_width, vesa_height, color);
        }
        cur_x += 1000.0f * scale; // Hacky fixed advance based on scale
    }
    wm_request_redraw();
}

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

/* Notepad Find bar */
static int  find_active = 0;
static char find_buf[64] = "";
static int  find_len     = 0;

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

    // ── Gradient Desktop Background (deep space / aurora) ────────────────────
    desktop_bg_buffer = (uint32_t*)kmalloc(vesa_width * vesa_height * 4);
    for (uint32_t row = 0; row < vesa_height; row++) {
        // Vertical gradient: deep navy → teal-purple
        uint8_t t = (uint8_t)((row * 255) / (vesa_height > 1 ? vesa_height - 1 : 1));
        uint32_t top_c = 0x0A0A1E; // deep navy
        uint32_t bot_c = 0x1A0A2E; // deep violet
        uint32_t base  = widget_blend_color(top_c, bot_c, t);

        for (uint32_t col = 0; col < vesa_width; col++) {
            // Subtle horizontal shimmer
            uint8_t h = (uint8_t)((col * 40) / (vesa_width > 1 ? vesa_width - 1 : 1));
            uint32_t shimmer = widget_blend_color(base, 0x112244, h);
            desktop_bg_buffer[row * vesa_width + col] = shimmer;
        }
    }

    // Overlay logo tiles with low opacity blend
    extern void bmp_load_to_buffer(const char*, uint32_t*, int, int, int, int);
    // Load logo to a temp buffer and alpha-blend onto gradient
    uint32_t* logo_tmp = (uint32_t*)kmalloc(250 * 150 * 4);
    if (logo_tmp) {
        memset(logo_tmp, 0, 250 * 150 * 4);
        bmp_load_to_buffer("logo.bmp", logo_tmp, 250, 150, 0, 0);
        for (int tile_y = 0; tile_y < (int)vesa_height; tile_y += 150) {
            for (int tile_x = 0; tile_x < (int)vesa_width; tile_x += 250) {
                for (int ly = 0; ly < 150; ly++) {
                    for (int lx = 0; lx < 250; lx++) {
                        int px = tile_x + lx, py = tile_y + ly;
                        if ((uint32_t)px >= vesa_width || (uint32_t)py >= vesa_height) continue;
                        uint32_t logo_px = logo_tmp[ly * 250 + lx];
                        if (logo_px == 0) continue;
                        uint32_t bg_px = desktop_bg_buffer[py * vesa_width + px];
                        // Blend logo at 15% opacity
                        desktop_bg_buffer[py * vesa_width + px] =
                            widget_blend_color(bg_px, logo_px, 38);
                    }
                }
            }
        }
        kfree(logo_tmp);
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
    if (key == 'u' || key == 'U') {
        window_t* m_win = wm_create_window(200, 200, 300, 150, "Music Player");
        extern void music_init(window_t*);
        music_init(m_win);
        redraw_needed = 1;
        return 1;
    }
    if (key == 'w' || key == 'W') {
        window_t* b_win = wm_create_window(100, 100, 600, 400, "Netscape Elsea");
        extern void browser_init(window_t*);
        browser_init(b_win);
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
    /* Ctrl+[ / Ctrl+] — window opacity */
    if (key == '[') {
        if (focused_window) {
            focused_window->alpha = (focused_window->alpha > 30) ? focused_window->alpha - 25 : 30;
            char _omsg[32]; sprintf(_omsg, "Opacity: %d%%", (focused_window->alpha * 100) / 255);
            wm_toast(_omsg, 80);
            redraw_needed = 1;
        }
        return 1;
    }
    if (key == ']') {
        if (focused_window) {
            int _na = (int)focused_window->alpha + 25;
            focused_window->alpha = (uint8_t)(_na > 255 ? 255 : _na);
            char _omsg[32]; sprintf(_omsg, "Opacity: %d%%", (focused_window->alpha * 100) / 255);
            wm_toast(_omsg, 80);
            redraw_needed = 1;
        }
        return 1;
    }
    /* Ctrl+F — Notepad Find */
    if (key == 'f' || key == 'F') {
        if (focused_window && focused_window->text_buf) {
            find_active = 1;
            find_len    = 0;
            find_buf[0] = '\0';
            wm_toast("Find: type search term, Enter to search", 250);
            redraw_needed = 1;
        }
        return 1;
    }
    /* Ctrl+Shift+1..4 — move focused window to another desktop */
    if (key == '!' || key == '@' || key == '#' || key == '$') {
        int _tgt = (key == '!') ? 0 : (key == '@') ? 1 : (key == '#') ? 2 : 3;
        if (focused_window) {
            focused_window->desktop_id = _tgt;
            char _tmsg[48];
            sprintf(_tmsg, "Window moved to Desktop %d", _tgt + 1);
            wm_toast(_tmsg, 180);
            redraw_needed = 1;
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
        /* ── Full ANSI / VT100 escape sequence parser ──────────────────── */
        static const uint32_t ansi_pal[8] = {
            0x000000, 0xCC0000, 0x00CC00, 0xCC8800,
            0x0000CC, 0xCC00CC, 0x00CCCC, 0xCCCCCC
        };
        static const uint32_t ansi_bright[8] = {
            0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
            0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
        };

        if (win->ansi_state == 1) {          /* saw ESC */
            if (c == '[') { win->ansi_state = 2; return; }
            if (c == 'c') { /* RIS — reset terminal */
                win->fg_color = 0xAAAAAA; win->bg_color = 0x000000;
                win->ansi_bold = 0;
                win->cursor_x = 0; win->term_line = 0;
            }
            win->ansi_state = 0; return;
        }
        if (win->ansi_state == 2) {          /* inside ESC[ */
            if (c >= '0' && c <= '9') {
                if (win->ansi_param_idx < 8)
                    win->ansi_params[win->ansi_param_idx] =
                        win->ansi_params[win->ansi_param_idx] * 10 + (c - '0');
                return;
            }
            if (c == ';') {
                win->ansi_param_idx++;
                if (win->ansi_param_idx >= 8) win->ansi_param_idx = 7;
                return;
            }
            /* Finalise params */
            int np = win->ansi_param_idx + 1;
            int* p = win->ansi_params;
            win->ansi_param = p[0]; /* legacy */

            if (c == 'm') {
                for (int pi = 0; pi < np; pi++) {
                    int v = p[pi];
                    if (v == 0)  { win->fg_color = 0xAAAAAA; win->bg_color = 0x000000; win->ansi_bold = 0; }
                    else if (v == 1)  win->ansi_bold = 1;
                    else if (v == 2 || v == 22) win->ansi_bold = 0;
                    else if (v >= 30 && v <= 37) win->fg_color = win->ansi_bold ? ansi_bright[v-30] : ansi_pal[v-30];
                    else if (v >= 90 && v <= 97) win->fg_color = ansi_bright[v-90];
                    else if (v == 39) win->fg_color = 0xAAAAAA;
                    else if (v >= 40 && v <= 47) win->bg_color = ansi_pal[v-40];
                    else if (v >= 100&&v<=107)   win->bg_color = ansi_bright[v-100];
                    else if (v == 49) win->bg_color = 0x000000;
                }
            } else if (c == 'H' || c == 'f') { /* CUP — cursor position */
                int row = (np >= 1 && p[0] > 0) ? p[0]-1 : 0;
                int col = (np >= 2 && p[1] > 0) ? p[1]-1 : 0;
                win->term_line = (uint32_t)(row < (int)win->term_rows ? row : (int)win->term_rows-1);
                win->cursor_x  = (uint32_t)(col < (int)win->term_cols ? col : (int)win->term_cols-1) * 8;
            } else if (c == 'A') { /* CUU — cursor up */
                int n = p[0] ? p[0] : 1;
                win->term_line = (win->term_line >= (uint32_t)n) ? win->term_line - n : 0;
            } else if (c == 'B') { /* CUD — cursor down */
                int n = p[0] ? p[0] : 1;
                win->term_line = ((int)win->term_line+n < (int)win->term_rows) ? win->term_line+n : win->term_rows-1;
            } else if (c == 'C') { /* CUF — cursor forward */
                int n = (p[0] ? p[0] : 1) * 8;
                win->cursor_x = ((int)win->cursor_x+n < (int)win->term_cols*8) ? win->cursor_x+n : (win->term_cols-1)*8;
            } else if (c == 'D') { /* CUB — cursor back */
                int n = (p[0] ? p[0] : 1) * 8;
                win->cursor_x = ((int)win->cursor_x >= n) ? win->cursor_x-n : 0;
            } else if (c == 'J') { /* ED — erase display */
                if (p[0] == 2 || p[0] == 3) { /* clear whole screen */
                    for (uint32_t i = 0; i < win->term_rows * win->term_cols; i++)
                        win->term_grid[i] = (' ') | (win->fg_color << 8);
                    win->term_line = 0; win->cursor_x = 0;
                } else if (p[0] == 0) { /* clear to end */
                    uint32_t pos = win->term_line * win->term_cols + win->cursor_x/8;
                    for (uint32_t i = pos; i < win->term_rows * win->term_cols; i++)
                        win->term_grid[i] = (' ') | (win->fg_color << 8);
                }
            } else if (c == 'K') { /* EL — erase line */
                uint32_t col = win->cursor_x / 8;
                if (p[0] == 0) { /* to end of line */
                    for (uint32_t x = col; x < win->term_cols; x++)
                        win->term_grid[win->term_line * win->term_cols + x] = (' ') | (win->fg_color << 8);
                } else if (p[0] == 1) { /* to start */
                    for (uint32_t x = 0; x <= col; x++)
                        win->term_grid[win->term_line * win->term_cols + x] = (' ') | (win->fg_color << 8);
                } else if (p[0] == 2) { /* whole line */
                    for (uint32_t x = 0; x < win->term_cols; x++)
                        win->term_grid[win->term_line * win->term_cols + x] = (' ') | (win->fg_color << 8);
                }
            } else if (c == 's') { /* save cursor */ /* no-op for now */ }
              else if (c == 'u') { /* restore cursor */ /* no-op */ }
            /* Reset parser */
            win->ansi_state = 0; win->ansi_param_idx = 0;
            for (int i = 0; i < 8; i++) win->ansi_params[i] = 0;
            return;
        }
        if (c == '\033') {
            win->ansi_state = 1;
            win->ansi_param_idx = 0;
            for (int i = 0; i < 8; i++) win->ansi_params[i] = 0;
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

/* ─── Toast notification + history ──────────────────────────────────── */
static char     toast_msg[80]  = "";
static uint32_t toast_until    = 0;

#define NOTIF_MAX 8
static char notif_history[NOTIF_MAX][80];
static int  notif_count = 0;
static int  notif_panel_open = 0;

void wm_toast(const char* msg, uint32_t duration_ticks) {
    strncpy(toast_msg, msg, 79);
    toast_msg[79] = '\0';
    toast_until = pit_get_ticks() + duration_ticks;
    /* push to notification history (newest at top) */
    if (notif_count < NOTIF_MAX) {
        strncpy(notif_history[notif_count], msg, 79);
        notif_history[notif_count][79] = '\0';
        notif_count++;
    } else {
        for (int _ni = 0; _ni < NOTIF_MAX - 1; _ni++)
            strcpy(notif_history[_ni], notif_history[_ni + 1]);
        strncpy(notif_history[NOTIF_MAX - 1], msg, 79);
        notif_history[NOTIF_MAX - 1][79] = '\0';
    }
    redraw_needed = 1;
}

/* find_active/buf/len declared at file top */

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

static void wm_update_system_monitor(window_t* w) {
    if (!w || !w->buffer) return;
    for (uint32_t _i = 0; _i < w->w * w->h; _i++) w->buffer[_i] = 0x0D1117;

    extern uint32_t pmm_get_max_frames(void);
    extern uint32_t pmm_get_used_frames(void);
    extern uint32_t pit_get_seconds(void);
    extern uint32_t task_count(void);

    uint32_t max_f  = pmm_get_max_frames();
    uint32_t used_f = pmm_get_used_frames();
    uint32_t free_f = max_f > used_f ? max_f - used_f : 0;
    uint32_t secs   = pit_get_seconds();
    uint32_t ntasks = task_count();

    char _line[80];
    wm_draw_string_window(w, 10, 8,  "System Monitor", 0x58A6FF);

    sprintf(_line, "Uptime:   %02u:%02u:%02u",
            secs / 3600, (secs % 3600) / 60, secs % 60);
    wm_draw_string_window(w, 10, 32, _line, 0xFFFFFF);

    sprintf(_line, "RAM Used: %u MB / %u MB  (%u free)",
            (used_f * 4) / 1024, (max_f * 4) / 1024, (free_f * 4) / 1024);
    wm_draw_string_window(w, 10, 50, _line, 0xFFFFFF);

    /* memory usage bar */
    int _bw  = (int)w->w - 20;
    int _pct = max_f > 0 ? (int)(used_f * 100 / max_f) : 0;
    int _fill = _bw * _pct / 100;
    for (int _y = 68; _y < 80; _y++)
        for (int _x = 10; _x < 10 + _bw && _x < (int)w->w; _x++)
            w->buffer[_y * w->w + _x] = _x < 10 + _fill ? 0x238636 : 0x21262D;
    sprintf(_line, "%d%%", _pct);
    wm_draw_string_window(w, 10 + _fill - 16 > 10 ? 10 + _fill - 16 : 10, 82, _line, 0x55FF88);

    sprintf(_line, "Tasks:    %u running", ntasks);
    wm_draw_string_window(w, 10, 100, _line, 0xFFFFFF);

    sprintf(_line, "PIT:      100 Hz  (ticks: %u)", pit_get_ticks());
    wm_draw_string_window(w, 10, 118, _line, 0xC9D1D9);

    sprintf(_line, "Heap:     %u KB used", (used_f * 4));
    wm_draw_string_window(w, 10, 136, _line, 0xC9D1D9);

    wm_draw_string_window(w, 10, 160, "Network: eth0  10.0.2.15  RTL8139", 0x79C0FF);
    wm_draw_string_window(w, 10, 178, "Audio:   SB16  8-bit PCM DMA", 0x79C0FF);
    wm_draw_string_window(w, 10, 196, "Storage: ATA(FAT16) | AHCI(EXT2) | NVMe", 0x79C0FF);

    wm_draw_string_window(w, 10, 220, "Press [x] to close", 0x484F58);
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
    
    // 1.8 Update live System Monitor windows + Video Player tick
    for (int _si = 0; _si < num_windows; _si++) {
        window_t* _sw = &windows[_si];
        if (!_sw->active || _sw->minimized || _sw->desktop_id != current_desktop) continue;
        if (strncmp(_sw->title, "System Monitor", 14) == 0)
            wm_update_system_monitor(_sw);
        if (strncmp(_sw->title, "Video Player", 12) == 0) {
            extern int video_player_tick(window_t*);
            if (video_player_tick(_sw)) redraw_needed = 1;
        }
    }

    // 2. Draw Windows (current desktop only)
    for (int i = 0; i < num_windows; i++) {
        window_t* w = &windows[i];
        if (!w->active || w->minimized) continue;
        if (w->desktop_id != current_desktop) continue;

        // Drop Shadow (10px offset, softer)
        vesa_draw_rect_alpha(w->x + 8, w->y + 8, w->w + 2, w->h + 22, 0x000000, 55);
        vesa_draw_rect_alpha(w->x + 5, w->y + 5, w->w + 2, w->h + 22, 0x000000, 35);

        // ── Window frame / border (1px, subtle glow for focused) ───────────────
        uint32_t border_col = (w == focused_window) ? 0x4466AA : 0x222233;
        uint8_t  border_a   = (w == focused_window) ? 180 : 110;
        vesa_draw_rect_alpha(w->x - 1, w->y - 1, w->w + 2, w->h + 22,
                             border_col, border_a);

        // ── Title bar: gradient glass effect ───────────────────────────────────
        uint32_t title_top, title_bot;
        if (w == focused_window) {
            title_top = widget_blend_color(current_theme.title_bg, 0xFFFFFF, 45);
            title_bot = widget_blend_color(current_theme.title_bg, 0x000000, 50);
        } else {
            title_top = widget_blend_color(current_theme.title_inactive_bg, 0xFFFFFF, 20);
            title_bot = current_theme.title_inactive_bg;
        }
        widget_draw_gradient_rect(w->x, w->y, w->w, 20, title_top, title_bot, w->alpha);

        /* Title — TTF if loaded, bitmap fallback */
        {
            extern uint32_t vesa_get_fb_addr(void);
            extern uint32_t vesa_width, vesa_height;
            extern int ttf_is_loaded(void);
            if (ttf_is_loaded()) {
                uint32_t* _fb = (uint32_t*)vesa_get_fb_addr();
                ttf_draw_string(_fb, (int)vesa_width, (int)vesa_height,
                                (int)(w->x + 5), (int)(w->y + 2),
                                w->title, 13, current_theme.title_fg);
            } else {
                wm_draw_string(w->x + 6, w->y + 3, w->title, 0x000000);
                wm_draw_string(w->x + 5, w->y + 2, w->title, current_theme.title_fg);
            }
        }
        /* Opacity indicator — small α% in title bar right area */
        if (w == focused_window && w->alpha < 250) {
            char _atxt[8]; sprintf(_atxt, "%d%%", (w->alpha * 100) / 255);
            wm_draw_string(w->x + w->w - 75, w->y + 5, _atxt, 0xAABBCC);
        }

        // ── Window control buttons (macOS-style circles) ────────────────────────
        // Close — red
        widget_draw_rounded_rect(w->x + w->w - 19, w->y + 3, 14, 14, 7,
                                 0xCC3333, w->alpha);
        wm_draw_string(w->x + w->w - 16, w->y + 5, "x", 0x800000);

        // Minimize — yellow
        widget_draw_rounded_rect(w->x + w->w - 37, w->y + 3, 14, 14, 7,
                                 0xCCAA00, w->alpha);
        wm_draw_string(w->x + w->w - 34, w->y + 5, "_", 0x664400);

        // Maximize — green
        widget_draw_rounded_rect(w->x + w->w - 55, w->y + 3, 14, 14, 7,
                                 0x33AA33, w->alpha);
        wm_draw_string(w->x + w->w - 52, w->y + 5,
                       w->maximized ? "v" : "^", 0x004400);

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

    // ── Top Bar (glass style) ─────────────────────────────────────────────────
    widget_draw_gradient_rect(0, 0, (int)vesa_width, 24,
                              0x1A1A2E, 0x0A0A18, 230);
    // Top bar highlight
    for (uint32_t col = 0; col < vesa_width; col++)
        vesa_putpixel_alpha(col, 0, 0xFFFFFF, 30);
    for (uint32_t col = 0; col < vesa_width; col++)
        vesa_putpixel_alpha(col, 23, 0x000000, 80);

    // Activities Button (pill style)
    widget_draw_rounded_rect(4, 3, 82, 18, 5,
                             start_btn_pressed ? 0x223366 : 0x334477, 220);
    wm_draw_string(8, 7, "Activities", current_theme.title_fg);

    // Centered Clock (badge style)
    {
        int cw = (int)strlen(clock_str) * 8 + 14;
        int cx = ((int)vesa_width - cw) / 2;
        widget_draw_rounded_rect(cx, 3, cw, 18, 5, 0x111133, 180);
        wm_draw_string((uint32_t)(cx + 7), 7, clock_str, current_theme.title_fg);
    }

    // Bell (notification) icon — left of desktop dots
    {
        int bell_x = (int)vesa_width - NUM_DESKTOPS * 16 - 36;
        uint32_t bell_col = notif_count > 0 ? 0xFFCC44 : 0x445566;
        widget_draw_rounded_rect(bell_x, 3, 18, 18, 4, bell_col, 200);
        wm_draw_string((uint32_t)(bell_x + 5), 7, notif_count > 0 ? "!" : "o", 0x000000);
        /* Notification history panel */
        if (notif_panel_open && notif_count > 0) {
            int pw = 280, ph = notif_count * 20 + 10;
            int px = (int)vesa_width - pw - 4;
            int py = 26;
            widget_draw_glass(px, py, pw, ph, 0x0D1117, 240, 1);
            wm_draw_string((uint32_t)(px + 6), (uint32_t)(py + 4), "Notifications", 0x58A6FF);
            for (int _nb = 0; _nb < notif_count; _nb++) {
                int _ny = py + 6 + (_nb + 1) * 18;
                char _ntxt[82]; _ntxt[0] = '\xAE'; _ntxt[1] = ' ';
                strncpy(_ntxt + 2, notif_history[notif_count - 1 - _nb], 76);
                _ntxt[78] = '\0';
                wm_draw_string((uint32_t)(px + 6), (uint32_t)_ny, _ntxt, 0xC9D1D9);
            }
        }
    }

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
    
    // ── Bottom Dock (glass floating launcher) ─────────────────────────────────
    int dock_w = 1000;
    int dock_h = 50;
    int dock_x = ((int)vesa_width - dock_w) / 2;
    int dock_y = (int)vesa_height - dock_h - 10;

    // Glass dock base
    widget_draw_glass(dock_x, dock_y, dock_w, dock_h,
                      0x0A0A1E, 170, 2);  // deep navy tint, 2 blur passes

    // Dock separator line at top
    for (int col = dock_x + 8; col < dock_x + dock_w - 8; col++)
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)dock_y, 0x6688BB, 120);
    
    // Dock icon helper (draws the BMP with a hover-like highlight area)
    void draw_dock_icon(int bx, int by, uint32_t* icon_buf) {
        if (!icon_buf) return;
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                uint32_t col = icon_buf[y * 32 + x];
                if (col != 0) vesa_putpixel_alpha(bx + x, by + y, col, 255);
            }
        }
    }

    // Helper: draw a dock app button (text-based) with gradient
    void draw_dock_btn(int bx, int by, const char* label, uint32_t accent) {
        widget_draw_rounded_rect(bx, by, 40, 40, 6,
                                 widget_blend_color(accent, 0x000000, 60), 200);
        widget_draw_gradient_rect(bx + 1, by + 1, 38, 19,
                                  widget_blend_color(accent, 0xFFFFFF, 50),
                                  accent, 200);
        widget_draw_gradient_rect(bx + 1, by + 20, 38, 19,
                                  accent,
                                  widget_blend_color(accent, 0x000000, 80), 200);
        // Top shine
        for (int col = bx + 2; col < bx + 38; col++)
            vesa_putpixel_alpha((uint32_t)col, (uint32_t)(by + 1), 0xFFFFFF, 50);
        wm_draw_string(bx + 4, by + 14, label, 0xFFFFFF);
    }

    // Draw dock icons
    draw_dock_icon(dock_x + 14,  dock_y + 9, icon_term_buf);
    draw_dock_icon(dock_x + 74,  dock_y + 9, icon_expl_buf);
    draw_dock_btn (dock_x + 130, dock_y + 5, "Snk",  0x006622);
    draw_dock_btn (dock_x + 190, dock_y + 5, "Rev",  0x884400);
    draw_dock_btn (dock_x + 250, dock_y + 5, "Thm",  0x660066);
    draw_dock_btn (dock_x + 310, dock_y + 5, "Calc", 0x005566);
    draw_dock_btn (dock_x + 370, dock_y + 5, "Time", 0x111133);
    draw_dock_btn (dock_x + 430, dock_y + 5, "Wall", 0x555500);
    draw_dock_icon(dock_x + 494, dock_y + 9, icon_pnt_buf);
    draw_dock_btn (dock_x + 550, dock_y + 5, "File", 0x886600);
    draw_dock_btn (dock_x + 610, dock_y + 5, "Note", 0x112244);
    draw_dock_btn (dock_x + 670, dock_y + 5, "Mine",  0x1A4A10);
    draw_dock_icon(dock_x + 734, dock_y + 9, icon_sett_buf);
    draw_dock_btn (dock_x + 798, dock_y + 5, "Sheet", 0x115511);
    draw_dock_btn (dock_x + 858, dock_y + 5, "Vid",   0x331155);
    draw_dock_btn (dock_x + 918, dock_y + 5, "PDF",   0x553311);
    draw_dock_btn (dock_x + 978, dock_y + 5, "Edit",  0x224455);
    
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
    
    // ── Toast Notification (animated, glass-style) ───────────────────────────
    if (toast_msg[0] && pit_get_ticks() < toast_until) {
        int tw = (int)strlen(toast_msg) * 8 + 24;
        int tx = ((int)vesa_width  - tw) / 2;
        int ty = (int)vesa_height - dock_h - 10 - 36;

        // Glass toast panel
        widget_draw_glass(tx - 4, ty - 4, tw + 8, 30,
                          0x1E3A5F, 200, 1);
        // Accent left bar
        vesa_draw_rect_alpha((uint32_t)(tx - 4), (uint32_t)(ty - 4),
                             3, 30, 0x4499FF, 230);
        // Text
        wm_draw_string((uint32_t)(tx + 8), (uint32_t)(ty + 4),
                       toast_msg, 0xFFFFFF);
        redraw_needed = 1; /* keep redrawing until expired */
    } else if (toast_msg[0] && pit_get_ticks() >= toast_until) {
        toast_msg[0] = '\0';
        redraw_needed = 1;
    }

    // 5.3 Notepad Find bar overlay
    if (find_active && focused_window && focused_window->text_buf) {
        int _fx = (int)focused_window->x;
        int _fy = (int)(focused_window->y + focused_window->h);
        int _fw = (int)focused_window->w;
        widget_draw_glass(_fx, _fy, _fw, 22, 0x1F2937, 240, 0);
        wm_draw_string((uint32_t)(_fx + 4),  (uint32_t)(_fy + 4), "Find:", 0x79C0FF);
        wm_draw_string((uint32_t)(_fx + 44), (uint32_t)(_fy + 4), find_buf,  0xFFFFFF);
        /* cursor blink */
        if (pit_get_ticks() % 50 < 25)
            vesa_draw_rect((uint32_t)(_fx + 44 + find_len * 8), (uint32_t)(_fy + 4), 2, 13, 0xFFFFFF);
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
    extern void music_process_audio(void);
    music_process_audio();
    
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

        // Check bell / notification icon click (top bar)
        if (!clicked_on_something && my >= 0 && my <= 24) {
            int _bell_x = (int)vesa_width - NUM_DESKTOPS * 16 - 36;
            if (mx >= _bell_x && mx <= _bell_x + 18) {
                notif_panel_open = !notif_panel_open;
                clicked_on_something = 1;
                redraw_needed = 1;
            }
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
                window_t* img_win = wm_create_window(250, 150, 500, 400, "Image Viewer");
                extern void imgview_init(window_t*, const char*);
                imgview_init(img_win, "logo.bmp");
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
                } else if (mx >= dock_x + 798 && mx <= dock_x + 855) {
                    // Spreadsheet
                    extern void spreadsheet_init(window_t*);
                    window_t* ss_win = wm_create_window(200, 100, 620, 420, "Spreadsheet");
                    spreadsheet_init(ss_win);
                } else if (mx >= dock_x + 858 && mx <= dock_x + 915) {
                    // Video Player
                    extern void video_player_init(window_t*);
                    window_t* vp_win = wm_create_window(200, 100, 520, 380, "Video Player");
                    video_player_init(vp_win);
                } else if (mx >= dock_x + 918 && mx <= dock_x + 975) {
                    // PDF Viewer
                    extern void pdf_init(window_t*, const char*);
                    window_t* pv_win = wm_create_window(200, 80, 500, 420, "PDF Viewer");
                    pdf_init(pv_win, "readme.txt");
                } else if (mx >= dock_x + 978 && mx <= dock_x + 1035) {
                    // Text Editor
                    extern void textedit_init(window_t*, const char*);
                    window_t* te_win = wm_create_window(150, 80, 600, 440, "Text Editor");
                    textedit_init(te_win, "");
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
                                extern void imgview_init(window_t*, const char*);
                                imgview_init(img_win, name);
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
                        extern void settings_handle_click(window_t*, int, int);
                        settings_handle_click(w, mx, my);
                    } else if (w->title[0] == 'M' && w->title[1] == 'u') {
                        extern void music_handle_click(window_t*, int, int);
                        music_handle_click(w, mx, my);
                    } else if (w->title[0] == 'N' && w->title[1] == 'e') {
                        extern void browser_handle_click(int, int);
                        browser_handle_click(mx, my);
                    }
                    clicked_on_something = 1;
                    break;
                    
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

    /* Forward mouse events to SDL ring buffer if a window is focused */
    static int sdl_last_mx = 0, sdl_last_my = 0;
    if (focused_window) {
        int wx = mx - (int)focused_window->x;
        int wy = my - (int)focused_window->y - 20;
        int dx = mx - sdl_last_mx;
        int dy = my - sdl_last_my;
        if (dx != 0 || dy != 0)
            sdl_push_mousemove(wx, wy, dx, dy);
        if (left_click_just_pressed)
            sdl_push_mousebutton(1, SDL_BUTTON_LEFT, wx, wy);
        else if (left_click_just_released)
            sdl_push_mousebutton(0, SDL_BUTTON_LEFT, wx, wy);
        int right_just_pressed  = (btns & 2) && !(last_btns & 2);
        int right_just_released = !(btns & 2) && (last_btns & 2);
        if (right_just_pressed)
            sdl_push_mousebutton(1, SDL_BUTTON_RIGHT, wx, wy);
        else if (right_just_released)
            sdl_push_mousebutton(0, SDL_BUTTON_RIGHT, wx, wy);
    }
    sdl_last_mx = mx;
    sdl_last_my = my;

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
    /* Notepad Find bar intercepts all input when active */
    if (find_active) {
        if (c == '\n' || c == '\r') {
            if (find_len > 0 && focused_window && focused_window->text_buf) {
                char* _match = strstr(focused_window->text_buf, find_buf);
                char _tmsg[80];
                if (_match) {
                    int _pos = (int)(_match - focused_window->text_buf);
                    sprintf(_tmsg, "Found \"%s\" at pos %d", find_buf, _pos);
                } else {
                    sprintf(_tmsg, "Not found: \"%s\"", find_buf);
                }
                wm_toast(_tmsg, 300);
            }
            find_active = 0;
            redraw_needed = 1;
        } else if (c == '\b') {
            if (find_len > 0) find_buf[--find_len] = '\0';
            redraw_needed = 1;
        } else if (c >= ' ' && c <= '~' && find_len < 63) {
            find_buf[find_len++] = c;
            find_buf[find_len]   = '\0';
            redraw_needed = 1;
        }
        return 1;
    }
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
    } else if (focused_window && strncmp(focused_window->title, "Netscape", 8) == 0) {
        extern void browser_handle_keypress(char);
        browser_handle_keypress(c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Calculator", 10) == 0) {
        calc_handle_input(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Image Viewer", 12) == 0) {
        extern void imgview_handle_key(window_t*, char);
        imgview_handle_key(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Spreadsheet", 11) == 0) {
        extern void spreadsheet_handle_key(window_t*, char);
        spreadsheet_handle_key(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Video Player", 12) == 0) {
        extern void video_player_handle_key(window_t*, char);
        video_player_handle_key(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "PDF Viewer", 10) == 0) {
        extern void pdf_handle_key(window_t*, char);
        pdf_handle_key(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Text Editor", 11) == 0) {
        extern void textedit_handle_key(window_t*, char);
        textedit_handle_key(focused_window, c);
        return 1;
    }
    return 0;
}

void wm_process_scroll(int delta) {
    if (!focused_window) return;
    
    if (strncmp(focused_window->title, "Terminal", 8) == 0) {
        focused_window->term_scroll += (delta * 3);
        int lines_visible = focused_window->h / 16;
        int max_scroll = focused_window->term_line + 1 - lines_visible;
        if (max_scroll < 0) max_scroll = 0;
        
        if (focused_window->term_scroll < 0) focused_window->term_scroll = 0;
        if (focused_window->term_scroll > max_scroll) focused_window->term_scroll = max_scroll;
        wm_redraw_term(focused_window);
        wm_request_redraw();
    } else if (strncmp(focused_window->title, "Netscape", 8) == 0) {
        extern void browser_handle_scroll(int);
        browser_handle_scroll(delta);
    }
}

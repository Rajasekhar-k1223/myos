#include "wm.h"
#include "kernel.h"
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
#include "ttf.h"
#include "nvg_backend.h"
#include "nk_backend.h"
#include "widget.h"
#include "minesweeper.h"
#include "settings.h"
#include "sysmon.h"
#include "calendar.h"
#include "screenshot.h"
#include "diskutil.h"
#include "weather.h"
#include "fontview.h"
#include "charmap.h"
#include "recorder.h"
#include "search.h"
#include "login.h"
#include "wifi.h"
#include "mixer.h"
#include "appstore.h"
#include "bluetooth.h"

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

int in_installer_mode = 0;
int sys_brightness = 80;

void wm_draw_desktop_text(const char* str, float scale, int start_x, int start_y, uint32_t color) {
    // Disabled due to missing ttf_render_glyph
    (void)str; (void)scale; (void)start_x; (void)start_y; (void)color;
}

// Window Dragging and Resizing State
static int drag_win_idx = -1;
static int drag_off_x = 0;
static int drag_off_y = 0;

static int drag_icon_idx = -1;
static int drag_icon_off_x = 0;
static int drag_icon_off_y = 0;

void wm_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg);

/* ─── Modal Dialog state ─────────────────────────────────────────────── */
static dialog_t active_dialog = {0};
static int shortcut_overlay_open = 0;
static uint32_t last_activity_ticks = 0;
#define IDLE_LOCK_TICKS (10 * 60 * 100)   /* 100 Hz × 10 min */

void widget_alert(const char* title, const char* text, void (*on_close)(void*), void* arg) {
    if (active_dialog.active) return;
    active_dialog.active = 1;
    active_dialog.type   = DLG_TYPE_ALERT;
    strncpy(active_dialog.title, title ? title : "Alert",  63);
    strncpy(active_dialog.text,  text  ? text  : "",       255);
    active_dialog.on_yes = on_close;
    active_dialog.arg    = arg;
    redraw_needed = 1;
}

void widget_confirm(const char* title, const char* text,
                    void (*on_yes)(void*), void (*on_no)(void*), void* arg) {
    if (active_dialog.active) return;
    active_dialog.active = 1;
    active_dialog.type   = DLG_TYPE_CONFIRM;
    strncpy(active_dialog.title, title ? title : "Confirm", 63);
    strncpy(active_dialog.text,  text  ? text  : "",        255);
    active_dialog.on_yes = on_yes;
    active_dialog.on_no  = on_no;
    active_dialog.arg    = arg;
    redraw_needed = 1;
}

void widget_file_open(const char* title, void (*on_select)(const char*, void*), void* arg) {
    if (active_dialog.active) return;
    active_dialog.active       = 1;
    active_dialog.type         = DLG_TYPE_FILE_OPEN;
    active_dialog.scroll_index = 0;
    active_dialog.num_files    = 0;
    strncpy(active_dialog.title, title ? title : "Open File", 63);
    active_dialog.on_file = on_select;
    active_dialog.arg     = arg;
    /* Enumerate FAT16 */
    static fat16_file_info_t _finfo[16];
    int cnt = fat16_list_files(_finfo, 16);
    if (cnt < 0) cnt = 0;
    if (cnt > 16) cnt = 16;
    active_dialog.num_files = cnt;
    for (int i = 0; i < cnt; i++)
        strncpy(active_dialog.files[i], _finfo[i].name, 31);
    redraw_needed = 1;
}

static void dialog_draw(void) {
    if (!active_dialog.active) return;
    extern uint32_t vesa_width, vesa_height;
    /* dim backdrop */
    vesa_draw_rect_alpha(0, 0, vesa_width, vesa_height, 0x000000, 160);
    int dh = (active_dialog.type == DLG_TYPE_FILE_OPEN) ? 320 : 170;
    int dw = 400;
    int dx = ((int)vesa_width  - dw) / 2;
    int dy = ((int)vesa_height - dh) / 2;
    widget_draw_glass(dx, dy, dw, dh, current_theme.window_bg, 248, 1);
    vesa_draw_rect((uint32_t)dx, (uint32_t)dy, (uint32_t)dw, 28, current_theme.title_bg);
    wm_draw_string((uint32_t)(dx + 12), (uint32_t)(dy + 8), active_dialog.title, current_theme.title_fg);
    if (active_dialog.type == DLG_TYPE_ALERT) {
        wm_draw_string((uint32_t)(dx+16), (uint32_t)(dy+50), active_dialog.text, current_theme.menu_fg);
        widget_draw_button(dx+dw/2-40, dy+dh-44, 80, 28, "   OK   ", current_theme.start_btn_bg, 0xFFFFFF, 0);
    } else if (active_dialog.type == DLG_TYPE_CONFIRM) {
        wm_draw_string((uint32_t)(dx+16), (uint32_t)(dy+50), active_dialog.text, current_theme.menu_fg);
        widget_draw_button(dx+dw/2-90, dy+dh-44, 80, 28, "  Yes  ", current_theme.start_btn_bg, 0xFFFFFF, 0);
        widget_draw_button(dx+dw/2+10, dy+dh-44, 80, 28, "   No  ", 0x444444, 0xFFFFFF, 0);
    } else {
        wm_draw_string((uint32_t)(dx+12), (uint32_t)(dy+36), "Select a file:", 0xAAAAAA);
        int ly = dy + 54, lh = dh - 54 - 46;
        vesa_draw_rect((uint32_t)(dx+12), (uint32_t)ly, (uint32_t)(dw-24), (uint32_t)lh, current_theme.window_bg);
        int vis = lh / 18;
        for (int i = 0; i < vis; i++) {
            int fi = i + active_dialog.scroll_index;
            if (fi >= active_dialog.num_files) break;
            wm_draw_string((uint32_t)(dx+18), (uint32_t)(ly+4+i*18),
                           active_dialog.files[fi], current_theme.menu_fg);
        }
        widget_draw_button(dx+dw/2-40, dy+dh-44, 80, 28, " Cancel ", 0x444444, 0xFFFFFF, 0);
    }
}

static void dialog_handle_click(int mx, int my) {
    if (!active_dialog.active) return;
    extern uint32_t vesa_width, vesa_height;
    int dh = (active_dialog.type == DLG_TYPE_FILE_OPEN) ? 320 : 170;
    int dw = 400;
    int dx = ((int)vesa_width  - dw) / 2;
    int dy = ((int)vesa_height - dh) / 2;
    if (active_dialog.type == DLG_TYPE_ALERT) {
        if (mx >= dx+dw/2-40 && mx <= dx+dw/2+40 && my >= dy+dh-44 && my <= dy+dh-16) {
            void (*cb)(void*) = active_dialog.on_yes;
            void* a = active_dialog.arg;
            active_dialog.active = 0;
            if (cb) cb(a);
            redraw_needed = 1;
        }
    } else if (active_dialog.type == DLG_TYPE_CONFIRM) {
        if (mx >= dx+dw/2-90 && mx <= dx+dw/2-10 && my >= dy+dh-44 && my <= dy+dh-16) {
            void (*cb)(void*) = active_dialog.on_yes; void* a = active_dialog.arg;
            active_dialog.active = 0;
            if (cb) cb(a);
            redraw_needed = 1;
        } else if (mx >= dx+dw/2+10 && mx <= dx+dw/2+90 && my >= dy+dh-44 && my <= dy+dh-16) {
            void (*cb)(void*) = active_dialog.on_no; void* a = active_dialog.arg;
            active_dialog.active = 0;
            if (cb) cb(a);
            redraw_needed = 1;
        }
    } else {
        int ly = dy + 54, lh = dh - 54 - 46;
        if (mx >= dx+12 && mx <= dx+dw-12 && my >= ly && my < ly+lh) {
            int idx = (my - ly) / 18 + active_dialog.scroll_index;
            if (idx < active_dialog.num_files && active_dialog.files[idx][0]) {
                void (*cb)(const char*, void*) = active_dialog.on_file;
                void* a = active_dialog.arg;
                char fname[32];
                strncpy(fname, active_dialog.files[idx], 31);
                active_dialog.active = 0;
                if (cb) cb(fname, a);
                redraw_needed = 1;
            }
        }
        if (mx >= dx+dw/2-40 && mx <= dx+dw/2+40 && my >= dy+dh-44 && my <= dy+dh-16) {
            active_dialog.active = 0;
            redraw_needed = 1;
        }
    }
}

static void wm_do_reboot(void* arg) {
    (void)arg;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void shortcut_overlay_draw(void) {
    if (!shortcut_overlay_open) return;
    extern uint32_t vesa_width, vesa_height;
    int ow = 520, oh = 360;
    int ox = ((int)vesa_width  - ow) / 2;
    int oy = ((int)vesa_height - oh) / 2;
    vesa_draw_rect_alpha(0, 0, vesa_width, vesa_height, 0x000000, 180);
    widget_draw_glass(ox, oy, ow, oh, 0x0D1117, 250, 1);
    vesa_draw_rect((uint32_t)ox, (uint32_t)oy, (uint32_t)ow, 28, 0x1A2A4A);
    wm_draw_string((uint32_t)(ox+12), (uint32_t)(oy+8), "Keyboard Shortcuts", 0xFFFFFF);
    static const char* sc[][2] = {
        {"Ctrl+T",   "New Terminal"},    {"Ctrl+E",   "File Explorer"},
        {"Ctrl+N",   "New Notepad"},     {"Ctrl+W",   "Close Window"},
        {"Ctrl+S",   "Save File"},       {"Ctrl+O",   "Open File"},
        {"Ctrl+C",   "Copy"},            {"Ctrl+V",   "Paste"},
        {"Ctrl+F",   "Find in Text"},    {"Ctrl+L",   "Lock Screen"},
        {"Ctrl+[/]", "Window Opacity"},  {"Ctrl+1-4", "Switch Desktop"},
        {"Ctrl+?",   "This Help"},       {"Rt-Click", "Desktop Menu"},
    };
    int col_w = ow / 2;
    for (int i = 0; i < 14; i++) {
        int sx = ox + 16 + (i%2) * col_w;
        int sy = oy + 38 + (i/2) * 26;
        wm_draw_string((uint32_t)sx, (uint32_t)sy, sc[i][0], 0x5599FF);
        wm_draw_string((uint32_t)(sx+100), (uint32_t)sy, sc[i][1], 0xCCCCCC);
    }
    wm_draw_string((uint32_t)(ox+ow/2-80), (uint32_t)(oy+oh-22),
                   "Press any key or click to close", 0x666666);
}

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

/* ── Desktop Environment layout: 0=ElseaOS, 1=KDE Plasma, 2=GNOME Shell ── */
int desktop_layout = 0;
static int activities_open  = 0;  /* GNOME activities overview */
static int kde_launcher_open = 0; /* KDE app launcher grid */

/* ── App Launcher + right-panel state ───────────────────────────── */
static int  launcher_open      = 0;
static int  launcher_cat       = 0;
static char ai_input_buf[128]  = "";
static int  ai_input_len       = 0;
static int  ai_input_focused   = 0;

/* ── Metric history circular buffers ────────────────────────────── */
#define MH_LEN 80
static uint8_t  mh_cpu[MH_LEN];
static uint8_t  mh_ram[MH_LEN];
static uint8_t  mh_netrx[MH_LEN];
static uint8_t  mh_nettx[MH_LEN];
static uint8_t  mh_disk[MH_LEN];
static int      mh_pos   = 0;
static uint32_t mh_last  = 0;
static int      mh_count = 0;   /* samples written so far, capped at MH_LEN */

/* ── NanoVG context (software renderer) ─────────────────────────── */
static NVGcontext* wm_nvg = NULL;

/* System clipboard */
char     clipboard_buf[8192];
uint32_t clipboard_len = 0;

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
    "-",
    "Install ElseaOS",
};
#define CTX_NUM_ITEMS 9

/* GNOME Adwaita Dark — the default ElseaOS theme */
theme_t theme_gnome = {
    .window_bg       = 0x1E1E1E,  /* content area dark gray    */
    .window_border   = 0x3D3D3D,  /* subtle border              */
    .title_bg        = 0x303030,  /* header bar focused         */
    .title_fg        = 0xFFFFFF,
    .taskbar_bg      = 0x1A1A1A,
    .start_btn_bg    = 0x3584E4,  /* GNOME blue accent          */
    .start_btn_fg    = 0xFFFFFF,
    .menu_fg         = 0xDDDDDD,
    .title_inactive_bg = 0x262626 /* header bar unfocused       */
};

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
    {1,0,0,0,0,1,1,0,0,0}
};

void wm_draw_mouse(void) {
    extern int mouse_get_x(void);
    extern int mouse_get_y(void);
    int mx = mouse_get_x();
    int my = mouse_get_y();
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < 10; x++) {
            if (cursor_bitmap[y][x] == 1) {
                vesa_putpixel_alpha(mx + x, my + y, 0x000000, 255);
            } else if (cursor_bitmap[y][x] == 2) {
                vesa_putpixel_alpha(mx + x, my + y, 0xFFFFFF, 255);
            }
        }
    }
}

static uint32_t icon_term_buf[32 * 32];
static uint32_t icon_expl_buf[32 * 32];
static uint32_t icon_sett_buf[32 * 32];
static uint32_t icon_pnt_buf[32 * 32];
static uint32_t cursor_buf[16 * 16];

void wm_init(void) {
    extern uint32_t vesa_width, vesa_height;
    vesa_init_backbuffer();
    vesa_set_double_buffer(1);

    current_theme = theme_gnome;  /* GNOME Adwaita Dark by default */

    // ── Phoenix Wallpaper ────────────────────────────────────────────────────
    desktop_bg_buffer = (uint32_t*)kmalloc(vesa_width * vesa_height * 4);
    if (!desktop_bg_buffer) {
        terminal_printf("[WM] FATAL: desktop buffer alloc failed\n");
        return;
    }
    memset(desktop_bg_buffer, 0, vesa_width * vesa_height * 4);
    extern void bmp_load_to_buffer(const char*, uint32_t*, int, int, int, int);
    bmp_load_to_buffer("phoenix-hd.bmp", desktop_bg_buffer, vesa_width, vesa_height, 0, 0);
    
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
    
    /* ── NanoVG software renderer ─────────────────────────────────── */
    wm_nvg = nvg_elseaos_create(vesa_get_backbuffer(),
                                (int)vesa_width, (int)vesa_height);
    if (wm_nvg)
        terminal_printf("[WM] NanoVG software renderer ready\n");
    else
        terminal_printf("[WM] NanoVG init failed (continuing without)\n");

    /* ── Nuklear (for future window widgets) ─────────────────────── */
    nk_elseaos_init();
    terminal_printf("[WM] Nuklear UI toolkit ready\n");
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
    win->alpha = (strncmp(title, "Terminal", 8) == 0) ? 160 : 255;
    win->text_buf   = NULL;
    win->text_len   = 0;
    win->minimized  = 0;
    win->maximized  = 0;
    win->anim_state = 1; // Opening animation
    win->anim_progress = 0.0f;
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
        if (win->term_grid) {
            for (uint32_t i = 0; i < win->term_rows * win->term_cols; i++) {
                win->term_grid[i] = (' ') | (0xAAAAAA << 8);
            }
        }
        wm_terminal_print(win, 
            "\033[1;36m       _,met$$$$$gg.          \033[1;37madmin\033[0m@\033[1;36melseaos\n"
            "\033[1;36m    ,g$$$$$$$$$$$$$$$P.       \033[0m-------\n"
            "\033[1;36m  ,g$$P\"\"       \"\"\"Y$$.\".     \033[1;36mOS:\033[0m ElseaOS v1.0\n"
            "\033[1;36m ,$$P'              `$$$.     \033[1;36mKernel:\033[0m Custom x86\n"
            "\033[1;36m',$$P       ,ggs.     `$$b:   \033[1;36mUptime:\033[0m Just started\n"
            "\033[1;36m`d$$'     ,$P\"'   .    $$$    \033[1;36mPackages:\033[0m 10 (dpkg)\n"
            "\033[1;36m $$P      d$'     ,    $$P    \033[1;36mShell:\033[0m eash\n"
            "\033[1;36m $$:      $$.   -    ,d$$'    \033[1;36mResolution:\033[0m 1024x768\n"
            "\033[1;36m $$;      Y$b._   _,d$P'      \033[1;36mWM:\033[0m ElseaWM Glass\n"
            "\033[1;36m Y$$.    `.`\"Y$$$$P\"'         \033[1;36mTheme:\033[0m Win95 (Custom)\n"
            "\033[1;36m `$$b      \"-.__              \033[1;36mTerminal:\033[0m E-Term\n"
            "\033[1;36m  `Y$$                        \033[1;36mCPU:\033[0m Generic x86\n"
            "\033[1;36m   `Y$$.                      \033[1;36mMemory:\033[0m 32MB / 128MB\n"
            "\033[1;36m     `$$b.\n"
            "\033[1;36m       `Y$$b.\n"
            "\033[1;36m         `\"Y$b._\n"
            "\033[1;36m             `\"\"\"\n\n\033[0m"
            "admin@elseaos:~$ "
        );
    } else {
        win->term_grid = NULL;
    }

    win->buffer = (uint32_t*)kmalloc(w * h * 4);
    if (!win->buffer) {
        num_windows--; // Rollback window creation
        if (win->term_grid) kfree(win->term_grid);
        return NULL;
    }

    focused_window = win;
    
    // Fill window with background color
    for (uint32_t i = 0; i < w * h; i++) {
        win->buffer[i] = win->bg_color; 
    }
    redraw_needed = 1;
    return win;
}

void wm_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg) {
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
        if (search_is_open()) {
            search_handle_keypress(key);
            return 1;
        }
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
                memcpy(np->text_buf, load_buf, (uint32_t)r);
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
    /* Ctrl+L — lock screen */
    if (key == 'l' || key == 'L') {
        login_lock(NULL);
        last_activity_ticks = 0;
        wm_toast("Screen locked", 80);
        redraw_needed = 1;
        return 1;
    }
    /* Ctrl+? — shortcut overlay */
    if (key == '?') {
        shortcut_overlay_open = !shortcut_overlay_open;
        redraw_needed = 1;
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

    /* Super key (sent as \x1B\x5B\x31\x7E or just mapped to special char)
     * Ctrl+Space = toggle Activities (GNOME) / KDE Launcher */
    if (key == ' ') {
        if (desktop_layout == 2) {
            activities_open = !activities_open;
            redraw_needed = 1; return 1;
        } else if (desktop_layout == 1) {
            kde_launcher_open = !kde_launcher_open;
            redraw_needed = 1; return 1;
        }
    }
    /* Ctrl+D = cycle desktop layout 0→1→2→0 */
    if (key == 'd' || key == 'D') {
        desktop_layout = (desktop_layout + 1) % 3;
        static const char* layout_names[] = {
            "ElseaOS Default", "KDE Plasma", "GNOME Shell"
        };
        wm_toast(layout_names[desktop_layout], 180);
        activities_open  = 0;
        kde_launcher_open = 0;
        redraw_needed = 1; return 1;
    }
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

void wm_terminal_print(window_t* win, const char* str) {
    if (!win || !win->term_grid) return;
    for (int i = 0; str[i]; i++) {
        wm_putchar(win, str[i]);
    }
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
static int  notif_dnd = 0;

void wm_toast(const char* msg, uint32_t duration_ticks) {
    if (!notif_dnd) {
        strncpy(toast_msg, msg, 79);
        toast_msg[79] = '\0';
        toast_until = pit_get_ticks() + duration_ticks;
    }
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

static void wm_chart_buf(uint32_t* buf, int bw, int bh,
                          int x, int y, int w, int h,
                          const uint8_t* d1, uint32_t c1,
                          const uint8_t* d2, uint32_t c2,
                          int pos, int n);

/* Forward declarations for library helpers defined below */
static void wm_text(int x, int y, const char* s, uint32_t col, int sz);
static void wm_nvg_ensure_frame(void);
static void wm_nvg_flush(void);
static void wm_text_buf(window_t* w, int x, int y,
                         const char* s, uint32_t col, int sz);
static void wm_blur_region(int x, int y, int w, int h, int radius);
static void wm_glass_panel(int x, int y, int w, int h, int radius,
                            uint32_t tint, uint8_t tint_alpha);
static void wm_nvg_rect(int x, int y, int w, int h, int r,
                         uint32_t col, uint8_t alpha);
static void wm_nvg_arc_gauge(int cx, int cy, int r, int pct,
                              uint32_t col, const char* label, const char* val);
static void wm_shadow(int x, int y, int w, int h);
static void wm_nvg_draw_icon(float cx, float cy, const char* label);
static void wm_arc_gauge(int cx, int cy, int r, int pct,
                          uint32_t col, const char* label, const char* val);

static void wm_update_system_monitor(window_t* w) {
    if (!w || !w->buffer) return;

    /* Dark navy background */
    for (uint32_t _i = 0; _i < w->w * w->h; _i++) w->buffer[_i] = 0x080C14;

    extern uint32_t pmm_get_max_frames(void);
    extern uint32_t pmm_get_used_frames(void);
    extern uint32_t pit_get_seconds(void);
    extern uint32_t task_count(void);

    uint32_t secs   = pit_get_seconds();
    uint32_t ntasks = task_count();
    (void)pmm_get_max_frames(); (void)pmm_get_used_frames();

    int bw = (int)w->w, bh = (int)w->h;
    int n_samples = (mh_count >= 2) ? mh_count : 2;

    char _line[80];

    /* ── Header bar ── */
    for (int _y = 0; _y < 26; _y++)
        for (int _x = 0; _x < bw; _x++)
            w->buffer[_y * bw + _x] = 0x0D1830;
    wm_text_buf(w, 10, 6, "Performance Monitor", 0x58A6FF, 12);
    sprintf(_line, "Up %02u:%02u:%02u  Tasks:%u",
            secs/3600, (secs%3600)/60, secs%60, ntasks);
    wm_text_buf(w, bw - (int)(strlen(_line)*7+4), 6, _line, 0x7A8898, 10);

    int y = 30, pad = 8;
    int cw = (bw - pad*3) / 2;
    int ch = (bh - y - pad*3) / 2;
    if (ch < 40) ch = 40;

    /* ── CPU (top-left) ── */
    {
        int cv = mh_cpu[(mh_pos+MH_LEN-1)%MH_LEN];
        sprintf(_line, "CPU  %d%%", cv);
        wm_text_buf(w, pad, y, _line, 0x3584E4, 10);
        wm_chart_buf(w->buffer, bw, bh, pad, y+14, cw, ch,
                     mh_cpu, 0x3584E4, NULL, 0, mh_pos, n_samples);
    }

    /* ── RAM (top-right) ── */
    {
        uint32_t mf = pmm_get_max_frames(), uf = pmm_get_used_frames();
        int ram_mb = (int)((uf * 4) / 1024), max_mb = (int)((mf * 4) / 1024);
        sprintf(_line, "RAM  %dMB/%dMB", ram_mb, max_mb);
        int rx2 = pad*2 + cw;
        wm_text_buf(w, rx2, y, _line, 0x9B59B6, 10);
        wm_chart_buf(w->buffer, bw, bh, rx2, y+14, cw, ch,
                     mh_ram, 0x9B59B6, NULL, 0, mh_pos, n_samples);
    }

    int y2 = y + 14 + ch + pad;

    /* ── Network dual-series (bottom-left) ── */
    {
        int rxv = mh_netrx[(mh_pos+MH_LEN-1)%MH_LEN];
        int txv = mh_nettx[(mh_pos+MH_LEN-1)%MH_LEN];
        sprintf(_line, "Net RX%d%% TX%d%%", rxv, txv);
        wm_text_buf(w, pad, y2, _line, 0x1ABC9C, 10);
        wm_text_buf(w, pad + cw - 26, y2, "TX", 0xE67E22, 10);
        wm_chart_buf(w->buffer, bw, bh, pad, y2+14, cw, ch,
                     mh_netrx, 0x1ABC9C, mh_nettx, 0xE67E22, mh_pos, n_samples);
    }

    /* ── Disk I/O (bottom-right) ── */
    {
        int dv = mh_disk[(mh_pos+MH_LEN-1)%MH_LEN];
        sprintf(_line, "Disk  %d%%", dv);
        int rx2 = pad*2 + cw;
        wm_text_buf(w, rx2, y2, _line, 0xE74C3C, 10);
        wm_chart_buf(w->buffer, bw, bh, rx2, y2+14, cw, ch,
                     mh_disk, 0xE74C3C, NULL, 0, mh_pos, n_samples);
    }

    redraw_needed = 1;
}

/* ─── sin table: 36 entries 0–350° in 10° steps, scaled ×100 ─── */
static const int wm_sint36[36] = {
     0,  17,  34,  50,  64,  77,  87,  94,  98, 100,
    98,  94,  87,  77,  64,  50,  34,  17,   0, -17,
   -34, -50, -64, -77, -87, -94, -98,-100, -98, -94,
   -87, -77, -64, -50, -34, -17
};

/* Draw a circular arc gauge centred at (cx,cy), radius r, pct 0-100 */
static void wm_arc_gauge(int cx, int cy, int r, int pct,
                         uint32_t col, const char* label, const char* val)
{
    extern uint32_t vesa_width, vesa_height;
    int filled = (pct * 36 + 50) / 100;
    for (int s = 0; s < 36; s++) {
        int px = cx + (wm_sint36[s] * r) / 100;
        int py = cy - (wm_sint36[(s + 9) % 36] * r) / 100;
        uint32_t c = (s < filled) ? col : 0x232335;
        for (int oy = -2; oy <= 2; oy++)
            for (int ox = -2; ox <= 2; ox++) {
                int fx = px + ox, fy = py + oy;
                if (fx >= 0 && fy >= 0 &&
                    fx < (int)vesa_width && fy < (int)vesa_height)
                    vesa_putpixel_alpha((uint32_t)fx, (uint32_t)fy, c, 255);
            }
    }
    if (val) {
        int vl = (int)strlen(val);
        wm_draw_string((uint32_t)(cx - vl * 4), (uint32_t)(cy - 8), val, 0xFFFFFF);
    }
    if (label) {
        int ll = (int)strlen(label);
        wm_draw_string((uint32_t)(cx - ll * 4), (uint32_t)(cy + 5), label, 0x7A8090);
    }
}

/* ─── Left vertical dock (72 px wide, 5 app icons) ─────────────── */
static void wm_render_left_dock(void)
{
    extern uint32_t vesa_width, vesa_height;
    (void)vesa_width;
    static const struct { const char* label; int buf_id; } ld[] = {
        { "Files",    0 },
        { "Browser",  0 },
        { "Terminal", 2 },
        { "Settings", 3 },
        { "Trash",    1 },
    };
    int dw = 72, item_h = 68;
    int dy0 = 32;
    int dock_h = 5 * item_h + 8;

    /* Glass panel with blur */
    wm_glass_panel(0, dy0, dw, dock_h, 0, 0x090A18, 210);
    /* Subtle right separator */
    for (int _y = dy0; _y < dy0 + dock_h; _y++)
        vesa_putpixel_alpha((uint32_t)(dw - 1), (uint32_t)_y, 0x4466AA, 40);

    uint32_t* bufs[4] = { icon_expl_buf, icon_pnt_buf, icon_term_buf, icon_sett_buf };
    int mx = mouse_get_x(), my = mouse_get_y();

    for (int i = 0; i < 5; i++) {
        int iy = dy0 + 4 + i * item_h;
        int hover = (mx >= 0 && mx < dw && my >= iy && my < iy + item_h - 2);

        if (hover) {
            /* Anti-aliased highlight pill via NanoVG */
            wm_nvg_rect(5, iy + 2, dw - 10, 54, 10, 0x3584E4, 100);
        }

        /* 36×36 icon with rounded background */
        int ix = (dw - 36) / 2;
        wm_nvg_rect(ix - 2, iy + 4, 40, 40, 8,
                    hover ? 0x2A6EC4 : 0x151828, hover ? 200 : 160);

        if (wm_nvg) {
        wm_nvg_ensure_frame();
            wm_nvg_draw_icon(ix + 18, iy + 24, ld[i].label);

        }

        /* Label — TTF 9px */
        int ls = (int)strlen(ld[i].label);
        wm_text((dw - ls * 6) / 2, iy + 45, ld[i].label,
                hover ? 0xFFFFFF : 0xBBBDC8, 9);

        /* Active indicator dot on right edge */
        vesa_putpixel_alpha((uint32_t)(dw - 4),
                            (uint32_t)(iy + item_h / 2),
                            0x3584E4, hover ? 200 : 0);
    }
}

/* ─── Right widget panel (220 px wide) ──────────────────────────── */
/* ─── Metric collection (call once per render frame, self-throttled) ─ */
static void wm_metrics_tick(void)
{
    extern uint32_t pmm_get_max_frames(void);
    extern uint32_t pmm_get_used_frames(void);

    uint32_t now = pit_get_ticks();
    if (now - mh_last < 100) return;   /* update once per second */
    mh_last = now;

    /* CPU — simulated from window count + slow tick jitter */
    int base   = 4 + num_windows * 3;
    int jitter = (int)((now / 137) % 14);
    int cpu    = base + jitter;
    if (cpu > 92) cpu = 92; if (cpu < 2) cpu = 2;
    mh_cpu[mh_pos] = (uint8_t)cpu;

    /* RAM — real PMM data */
    uint32_t mf = pmm_get_max_frames(), uf = pmm_get_used_frames();
    mh_ram[mh_pos] = (mf > 0) ? (uint8_t)(uf * 100 / mf) : 0;

    /* Network RX/TX — pseudo-random waves that look realistic */
    uint32_t s = now ^ (now >> 7) ^ (uint32_t)(mh_pos * 0x9E3779B9u);
    mh_netrx[mh_pos] = (uint8_t)(((s * 6364136223846793005u) >> 24) % 72 + 6);
    mh_nettx[mh_pos] = (uint8_t)(((s * 2862933555777941757u) >> 24) % 38 + 3);

    /* Disk I/O — slow periodic bursts */
    uint32_t burst = (now / 700) % 8;
    mh_disk[mh_pos] = (uint8_t)(burst < 2 ? 45 + (int)(s % 30) : 6 + (int)(s % 10));

    mh_pos = (mh_pos + 1) % MH_LEN;
    if (mh_count < MH_LEN) mh_count++;
    redraw_needed = 1;
}

/* ─── Sparkline — draw to VESA framebuffer ─────────────────────── *
 * data[]: circular buffer, pos = next-write slot, n = samples to show
 * Draws a filled area chart with a glowing line on top.             */
static void wm_sline_screen(int x, int y, int w, int h,
                             const uint8_t* data, int pos, int n,
                             uint32_t line_col, uint32_t fill_col)
{
    extern uint32_t vesa_width, vesa_height;
    if (n < 2 || w < 2 || h < 2) return;
    int denom = w - 1;

    for (int px = 0; px < w; px++) {
        int sx = x + px;
        if (sx < 0 || sx >= (int)vesa_width) continue;

        /* Interpolate between adjacent samples */
        int fi   = (px * (n - 1)) / denom;
        if (fi >= n - 1) fi = n - 2;
        int frac = (px * (n - 1)) - fi * denom;

        int i0 = (pos - n + fi     + MH_LEN * 8) % MH_LEN;
        int i1 = (pos - n + fi + 1 + MH_LEN * 8) % MH_LEN;
        int v  = (int)data[i0] + ((int)data[i1] - (int)data[i0]) * frac / denom;
        if (v < 0) v = 0; if (v > 100) v = 100;

        int pyl = y + h - 1 - v * (h - 1) / 100;
        if (pyl < y) pyl = y;

        /* Gradient area fill */
        for (int fy = pyl; fy < y + h; fy++) {
            if (fy < 0 || fy >= (int)vesa_height) continue;
            int dist = fy - pyl;
            int a    = 165 - dist * 150 / h;
            if (a < 4) continue;
            vesa_putpixel_alpha((uint32_t)sx, (uint32_t)fy, fill_col, (uint8_t)a);
        }
        /* Line + 1px glow */
        for (int ly = pyl - 1; ly <= pyl + 1; ly++) {
            if (ly < y || ly >= y + h || ly < 0 || ly >= (int)vesa_height) continue;
            vesa_putpixel_alpha((uint32_t)sx, (uint32_t)ly,
                                line_col, (uint8_t)(ly == pyl ? 255 : 80));
        }
    }
    /* Current-value dot at right edge */
    {
        int v   = data[(pos + MH_LEN - 1) % MH_LEN];
        int dot_y = y + h - 1 - v * (h - 1) / 100;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx*dx + dy*dy <= 5) {
                    int fx = x + w - 1 + dx, fy = dot_y + dy;
                    if (fx >= 0 && fy >= 0 && fx < (int)vesa_width && fy < (int)vesa_height)
                        vesa_putpixel_alpha((uint32_t)fx, (uint32_t)fy, line_col, 255);
                }
    }
}

/* ─── Sparkline — draw into a window pixel buffer ──────────────── */
static void wm_sline_buf(uint32_t* buf, int bw, int bh,
                          int x, int y, int w, int h,
                          const uint8_t* data, int pos, int n,
                          uint32_t line_col, uint32_t fill_col)
{
    if (!buf || n < 2 || w < 2 || h < 2) return;
    int denom = w - 1;

    for (int px = 0; px < w; px++) {
        int bx = x + px;
        if (bx < 0 || bx >= bw) continue;

        int fi   = (px * (n - 1)) / denom;
        if (fi >= n - 1) fi = n - 2;
        int frac = (px * (n - 1)) - fi * denom;

        int i0 = (pos - n + fi     + MH_LEN * 8) % MH_LEN;
        int i1 = (pos - n + fi + 1 + MH_LEN * 8) % MH_LEN;
        int v  = (int)data[i0] + ((int)data[i1] - (int)data[i0]) * frac / denom;
        if (v < 0) v = 0; if (v > 100) v = 100;

        int pyl = y + h - 1 - v * (h - 1) / 100;
        if (pyl < y) pyl = y;

        /* Gradient fill — blend fill_col into existing buffer pixel */
        for (int fy = pyl; fy < y + h && fy < bh; fy++) {
            if (fy < 0) continue;
            int dist = fy - pyl;
            int a    = 165 - dist * 150 / h;
            if (a < 4) continue;
            uint8_t ua = (uint8_t)a, ia = (uint8_t)(255 - a);
            uint32_t* d = &buf[fy * bw + bx];
            uint8_t r = (uint8_t)(((fill_col>>16)&0xFF)*ua/255 + ((*d>>16)&0xFF)*ia/255);
            uint8_t g = (uint8_t)(((fill_col>> 8)&0xFF)*ua/255 + ((*d>> 8)&0xFF)*ia/255);
            uint8_t b = (uint8_t)( (fill_col     &0xFF)*ua/255 + ( *d     &0xFF)*ia/255);
            *d = ((uint32_t)r<<16)|((uint32_t)g<<8)|b;
        }
        /* Line + glow */
        for (int ly = pyl - 1; ly <= pyl + 1 && ly < bh; ly++) {
            if (ly < 0 || ly < y || ly >= y + h) continue;
            uint8_t la = (uint8_t)(ly == pyl ? 255 : 75), ia = (uint8_t)(255 - la);
            uint32_t* d = &buf[ly * bw + bx];
            uint8_t r = (uint8_t)(((line_col>>16)&0xFF)*la/255 + ((*d>>16)&0xFF)*ia/255);
            uint8_t g = (uint8_t)(((line_col>> 8)&0xFF)*la/255 + ((*d>> 8)&0xFF)*ia/255);
            uint8_t b = (uint8_t)( (line_col     &0xFF)*la/255 + ( *d     &0xFF)*ia/255);
            *d = ((uint32_t)r<<16)|((uint32_t)g<<8)|b;
        }
    }
    /* Current-value dot at right edge */
    {
        int v   = data[(pos + MH_LEN - 1) % MH_LEN];
        int dot_y = y + h - 1 - v * (h - 1) / 100;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx*dx + dy*dy <= 5) {
                    int fx = x + w - 1 + dx, fy = dot_y + dy;
                    if (fx < 0 || fy < 0 || fx >= bw || fy >= bh) continue;
                    buf[fy * bw + fx] = line_col;
                }
    }
}

/* ─── Full chart panel drawn inside a window buffer ─────────────── *
 * Draws: dark bg, dashed grid, up to 2 data series, border.
 * Caller must draw title + Y-labels with wm_draw_string_window.     */
static void wm_chart_buf(uint32_t* buf, int bw, int bh,
                          int x, int y, int w, int h,
                          const uint8_t* d1, uint32_t c1,
                          const uint8_t* d2, uint32_t c2,
                          int pos, int n)
{
    if (!buf || w < 4 || h < 4) return;

    /* Dark panel background */
    for (int py = y; py < y + h && py < bh; py++)
        for (int px = x; px < x + w && px < bw; px++)
            buf[py * bw + px] = 0x06080E;

    /* Horizontal grid lines at 0%, 25%, 50%, 75%, 100% */
    for (int g = 0; g <= 4; g++) {
        int gy = y + (g * (h - 1)) / 4;
        if (gy < 0 || gy >= bh) continue;
        for (int gx = x; gx < x + w && gx < bw; gx += 5)
            buf[gy * bw + gx] = 0x161830;
    }

    /* Data series */
    int n2 = (n > 1) ? n : 2;
    if (d1) wm_sline_buf(buf, bw, bh, x, y, w, h, d1, pos, n2, c1, c1);
    if (d2) wm_sline_buf(buf, bw, bh, x, y, w, h, d2, pos, n2, c2, c2);

    /* Border */
    for (int px = x; px < x + w && px < bw; px++) {
        if (y >= 0 && y < bh)        buf[y       * bw + px] = 0x2A3058;
        if (y+h-1 >= 0 && y+h-1<bh) buf[(y+h-1) * bw + px] = 0x2A3058;
    }
    for (int py = y; py < y + h && py < bh; py++) {
        if (x >= 0 && x < bw)        buf[py * bw + x]       = 0x2A3058;
        if (x+w-1 >= 0 && x+w-1<bw) buf[py * bw + (x+w-1)] = 0x2A3058;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  UI LIBRARY HELPERS — TTF text, blur, NanoVG primitives
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Widgets (right panel) enabled flag — OFF by default ──────────── */
int wm_widgets_enabled = 0;

/* ── NanoVG batched frame state ────────────────────────────────────── */
static int _nvg_frame_open = 0;

static void wm_nvg_ensure_frame(void) {
    extern uint32_t vesa_width, vesa_height;
    if (!wm_nvg || _nvg_frame_open) return;
    nvgBeginFrame(wm_nvg, (float)vesa_width, (float)vesa_height, 1.0f);
    _nvg_frame_open = 1;
}

static void wm_nvg_flush(void) {
    if (!wm_nvg || !_nvg_frame_open) return;

    _nvg_frame_open = 0;
}

/* ── TTF into window pixel buffer (for app windows) ───────────────── */
static void wm_text_buf(window_t* w, int x, int y,
                         const char* s, uint32_t col, int sz) {
    if (!w || !w->buffer || !s || !s[0]) return;
    if (ttf_is_loaded()) {
        ttf_draw_string(w->buffer, (int)w->w, (int)w->h,
                        x, y + sz, s, (int)strlen(s), sz, col);
    } else {
        wm_draw_string_window(w, (uint32_t)x, (uint32_t)y, s, col);
    }
}

static void wm_text(int x, int y, const char* s, uint32_t col, int sz) {
    extern uint32_t vesa_width, vesa_height;
    if (!s || !s[0]) return;
    if (ttf_is_loaded()) {
        uint32_t* fb = vesa_get_backbuffer();
        ttf_draw_string(fb, (int)vesa_width, (int)vesa_height,
                        x, y + sz, s, (int)strlen(s), sz, col);
    } else {
        wm_draw_string((uint32_t)x, (uint32_t)y, s, col);
    }
}

static void wm_blur_region(int x, int y, int w, int h, int radius) {
    extern uint32_t vesa_width, vesa_height;
    uint32_t* fb = vesa_get_backbuffer();
    if (!fb || radius < 1) return;
    int vw = (int)vesa_width, vh = (int)vesa_height;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > vw) w = vw - x;
    if (y + h > vh) h = vh - y;
    if (w <= 0 || h <= 0) return;
    /* Guard: skip blur if region exceeds static buffer (1024×768 max) */
    if (w * h > 1024 * 768) return;
    static uint32_t _blur_tmp[1024 * 768];
    /* Horizontal pass */
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int rr = 0, gg = 0, bb = 0, cnt = 0;
            for (int k = -radius; k <= radius; k++) {
                int sc = col + k;
                if (sc < 0) sc = 0; if (sc >= w) sc = w - 1;
                uint32_t p = fb[(y + row) * vw + (x + sc)];
                rr += (p >> 16) & 0xFF; gg += (p >> 8) & 0xFF; bb += p & 0xFF; cnt++;
            }
            _blur_tmp[row * w + col] = ((rr/cnt) << 16) | ((gg/cnt) << 8) | (bb/cnt);
        }
    }
    /* Vertical pass */
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int rr = 0, gg = 0, bb = 0, cnt = 0;
            for (int k = -radius; k <= radius; k++) {
                int sr = row + k;
                if (sr < 0) sr = 0; if (sr >= h) sr = h - 1;
                uint32_t p = _blur_tmp[sr * w + col];
                rr += (p >> 16) & 0xFF; gg += (p >> 8) & 0xFF; bb += p & 0xFF; cnt++;
            }
            fb[(y + row) * vw + (x + col)] =
                ((rr/cnt) << 16) | ((gg/cnt) << 8) | (bb/cnt);
        }
    }
}

static void wm_glass_panel(int x, int y, int w, int h, int radius,
                            uint32_t tint, uint8_t tint_alpha) {
    wm_blur_region(x, y, w, h, 2);
    vesa_draw_rect_alpha((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                         tint, tint_alpha);
    for (int c = x + radius; c < x + w - radius; c++)
        vesa_putpixel_alpha((uint32_t)c, (uint32_t)y, 0xFFFFFF, 22);
    if (wm_nvg) {
        wm_nvg_ensure_frame();
        nvgBeginPath(wm_nvg);
        if (radius > 0)
            nvgRoundedRect(wm_nvg, (float)x+0.5f, (float)y+0.5f,
                           (float)w-1.0f, (float)h-1.0f, (float)radius);
        else
            nvgRect(wm_nvg, (float)x+0.5f, (float)y+0.5f,
                    (float)w-1.0f, (float)h-1.0f);
        nvgStrokeColor(wm_nvg, nvgRGBA(100, 130, 200, 55));
        nvgStrokeWidth(wm_nvg, 1.0f);
        nvgStroke(wm_nvg);
    }
}

static void wm_nvg_rect(int x, int y, int w, int h, int r,
                         uint32_t col, uint8_t alpha) {
    if (!wm_nvg) {
        widget_draw_rounded_rect(x, y, w, h, r, col, alpha);
        return;
    }
    wm_nvg_ensure_frame();
    nvgBeginPath(wm_nvg);
    if (r > 0)
        nvgRoundedRect(wm_nvg, (float)x, (float)y, (float)w, (float)h, (float)r);
    else
        nvgRect(wm_nvg, (float)x, (float)y, (float)w, (float)h);
    uint8_t rr = (col >> 16) & 0xFF, gg = (col >> 8) & 0xFF, bb = col & 0xFF;
    nvgFillColor(wm_nvg, nvgRGBA(rr, gg, bb, alpha));
    nvgFill(wm_nvg);
}

static void wm_nvg_arc_gauge(int cx, int cy, int r, int pct,
                              uint32_t col, const char* label, const char* val) {
    if (!wm_nvg) { wm_arc_gauge(cx, cy, r, pct, col, label, val); return; }
    wm_nvg_ensure_frame();
    float pi = 3.14159265f, sa = -pi / 2.0f;
    float sw = (float)(r / 3 > 4 ? r / 3 : 4);
    nvgBeginPath(wm_nvg);
    nvgArc(wm_nvg, (float)cx, (float)cy, (float)r, sa, sa + 2.0f*pi, NVG_CW);
    nvgStrokeColor(wm_nvg, nvgRGBA(35, 35, 53, 200));
    nvgStrokeWidth(wm_nvg, sw); nvgLineCap(wm_nvg, NVG_ROUND); nvgStroke(wm_nvg);
    if (pct > 0) {
        nvgBeginPath(wm_nvg);
        nvgArc(wm_nvg, (float)cx, (float)cy, (float)r,
               sa, sa + 2.0f*pi*(float)pct/100.0f, NVG_CW);
        uint8_t rr=(col>>16)&0xFF, gg=(col>>8)&0xFF, bb=col&0xFF;
        nvgStrokeColor(wm_nvg, nvgRGBA(rr, gg, bb, 240));
        nvgStrokeWidth(wm_nvg, sw); nvgLineCap(wm_nvg, NVG_ROUND); nvgStroke(wm_nvg);
    }
    /* flush before TTF text so text draws on top */
    wm_nvg_flush();
    if (val)   wm_text(cx-(int)(strlen(val)*4),   cy-7, val,   0xFFFFFF, 10);
    if (label) wm_text(cx-(int)(strlen(label)*4), cy+4, label, 0x7A8090,  9);
}

static void wm_shadow(int x, int y, int w, int h) {
    vesa_draw_rect_alpha((uint32_t)(x+6),(uint32_t)(y+6),(uint32_t)w,(uint32_t)h,0x000000,40);
    vesa_draw_rect_alpha((uint32_t)(x+4),(uint32_t)(y+4),(uint32_t)w,(uint32_t)h,0x000000,28);
    vesa_draw_rect_alpha((uint32_t)(x+2),(uint32_t)(y+2),(uint32_t)w,(uint32_t)h,0x000000,16);
}

static void wm_nvg_draw_icon(float cx, float cy, const char* label) {
    if (!wm_nvg) return;
    nvgBeginPath(wm_nvg);
    if (strcmp(label, "ElseaOS") == 0) {
        nvgCircle(wm_nvg, cx, cy, 12);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,200));
        nvgStrokeWidth(wm_nvg, 3); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgCircle(wm_nvg, cx, cy, 4);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,200)); nvgFill(wm_nvg);
    } else if (strcmp(label, "Files") == 0) {
        nvgRoundedRect(wm_nvg, cx - 10, cy - 8, 20, 16, 2);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
        nvgBeginPath(wm_nvg); nvgRoundedRect(wm_nvg, cx - 10, cy - 11, 8, 5, 1);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
    } else if (strcmp(label, "Browser") == 0) {
        nvgCircle(wm_nvg, cx, cy, 10);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgStrokeWidth(wm_nvg, 2); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgEllipse(wm_nvg, cx, cy, 4, 10); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx - 10, cy); nvgLineTo(wm_nvg, cx + 10, cy); nvgStroke(wm_nvg);
    } else if (strcmp(label, "Terminal") == 0) {
        nvgMoveTo(wm_nvg, cx - 8, cy - 6); nvgLineTo(wm_nvg, cx - 2, cy); nvgLineTo(wm_nvg, cx - 8, cy + 6);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgStrokeWidth(wm_nvg, 2); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx, cy + 6); nvgLineTo(wm_nvg, cx + 8, cy + 6); nvgStroke(wm_nvg);
    } else if (strcmp(label, "Notes") == 0) {
        nvgRoundedRect(wm_nvg, cx - 8, cy - 10, 16, 20, 2);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
        nvgBeginPath(wm_nvg);
        nvgMoveTo(wm_nvg, cx - 4, cy - 4); nvgLineTo(wm_nvg, cx + 4, cy - 4);
        nvgMoveTo(wm_nvg, cx - 4, cy);     nvgLineTo(wm_nvg, cx + 4, cy);
        nvgMoveTo(wm_nvg, cx - 4, cy + 4); nvgLineTo(wm_nvg, cx + 4, cy + 4);
        nvgStrokeColor(wm_nvg, nvgRGBA(150,150,150,255)); nvgStrokeWidth(wm_nvg, 1.5f); nvgStroke(wm_nvg);
    } else if (strcmp(label, "Music") == 0) {
        nvgCircle(wm_nvg, cx - 4, cy + 6, 4); nvgCircle(wm_nvg, cx + 6, cy + 4, 4);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx - 1, cy + 6); nvgLineTo(wm_nvg, cx - 1, cy - 6);
        nvgLineTo(wm_nvg, cx + 9, cy - 8); nvgLineTo(wm_nvg, cx + 9, cy + 4);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgStrokeWidth(wm_nvg, 2); nvgStroke(wm_nvg);
    } else if (strcmp(label, "Calendar") == 0) {
        nvgRoundedRect(wm_nvg, cx - 10, cy - 10, 20, 20, 3);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
        nvgBeginPath(wm_nvg); nvgRoundedRect(wm_nvg, cx - 10, cy - 10, 20, 6, 3);
        nvgFillColor(wm_nvg, nvgRGBA(200,50,50,220)); nvgFill(wm_nvg);
    } else if (strcmp(label, "Settings") == 0) {
        nvgCircle(wm_nvg, cx, cy, 6);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgStrokeWidth(wm_nvg, 4); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg);
        nvgMoveTo(wm_nvg, cx - 10, cy); nvgLineTo(wm_nvg, cx + 10, cy);
        nvgMoveTo(wm_nvg, cx, cy - 10); nvgLineTo(wm_nvg, cx, cy + 10);
        nvgMoveTo(wm_nvg, cx - 7, cy - 7); nvgLineTo(wm_nvg, cx + 7, cy + 7);
        nvgMoveTo(wm_nvg, cx - 7, cy + 7); nvgLineTo(wm_nvg, cx + 7, cy - 7);
        nvgStrokeWidth(wm_nvg, 3); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgCircle(wm_nvg, cx, cy, 4);
        nvgFillColor(wm_nvg, nvgRGBA(40,40,40,255)); nvgFill(wm_nvg);
    } else if (strcmp(label, "Store") == 0) {
        nvgRoundedRect(wm_nvg, cx - 9, cy - 4, 18, 14, 2);
        nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
        nvgBeginPath(wm_nvg); nvgArc(wm_nvg, cx, cy - 4, 5, 3.14159f, 0, NVG_CW);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgStrokeWidth(wm_nvg, 2); nvgStroke(wm_nvg);
    } else if (strcmp(label, "Trash") == 0) {
        nvgRoundedRect(wm_nvg, cx - 7, cy - 5, 14, 16, 1);
        nvgStrokeColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgStrokeWidth(wm_nvg, 2); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx - 10, cy - 5); nvgLineTo(wm_nvg, cx + 10, cy - 5); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx - 3, cy - 9); nvgLineTo(wm_nvg, cx + 3, cy - 9); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx - 3, cy - 1); nvgLineTo(wm_nvg, cx - 3, cy + 7); nvgStroke(wm_nvg);
        nvgBeginPath(wm_nvg); nvgMoveTo(wm_nvg, cx + 3, cy - 1); nvgLineTo(wm_nvg, cx + 3, cy + 7); nvgStroke(wm_nvg);
    } else if (strcmp(label, "Apps") == 0) {
        for (int gy = 0; gy < 3; gy++) {
            for (int gx = 0; gx < 3; gx++) {
                nvgBeginPath(wm_nvg);
                nvgRoundedRect(wm_nvg, cx - 8 + gx * 6, cy - 8 + gy * 6, 4, 4, 1);
                nvgFillColor(wm_nvg, nvgRGBA(255,255,255,220)); nvgFill(wm_nvg);
            }
        }
    } else {
        char lb[2] = { label[0], 0 };
        wm_text(cx - 5, cy - 6, lb, 0xFFFFFF, 16);
    }
}

/* ══════════════════════════════════════════════════════════════════════ */

/* ── helpers for dynamic calendar ─────────────────────────────── */
/* Returns 0=Sun..6=Sat for 1st day of month (Sakamoto's algorithm) */
static int wm_first_wday(int year, int month) {
    static const int t[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (month < 3) year--;
    return (year + year/4 - year/100 + year/400 + t[month-1] + 1) % 7;
}
static int wm_days_in_month(int year, int month) {
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int d = dm[month - 1];
    if (month == 2 && ((year%4==0 && year%100!=0) || year%400==0)) d = 29;
    return d;
}

/* CPU % — simulated from uptime + active window count (no hardware perf counter) */
static int wm_cpu_pct_cached  = 8;
static uint32_t wm_cpu_last_t = 0;
static int wm_get_cpu_pct(void) {
    uint32_t now = pit_get_ticks();
    if (now - wm_cpu_last_t >= 200) { /* update every 2 s */
        wm_cpu_last_t = now;
        int base  = 4 + num_windows * 3;            /* more windows → more CPU */
        int jitter = (int)((now / 137) % 14);       /* slow pseudo-noise */
        wm_cpu_pct_cached = base + jitter;
        if (wm_cpu_pct_cached > 92) wm_cpu_pct_cached = 92;
        if (wm_cpu_pct_cached <  2) wm_cpu_pct_cached  = 2;
    }
    return wm_cpu_pct_cached;
}

/* Disk % — stable estimate derived from uptime (no FS query in render path) */
static int wm_get_disk_pct(void) {
    /* 18 % base + small uptime drift, capped at 35 % to look realistic */
    uint32_t secs = pit_get_ticks() / 100;
    int d = 18 + (int)((secs / 60) % 18);
    return d;
}

static void wm_render_right_panel(void)
{
    if (!wm_widgets_enabled) return;   /* hidden until user enables in Settings */

    extern uint32_t vesa_width, vesa_height;
    extern uint32_t pmm_get_max_frames(void);
    extern uint32_t pmm_get_used_frames(void);

    int rw = 220;
    int rx = (int)vesa_width - rw;
    int ry = 32;
    int rh = (int)vesa_height - 32 - 80;

    /* Frosted glass right panel */
    wm_glass_panel(rx, ry, rw, rh, 0, 0x090A18, 210);
    /* Left border accent */
    for (int _py = ry; _py < ry + rh; _py++)
        vesa_putpixel_alpha((uint32_t)rx, (uint32_t)_py, 0x4466AA, 40);

    int y = ry + 10;

    /* ── Calendar (fully dynamic via RTC) ── */
    struct rtc_time rt;
    rtc_read(&rt);
    int cur_day   = (int)rt.day;
    int cur_month = (int)rt.month;
    int cur_year  = (int)rt.year;
    if (cur_year  < 2000) cur_year  += 2000;
    if (cur_month < 1 || cur_month > 12) cur_month = 1;
    if (cur_day   < 1)  cur_day  = 1;

    int dim        = wm_days_in_month(cur_year, cur_month);
    int start_wday = wm_first_wday(cur_year, cur_month);

    /* Month + year header */
    static const char* mnames[12] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    char cal_hdr[24];
    sprintf(cal_hdr, "%s %d", mnames[cur_month - 1], cur_year);
    wm_text(rx + 10, y, cal_hdr, 0xFFFFFF, 12);
    y += 18;

    /* Day-of-week headers */
    static const char* dh[7] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    for (int d = 0; d < 7; d++)
        wm_text(rx + 5 + d * 30, y, dh[d], 0x4466CC, 9);
    y += 14;

    /* Day grid */
    int day = 1;
    for (int row = 0; row < 6 && day <= dim; row++) {
        for (int col = 0; col < 7 && day <= dim; col++) {
            if (row == 0 && col < start_wday) continue;
            int nx = rx + 5 + col * 30;
            int ny = y + row * 16;
            char ds[4]; sprintf(ds, "%d", day);
            if (day == cur_day) {
                wm_nvg_rect(nx - 3, ny - 2, 22, 16, 4, 0x3584E4, 230);
                wm_text(nx, ny, ds, 0xFFFFFF, 10);
            } else {
                uint32_t dc = (day < cur_day) ? 0x555566 : 0xAAACBB;
                wm_text(nx, ny, ds, dc, 10);
            }
            day++;
        }
    }
    y += 6 * 16 + 2;

    /* Events */
    vesa_draw_rect_alpha((uint32_t)(rx + 6), (uint32_t)y,
                         (uint32_t)(rw - 12), 1, 0xFFFFFF, 18);
    y += 6;
    wm_text(rx + 10, y, "Events", 0xCCCCDD, 11);
    y += 15;

    static const struct { uint8_t hour; const char* label; uint32_t col; } evts[3] = {
        {  9, "Team Meeting",    0x3584E4 },
        { 14, "Project Review",  0x9B59B6 },
        { 19, "Dinner w/ Elsea", 0x1ABC9C },
    };
    int cur_hour = (int)rt.hour;
    for (int e = 0; e < 3; e++) {
        int is_now = (cur_hour == evts[e].hour);
        uint32_t tc = is_now ? 0xFFFFFF : 0x8899BB;
        /* Accent bar */
        vesa_draw_rect((uint32_t)(rx + 6), (uint32_t)(y + e * 20),
                       is_now ? 4 : 3, 13, evts[e].col);
        /* Time label */
        char tstr[10];
        int h12 = evts[e].hour > 12 ? evts[e].hour - 12 : evts[e].hour;
        const char* ampm = evts[e].hour >= 12 ? "PM" : "AM";
        sprintf(tstr, "%d:00 %s", h12, ampm);
        wm_text(rx + 12, y + e * 22,      tstr,          tc,                             9);
        wm_text(rx + 12, y + e * 22 + 10, evts[e].label, is_now ? 0xFFFFEE : 0xCCCCDD, 9);
        if (is_now)
            wm_text(rx + rw - 32, y + e * 22 + 2, "Now", evts[e].col, 9);
    }
    y += 3 * 20 + 4;

    /* ── Elsea AI ── */
    vesa_draw_rect_alpha((uint32_t)(rx + 6), (uint32_t)y,
                         (uint32_t)(rw - 12), 1, 0xFFFFFF, 18);
    y += 8;
    wm_text(rx + 10, y, "Elsea AI", 0xFFFFFF, 12);
    y += 18;

    /* AI bubble */
    wm_nvg_rect(rx + 6, y, rw - 12, 46, 8, 0x131423, 240);
    /* Blue dot avatar */
    wm_nvg_rect(rx + 12, y + 10, 16, 16, 8, 0x3584E4, 220);
    wm_text(rx + 15, y + 12, "E", 0xFFFFFF, 10);
    wm_text(rx + 32, y + 8,  "Hello! How can I", 0xCCCCDD, 9);
    wm_text(rx + 32, y + 20, "help you today?",  0xCCCCDD, 9);
    wm_text(rx + 32, y + 32, "-- Elsea",         0x3584E4, 8);
    y += 54;

    /* AI input field */
    wm_nvg_rect(rx + 6, y, rw - 12, 28, 8,
                ai_input_focused ? 0x1E2845 : 0x151626, 240);
    /* border glow when focused */
    if (ai_input_focused && wm_nvg) {
        wm_nvg_ensure_frame();
        nvgBeginPath(wm_nvg);
        nvgRoundedRect(wm_nvg, (float)(rx+6)+0.5f, (float)y+0.5f,
                       (float)(rw-12)-1.0f, 27.0f, 8.0f);
        nvgStrokeColor(wm_nvg, nvgRGBA(53, 132, 228, 180));
        nvgStrokeWidth(wm_nvg, 1.5f);
        nvgStroke(wm_nvg);

    }
    {
        const char* ph = ai_input_len ? ai_input_buf : "Ask Elsea...";
        uint32_t phc  = ai_input_len ? 0xFFFFFF : 0x3D5577;
        wm_text(rx + 14, y + 8, ph, phc, 10);
        if (ai_input_focused && (pit_get_ticks() % 60 < 30)) {
            int cx = rx + 14 + ai_input_len * 6;
            vesa_draw_rect_alpha((uint32_t)cx, (uint32_t)(y + 8), 2, 12, 0x3584E4, 220);
        }
    }
    y += 36;

    /* ── System Monitor (live charts) ── */
    vesa_draw_rect_alpha((uint32_t)(rx + 6), (uint32_t)y,
                         (uint32_t)(rw - 12), 1, 0xFFFFFF, 18);
    y += 8;
    wm_text(rx + 10, y, "System Monitor", 0xFFFFFF, 12);
    y += 18;

    int n_samples = (mh_count >= 2) ? mh_count : 2;
    int sl_x = rx + 6, sl_w = rw - 12;
    int sl_h = 30;   /* sparkline height */
    char valstr[10];

    /* ── CPU ── */
    {
        int cv = mh_cpu[(mh_pos + MH_LEN - 1) % MH_LEN];
        sprintf(valstr, "CPU  %d%%", cv);
        wm_text(rx + 10, y, valstr, 0x3584E4, 10);
        wm_nvg_arc_gauge(rx + rw - 26, y + 16, 14, cv, 0x3584E4, NULL, NULL);
        y += 16;
        wm_nvg_rect(sl_x, y, sl_w, sl_h, 4, 0x06080E, 240);
        wm_sline_screen(sl_x + 2, y + 2, sl_w - 40, sl_h - 4,
                        mh_cpu, mh_pos, n_samples, 0x3584E4, 0x1A3A6A);
        y += sl_h + 5;
    }

    /* ── RAM ── */
    {
        uint32_t mf = pmm_get_max_frames(), uf = pmm_get_used_frames();
        int rv = (mf > 0) ? (int)(uf * 100 / mf) : mh_ram[(mh_pos+MH_LEN-1)%MH_LEN];
        if (rv > 100) rv = 100;
        sprintf(valstr, "RAM  %d%%", rv);
        wm_text(rx + 10, y, valstr, 0x9B59B6, 10);
        wm_nvg_arc_gauge(rx + rw - 26, y + 16, 14, rv, 0x9B59B6, NULL, NULL);
        y += 16;
        wm_nvg_rect(sl_x, y, sl_w, sl_h, 4, 0x06080E, 240);
        wm_sline_screen(sl_x + 2, y + 2, sl_w - 40, sl_h - 4,
                        mh_ram, mh_pos, n_samples, 0x9B59B6, 0x3A1A6A);
        y += sl_h + 5;
    }

    /* ── Network RX/TX dual sparkline ── */
    {
        int rx_v = mh_netrx[(mh_pos + MH_LEN - 1) % MH_LEN];
        int tx_v = mh_nettx[(mh_pos + MH_LEN - 1) % MH_LEN];
        sprintf(valstr, "Net RX%d TX%d", rx_v, tx_v);
        wm_text(rx + 10, y, valstr, 0x1ABC9C, 10);
        y += 14;
        wm_nvg_rect(sl_x, y, sl_w, sl_h + 4, 4, 0x06080E, 240);
        wm_sline_screen(sl_x + 2, y + 2, sl_w - 4, sl_h,
                        mh_netrx, mh_pos, n_samples, 0x1ABC9C, 0x0A3A2A);
        wm_sline_screen(sl_x + 2, y + 2, sl_w - 4, sl_h,
                        mh_nettx, mh_pos, n_samples, 0xE67E22, 0x3A1A00);
        y += sl_h + 8;
    }

    /* ── Disk I/O ── */
    {
        int dv = mh_disk[(mh_pos + MH_LEN - 1) % MH_LEN];
        sprintf(valstr, "Disk %d%%", dv);
        wm_text(rx + 10, y, valstr, 0xE74C3C, 10);
        wm_nvg_arc_gauge(rx + rw - 26, y + 16, 14, dv, 0xE74C3C, NULL, NULL);
        y += 16;
        wm_nvg_rect(sl_x, y, sl_w, sl_h, 4, 0x06080E, 240);
        wm_sline_screen(sl_x + 2, y + 2, sl_w - 40, sl_h - 4,
                        mh_disk, mh_pos, n_samples, 0xE74C3C, 0x3A0A08);
        y += sl_h + 4;
    }

    (void)y;
}

/* ─── App launcher overlay ─────────────────────────────────────── */
static const char* lcat_names[9] = {
    "Favorites", "All Apps", "Development", "Graphics",
    "Internet", "Multimedia", "Office", "System", "Power"
};

static void wm_render_launcher(void)
{
    extern uint32_t vesa_width, vesa_height;

    /* Blurred dim overlay */
    wm_blur_region(0, 0, (int)vesa_width, (int)vesa_height, 1);
    vesa_draw_rect_alpha(0, 0, vesa_width, vesa_height, 0x000010, 140);

    int lw = ((int)vesa_width > 940) ? 920 : (int)vesa_width - 40;
    int lh = 530;
    int lx = ((int)vesa_width  - lw) / 2;
    int ly = ((int)vesa_height - lh) / 2 - 20;

    /* Glass launcher panel */
    wm_glass_panel(lx, ly, lw, lh, 12, 0x0C0D1A, 245);

    /* Top blue accent stripe */
    wm_nvg_rect(lx, ly, lw, 4, 0, 0x3584E4, 200);

    /* User avatar circle */
    wm_nvg_rect(lx + 16, ly + 14, 40, 40, 20, 0x223355, 255);
    wm_text(lx + 26, ly + 21, "A", 0xFFFFFF, 16);
    wm_text(lx + 64, ly + 16, "Admin User",    0xFFFFFF, 12);
    wm_text(lx + 64, ly + 30, "admin@elseaos", 0x5577AA, 10);

    /* Close button */
    wm_nvg_rect(lx + lw - 28, ly + 10, 20, 20, 10, 0x2A2B40, 200);
    wm_text(lx + lw - 23, ly + 12, "X", 0x888899, 11);

    /* Search bar */
    wm_nvg_rect(lx + 14, ly + 60, lw - 28, 32, 10, 0x14152A, 255);
    /* search icon circle */
    wm_nvg_rect(lx + 26, ly + 70, 12, 12, 6, 0x3A4060, 200);
    wm_text(lx + 44, ly + 64, "Search apps and files...", 0x445577, 11);

    /* Category panel */
    int cat_w = 168;
    for (int i = 0; i < 9; i++) {
        int cy2 = ly + 104 + i * 36;
        if (i == launcher_cat) {
            wm_nvg_rect(lx + 8, cy2 + 2, cat_w - 10, 28, 7, 0x3584E4, 190);
            wm_text(lx + 20, cy2 + 8, lcat_names[i], 0xFFFFFF, 11);
        } else {
            wm_text(lx + 20, cy2 + 8, lcat_names[i], 0x8899BB, 11);
        }
    }

    /* Vertical divider */
    for (int _dy = ly + 100; _dy < ly + lh - 10; _dy++)
        vesa_putpixel_alpha((uint32_t)(lx + cat_w + 6), (uint32_t)_dy, 0x6688CC, 25);

    /* Apps area */
    int ax0 = lx + cat_w + 16;
    int aw  = lw - cat_w - 24;
    wm_text(ax0 + 4, ly + 103, "Pinned Apps", 0xCCCCEE, 11);
    wm_text(ax0 + aw - 36, ly + 103, "Edit", 0x3584E4, 11);

    /* Horizontal divider under section header */
    vesa_draw_rect_alpha((uint32_t)(ax0), (uint32_t)(ly + 118),
                         (uint32_t)aw, 1, 0x3584E4, 40);

    static const char* pnames[20] = {
        "Browser","Files","Terminal","Settings",
        "Calendar","Notes","Music","Image View",
        "Calculator","Clock","App Store","Elsea AI",
        "Spreadsheet","Sys Monitor","Weather","Disk Util",
        "Charmap","Font View","Snake","Video"
    };
    static const uint32_t pcolors[20] = {
        0xE87722, 0x3D88C8, 0x1ABC9C, 0x7F8C8D,
        0x9B59B6, 0xDFAD20, 0xE74C3C, 0x3584E4,
        0x27AE60, 0xF39C12, 0x2980B9, 0x3584E4,
        0x16A085, 0x8E44AD, 0x2471A3, 0xCB4335,
        0x117A65, 0xD35400, 0x1E8449, 0x7D3C98,
    };
    static const int pids[20] = { 0,0,2,3, 3,1,3,0, 3,3,0,3, 3,3,0,1, 3,3,0,3 };
    uint32_t* pbufs[4] = { icon_expl_buf, icon_pnt_buf, icon_term_buf, icon_sett_buf };

    int pw_slot = aw / 4;
    int ph_slot = 78;
    int row0 = ly + 126;
    int pmx = mouse_get_x(), pmy = mouse_get_y();

    for (int i = 0; i < 20; i++) {
        int col = i % 4;
        int row = i / 4;
        int px  = ax0 + col * pw_slot + (pw_slot - 56) / 2;
        int py  = row0 + row * ph_slot;

        int phov = (pmx >= ax0 + col * pw_slot &&
                    pmx <  ax0 + (col + 1) * pw_slot &&
                    pmy >= py && pmy < py + ph_slot);

        /* Hover highlight */
        if (phov) wm_nvg_rect(px - 4, py - 2, 72, 82, 12, 0x2A2B45, 200);

        /* Icon background circle */
        wm_nvg_rect(px + 2, py + 2, 52, 52, 12, pcolors[i], 220);

        /* BMP icon scaled into 22×22 */
        uint32_t* ib = pbufs[pids[i]];
        if (ib) {
            for (int yy = 0; yy < 22; yy++)
                for (int xx = 0; xx < 22; xx++) {
                    uint32_t pc = ib[(yy * 32 / 22) * 32 + (xx * 32 / 22)];
                    if (pc) vesa_putpixel_alpha((uint32_t)(px + 17 + xx),
                                               (uint32_t)(py + 17 + yy), pc, 220);
                }
        }

        /* App name — TTF centered */
        int nl = (int)strlen(pnames[i]);
        int nlx = px + 28 - nl * 4;
        if (nlx < ax0) nlx = ax0;
        wm_text(nlx, py + 56, pnames[i], phov ? 0xFFFFFF : 0xBBBBDD, 9);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  KDE PLASMA — full-width bottom panel + taskbar
 * ═══════════════════════════════════════════════════════════════════ */
static void wm_render_kde_panel(void) {
    int ph = 42, py = (int)vesa_height - ph;
    /* glass panel */
    wm_blur_region(0, py, (int)vesa_width, ph, 2);
    vesa_draw_rect_alpha(0, (uint32_t)py, vesa_width, (uint32_t)ph, 0x12152A, 235);
    /* top separator */
    for (int _x = 0; _x < (int)vesa_width; _x++)
        vesa_putpixel_alpha((uint32_t)_x, (uint32_t)py, 0x3584E4, 50);

    /* LEFT: KDE launcher button */
    int lbw = 44;
    int lhov = (mouse_get_x() >= 4 && mouse_get_x() < 4+lbw &&
                mouse_get_y() >= py+4 && mouse_get_y() < py+ph-4);
    wm_nvg_rect(4, py+5, lbw, ph-10, 8,
                kde_launcher_open ? 0x3584E4 : (lhov ? 0x2A3A6E : 0x1A2050), 230);
    wm_text(14, py+14, "[K]", kde_launcher_open ? 0xFFFFFF : 0x88AAFF, 12);

    /* CENTER: Taskbar — one pill per open window on current desktop */
    int tx0 = lbw + 10, tx_right = (int)vesa_width - 210;
    int tw_max = 150, tw_gap = 4;
    /* count visible */
    int vis = 0;
    for (int i = 0; i < num_windows; i++)
        if (windows[i].active && !windows[i].minimized &&
            windows[i].desktop_id == current_desktop) vis++;
    int tw = vis > 0 ? (tx_right - tx0) / vis - tw_gap : tw_max;
    if (tw > tw_max) tw = tw_max;
    if (tw < 40)     tw = 40;
    int shown = 0;
    for (int i = 0; i < num_windows; i++) {
        if (!windows[i].active || windows[i].minimized ||
            windows[i].desktop_id != current_desktop) continue;
        int bx = tx0 + shown * (tw + tw_gap);
        int focused = (&windows[i] == focused_window);
        int thov = (mouse_get_x() >= bx && mouse_get_x() < bx+tw &&
                    mouse_get_y() >= py+5 && mouse_get_y() < py+ph-5);
        wm_nvg_rect(bx, py+5, tw, ph-10, 5,
                    focused ? 0x1E3A70 : (thov ? 0x1A2A50 : 0x131828), 220);
        if (focused)
            vesa_draw_rect_alpha((uint32_t)bx, (uint32_t)(py+ph-4),
                                 (uint32_t)tw, 3, 0x3584E4, 220);
        /* truncate title */
        char tit[18]; int tl = 0;
        while (windows[i].title[tl] && tl < 16) { tit[tl] = windows[i].title[tl]; tl++; }
        tit[tl] = 0;
        wm_text(bx + 8, py + 15, tit, focused ? 0xFFFFFF : 0x8899BB, 10);
        shown++;
    }

    /* RIGHT: system tray */
    int ri = (int)vesa_width - 6;
    /* Clock */
    ri -= 90;
    wm_nvg_rect(ri, py+5, 88, ph-10, 5, 0x12182E, 220);
    wm_text(ri + 8, py + 14, clock_str, 0xDDEEFF, 11);
    /* Volume */
    ri -= 46;
    { extern uint8_t master_volume;
      wm_nvg_rect(ri, py+5, 44, ph-10, 5, 0x111525, 220);
      int vf = (master_volume * 32) / 255;
      if (vf > 0)
          vesa_draw_rect_alpha((uint32_t)(ri+4), (uint32_t)(py+20),
                               (uint32_t)vf, 5, 0x3584E4, 200);
      wm_text(ri+5, py+14, "VOL", 0x8899AA, 10); }
    /* WiFi */
    ri -= 46;
    { int wfc = wifi_is_connected();
      wm_nvg_rect(ri, py+5, 44, ph-10, 5, 0x111525, 220);
      wm_text(ri+4, py+14, "WiFi", wfc ? 0x3584E4 : 0x3A4455, 10); }
    /* Notif */
    ri -= 36;
    { wm_nvg_rect(ri, py+5, 34, ph-10, 5,
                  notif_count > 0 ? 0x3A1800 : 0x111525, 220);
      wm_text(ri+5, py+14, notif_count>0?"(!)":"[-]",
              notif_count>0 ? 0xFFAA33 : 0x556677, 10); }
    /* Desktop indicators */
    ri -= 4;
    for (int _d = NUM_DESKTOPS-1; _d >= 0; _d--) {
        ri -= 16;
        uint32_t dc = (_d == current_desktop) ? 0x3584E4 : 0x222B44;
        wm_nvg_rect(ri+2, py+15, 10, 10, 5, dc, 200);
    }
}

/* KDE App Launcher grid overlay */
static void wm_render_kde_launcher(void) {
    int lw = 380, lh = 460;
    int lx = 4, ly = (int)vesa_height - 42 - lh - 6;
    wm_blur_region(lx, ly, lw, lh, 3);
    wm_nvg_rect(lx, ly, lw, lh, 14, 0x0C1020, 248);
    /* Border */
    if (wm_nvg) {
        wm_nvg_ensure_frame();
        nvgBeginPath(wm_nvg);
        nvgRoundedRect(wm_nvg, (float)lx+0.5f, (float)ly+0.5f,
                       (float)lw-1, (float)lh-1, 14);
        nvgStrokeColor(wm_nvg, nvgRGBA(60, 90, 180, 80));
        nvgStrokeWidth(wm_nvg, 1.2f);
        nvgStroke(wm_nvg);
    }
    /* Header */
    wm_text(lx+16, ly+14, "Application Launcher", 0x88AADD, 12);
    /* Search bar */
    wm_nvg_rect(lx+10, ly+36, lw-20, 28, 6, 0x14203A, 230);
    wm_text(lx+18, ly+44, "Search applications...", 0x445566, 10);
    /* Separator */
    vesa_draw_rect_alpha((uint32_t)(lx+10), (uint32_t)(ly+68), (uint32_t)(lw-20), 1, 0x223366, 120);

    /* App grid — same apps as existing launcher */
    static const struct { const char* name; uint32_t col; char letter; } apps[] = {
        {"Files",      0x3D88C8, 'F'}, {"Browser",   0xE87722, 'B'},
        {"Terminal",   0x1ABC9C, 'T'}, {"Settings",  0x7F8C8D, 'S'},
        {"Music",      0xE74C3C, 'M'}, {"Calendar",  0x9B59B6, 'C'},
        {"Store",      0x27AE60, 'A'}, {"Notes",     0xDFAD20, 'N'},
        {"Image View", 0x3584E4, 'I'}, {"Calc",      0xAA6622, 'X'},
        {"PDF",        0xCC2222, 'P'}, {"Bluetooth", 0x0088CC, 'B'},
    };
    int cols = 4, rows = 3;
    int cw = (lw - 20) / cols, rh = (lh - 80) / rows;
    int mx = mouse_get_x(), my = mouse_get_y();
    for (int i = 0; i < 12; i++) {
        int col = i % cols, row = i / cols;
        int ax = lx + 10 + col * cw, ay = ly + 74 + row * rh;
        int hov = (mx>=ax && mx<ax+cw-6 && my>=ay && my<ay+rh-6);
        wm_nvg_rect(ax+2, ay+2, cw-8, rh-8, 8,
                    hov ? 0x1E3060 : 0x121A30, hov ? 220 : 170);
        /* Icon circle */
        wm_nvg_rect(ax+cw/2-18, ay+8, 36, 36, 18, apps[i].col, 220);
        char ll[2] = {apps[i].letter, 0};
        wm_text(ax+cw/2-4, ay+18, ll, 0xFFFFFF, 14);
        /* Label */
        int nl = (int)strlen(apps[i].name);
        wm_text(ax+cw/2-nl*4, ay+48, apps[i].name, hov?0xFFFFFF:0xAABBCC, 9);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  GNOME SHELL — modified top bar + Activities overview
 * ═══════════════════════════════════════════════════════════════════ */
static void wm_render_gnome_topbar(void) {
    int tbh = 32;
    wm_blur_region(0, 0, (int)vesa_width, tbh, 2);
    vesa_draw_rect_alpha(0, 0, vesa_width, (uint32_t)tbh, 0x0E1020, 210);
    for (int _c = 0; _c < (int)vesa_width; _c++)
        vesa_putpixel_alpha((uint32_t)_c, (uint32_t)(tbh-1), 0x3584E4, 25);

    /* LEFT: Activities button */
    int abhov = (mouse_get_x() >= 4 && mouse_get_x() < 84 &&
                 mouse_get_y() >= 3 && mouse_get_y() < tbh-3);
    wm_nvg_rect(4, 3, 80, tbh-6, 5,
                activities_open ? 0x3584E4 : (abhov ? 0x1E3060 : 0x151A2E), 220);
    wm_text(14, 10, "Activities", activities_open ? 0xFFFFFF : 0xCCDDFF, 11);
    if (activities_open)
        vesa_draw_rect_alpha(4, (uint32_t)(tbh-3), 80, 3, 0x3584E4, 200);

    /* CENTER-LEFT: focused app name */
    if (focused_window && !activities_open) {
        wm_text(96, 10, focused_window->title, 0xBBCCDD, 11);
    }

    /* CENTER: clock */
    { int cw = (int)strlen(clock_str) * 7;
      int cx = ((int)vesa_width - cw) / 2;
      wm_text(cx, 10, clock_str, 0xEEEEFF, 12); }

    /* RIGHT: quick settings pill */
    int ri = (int)vesa_width - 6;
    ri -= 44; /* Power */
    wm_nvg_rect(ri, 5, 42, tbh-10, 5, 0x1A1024, 210);
    wm_text(ri+8, 10, "PWR", 0xFF6677, 10);
    ri -= 46; /* Volume */
    { extern uint8_t master_volume;
      wm_nvg_rect(ri, 5, 44, tbh-10, 5, 0x141525, 210);
      int vf = (master_volume*30)/255;
      if(vf>0) vesa_draw_rect_alpha((uint32_t)(ri+4),(uint32_t)12,(uint32_t)vf,5,0x3584E4,200);
      wm_text(ri+5, 10, "VOL", 0x99AABB, 10); }
    ri -= 46; /* WiFi */
    { int wfc = wifi_is_connected();
      wm_nvg_rect(ri, 5, 44, tbh-10, 5, 0x141525, 210);
      wm_text(ri+4, 10, "WiFi", wfc?0x3584E4:0x445566, 10); }
    /* Desktop dots */
    ri -= 4;
    for (int _d = NUM_DESKTOPS-1; _d >= 0; _d--) {
        ri -= 16;
        uint32_t dc=(_d==current_desktop)?0x3584E4:0x222233;
        wm_nvg_rect(ri+2, 11, 10, 10, 5, dc, 200);
    }
}

/* GNOME Activities Overview */
static void wm_render_activities_overview(void) {
    /* Dark overlay */
    vesa_draw_rect_alpha(0, 32, vesa_width, vesa_height - 32, 0x0A0E1A, 210);

    /* Search bar */
    int sbw = 380, sbh = 34;
    int sbx = ((int)vesa_width - sbw) / 2, sby = 48;
    wm_nvg_rect(sbx, sby, sbw, sbh, 8, 0x1A2540, 240);
    if (wm_nvg) {
        wm_nvg_ensure_frame();
        nvgBeginPath(wm_nvg);
        nvgRoundedRect(wm_nvg,(float)sbx+.5f,(float)sby+.5f,(float)sbw-1,(float)sbh-1,8);
        nvgStrokeColor(wm_nvg, nvgRGBA(80,120,200,100));
        nvgStrokeWidth(wm_nvg,1.2f); nvgStroke(wm_nvg);
    }
    wm_text(sbx+14, sby+11, "Type to search...", 0x445566, 11);

    /* Window thumbnail cards */
    int vis = 0;
    for (int i = 0; i < num_windows; i++)
        if (windows[i].active && !windows[i].minimized &&
            windows[i].desktop_id == current_desktop) vis++;

    int thw = 200, thh = 130, thgap = 20;
    int total_thw = vis * (thw + thgap) - thgap;
    int thx0 = ((int)vesa_width - 100 - total_thw) / 2; /* shift left for workspace strip */
    if (thx0 < 10) thx0 = 10;
    int shown = 0;
    int mx = mouse_get_x(), my = mouse_get_y();
    for (int i = 0; i < num_windows; i++) {
        if (!windows[i].active || windows[i].minimized ||
            windows[i].desktop_id != current_desktop) continue;
        int tx = thx0 + shown * (thw + thgap);
        int ty = 100;
        int hov = (mx>=tx && mx<tx+thw && my>=ty && my<ty+thh);
        /* Card */
        wm_nvg_rect(tx, ty, thw, thh, 8, hov ? 0x1E3060 : 0x141A2E, 230);
        /* Title bar strip */
        vesa_draw_rect_alpha((uint32_t)tx, (uint32_t)ty, (uint32_t)thw, 22,
                             current_theme.title_bg, 220);
        /* Close dot */
        wm_nvg_rect(tx+thw-18, ty+5, 12, 12, 6, 0xCC3333, 200);
        wm_text(tx+thw-15, ty+6, "x", 0xFFFFFF, 9);
        /* Title */
        char tit[22]; int tl=0;
        while(windows[i].title[tl]&&tl<20){tit[tl]=windows[i].title[tl];tl++;} tit[tl]=0;
        wm_text(tx+8, ty+7, tit, 0xFFFFFF, 9);
        /* Content placeholder lines */
        for (int row = 26; row < thh-6; row += 6)
            vesa_draw_rect_alpha((uint32_t)(tx+6),(uint32_t)(ty+row),
                                 (uint32_t)(thw-12),3,0x12172B,180);
        /* Hover glow border */
        if (hov && wm_nvg) {
            wm_nvg_ensure_frame();
            nvgBeginPath(wm_nvg);
            nvgRoundedRect(wm_nvg,(float)tx+.5f,(float)ty+.5f,(float)thw-1,(float)thh-1,8);
            nvgStrokeColor(wm_nvg,nvgRGBA(80,140,255,160));
            nvgStrokeWidth(wm_nvg,2.0f); nvgStroke(wm_nvg);
        }
        shown++;
    }

    /* App icon grid below thumbnails */
    static const struct { const char* n; uint32_t c; } oa[] = {
        {"Files",0x3D88C8},{"Browser",0xE87722},{"Terminal",0x1ABC9C},
        {"Settings",0x7F8C8D},{"Music",0xE74C3C},{"Calendar",0x9B59B6},
        {"Store",0x27AE60},{"Notes",0xDFAD20},{"Calc",0xAA6622},
    };
    int agrid_y = 250, asz = 54, agap = 16;
    int agrid_total = 9 * (asz + agap) - agap;
    int agx0 = ((int)vesa_width - 100 - agrid_total) / 2;
    for (int i = 0; i < 9; i++) {
        int ax = agx0 + i*(asz+agap), ay = agrid_y;
        int ahov = (mx>=ax&&mx<ax+asz&&my>=ay&&my<ay+asz+18);
        wm_nvg_rect(ax, ay, asz, asz, asz/2, oa[i].c, ahov?240:190);
        int nl=(int)strlen(oa[i].n);
        wm_text(ax+asz/2-nl*4, ay+asz+4, oa[i].n, ahov?0xFFFFFF:0xAABBCC, 9);
    }

    /* Workspace strip — right side */
    int wsx = (int)vesa_width - 96, wsy = 48;
    int wsw = 84, wsh = 64, wsgap = 10;
    wm_text(wsx+8, wsy-16, "Workspaces", 0x8899BB, 10);
    for (int d = 0; d < NUM_DESKTOPS; d++) {
        int wy = wsy + d*(wsh+wsgap);
        int wfoc = (d == current_desktop);
        wm_nvg_rect(wsx, wy, wsw, wsh, 6, wfoc?0x1E3A70:0x0E1428, 220);
        if (wfoc && wm_nvg) {
            wm_nvg_ensure_frame();
            nvgBeginPath(wm_nvg);
            nvgRoundedRect(wm_nvg,(float)wsx+.5f,(float)wy+.5f,(float)wsw-1,(float)wsh-1,6);
            nvgStrokeColor(wm_nvg,nvgRGBA(53,132,228,200));
            nvgStrokeWidth(wm_nvg,1.5f); nvgStroke(wm_nvg);
        }
        char ds[3] = {'W','S','1'+d}; ds[2]=0;
        wm_text(wsx+wsw/2-12, wy+wsh/2-6, ds, wfoc?0x88BBFF:0x445566, 11);
    }
    /* "New workspace" button */
    int nwy = wsy + NUM_DESKTOPS*(wsh+wsgap);
    wm_nvg_rect(wsx, nwy, wsw, 22, 5, 0x0E1828, 180);
    wm_text(wsx+28, nwy+6, "+ WS", 0x445566, 10);
}

/* ═══════════════════════════════════════════════════════════════════ */

static void wm_render(void) {
    extern int sdl_app_active;
    if (sdl_app_active) return;

    /* Collect live metrics once per second */
    wm_metrics_tick();

    /* Sync NanoVG canvas pointer to current backbuffer each frame */
    if (wm_nvg)
        nvg_elseaos_set_canvas(wm_nvg, vesa_get_backbuffer(),
                               (int)vesa_width, (int)vesa_height);

    extern uint32_t vesa_width, vesa_height;

    // 1. Draw Desktop Background
    extern void vesa_draw_desktop_bg(uint32_t*);
    if (desktop_bg_buffer) {
        vesa_draw_desktop_bg(desktop_bg_buffer);
    } else {
        vesa_clear(0x008080);
    }
    
    // Check Authentication
    if (!login_is_authenticated()) {
        // Draw the login window as a glass pane
        for (int i = 0; i < num_windows; i++) {
            if (strncmp(windows[i].title, "Login - ElseaOS", 15) == 0) {
                window_t* w = &windows[i];
                int gx = w->x;
                int gy = w->y;
                int gw = w->w;
                int gh = w->h + 20;

                // Subtle shadow
                vesa_draw_rect_alpha(gx + 10, gy + 10, gw, gh, 0x000000, 100);
                
                // Glass panel
                widget_draw_glass(gx, gy, gw, gh, 0x111122, 180, 2);
                
                // Header text
                wm_draw_string(gx + (gw / 2) - 36, gy + 15, "ElseaOS", 0xFFFFFF);
                vesa_draw_rect_alpha(gx + 20, gy + 40, gw - 40, 1, 0xFFFFFF, 50);
            }
        }
        
        // Pump logic (draws inputs/buttons directly to screen over the glass pane)
        login_pump();

        // 7. Draw Mouse (needed since we early return)
        int mx = mouse_get_x();
        int my = mouse_get_y();
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 10; x++) {
                if (cursor_bitmap[y][x] == 1) {
                    vesa_putpixel_alpha(mx + x, my + y, 0x000000, 255);
                } else if (cursor_bitmap[y][x] == 2) {
                    vesa_putpixel_alpha(mx + x, my + y, 0xFFFFFF, 255);
                }
            }
        }

        vesa_swap_buffers();
        return;
    }
    
    // Desktop icons hidden — GNOME default has no desktop icons

    // 1.5 Center Desktop Branding
    {
        int cbx = 72 + ((int)vesa_width - 72 - 220) / 2;
        int cby = (int)vesa_height / 2 - 20;
        /* Outer glow ring */
        wm_nvg_rect(cbx - 42, cby - 62, 84, 84, 42, 0x3584E4, 50);
        /* Ring */
        wm_nvg_rect(cbx - 36, cby - 56, 72, 72, 36, 0x3584E4, 100);
        /* Inner dark fill */
        wm_nvg_rect(cbx - 28, cby - 48, 56, 56, 28, 0x06080F, 240);
        /* E letter — TTF */
        wm_text(cbx - 7, cby - 38, "E", 0x3584E4, 24);
        /* Title — large TTF */
        wm_text(cbx - 38, cby + 18, "ElseaOS", 0xBBCCEE, 18);
        /* Tagline — small TTF */
        wm_text(cbx - 110, cby + 42, "Intelligent. Elegant. Elsea.", 0x3A5070, 11);
    }

    // 1.8 Update live System Monitor windows + Video Player tick + Snake tick
    for (int _si = 0; _si < num_windows; _si++) {
        window_t* _sw = &windows[_si];
        if (!_sw->active || _sw->minimized || _sw->desktop_id != current_desktop) continue;
        if (strncmp(_sw->title, "System Monitor", 14) == 0)
            wm_update_system_monitor(_sw);
        if (strncmp(_sw->title, "Video Player", 12) == 0) {
            extern int video_player_tick(window_t*);
            if (video_player_tick(_sw)) redraw_needed = 1;
        }
        if (strncmp(_sw->title, "Snake", 5) == 0) {
            extern void snake_tick(void);
            snake_tick();
            redraw_needed = 1;
        }
    }

    // 2. Draw Windows (current desktop only)
    for (int i = 0; i < num_windows; i++) {
        window_t* w = &windows[i];
        if (!w->active || w->minimized) continue;
        if (w->desktop_id != current_desktop) continue;
        
        // Handle Animations
        if (w->anim_state == 1) { // Opening
            w->anim_progress += 0.2f;
            if (w->anim_progress >= 1.0f) {
                w->anim_progress = 1.0f;
                w->anim_state = 0;
            }
            redraw_needed = 1;
        } else if (w->anim_state == 2) { // Closing
            w->anim_progress -= 0.2f;
            if (w->anim_progress <= 0.0f) {
                w->active = 0;
                continue;
            }
            redraw_needed = 1;
        }
        
        // Draw animation wireframe instead of window if animating
        if (w->anim_state != 0) {
            int cx = w->x + w->w / 2;
            int cy = w->y + w->h / 2;
            int cur_w = (int)(w->w * w->anim_progress);
            int cur_h = (int)(w->h * w->anim_progress);
            int cur_x = cx - cur_w / 2;
            int cur_y = cy - cur_h / 2;
            vesa_draw_rect(cur_x, cur_y, cur_w, 2, current_theme.window_border);
            vesa_draw_rect(cur_x, cur_y + cur_h - 2, cur_w, 2, current_theme.window_border);
            vesa_draw_rect(cur_x, cur_y, 2, cur_h, current_theme.window_border);
            vesa_draw_rect(cur_x + cur_w - 2, cur_y, 2, cur_h, current_theme.window_border);
            continue;
        }

        /* ── Multi-layer soft shadow ─────────────────────────────────── */
        wm_shadow((int)w->x, (int)w->y, (int)w->w, (int)(w->h + 22));

        /* ── Window border / focus glow ──────────────────────────────── */
        if (wm_nvg) {
        wm_nvg_ensure_frame();
            nvgBeginPath(wm_nvg);
            nvgRoundedRect(wm_nvg, (float)w->x - 0.5f, (float)w->y - 0.5f,
                           (float)w->w + 1.0f, (float)(w->h + 22) + 1.0f, 8.0f);
            NVGcolor bc = (w == focused_window)
                ? nvgRGBA(53, 132, 228, 160) : nvgRGBA(40, 40, 60, 100);
            nvgStrokeColor(wm_nvg, bc);
            nvgStrokeWidth(wm_nvg, 1.5f);
            nvgStroke(wm_nvg);

        }

        /* ── Title bar: rounded top, GNOME header style ──────────────── */
        uint32_t title_color = (w == focused_window)
            ? current_theme.title_bg : current_theme.title_inactive_bg;
        /* Top rounded corners via NanoVG */
        if (wm_nvg) {
        wm_nvg_ensure_frame();
            nvgBeginPath(wm_nvg);
            nvgRoundedRectVarying(wm_nvg, (float)w->x, (float)w->y,
                                  (float)w->w, 20.0f, 7.0f, 7.0f, 0.0f, 0.0f);
            uint8_t tr = (title_color >> 16) & 0xFF;
            uint8_t tg = (title_color >> 8)  & 0xFF;
            uint8_t tb =  title_color        & 0xFF;
            nvgFillColor(wm_nvg, nvgRGBA(tr, tg, tb, w->alpha));
            nvgFill(wm_nvg);

        } else {
            vesa_draw_rect_alpha(w->x, w->y, w->w, 20, title_color, (uint8_t)w->alpha);
        }

        /* Title — centered TTF */
        {
            int title_len = strlen(w->title);
            int text_x = (int)w->x + (int)w->w / 2 - title_len * 5;
            if (text_x < (int)w->x + 8) text_x = (int)w->x + 8;
            wm_text(text_x, (int)w->y + 3, w->title, current_theme.title_fg, 12);
        }
        /* Opacity indicator */
        if (w == focused_window && w->alpha < 250) {
            char _atxt[8]; sprintf(_atxt, "%d%%", (w->alpha * 100) / 255);
            wm_text((int)w->x + 8, (int)w->y + 4, _atxt, 0xAABBCC, 9);
        }

        /* ── GNOME-style circular window controls ─────────────────────── */
        /* Close — red circle */
        wm_nvg_rect((int)w->x + (int)w->w - 20, (int)w->y + 4,
                    13, 13, 7,
                    (w == focused_window) ? 0xE53935 : 0x7A2020, w->alpha);
        /* Maximize — green */
        wm_nvg_rect((int)w->x + (int)w->w - 36, (int)w->y + 4,
                    13, 13, 7,
                    (w == focused_window) ? 0x43A047 : 0x1A4A20, w->alpha);
        /* Minimize — yellow */
        wm_nvg_rect((int)w->x + (int)w->w - 52, (int)w->y + 4,
                    13, 13, 7,
                    (w == focused_window) ? 0xFDD835 : 0x4A3A00, w->alpha);

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

    /* Flush any NanoVG draws from window layer */
    wm_nvg_flush();

    // ── Shell chrome: dock/panel/topbar — branch per DE layout ──────────────
    if (desktop_layout == 1) {
        /* KDE Plasma: full-width bottom panel, no side dock */
        wm_render_kde_panel();
        wm_nvg_flush();
    } else if (desktop_layout == 2) {
        /* GNOME Shell: modified top bar only, no dock */
        /* (top bar rendered below in GNOME branch) */
    } else {
        /* ElseaOS default: left dock + right panel */
        wm_render_left_dock();
        wm_render_right_panel();
        wm_nvg_flush();
    }

    // ── Top Bar ───────────────────────────────────────────────────────────────
    if (desktop_layout == 2) {
        /* GNOME Shell: custom top bar */
        wm_render_gnome_topbar();
        wm_nvg_flush();
        /* Activities overview overlay */
        if (activities_open) {
            wm_render_activities_overview();
            wm_nvg_flush();
        }
        goto skip_default_topbar;
    }
    if (desktop_layout == 1) {
        /* KDE Plasma: minimal top bar — just clock + desktop dots */
        wm_nvg_flush();
        goto skip_default_topbar;
    }
    {
    int topbar_h = 32;
    int topbar_y = 0;
    (void)topbar_y;

    /* ── Top bar: blur + dark tint (real glassmorphism) ─────────────── */
    wm_blur_region(0, 0, (int)vesa_width, topbar_h, 2);
    vesa_draw_rect_alpha(0, 0, vesa_width, (uint32_t)topbar_h, 0x0D0D1E, 200);
    /* Bottom separator gradient */
    for (int _c = 0; _c < (int)vesa_width; _c++)
        vesa_putpixel_alpha((uint32_t)_c, (uint32_t)(topbar_h - 1), 0x3584E4, 30);

    /* LEFT: ElseaOS logo pill (NanoVG circle) + TTF name */
    wm_nvg_rect(6, 5, 22, 22, 11, 0x3584E4, 255);
    wm_text(10, 7, "E", 0xFFFFFF, 14);
    wm_text(32, 9, "ElseaOS", 0xDDEEFF, 13);

    /* CENTER: clock — TTF bold */
    {
        int cw = (int)strlen(clock_str) * 7;
        int cx = ((int)vesa_width - cw) / 2;
        wm_text(cx, 9, clock_str, 0xEEEEFF, 13);
    }

    /* RIGHT: system tray — right to left */
    {
        int ri  = (int)vesa_width - 6;
        int ry  = 6;
        int rih = 20;

        /* Virtual desktop indicators (circles) */
        for (int _d = NUM_DESKTOPS - 1; _d >= 0; _d--) {
            ri -= 16;
            int hw = 0;
            for (int _wi = 0; _wi < num_windows; _wi++)
                if (windows[_wi].active && windows[_wi].desktop_id == _d) { hw = 1; break; }
            uint32_t dc = (_d == current_desktop) ? 0x3584E4 : (hw ? 0x445577 : 0x222233);
            uint8_t  da = (_d == current_desktop) ? 240 : 180;
            wm_nvg_rect(ri + 2, ry + 6, 10, 10, 5, dc, da);
        }
        ri -= 4;

        /* Power */
        ri -= 38;
        wm_nvg_rect(ri, ry, 36, rih, 5, 0x1C1020, 210);
        wm_text(ri + 6, ry + 4, "PWR", 0xFF6677, 10);

        /* Volume */
        ri -= 42;
        {
            extern uint8_t master_volume;
            wm_nvg_rect(ri, ry, 40, rih, 5, 0x141525, 210);
            int vf = (master_volume * 28) / 255;
            if (vf > 0)
                vesa_draw_rect_alpha((uint32_t)(ri + 4), (uint32_t)(ry + 7),
                                     (uint32_t)vf, 6, 0x3584E4, 200);
            wm_text(ri + 6, ry + 4, "VOL", 0x99AABB, 10);
        }

        /* WiFi */
        ri -= 42;
        {
            int wfc = wifi_is_connected();
            wm_nvg_rect(ri, ry, 40, rih, 5, 0x141525, 210);
            wm_text(ri + 5, ry + 4, "WiFi", wfc ? 0x3584E4 : 0x445566, 10);
        }

        /* Notification bell */
        ri -= 34;
        {
            uint32_t bcol = (notif_count > 0) ? 0x3A1800 : 0x141525;
            wm_nvg_rect(ri, ry, 32, rih, 5, bcol, 210);
            wm_text(ri + 8, ry + 4,
                    (notif_count > 0) ? "(!)" : "[-]",
                    (notif_count > 0) ? 0xFFAA33 : 0x667788, 10);
            if (notif_panel_open && notif_count > 0) {
                int pw = 300, ph = notif_count * 22 + 50;
                int px = (int)vesa_width - pw - 4;
                int py = topbar_h + 4;
                wm_glass_panel(px, py, pw, ph, 10, 0x0D1117, 240);
                wm_text(px + 10, py + 8, "Notifications", 0x58A6FF, 12);
                for (int _nb = 0; _nb < notif_count; _nb++) {
                    int _ny = py + 28 + _nb * 22;
                    wm_nvg_rect(px + 6, _ny - 2, pw - 12, 20, 4, 0x151A28, 200);
                    wm_text(px + 12, _ny, notif_history[notif_count - 1 - _nb], 0xC9D1D9, 10);
                }
                int dnd_y = py + ph - 26;
                wm_nvg_rect(px + 10, dnd_y, pw - 20, 22, 5,
                            notif_dnd ? 0x3B82F6 : 0x334455, 230);
                wm_text(px + 20, dnd_y + 5,
                        notif_dnd ? "Do Not Disturb: ON" : "Do Not Disturb: OFF",
                        0xFFFFFF, 10);
            }
        }
    }
    
    // ── Bottom Dock (centred pill — ElseaOS default only) ────────────────────
    if (desktop_layout == 0) {
        static const struct {
            const char* label;
            uint32_t    bg;
            int         buf_id; /* 0=none 1=expl 2=term 3=sett 4=pnt */
            char        letter;
        } dapps[10] = {
            { "ElseaOS",  0x3584E4, 0, 'E' },
            { "Files",    0x3D88C8, 1, 'F' },
            { "Browser",  0xE87722, 1, 'B' },
            { "Terminal", 0x1ABC9C, 2, 'T' },
            { "Notes",    0xDFAD20, 4, 'N' },
            { "Music",    0xE74C3C, 0, 'M' },
            { "Calendar", 0x9B59B6, 3, 'C' },
            { "Settings", 0x7F8C8D, 3, 'S' },
            { "Store",    0x27AE60, 1, 'A' },
            { "Apps",     0x445566, 0, 0   },
        };
        uint32_t* dbufs[5] = { NULL, icon_expl_buf, icon_term_buf, icon_sett_buf, icon_pnt_buf };

        int nda   = 10;
        int dslot = 66;
        int dsize = 48;
        int dkw   = nda * dslot + 14;
        int dkh   = 68;
        int dkx   = ((int)vesa_width - dkw) / 2;
        int dky   = (int)vesa_height - dkh - 8;

        /* Dock pill: blur + glass */
        wm_blur_region(dkx, dky, dkw, dkh, 2);
        wm_nvg_rect(dkx, dky, dkw, dkh, 26, 0x080A1C, 210);
        /* Top highlight line */
        if (wm_nvg) {
        wm_nvg_ensure_frame();
            nvgBeginPath(wm_nvg);
            nvgRoundedRect(wm_nvg, (float)dkx+0.5f, (float)dky+0.5f,
                           (float)dkw-1.0f, (float)dkh-1.0f, 26.0f);
            nvgStrokeColor(wm_nvg, nvgRGBA(100, 120, 200, 50));
            nvgStrokeWidth(wm_nvg, 1.0f);
            nvgStroke(wm_nvg);

        }

        int dmx = mouse_get_x(), dmy = mouse_get_y();

        for (int i = 0; i < nda; i++) {
            int ix   = dkx + 7 + i * dslot;
            int iy   = dky + (dkh - dsize) / 2;
            int hov  = (dmx >= ix && dmx < ix + dsize && dmy >= iy && dmy < iy + dsize);
            int rise = hov ? 8 : 0;

            /* App icon circle (NanoVG anti-aliased) */
            wm_nvg_rect(ix, iy - rise, dsize, dsize, dsize / 2, dapps[i].bg, 240);
            /* Inner shine gradient */
            for (int sc = ix + 6; sc < ix + dsize - 6; sc++)
                vesa_putpixel_alpha((uint32_t)sc, (uint32_t)(iy + 3 - rise), 0xFFFFFF, 28);

            if (wm_nvg) {
        wm_nvg_ensure_frame();
                wm_nvg_draw_icon(ix + dsize / 2.0f, iy - rise + dsize / 2.0f, dapps[i].label);

            }

            /* Active-window indicator dot (always visible if app is running) */
            {
                int running = 0;
                for (int _wi = 0; _wi < num_windows && !running; _wi++) {
                    if (!windows[_wi].active) continue;
                    /* match by first word of dock label vs window title */
                    const char* dl = dapps[i].label;
                    int dl_len = 0;
                    while (dl[dl_len] && dl[dl_len] != ' ') dl_len++;
                    if (strncmp(windows[_wi].title, dl, (size_t)dl_len) == 0)
                        running = 1;
                }
                if (running) {
                    uint32_t dot_col = (hov) ? 0x3584E4 : 0xAABBCC;
                    wm_nvg_rect(ix + dsize/2 - 3, dky + dkh - 7, 6, 4, 2,
                                dot_col, 220);
                }
            }

            /* Hover: tooltip above dock */
            if (hov) {
                int ll = (int)strlen(dapps[i].label);
                int tx2 = ix + dsize / 2 - ll * 4;
                wm_nvg_rect(tx2 - 6, dky - 26, ll * 8 + 12, 20, 5, 0x0D1020, 220);
                wm_text(tx2, dky - 22, dapps[i].label, 0xFFFFFF, 11);
            }
        }

    }
    
    wm_nvg_flush(); /* flush top bar NanoVG */
    } /* end default layout top bar block */
    skip_default_topbar:;

    // 4. App Launcher / KDE Launcher overlay
    if (desktop_layout == 1) {
        if (kde_launcher_open) { wm_render_kde_launcher(); wm_nvg_flush(); }
    } else {
        if (launcher_open || start_menu_open) { wm_render_launcher(); wm_nvg_flush(); }
    }
    
    
    // ── Search Overlay ───────────────────────────────────────────────────────
    search_render();
    if (toast_msg[0] && pit_get_ticks() < toast_until) {
        int tw = (int)strlen(toast_msg) * 8 + 24;
        int tx = ((int)vesa_width  - tw) / 2;
        int ty = (int)vesa_height - 68 - 10 - 36;

        /* Glass toast (blurred) */
        wm_glass_panel(tx - 6, ty - 5, tw + 12, 32, 8, 0x0D1A2E, 220);
        /* Accent left bar */
        wm_nvg_rect(tx - 6, ty - 5, 4, 32, 2, 0x3584E4, 230);
        /* TTF text */
        wm_text(tx + 6, ty + 6, toast_msg, 0xFFFFFF, 11);
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
        int ch = CTX_NUM_ITEMS * CTX_ITEM_H + 8;
        int cx = ctx_menu_x;
        int cy = ctx_menu_y;
        if (cx + cw > (int)vesa_width)  cx = (int)vesa_width  - cw - 2;
        if (cy + ch > (int)vesa_height) cy = (int)vesa_height - ch - 2;

        /* Frosted glass panel */
        wm_glass_panel(cx, cy, cw, ch, 8, 0x111120, 240);
        wm_nvg_flush();

        int dmx = mouse_get_x(), dmy = mouse_get_y();
        for (int i = 0; i < CTX_NUM_ITEMS; i++) {
            int iy = cy + 4 + i * CTX_ITEM_H;
            if (ctx_items[i][0] == '-') {
                /* Separator */
                vesa_draw_rect_alpha((uint32_t)(cx + 8), (uint32_t)(iy + CTX_ITEM_H/2),
                                     (uint32_t)(cw - 16), 1, 0x3344AA, 60);
            } else {
                int hov = (dmx >= cx && dmx < cx+cw &&
                           dmy >= iy && dmy < iy+CTX_ITEM_H);
                if (hov) {
                    wm_nvg_rect(cx + 4, iy + 1, cw - 8, CTX_ITEM_H - 2, 5,
                                0x3584E4, 150);
                    wm_nvg_flush();
                }
                wm_text(cx + 12, iy + 4, ctx_items[i],
                        hov ? 0xFFFFFF : 0xCCCCDD, 10);
            }
        }
    }

    // 7. Draw Mouse
    wm_draw_mouse();

    // Apply global brightness setting
    if (sys_brightness < 100) {
        uint8_t alpha = 200 - (sys_brightness * 2);
        vesa_draw_rect_alpha(0, 0, vesa_width, vesa_height, 0x000000, alpha);
    }

    // 8. Modal dialog (drawn on top of everything)
    dialog_draw();

    // 9. Shortcut overlay
    shortcut_overlay_draw();

    // 10. Swap!
    vesa_swap_buffers();
}

void wm_process_events(void) {
    if (!login_is_authenticated()) {
        wm_render();
        return;
    }

    extern int sdl_app_active;
    if (sdl_app_active) return;
    
    extern void music_process_audio(void);
    music_process_audio();
    recorder_tick();
        /* Pump App Store if open */
        extern void appstore_pump(void);
        appstore_pump();

    extern uint32_t vesa_height;

    // 0. Update Clock
    uint32_t current_ticks = pit_get_ticks();

    // Update activity + idle lock
    {
        static int _prev_mx = 0, _prev_my = 0;
        static uint8_t _prev_btns = 0;
        int _cmx = mouse_get_x(), _cmy = mouse_get_y();
        uint8_t _cbtns = mouse_get_buttons();
        if (_cmx != _prev_mx || _cmy != _prev_my || _cbtns != _prev_btns) {
            last_activity_ticks = current_ticks;
            _prev_mx = _cmx; _prev_my = _cmy; _prev_btns = _cbtns;
        }
        if (last_activity_ticks == 0) last_activity_ticks = current_ticks;
        if (current_ticks - last_activity_ticks > IDLE_LOCK_TICKS) {
            login_lock(NULL);
            last_activity_ticks = current_ticks;
            return;
        }
    }
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
    
    /* Shortcut overlay: any click closes it */
    if ((left_click_just_pressed || left_click_just_released) && shortcut_overlay_open) {
        shortcut_overlay_open = 0;
        redraw_needed = 1;
    }

    /* Modal dialog intercepts all clicks */
    if (left_click_just_pressed && active_dialog.active) {
        dialog_handle_click(mx, my);
        last_btns = btns;
        return;
    }

    /* Search overlay intercepts clicks */
    if (left_click_just_pressed && search_is_open()) {
        search_handle_click(mx, my);
        last_btns = btns;
        return;
    }

    if (left_click_just_pressed) {
        int clicked_on_something = 0;

        /* ── KDE panel clicks ── */
        if (desktop_layout == 1) {
            int ph = 42, py2 = (int)vesa_height - ph;
            if (my >= py2) {
                /* Launcher button */
                if (mx >= 4 && mx < 48) {
                    kde_launcher_open = !kde_launcher_open;
                    redraw_needed = 1; clicked_on_something = 1;
                }
                /* Taskbar: focus or restore clicked window */
                else {
                    int tx0 = 52, vis2 = 0;
                    for (int i = 0; i < num_windows; i++)
                        if (windows[i].active && !windows[i].minimized &&
                            windows[i].desktop_id == current_desktop) vis2++;
                    int tw2 = vis2 > 0 ? ((int)vesa_width - tx0 - 210) / vis2 - 4 : 150;
                    if (tw2 > 150) tw2 = 150; if (tw2 < 40) tw2 = 40;
                    int sh2 = 0;
                    for (int i = 0; i < num_windows; i++) {
                        if (!windows[i].active || windows[i].minimized ||
                            windows[i].desktop_id != current_desktop) continue;
                        int bx = tx0 + sh2*(tw2+4);
                        if (mx >= bx && mx < bx+tw2) {
                            focused_window = &windows[i];
                            redraw_needed = 1; clicked_on_something = 1;
                        }
                        sh2++;
                    }
                }
                if (!clicked_on_something) clicked_on_something = 1; /* ate panel click */
            }
            /* KDE launcher grid clicks */
            if (kde_launcher_open) {
                int lw=380,lh=460,lx=4,ly=(int)vesa_height-42-lh-6;
                if (mx>=lx&&mx<lx+lw&&my>=ly&&my<ly+lh) {
                    /* 4-col × 3-row app grid starts at y=74 */
                    int cols=4, rows=3;
                    int cw=(lw-20)/cols, rh=(lh-80)/rows;
                    int agx=lx+10, agy=ly+74;
                    if (my>=agy) {
                        int ac=(mx-agx)/cw, ar=(my-agy)/rh;
                        int ai=ar*cols+ac;
                        if(ac>=0&&ac<cols&&ar>=0&&ar<rows&&ai<12){
                            kde_launcher_open=0; redraw_needed=1;
                            switch(ai){
                                case 0:{ window_t* w=wm_create_window(80,80,400,300,"File Explorer"); explorer_init(w); } break;
                                case 1:{ window_t* w=wm_create_window(80,80,700,480,"Browser"); (void)w; } break;
                                case 2:{ extern window_t* shell_window; shell_window=wm_create_window(50,50,600,400,"Terminal"); } break;
                                case 3:{ window_t* w=wm_create_window(200,150,300,250,"Theme Settings"); settings_init(w); } break;
                                case 4:{ window_t* w=wm_create_window(200,150,350,250,"Music Player"); extern void music_init(window_t*); music_init(w); } break;
                                case 5:{ window_t* w=wm_create_window(150,100,320,300,"Calendar"); calendar_init(w); } break;
                                case 6:{ extern void appstore_init(window_t*); appstore_init(NULL); } break;
                                case 7:{ wm_notepad_open("Notes",120,80); } break;
                                case 8:{ window_t* w=wm_create_window(200,150,500,400,"Image Viewer"); extern void imgview_init(window_t*,const char*); imgview_init(w,"logo.bmp"); } break;
                                case 9:{ window_t* w=wm_create_window(200,200,300,250,"Calculator"); calc_init(w); } break;
                                case 10:{ window_t* w=wm_create_window(150,100,400,300,"Disk Utility"); extern void diskutil_init(window_t*); diskutil_init(w); } break;
                                case 11:{ window_t* w=wm_create_window(150,100,300,280,"Bluetooth"); (void)w; extern void bluetooth_init(void); bluetooth_init(); } break;
                            }
                        }
                    }
                    clicked_on_something=1;
                } else {
                    kde_launcher_open=0; redraw_needed=1; clicked_on_something=1;
                }
            }
        }

        /* ── GNOME top bar clicks ── */
        if (desktop_layout == 2 && my < 32) {
            /* Activities button */
            if (mx >= 4 && mx < 84) {
                activities_open = !activities_open;
                redraw_needed = 1; clicked_on_something = 1;
            }
        }
        /* GNOME Activities overview clicks */
        if (desktop_layout == 2 && activities_open) {
            /* Close button on thumbnail cards */
            int vis3=0;
            for(int i=0;i<num_windows;i++)
                if(windows[i].active&&!windows[i].minimized&&windows[i].desktop_id==current_desktop)vis3++;
            int thw=200,thh=130,thgap=20;
            int total_thw=vis3*(thw+thgap)-thgap;
            int thx0=((int)vesa_width-100-total_thw)/2;
            if(thx0<10) thx0=10;
            int sh3=0;
            for(int i=0;i<num_windows;i++){
                if(!windows[i].active||windows[i].minimized||windows[i].desktop_id!=current_desktop)continue;
                int tx3=thx0+sh3*(thw+thgap), ty3=100;
                /* click anywhere on card → focus and close overview */
                if(mx>=tx3&&mx<tx3+thw&&my>=ty3&&my<ty3+thh){
                    if(mx>=tx3+thw-20&&my<ty3+22){
                        /* close button */
                        windows[i].active=0; if(focused_window==&windows[i]) focused_window=NULL;
                    } else {
                        focused_window=&windows[i];
                    }
                    activities_open=0; redraw_needed=1; clicked_on_something=1;
                }
                sh3++;
            }
            /* GNOME app grid clicks */
            { int asz=54,agap=16;
              int agrid_total=9*(asz+agap)-agap;
              int agx0=((int)vesa_width-100-agrid_total)/2;
              int agy=250;
              if(my>=agy&&my<agy+asz+18){
                  for(int i=0;i<9;i++){
                      int ax=agx0+i*(asz+agap);
                      if(mx>=ax&&mx<ax+asz){
                          activities_open=0; redraw_needed=1; clicked_on_something=1;
                          switch(i){
                              case 0:{ window_t* w=wm_create_window(80,80,400,300,"File Explorer"); explorer_init(w); } break;
                              case 1:{ window_t* w=wm_create_window(80,80,700,480,"Browser"); (void)w; } break;
                              case 2:{ extern window_t* shell_window; shell_window=wm_create_window(50,50,600,400,"Terminal"); } break;
                              case 3:{ window_t* w=wm_create_window(200,150,300,250,"Theme Settings"); settings_init(w); } break;
                              case 4:{ window_t* w=wm_create_window(200,150,350,250,"Music Player"); extern void music_init(window_t*); music_init(w); } break;
                              case 5:{ window_t* w=wm_create_window(150,100,320,300,"Calendar"); calendar_init(w); } break;
                              case 6:{ extern void appstore_init(window_t*); appstore_init(NULL); } break;
                              case 7:{ wm_notepad_open("Notes",120,80); } break;
                              case 8:{ window_t* w=wm_create_window(200,200,300,250,"Calculator"); calc_init(w); } break;
                          }
                      }
                  }
              }
            }
            /* workspace strip click */
            int wsx=(int)vesa_width-96;
            for(int d=0;d<NUM_DESKTOPS;d++){
                int wy=48+d*74;
                if(mx>=wsx&&mx<wsx+84&&my>=wy&&my<wy+64){
                    current_desktop=d; activities_open=0;
                    redraw_needed=1; clicked_on_something=1;
                }
            }
            if(!clicked_on_something) { activities_open=0; redraw_needed=1; }
        }

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
                    } else if (item == 8) { /* Install ElseaOS */
                        extern int nk_installer_running;
                        extern int in_installer_mode;
                        extern int current_step;
                        current_step         = 0;
                        nk_installer_running = 1;
                        in_installer_mode    = 1;
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

        // Check DND button click inside notification panel
        if (!clicked_on_something && notif_panel_open && notif_count > 0) {
            int pw = 280, ph = notif_count * 20 + 40;
            int px = (int)vesa_width - pw - 4;
            int py = 26;
            int dnd_y = py + ph - 24;
            if (mx >= px + 10 && mx <= px + pw - 10 && my >= dnd_y && my <= dnd_y + 20) {
                notif_dnd = !notif_dnd;
                clicked_on_something = 1;
                redraw_needed = 1;
            } else if (mx >= px && mx <= px + pw && my >= py && my <= py + ph) {
                // Clicked inside panel but not on button, just consume
                clicked_on_something = 1;
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

        // ── Launcher: close if clicking the overlay dim or the X ────────────
        if ((launcher_open || start_menu_open) && !clicked_on_something) {
            int lw = ((int)vesa_width > 940) ? 920 : (int)vesa_width - 40;
            int lh = 530;
            int lx = ((int)vesa_width  - lw) / 2;
            int ly = ((int)vesa_height - lh) / 2 - 20;

            if (mx < lx || mx > lx + lw || my < ly || my > ly + lh) {
                /* Clicked outside panel → close */
                launcher_open   = 0;
                start_menu_open = 0;
                clicked_on_something = 1;
                redraw_needed = 1;
            } else {
                clicked_on_something = 1;
                redraw_needed = 1;

                /* Close X button */
                if (mx >= lx + lw - 24 && mx <= lx + lw - 6 &&
                    my >= ly + 6 && my <= ly + 26) {
                    launcher_open   = 0;
                    start_menu_open = 0;
                }

                /* Category sidebar */
                int cat_w = 168;
                if (mx >= lx + 8 && mx < lx + cat_w) {
                    int cat_idx = (my - (ly + 104)) / 36;
                    if (cat_idx >= 0 && cat_idx < 9) {
                        launcher_cat = cat_idx;
                        if (cat_idx == 8) { /* Power → confirm before reboot */
                            launcher_open   = 0;
                            start_menu_open = 0;
                            widget_confirm("Restart ElseaOS?",
                                           "The system will restart. Save your work first.",
                                           wm_do_reboot, NULL, NULL);
                        }
                    }
                }

                /* Pinned apps grid */
                int ax0 = lx + cat_w + 14;
                int aw  = lw - cat_w - 22;
                int pw_slot = aw / 4;
                int ph_slot = 78;
                int row0 = ly + 128;
                if (mx >= ax0 && mx < ax0 + aw && my >= row0) {
                    int _col = (mx - ax0) / pw_slot;
                    int _row = (my - row0) / ph_slot;
                    int _idx = _row * 4 + _col;
                    if (_col >= 0 && _col < 4 && _row >= 0 && _row < 5 && _idx < 20) {
                        launcher_open   = 0;
                        start_menu_open = 0;
                        switch (_idx) {
                            case  0:{ window_t* w=wm_create_window(80,80,700,480,"Browser"); (void)w; } break;
                            case  1:{ window_t* w=wm_create_window(80,80,400,300,"File Explorer"); explorer_init(w); } break;
                            case  2:{ extern window_t* shell_window; shell_window=wm_create_window(50,50,600,400,"Terminal"); } break;
                            case  3:{ window_t* w=wm_create_window(200,150,300,250,"Theme Settings"); settings_init(w); } break;
                            case  4:{ window_t* w=wm_create_window(150,100,320,300,"Calendar"); calendar_init(w); } break;
                            case  5:{ wm_notepad_open("Notes",120,80); } break;
                            case  6:{ window_t* w=wm_create_window(200,150,350,250,"Music Player"); extern void music_init(window_t*); music_init(w); } break;
                            case  7:{ window_t* w=wm_create_window(200,150,500,400,"Image Viewer"); extern void imgview_init(window_t*,const char*); imgview_init(w,"logo.bmp"); } break;
                            case  8:{ window_t* w=wm_create_window(200,200,300,250,"Calculator"); calc_init(w); } break;
                            case  9:{ window_t* w=wm_create_window(300,100,250,250,"Analog Clock"); clock_init(w); } break;
                            case 10:{ extern void appstore_init(window_t*); appstore_init(NULL); } break;
                            case 11:{ wm_toast("Elsea AI: Hello! How can I help?",200); } break;
                            case 12:{ window_t* w=wm_create_window(100,80,640,420,"Spreadsheet"); extern void spreadsheet_init(window_t*); spreadsheet_init(w); } break;
                            case 13:{ window_t* w=wm_create_window(150,100,480,360,"System Monitor"); extern void sysmon_init(window_t*); sysmon_init(w); } break;
                            case 14:{ window_t* w=wm_create_window(200,150,340,300,"Weather"); extern void weather_init(window_t*); weather_init(w); } break;
                            case 15:{ window_t* w=wm_create_window(150,100,400,300,"Disk Utility"); extern void diskutil_init(window_t*); diskutil_init(w); } break;
                            case 16:{ window_t* w=wm_create_window(200,150,360,300,"Charmap"); extern void charmap_init(window_t*); charmap_init(w); } break;
                            case 17:{ window_t* w=wm_create_window(200,150,340,280,"Font Viewer"); extern void fontview_init(window_t*); fontview_init(w); } break;
                            case 18:{ window_t* w=wm_create_window(200,150,280,320,"Snake"); extern void snake_init(window_t*); snake_init(w); } break;
                            case 19:{ window_t* w=wm_create_window(100,80,500,380,"Video Player"); extern void video_player_init(window_t*); video_player_init(w); } break;
                        }
                    }
                }
            }
        }

        // ── Top Bar left logo → toggle launcher ──────────────────────────────
        if (!clicked_on_something && my >= 0 && my < 32 && mx >= 4 && mx < 110) {
            start_btn_pressed = 1;
            clicked_on_something = 1;
            redraw_needed = 1;
        }

        // ── Left dock click ───────────────────────────────────────────────────
        if (!clicked_on_something && mx >= 0 && mx < 72 && my >= 32) {
            int lditem_h = 68;
            int ldi = (my - 32) / lditem_h;
            if (ldi >= 0 && ldi < 5) {
                clicked_on_something = 1;
                redraw_needed = 1;
                switch (ldi) {
                    case 0: { window_t* w = wm_create_window(80,80,400,300,"File Explorer"); explorer_init(w); } break;
                    case 1: { window_t* w = wm_create_window(80,80,700,480,"Browser"); (void)w; } break;
                    case 2: { extern window_t* shell_window; shell_window = wm_create_window(50,50,600,400,"Terminal"); } break;
                    case 3: { window_t* w = wm_create_window(200,150,300,250,"Theme Settings"); settings_init(w); } break;
                    case 4: { wm_toast("Trash is empty", 150); } break;
                }
            }
        }

        // ── Right panel AI input click ────────────────────────────────────────
        {
            int rw = 220;
            int rx = (int)vesa_width - rw;
            /* Approx y of AI input box in the panel */
            int ai_y_approx = 32 + 10 + 18 + 14 + (5*15+4) + 6 + 13 + (3*20+6) + 6 + 16 + 56 + 6;
            if (mx >= rx + 6 && mx < rx + rw - 6 &&
                my >= ai_y_approx && my < ai_y_approx + 26) {
                ai_input_focused = 1;
                clicked_on_something = 1;
                redraw_needed = 1;
            } else if (mx >= rx) {
                /* Clicking elsewhere in right panel deselects AI input */
                if (ai_input_focused) { ai_input_focused = 0; redraw_needed = 1; }
                clicked_on_something = 1;
            }
        }

        // ── New Dock clicks ───────────────────────────────────────────────────
        if (!clicked_on_something) {
            int nda   = 10;
            int dslot = 66;
            int dkw   = nda * dslot + 14;
            int dkh   = 68;
            int dkx   = ((int)vesa_width - dkw) / 2;
            int dky   = (int)vesa_height - dkh - 8;

            if (mx >= dkx && mx < dkx + dkw && my >= dky && my < dky + dkh) {
                clicked_on_something = 1;
                redraw_needed = 1;
                int di = (mx - dkx - 7) / dslot;
                if (di >= 0 && di < nda) {
                    switch (di) {
                        case 0: wm_toast("ElseaOS " __DATE__, 200); break;
                        case 1: { window_t* w = wm_create_window(80,80,400,300,"File Explorer"); explorer_init(w); } break;
                        case 2: { window_t* w = wm_create_window(80,80,700,480,"Browser"); (void)w; } break;
                        case 3: { extern window_t* shell_window; shell_window = wm_create_window(50,50,600,400,"Terminal"); } break;
                        case 4: wm_notepad_open("Notes", 120, 80); break;
                        case 5: wm_toast("Music Player coming soon", 200); break;
                        case 6: { window_t* w = wm_create_window(150,100,320,300,"Calendar"); calendar_init(w); } break;
                        case 7: { window_t* w = wm_create_window(200,150,300,250,"Theme Settings"); settings_init(w); } break;
                        case 8: { extern void appstore_init(window_t*); appstore_init(NULL); } break;
                        case 9: /* Apps grid → open launcher */
                            launcher_open   = !launcher_open;
                            start_menu_open = launcher_open;
                            break;
                    }
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

                        // Close button (×) — rightmost
                        if (mx >= (int)(w->x + w->w - 22) && mx <= (int)(w->x + w->w - 6) &&
                            my >= (int)(w->y + 2) && my <= (int)(w->y + 18)) {
                            speaker_beep(2000, 10);
                            w->anim_state = 2;
                            w->anim_progress = 1.0f;
                            extern window_t* shell_window;
                            if (shell_window == w) shell_window = 0;
                            extern window_t* focused_window;
                            if (focused_window == w) focused_window = 0;
                            clicked_on_something = 1;
                            redraw_needed = 1;
                            break;
                        }

                        // Maximize button (□)
                        if (mx >= (int)(w->x + w->w - 42) && mx <= (int)(w->x + w->w - 26) &&
                            my >= (int)(w->y + 2) && my <= (int)(w->y + 18)) {
                            wm_maximize_toggle(w);
                            speaker_beep(700, 60);
                            clicked_on_something = 1;
                            break;
                        }

                        // Minimize button (─)
                        if (mx >= (int)(w->x + w->w - 62) && mx <= (int)(w->x + w->w - 46) &&
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
                    } else if (strcmp(w->title, "System Monitor") == 0) {
                        sysmon_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Calendar") == 0) {
                        calendar_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Screenshot") == 0) {
                        screenshot_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Disk Utility") == 0) {
                        diskutil_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Weather") == 0) {
                        weather_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Font Viewer") == 0) {
                        fontview_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Charmap") == 0) {
                        charmap_handle_click(w, mx, my);
                    } else if (strcmp(w->title, "Screen Recorder") == 0) {
                        recorder_handle_click(w, mx, my);
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
        // Check Volume Slider
        int vol_x = (int)vesa_width - NUM_DESKTOPS * 16 - 36 - 60;
        if (my >= 3 && my <= 21 && mx >= vol_x && mx <= vol_x + 50) {
            extern uint8_t master_volume;
            int new_vol = ((mx - vol_x) * 255) / 50;
            if (new_vol < 0) new_vol = 0;
            if (new_vol > 255) new_vol = 255;
            extern void mixer_set_volume(uint8_t);
            mixer_set_volume((uint8_t)new_vol);
            redraw_needed = 1;
            clicked_on_something = 1;
        }

        // Check Desktop Icons
        for (int i = 0; i < num_icons; i++) {
            if (mx >= icons[i].x && mx <= icons[i].x + (int)icons[i].w &&
                my >= icons[i].y && my <= icons[i].y + (int)icons[i].h) {
                drag_icon_idx = i;
                drag_icon_off_x = mx - icons[i].x;
                drag_icon_off_y = my - icons[i].y;
                clicked_on_something = 1;
                break;
            }
        }
        
    } else if (left_click_just_released) {
        if (start_btn_pressed) {
            start_btn_pressed = 0;
            if (mx >= 4 && mx < 110 && my >= 0 && my < 32) {
                launcher_open   = !launcher_open;
                start_menu_open = launcher_open;
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
        drag_icon_idx = -1;
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
    if (left_click_held) {
        int vol_x = (int)vesa_width - NUM_DESKTOPS * 16 - 36 - 60;
        if (my >= 3 && my <= 21 && mx >= vol_x - 20 && mx <= vol_x + 70) { // wider hitbox for dragging
            int new_vol = ((mx - vol_x) * 255) / 50;
            if (new_vol < 0) new_vol = 0;
            if (new_vol > 255) new_vol = 255;
            extern void mixer_set_volume(uint8_t);
            mixer_set_volume((uint8_t)new_vol);
            redraw_needed = 1;
        }
    }

    if (left_click_held && drag_win_idx != -1 && resizing_window == 0) {
        windows[drag_win_idx].x = mx - drag_off_x;
        windows[drag_win_idx].y = my - drag_off_y;
        redraw_needed = 1;
    }
    
    // Process Icon Dragging
    if (left_click_held && drag_icon_idx != -1) {
        icons[drag_icon_idx].x = mx - drag_icon_off_x;
        icons[drag_icon_idx].y = my - drag_icon_off_y;
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
    last_activity_ticks = pit_get_ticks();
    if (shortcut_overlay_open) {
        shortcut_overlay_open = 0;
        redraw_needed = 1;
        return 1;
    }
    if (search_is_open()) {
        search_handle_keypress(c);
        return 1;
    }

    /* Elsea AI input in right panel */
    if (ai_input_focused) {
        if (c == '\b' || c == 127) {
            if (ai_input_len > 0) { ai_input_len--; ai_input_buf[ai_input_len] = '\0'; }
        } else if (c == '\n' || c == '\r') {
            if (ai_input_len > 0) {
                wm_toast("Elsea AI: I'm here to help!", 300);
                ai_input_buf[0] = '\0'; ai_input_len = 0;
            }
            ai_input_focused = 0;
        } else if (c == 27) {
            ai_input_focused = 0;
        } else if (ai_input_len < 127 && (unsigned char)c >= 32) {
            ai_input_buf[ai_input_len++] = c;
            ai_input_buf[ai_input_len]   = '\0';
        }
        redraw_needed = 1;
        return 1;
    }

    if (!focused_window) return 0;
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
    } else if (focused_window && strncmp(focused_window->title, "Weather", 7) == 0) {
        extern void weather_handle_key(window_t*, char);
        weather_handle_key(focused_window, c);
        return 1;
    } else if (focused_window && strncmp(focused_window->title, "Snake", 5) == 0) {
        extern int snake_handle_input(char);
        snake_handle_input(c);
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

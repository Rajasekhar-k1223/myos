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
    int      minimized;
    int      maximized;
    /* Saved geometry for maximize/restore */
    uint32_t orig_x, orig_y, orig_w, orig_h;
    /* Virtual desktop this window lives on (0-3) */
    int      desktop_id;
    /* Terminal Scrollback */
    uint32_t* term_grid;
    uint32_t term_cols;
    uint32_t term_rows;
    uint32_t term_line;
    int      term_scroll;
    /* ANSI Escape State */
    int      ansi_state;
    int      ansi_params[8]; /* semicolon-separated params */
    int      ansi_param_idx;
    int      ansi_param;     /* legacy: same as ansi_params[0] */
    int      ansi_bold;
    
    /* Animation state */
    float    anim_progress;  /* 0.0 to 1.0 */
    int      anim_state;     /* 0=open, 1=opening, 2=closing */
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
extern int desktop_layout; /* 0=ElseaOS default, 1=KDE Plasma, 2=GNOME Shell */

int wm_handle_keypress(char c);
int wm_handle_shortcut(char key);

void wm_init(void);
void wm_request_redraw(void);
window_t* wm_create_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title);
void wm_draw_string_window(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t fg);
void wm_draw_string_window_scaled(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t fg, int scale);
void wm_putchar(window_t* win, char c);
void wm_process_events(void);
void wm_toast(const char* msg, uint32_t duration_ticks);
void wm_set_wallpaper(const char* filename);
void wm_draw_desktop_text(const char* str, float scale, int start_x, int start_y, uint32_t color);

typedef void (*ctx_menu_cb_t)(void* arg);
typedef struct {
    char name[32];
    ctx_menu_cb_t cb;
    void* arg;
} ctx_menu_item_t;

void wm_ctx_menu_open(int x, int y, ctx_menu_item_t* items, int num_items);

// ─── Dialog Widgets ─────────────────────────────────────────────────────────────

typedef enum {
    DLG_TYPE_ALERT,
    DLG_TYPE_CONFIRM,
    DLG_TYPE_FILE_OPEN
} dialog_type_t;

typedef struct {
    int active;
    dialog_type_t type;
    char title[64];
    char text[256];
    
    // For FILE_OPEN
    char files[16][32];
    int num_files;
    int scroll_index;
    
    // Callbacks
    void (*on_yes)(void* arg);
    void (*on_no)(void* arg);
    void (*on_file)(const char* file, void* arg);
    void* arg;
} dialog_t;

void widget_alert(const char* title, const char* text, void (*on_close)(void*), void* arg);
void widget_confirm(const char* title, const char* text, void (*on_yes)(void*), void (*on_no)(void*), void* arg);
void widget_file_open(const char* title, void (*on_select)(const char*, void*), void* arg);

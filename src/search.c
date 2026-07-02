#include "search.h"
#include "string.h"
#include "vesa.h"
#include "widget.h"
#include "explorer.h"
#include "settings.h"
#include "sysmon.h"
#include "calendar.h"
#include "weather.h"
#include "fontview.h"
#include "charmap.h"
#include "recorder.h"
#include "diskutil.h"
#include "screenshot.h"

int sprintf(char *str, const char *format, ...);

static int  is_open    = 0;
static char search_buf[64] = {0};
static int  search_len = 0;
static int  hover_idx  = 0;

typedef struct {
    const char* name;
    void (*launch)(void);
} app_entry_t;

static void launch_terminal(void);
static void launch_explorer(void);
static void launch_settings(void);
static void launch_sysmon(void);
static void launch_calendar(void);
static void launch_weather(void);
static void launch_fontview(void);
static void launch_charmap(void);
static void launch_recorder(void);
static void launch_diskutil(void);
static void launch_screenshot(void);
static void launch_calculator(void);
static void launch_browser(void);
static void launch_music(void);
static void launch_paint(void);
static void launch_clock(void);

static const app_entry_t all_apps[] = {
    {"Terminal",       launch_terminal   },
    {"File Explorer",  launch_explorer   },
    {"Settings",       launch_settings   },
    {"System Monitor", launch_sysmon     },
    {"Calendar",       launch_calendar   },
    {"Weather",        launch_weather    },
    {"Font Viewer",    launch_fontview   },
    {"Character Map",  launch_charmap    },
    {"Recorder",       launch_recorder   },
    {"Disk Utility",   launch_diskutil   },
    {"Screenshot",     launch_screenshot },
    {"Calculator",     launch_calculator },
    {"Browser",        launch_browser    },
    {"Music Player",   launch_music      },
    {"Paint",          launch_paint      },
    {"Clock",          launch_clock      },
};
#define NUM_APPS 16

static int result_idx[NUM_APPS];
static int num_results = 0;

static void filter_apps(void) {
    num_results = 0;
    if (search_len == 0) {
        for (int i = 0; i < NUM_APPS; i++) result_idx[num_results++] = i;
        return;
    }
    for (int i = 0; i < NUM_APPS; i++) {
        const char* name = all_apps[i].name;
        int nl = (int)strlen(name), sl = search_len, found = 0;
        for (int j = 0; j <= nl - sl && !found; j++) {
            int ok = 1;
            for (int k = 0; k < sl && ok; k++) {
                char a = name[j+k], b = search_buf[k];
                if (a>='A'&&a<='Z') a+=32;
                if (b>='A'&&b<='Z') b+=32;
                if (a != b) ok = 0;
            }
            if (ok) found = 1;
        }
        if (found) result_idx[num_results++] = i;
    }
}

void search_toggle(void) {
    is_open = !is_open;
    if (is_open) {
        search_len = 0; search_buf[0] = 0;
        hover_idx = 0; filter_apps();
    }
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

int search_is_open(void) { return is_open; }

void search_render(void) {
    if (!is_open) return;
    int sw = (int)vesa_width, sh = (int)vesa_height;
    int visible = num_results < 8 ? num_results : 8;
    int bh = 70 + visible * 32, bw = 520;
    int bx = (sw - bw) / 2, by = sh / 6;

    vesa_draw_rect_alpha(0, 0, (uint32_t)sw, (uint32_t)sh, 0x000000, 160);
    widget_draw_glass(bx, by, bw, bh, 0x0D1117, 250, 1);
    vesa_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, 28, 0x1A2A4A);

    extern void wm_draw_string(uint32_t, uint32_t, const char*, uint32_t);
    wm_draw_string(bx+12, by+8, "Search Applications (Esc to close)", 0xFFFFFF);

    /* Input box */
    vesa_draw_rect((uint32_t)(bx+12), (uint32_t)(by+34), (uint32_t)(bw-24), 26, 0x0A0A14);
    vesa_draw_rect((uint32_t)(bx+12), (uint32_t)(by+34), (uint32_t)(bw-24), 1,  0x3B82F6);
    wm_draw_string(bx+18, by+40, search_buf, 0xFFFFFF);
    vesa_draw_rect((uint32_t)(bx+18+search_len*8), (uint32_t)(by+40), 2, 12, 0x5599FF);

    /* Result list */
    for (int i = 0; i < visible; i++) {
        int ai = result_idx[i];
        int iy = by + 66 + i * 32;
        uint32_t bg = (i == hover_idx) ? 0x1D3A5A : 0x0D1117;
        vesa_draw_rect((uint32_t)(bx+8), (uint32_t)iy, (uint32_t)(bw-16), 28, bg);
        wm_draw_string(bx+16, iy+8, all_apps[ai].name,
                       i == hover_idx ? 0xFFFFFF : 0xC9D1D9);
    }
    if (num_results == 0)
        wm_draw_string(bx+18, by+68, "No matches", 0x555555);
}

void search_handle_keypress(char c) {
    if (!is_open) return;
    if (c == 27) { search_toggle(); return; }
    if (c == '\n' || c == '\r') {
        if (num_results > 0) {
            int ai = result_idx[hover_idx < num_results ? hover_idx : 0];
            is_open = 0;
            if (all_apps[ai].launch) all_apps[ai].launch();
        }
        return;
    }
    if (c == '\b') { if (search_len > 0) search_buf[--search_len] = 0; }
    else if (c >= 32 && c <= 126 && search_len < 63) {
        search_buf[search_len++] = c; search_buf[search_len] = 0;
    }
    hover_idx = 0; filter_apps();
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void search_handle_click(int mx, int my) {
    if (!is_open) return;
    int sw = (int)vesa_width, sh = (int)vesa_height;
    int visible = num_results < 8 ? num_results : 8;
    int bh = 70 + visible * 32, bw = 520;
    int bx = (sw - bw) / 2, by = sh / 6;

    for (int i = 0; i < visible; i++) {
        int iy = by + 66 + i * 32;
        if (mx >= bx+8 && mx <= bx+bw-8 && my >= iy && my <= iy+28) {
            int ai = result_idx[i];
            is_open = 0;
            if (all_apps[ai].launch) all_apps[ai].launch();
            return;
        }
    }
    if (mx < bx || mx > bx+bw || my < by || my > by+bh) is_open = 0;
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

/* ── Launchers ──────────────────────────────────────────────── */
extern window_t* wm_create_window(uint32_t,uint32_t,uint32_t,uint32_t,const char*);
extern window_t* shell_window;

static void launch_terminal(void) {
    shell_window = wm_create_window(60, 60, 560, 380, "Terminal");
}
static void launch_explorer(void) {
    window_t* w = wm_create_window(80, 80, 400, 300, "File Explorer");
    explorer_init(w);
}
static void launch_settings(void) {
    window_t* w = wm_create_window(100, 100, 420, 300, "Settings");
    settings_init(w);
}
static void launch_sysmon(void) {
    window_t* w = wm_create_window(100, 100, 320, 300, "System Monitor");
    sysmon_init(w);
}
static void launch_calendar(void) {
    window_t* w = wm_create_window(120, 120, 300, 340, "Calendar");
    calendar_init(w);
}
static void launch_weather(void) {
    window_t* w = wm_create_window(120, 120, 300, 200, "Weather");
    weather_init(w);
}
static void launch_fontview(void) {
    window_t* w = wm_create_window(100, 100, 520, 340, "Font Viewer");
    fontview_init(w);
}
static void launch_charmap(void) {
    window_t* w = wm_create_window(100, 120, 360, 260, "Character Map");
    charmap_init(w);
}
static void launch_recorder(void) {
    window_t* w = wm_create_window(120, 120, 240, 110, "Recorder");
    recorder_init(w);
}
static void launch_diskutil(void) {
    window_t* w = wm_create_window(100, 100, 360, 260, "Disk Utility");
    diskutil_init(w);
}
static void launch_screenshot(void) {
    window_t* w = wm_create_window(120, 120, 280, 180, "Screenshot");
    extern void screenshot_init(window_t*);
    screenshot_init(w);
}
static void launch_calculator(void) {
    window_t* w = wm_create_window(120, 120, 220, 280, "Calculator");
    extern void calc_init(window_t*);
    calc_init(w);
}
static void launch_browser(void) {
    window_t* w = wm_create_window(80, 80, 600, 400, "Netscape Elsea");
    extern void browser_init(window_t*);
    browser_init(w);
}
static void launch_music(void) {
    window_t* w = wm_create_window(100, 100, 300, 180, "Music Player");
    extern void music_init(window_t*);
    music_init(w);
}
static void launch_paint(void) {
    window_t* w = wm_create_window(80, 80, 400, 300, "Paint");
    extern void paint_init(window_t*);
    paint_init(w);
}
static void launch_clock(void) {
    window_t* w = wm_create_window(100, 100, 250, 250, "Analog Clock");
    extern void clock_init(window_t*);
    clock_init(w);
}

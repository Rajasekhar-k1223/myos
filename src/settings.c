#include "settings.h"
#include "fat16.h"
#include "kheap.h"
#include "string.h"
#include "speaker.h"
#include "kernel.h"

static window_t* my_win = 0;

/* ── Preset themes ──────────────────────────────────────────────────── */
typedef struct { const char* name; uint32_t title_bg; uint32_t border; uint32_t win_bg; uint32_t title_fg; } preset_t;

static const preset_t presets[] = {
    { "Dark Blue",   0x2A5298, 0x1A3A6B, 0x0D1117, 0xFFFFFF },
    { "Dark Gray",   0x3A3A3A, 0x222222, 0x181818, 0xEEEEEE },
    { "Deep Purple", 0x5A2D8A, 0x3A1A5A, 0x120820, 0xFFFFFF },
    { "Ocean",       0x1A5276, 0x0E3A52, 0x0A1E2E, 0xFFFFFF },
    { "Forest",      0x1E6B3A, 0x124225, 0x0A1E10, 0xFFFFFF },
    { "Crimson",     0x8A1A1A, 0x5A0808, 0x1E0808, 0xFFFFFF },
    { "Light",       0xCCCCCC, 0x888888, 0xF0F0F0, 0x111111 },
    { "Retro Teal",  0x008080, 0x005555, 0x002020, 0xFFFFFF },
};
#define NUM_PRESETS ((int)(sizeof(presets)/sizeof(presets[0])))

/* ── Persist theme to FAT16 ─────────────────────────────────────────── */
static void theme_save(void) {
    char buf[256];
    sprintf(buf, "title_bg=%08x\nborder=%08x\nwin_bg=%08x\ntitle_fg=%08x\n",
            current_theme.title_bg, current_theme.window_border,
            current_theme.window_bg, current_theme.title_fg);
    fat16_write_file("theme.cfg", (const uint8_t*)buf, strlen(buf));
}

static uint32_t parse_hex(const char* s) {
    uint32_t v = 0;
    while (*s) {
        char c = *s++;
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    }
    return v;
}

static char* find_char(char* s, char c) {
    while (*s) { if (*s == c) return s; s++; }
    return 0;
}

static void theme_load(void) {
    static uint8_t _tbuf[256];
    int r = fat16_read_file("theme.cfg", _tbuf, sizeof(_tbuf) - 1);
    if (r <= 0) return;
    _tbuf[r] = '\0';
    char* p = (char*)_tbuf;
    while (*p) {
        char* eq = find_char(p, '=');
        char* nl = find_char(p, '\n');
        if (!eq) break;
        *eq = '\0';
        char* val = eq + 1;
        if (nl) *nl = '\0';
        uint32_t v = parse_hex(val);
        if      (strcmp(p, "title_bg") == 0) current_theme.title_bg = v;
        else if (strcmp(p, "border")   == 0) current_theme.window_border = v;
        else if (strcmp(p, "win_bg")   == 0) current_theme.window_bg = v;
        else if (strcmp(p, "title_fg") == 0) current_theme.title_fg = v;
        p = nl ? nl + 1 : p + strlen(p);
    }
    current_theme.title_inactive_bg = (current_theme.title_bg & 0xFEFEFE) >> 1;
}

/* ── Render ─────────────────────────────────────────────────────────── */
static void settings_render(void) {
    if (!my_win) return;

    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0x0D1117;

    wm_draw_string_window(my_win, 10, 8, "Theme Settings", 0x58A6FF);
    wm_draw_string_window(my_win, 10, 26, "Click a preset to apply & save", 0x8B949E);

    for (int i = 0; i < NUM_PRESETS; i++) {
        int row = i / 2;
        int col = i % 2;
        int bx  = 10  + col * 142;
        int by  = 50  + row * 40;
        int bw  = 130, bh = 28;

        /* swatch border */
        uint32_t is_active = (current_theme.title_bg == presets[i].title_bg) ? 1 : 0;
        for (int yy = by - 1; yy < by + bh + 1; yy++)
            for (int xx = bx - 1; xx < bx + bw + 1; xx++)
                if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                    my_win->buffer[yy * my_win->w + xx] = is_active ? 0x58A6FF : 0x21262D;

        /* title bar strip */
        for (int yy = by; yy < by + 10 && yy < (int)my_win->h; yy++)
            for (int xx = bx; xx < bx + bw && xx < (int)my_win->w; xx++)
                my_win->buffer[yy * my_win->w + xx] = presets[i].title_bg;

        /* body strip */
        for (int yy = by + 10; yy < by + bh && yy < (int)my_win->h; yy++)
            for (int xx = bx; xx < bx + bw && xx < (int)my_win->w; xx++)
                my_win->buffer[yy * my_win->w + xx] = presets[i].win_bg;

        wm_draw_string_window(my_win, bx + 4, by + 1, presets[i].name, presets[i].title_fg);
    }

    wm_draw_string_window(my_win, 10, (uint32_t)(50 + (NUM_PRESETS / 2 + 1) * 40),
                          "Theme is saved to FAT16 disk.", 0x8B949E);

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void settings_init(window_t* win) {
    my_win = win;
    theme_load(); /* apply persisted theme on open */
    settings_render();
}

void settings_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;

    for (int i = 0; i < NUM_PRESETS; i++) {
        int row = i / 2, col = i % 2;
        int bx = 10  + col * 142;
        int by = 50  + row * 40;
        if (lx >= bx && lx <= bx + 130 && ly >= by && ly <= by + 28) {
            current_theme.title_bg          = presets[i].title_bg;
            current_theme.window_border     = presets[i].border;
            current_theme.window_bg         = presets[i].win_bg;
            current_theme.title_fg          = presets[i].title_fg;
            current_theme.title_inactive_bg = (presets[i].title_bg & 0xFEFEFE) >> 1;
            theme_save();
            speaker_beep(880, 30);
            settings_render();
            return;
        }
    }
}

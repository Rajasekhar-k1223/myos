#include "appstore.h"
#include "widget.h"
#include "pkg.h"
#include "string.h"
#include "kernel.h"
#include "mouse.h"

#define AS_W         480
#define AS_H         360
#define LIST_ROWS    10
#define ROW_H        22
#define MAX_PKGS_VIEW 32

static window_t*  my_win       = NULL;
static int        selected_row = -1;
static int        scroll_top   = 0;

static pkg_info_t pkgs[MAX_PKGS_VIEW];
static int        pkg_count    = 0;

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= (int)my_win->h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= (int)my_win->w) continue;
            my_win->buffer[(uint32_t)yy * my_win->w + (uint32_t)xx] = c;
        }
    }
}

static void refresh_list(void) {
    pkg_count   = pkg_list(pkgs, MAX_PKGS_VIEW);
    selected_row = -1;
    scroll_top   = 0;
}

void appstore_init(window_t* desktop_win) {
    (void)desktop_win;
    if (my_win) { my_win->active = 1; return; }
    my_win = wm_create_window(100, 80, AS_W, AS_H, "App Store");
    if (!my_win) return;
    my_win->alpha = 210; // Translucent Glassmorphism effect
    refresh_list();
}

static void render(void) {
    /* Background (Dark translucent base) */
    fill_rect(0, 0, AS_W, AS_H, 0x0A0A15);

    /* Header bar (Vibrant Glass tint) */
    fill_rect(0, 0, AS_W, 46, 0x1A2A44);
    wm_draw_string_window(my_win, 16, 16, "ElseaOS App Store", 0xFFFFFF);

    /* Toolbar buttons */
    int mx = (int)mouse_get_x() - (int)my_win->x;
    int my = (int)mouse_get_y() - (int)my_win->y - 20; // adjust for title bar
    // We'll draw modern rounded buttons directly if we could, but widget_draw_button writes to screen.
    // Wait, widget_draw_button takes x, y. In appstore.c it was used incorrectly! It draws to screen!
    // We must draw to my_win->buffer manually. 
    // For now, let's fix widget_draw_button usage to actually draw to buffer, or just draw rects.
    // Actually, widget_draw_button writes to screen, so it overlaps properly if called AFTER wm_render.
    // But render() is called during appstore_pump() which is called in wm_process_events().
    // So the buttons are drawn on screen, then overwritten by wm_render!
    // To fix: draw buttons manually in my_win->buffer!
    int btn1_hover = (mx >= AS_W-180 && mx < AS_W-100 && my >= 10 && my < 34);
    int btn2_hover = (mx >= AS_W-90  && mx < AS_W-10  && my >= 10 && my < 34);
    fill_rect(AS_W - 180, 10, 80, 24, btn1_hover ? 0x2A4A74 : 0x1A3A64);
    wm_draw_string_window(my_win, AS_W - 170, 15, "Refresh", 0xFFFFFF);
    fill_rect(AS_W - 90, 10, 80, 24, btn2_hover ? 0x6A3493 : 0x532483);
    wm_draw_string_window(my_win, AS_W - 80, 15, "Install", 0xFFFFFF);

    /* Column headers */
    fill_rect(0, 46, AS_W, 20, 0x050D1A);
    wm_draw_string_window(my_win,  16, 50, "Name",         0x88AAFF);
    wm_draw_string_window(my_win, 150, 50, "Version",      0x88AAFF);
    wm_draw_string_window(my_win, 220, 50, "Description",  0x88AAFF);
    wm_draw_string_window(my_win, AS_W - 80, 50, "Status", 0x88AAFF);

    /* Package list */
    int visible = LIST_ROWS;
    for (int i = 0; i < visible; i++) {
        int idx = scroll_top + i;
        int ry  = 66 + i * ROW_H;
        if (idx >= pkg_count) break;
        uint32_t bg = (idx == selected_row) ? 0x2A3A5A : ((i & 1) ? 0x0C0C16 : 0x0F0F1A);
        fill_rect(0, ry, AS_W, ROW_H, bg);
        pkg_info_t* p = &pkgs[idx];
        wm_draw_string_window(my_win,  16, ry + 4, p->name,    0xFFFFFF);
        wm_draw_string_window(my_win, 150, ry + 4, p->version,  0xCCCCCC);
        /* Truncate description to fit */
        char desc[32];
        strncpy(desc, p->description, 31);
        desc[31] = '\0';
        wm_draw_string_window(my_win, 220, ry + 4, desc, 0xAABBCC);
        wm_draw_string_window(my_win, AS_W - 75, ry + 4,
                              p->installed ? "Installed" : "Get",
                              p->installed ? 0x44FF88 : 0x00AAFF);
    }

    /* Divider below list */
    int list_bottom = 66 + LIST_ROWS * ROW_H;
    fill_rect(0, list_bottom, AS_W, 1, 0x223344);

    /* Status bar */
    fill_rect(0, AS_H - 24, AS_W, 24, 0x05050A);
    char status[64];
    sprintf(status, "%d packages  |  Selected: %s",
            pkg_count,
            (selected_row >= 0 && selected_row < pkg_count) ? pkgs[selected_row].name : "none");
    wm_draw_string_window(my_win, 16, AS_H - 17, status, 0x667788);
}

void appstore_pump(void) {
    if (!my_win || !my_win->active) return;
    render();

    int mx = (int)mouse_get_x() - (int)my_win->x;
    int my_r = (int)mouse_get_y() - (int)my_win->y;
    uint8_t btns = mouse_get_buttons();
    static uint8_t last_btns = 0;
    int click = (btns & 1) && !(last_btns & 1);
    last_btns = btns;

    if (!click) return;

    /* Refresh button */
    if (mx >= AS_W-180 && mx < AS_W-100 && my_r >= 6 && my_r < 30) {
        pkg_update();
        refresh_list();
        return;
    }
    /* Install button */
    if (mx >= AS_W-90 && mx < AS_W-10 && my_r >= 6 && my_r < 30) {
        if (selected_row >= 0 && selected_row < pkg_count) {
            pkg_install(pkgs[selected_row].name);
            refresh_list();
        }
        return;
    }
    /* Row selection */
    if (my_r >= 56 && my_r < 56 + LIST_ROWS * ROW_H) {
        int row = scroll_top + (my_r - 56) / ROW_H;
        if (row < pkg_count) selected_row = row;
    }
}

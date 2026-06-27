#include "video_player.h"
#include "wm.h"
#include "bmp.h"
#include "tar.h"
#include "pit.h"
#include "string.h"

int sprintf(char* str, const char* fmt, ...);

#define MAX_FRAMES   99

static window_t* vp_win         = NULL;
static int       vp_frame_count = 0;
static int       vp_current     = 0;
static int       vp_playing     = 0;
static int       vp_loop        = 1;
static uint32_t  vp_last_tick   = 0;
static uint32_t  vp_interval    = 10;  /* PIT ticks between frames (10=10fps) */

static void vp_render(void) {
    if (!vp_win) return;
    int ww = (int)vp_win->w;
    int wh = (int)vp_win->h;

    /* Dark background */
    for (int i = 0; i < ww * wh; i++) vp_win->buffer[i] = 0x0A0A0A;

    if (vp_frame_count == 0) {
        wm_draw_string_window(vp_win, 10, wh/2 - 16,
            "No video frames found.", 0xFF6666);
        wm_draw_string_window(vp_win, 10, wh/2,
            "Place frame0.bmp...frame9.bmp in initrd/", 0x888888);
        wm_request_redraw();
        return;
    }

    /* Load and blit frame centered in window */
    char fname[32];
    sprintf(fname, "frame%d.bmp", vp_current);
    bmp_load_to_buffer(fname, vp_win->buffer, ww, wh, 0, 0);

    /* Top-left OSD: frame number */
    char osd_left[32];
    sprintf(osd_left, "Frame %d / %d", vp_current + 1, vp_frame_count);
    /* dim top strip */
    for (int y = 0; y < 14 && y < wh; y++)
        for (int x = 0; x < ww; x++)
            vp_win->buffer[y * ww + x] = (vp_win->buffer[y * ww + x] & 0xFEFEFE) >> 1;
    wm_draw_string_window(vp_win, 4, 1, osd_left, 0xFFFFFF);

    /* Top-right OSD: fps */
    uint32_t fps = (vp_interval > 0) ? (100 / vp_interval) : 0;
    char osd_fps[16];
    sprintf(osd_fps, "%u fps", (unsigned)fps);
    wm_draw_string_window(vp_win, (uint32_t)(ww - 60), 1, osd_fps, 0xAAFFAA);

    /* Bottom status bar */
    int bot_y = wh - 14;
    if (bot_y > 0) {
        for (int y = bot_y; y < wh; y++)
            for (int x = 0; x < ww; x++)
                vp_win->buffer[y * ww + x] = 0x222222;
        const char* state = vp_playing ? "PLAYING" : "PAUSED";
        char status[80];
        sprintf(status, "%s  SPACE=play/pause  <>= prev/next  +/-= fps  r=restart  l=loop(%s)",
                state, vp_loop ? "ON" : "OFF");
        wm_draw_string_window(vp_win, 4, (uint32_t)bot_y + 1, status, 0xFFFFFF);
    }

    wm_request_redraw();
}

void video_player_init(window_t* win) {
    vp_win         = win;
    vp_frame_count = 0;
    vp_current     = 0;
    vp_playing     = 0;
    vp_loop        = 1;
    vp_interval    = 10;
    vp_last_tick   = pit_get_ticks();

    /* Count how many frameN.bmp files exist */
    for (int i = 0; i < MAX_FRAMES; i++) {
        char fname[32];
        sprintf(fname, "frame%d.bmp", i);
        size_t sz = 0;
        if (tar_get_file(fname, &sz) && sz > 0)
            vp_frame_count = i + 1;
        else
            break;
    }

    vp_render();
}

void video_player_handle_key(window_t* win, char c) {
    if (!vp_win || win != vp_win) return;

    if (c == ' ') {
        vp_playing = !vp_playing;
        vp_last_tick = pit_get_ticks();
    } else if (c == '<' || c == ',') {
        if (vp_current > 0) vp_current--;
        else if (vp_loop && vp_frame_count > 0) vp_current = vp_frame_count - 1;
    } else if (c == '>' || c == '.') {
        if (vp_frame_count > 0) {
            vp_current++;
            if (vp_current >= vp_frame_count) vp_current = vp_loop ? 0 : vp_frame_count - 1;
        }
    } else if ((c == '+' || c == '=') && vp_interval > 3) {
        vp_interval -= 2;   /* faster */
    } else if (c == '-' && vp_interval < 100) {
        vp_interval += 2;   /* slower */
    } else if (c == 'r' || c == 'R') {
        vp_current = 0;
    } else if (c == 'l' || c == 'L') {
        vp_loop = !vp_loop;
    } else {
        return;
    }

    vp_render();
}

int video_player_tick(window_t* win) {
    if (!vp_win || win != vp_win) return 0;
    if (!vp_playing || vp_frame_count == 0) return 0;

    uint32_t now = pit_get_ticks();
    if (now - vp_last_tick < vp_interval) return 0;

    vp_last_tick = now;
    vp_current++;
    if (vp_current >= vp_frame_count) {
        if (vp_loop) vp_current = 0;
        else { vp_current = vp_frame_count - 1; vp_playing = 0; }
    }
    vp_render();
    return 1;
}

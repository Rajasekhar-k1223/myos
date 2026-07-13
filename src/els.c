/* els.c — Elsea Video (.els) player for ElseaOS
 *
 * .els binary layout:
 *   [0-3]   "ELS1"
 *   [4-5]   src_width  (uint16 LE)
 *   [6-7]   src_height (uint16 LE)
 *   [8-9]   fps        (uint16 LE)
 *   [10-13] frame_count (uint32 LE)
 *   [14-17] audio_samples (uint32 LE)  8-bit unsigned mono 22050Hz
 *   [18-31] reserved
 *   [32 ..]           audio PCM data
 *   [32+audio ..]     frame_count * src_w * src_h * 3  (raw RGB24)
 *
 * Playback uses counter-based timing: each video frame is shown for
 * els_hold render iterations regardless of wall-clock time. This is
 * 100% cascade-proof — no PIT dependency during playback.
 * Call els_tick() once per vesa_swap_buffers() call.
 */

#include "els.h"
#include "tar.h"
#include "sb16.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t  vesa_width, vesa_height;
extern uint32_t* vesa_get_backbuffer(void);

/* ── state ── */
static int            els_active      = 0;
static int            els_loop        = 0;
static int            els_cur_frame   = 0;
static int            els_hold        = 1;   /* render-iterations per video frame */
static int            els_held        = 0;   /* iterations current frame has been shown */
static uint16_t       els_src_w       = 0;
static uint16_t       els_src_h       = 0;
static uint32_t       els_frame_count = 0;
static uint32_t       els_frame_size  = 0;
static const uint8_t* els_frames      = NULL;
static const uint8_t* els_audio       = NULL;
static uint32_t       els_audio_n     = 0;

/* cached scaled geometry */
static uint32_t cached_vw = 0, cached_vh = 0;
static uint32_t draw_x = 0, draw_y = 0, draw_w = 0, draw_h = 0;

static void recompute_layout(void) {
    if (vesa_width == cached_vw && vesa_height == cached_vh) return;
    cached_vw = vesa_width;
    cached_vh = vesa_height;
    uint32_t sx = vesa_width  * 256 / els_src_w;
    uint32_t sy = vesa_height * 256 / els_src_h;
    uint32_t sc = sx < sy ? sx : sy;
    draw_w = (uint32_t)els_src_w * sc / 256;
    draw_h = (uint32_t)els_src_h * sc / 256;
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;
    draw_x = (vesa_width  - draw_w) / 2;
    draw_y = (vesa_height - draw_h) / 2;
}

static void blit_frame(const uint8_t* rgb) {
    recompute_layout();
    uint32_t* fb = vesa_get_backbuffer();
    if (!fb) return;
    /* clear to opaque black */
    uint32_t total = vesa_width * vesa_height;
    for (uint32_t i = 0; i < total; i++) fb[i] = 0xFF000000u;
    /* nearest-neighbour scale RGB24 → ARGB32 */
    uint32_t sw = els_src_w, sh = els_src_h;
    for (uint32_t dy = 0; dy < draw_h; dy++) {
        uint32_t sy_idx = dy * sh / draw_h;
        if (sy_idx >= sh) sy_idx = sh - 1;
        const uint8_t* src_row = rgb + sy_idx * sw * 3;
        uint32_t*      dst_row = fb  + (draw_y + dy) * vesa_width + draw_x;
        for (uint32_t dx = 0; dx < draw_w; dx++) {
            uint32_t sx_idx = dx * sw / draw_w;
            if (sx_idx >= sw) sx_idx = sw - 1;
            const uint8_t* px = src_row + sx_idx * 3;
            dst_row[dx] = 0xFF000000u
                        | ((uint32_t)px[0] << 16)
                        | ((uint32_t)px[1] <<  8)
                        |  (uint32_t)px[2];
        }
    }
}

/* ── public API ── */

/* Set how many render iterations each video frame is held for.
 * hold=1 → one frame per swap (fastest, video speed = render fps)
 * hold=2 → each frame shown twice (video speed = render fps / 2)
 * Default: 1  */
void els_set_hold(int hold) {
    if (hold < 1) hold = 1;
    els_hold = hold;
}

/* Kept for API compatibility — maps fps to approximate hold count.
 * With render loop typically 30-60fps: hold = round(render_fps / fps).
 * Passing 0 resets to hold=1. */
void els_set_fps(int fps) {
    if (fps <= 0) { els_hold = 1; return; }
    /* Approximate: assume 30fps render loop */
    int h = 30 / fps;
    if (h < 1) h = 1;
    els_hold = h;
}

int els_play(const char* filename) {
    els_stop();   /* resets els_loop = 0; caller sets it after if needed */

    size_t sz = 0;
    const uint8_t* data = (const uint8_t*)tar_get_file(filename, &sz);
    if (!data || sz < 32) return -1;

    if (data[0]!='E'||data[1]!='L'||data[2]!='S'||data[3]!='1') return -1;

    uint16_t src_w   = (uint16_t)(data[4]  | ((uint16_t)data[5]  << 8));
    uint16_t src_h   = (uint16_t)(data[6]  | ((uint16_t)data[7]  << 8));
    uint32_t nf      = data[10]|((uint32_t)data[11]<<8)|((uint32_t)data[12]<<16)|((uint32_t)data[13]<<24);
    uint32_t audio_n = data[14]|((uint32_t)data[15]<<8)|((uint32_t)data[16]<<16)|((uint32_t)data[17]<<24);

    if (src_w == 0 || src_h == 0 || nf == 0) return -1;

    /* Sanity check: frames must fit within the file */
    uint64_t needed = (uint64_t)32 + audio_n + (uint64_t)src_w * src_h * 3 * nf;
    if ((uint64_t)sz < needed) return -1;

    els_src_w       = src_w;
    els_src_h       = src_h;
    els_frame_count = nf;
    els_frame_size  = (uint32_t)src_w * src_h * 3;
    els_frames      = data + 32 + audio_n;
    els_audio       = (audio_n > 0) ? (data + 32) : NULL;
    els_audio_n     = audio_n;

    cached_vw = cached_vh = 0;   /* invalidate layout cache */

    if (audio_n > 0)
        sb16_play_pcm(els_audio, els_audio_n, 0);

    els_cur_frame = 0;
    els_held      = 0;
    els_active    = 1;

    blit_frame(els_frames);   /* show first frame immediately */
    return 0;
}

int els_play_loop(const char* filename) {
    /* els_play() calls els_stop() which resets els_loop, so set AFTER */
    int r = els_play(filename);
    if (r == 0) {
        els_loop = 1;
        if (els_audio && els_audio_n > 0)
            sb16_play_pcm(els_audio, els_audio_n, 1);
    }
    return r;
}

/* Counter-based tick: advance one video frame every els_hold render iterations.
 * This is completely cascade-proof — no wall-clock timing involved.
 * Call ONCE per vesa_swap_buffers() call. */
int els_tick(void) {
    if (!els_active) return 0;

    els_held++;
    if (els_held < els_hold) return 0;   /* still holding current frame */
    els_held = 0;

    /* advance frame */
    els_cur_frame++;
    if (els_cur_frame >= (int)els_frame_count) {
        if (els_loop) {
            els_cur_frame = 0;
            if (els_audio && els_audio_n > 0)
                sb16_play_pcm(els_audio, els_audio_n, 1);
        } else {
            els_active = 0;
            return 0;
        }
    }
    blit_frame(els_frames + (uint32_t)els_cur_frame * els_frame_size);
    return 1;
}

int  els_is_playing(void) { return els_active; }

/* No longer needed (counter-based timing has no cascade).
 * Kept for API compatibility. */
void els_reset_tick(void) {}

void els_stop(void) {
    els_active    = 0;
    els_loop      = 0;
    els_frames    = NULL;
    els_audio     = NULL;
    els_audio_n   = 0;
    els_cur_frame = 0;
    els_held      = 0;
    sb16_stop();
}

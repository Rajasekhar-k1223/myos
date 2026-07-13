/* bsplash.c — multi-resolution BMP-frame boot splash player for ElseaOS
 *
 * At boot, reads vesa_width × vesa_height and tries to load
 * "bsplash_WxH.bin" from initrd.  Falls back to "bsplash.bin" if the
 * resolution-specific file is not present.
 *
 * Generate resolution files with:
 *   python3 make_bmp_splash.py video.mp4 15 1920x1080
 *   python3 make_bmp_splash.py video.mp4 15          # all resolutions
 */

#include "bsplash.h"
#include "tar.h"
#include "pit.h"
#include "sb16.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t  vesa_width, vesa_height;
extern uint32_t* vesa_get_backbuffer(void);

/* ── player state ─────────────────────────────────────────────────────────── */
static int            bsp_active      = 0;
static int            bsp_cur         = 0;
static uint16_t       bsp_w           = 0;
static uint16_t       bsp_h           = 0;
static uint32_t       bsp_nframes     = 0;
static uint32_t       bsp_fsize       = 0;
static const uint8_t* bsp_frames      = NULL;
static const uint8_t* bsp_frames_end  = NULL;   /* one-past-last valid byte */
static const uint8_t* bsp_audio       = NULL;
static uint32_t       bsp_audio_n     = 0;

/* PIT-based frame timing */
static uint32_t bsp_last_tick = 0;
static uint32_t bsp_tpf       = 4;   /* ticks per frame (100Hz / fps) */

static void blit_frame(const uint8_t* rgb) {
    uint32_t* fb = vesa_get_backbuffer();
    if (!fb || !rgb) return;

    uint32_t vw = vesa_width, vh = vesa_height;
    uint32_t sw = bsp_w,      sh = bsp_h;

    if (vw == 0 || vh == 0 || sw == 0 || sh == 0) return;

    /* Stretch-to-fill: map source proportionally to every screen pixel.
     * Fixed-point 16.16 steps — no division in inner loop, fast on 32-bit CPU. */
    uint32_t step_y = (sh << 16) / vh;   /* 16.16 source-row step */
    uint32_t step_x = (sw << 16) / vw;   /* 16.16 source-col step */

    uint32_t fy = 0;
    for (uint32_t dy = 0; dy < vh; dy++, fy += step_y) {
        uint32_t si_y = fy >> 16;
        if (si_y >= sh) si_y = sh - 1;
        const uint8_t* src_row = rgb + si_y * sw * 3;
        uint32_t*      dst_row = fb  + dy * vw;
        uint32_t fx = 0;
        for (uint32_t dx = 0; dx < vw; dx++, fx += step_x) {
            uint32_t si_x = fx >> 16;
            if (si_x >= sw) si_x = sw - 1;
            const uint8_t* p = src_row + si_x * 3;
            dst_row[dx] = 0xFF000000u
                        | ((uint32_t)p[0] << 16)
                        | ((uint32_t)p[1] <<  8)
                        |  (uint32_t)p[2];
        }
    }
}

/* ── filename builder ─────────────────────────────────────────────────────── */

/* Writes "bsplash_WxH.bin\0" into buf (needs ≥32 bytes). */
static void bsp_make_fname(char* buf, uint32_t w, uint32_t h) {
    const char pre[] = "bsplash_";
    const char suf[] = ".bin";
    int i = 0;
    for (int j = 0; pre[j]; j++) buf[i++] = pre[j];
    /* width */
    char tmp[10]; int n = 0;
    uint32_t v = w ? w : 0;
    if (!v) { tmp[n++] = '0'; }
    else { while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; } }
    while (n > 0) buf[i++] = tmp[--n];
    buf[i++] = 'x';
    /* height */
    n = 0; v = h ? h : 0;
    if (!v) { tmp[n++] = '0'; }
    else { while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; } }
    while (n > 0) buf[i++] = tmp[--n];
    for (int j = 0; suf[j]; j++) buf[i++] = suf[j];
    buf[i] = '\0';
}

/* ── public API ───────────────────────────────────────────────────────────── */

void bsplash_set_fps(int fps) {
    if (fps <= 0) fps = 25;
    bsp_tpf = 100u / (uint32_t)fps;
    if (bsp_tpf < 1) bsp_tpf = 1;
}

int bsplash_play(void) {
    bsplash_stop();

    /* Try resolution-specific file first (bsplash_WxH.bin), then generic */
    char fname[32];
    bsp_make_fname(fname, vesa_width, vesa_height);

    size_t sz = 0;
    const uint8_t* data = (const uint8_t*)tar_get_file(fname, &sz);
    if (!data || sz < 32) {
        data = (const uint8_t*)tar_get_file("bsplash.bin", &sz);
    }
    if (!data || sz < 32) return -1;

    if (data[0]!='B'||data[1]!='S'||data[2]!='P'||data[3]!='L') return -1;

    uint16_t w   = (uint16_t)(data[4]  | ((uint16_t)data[5]  << 8));
    uint16_t h   = (uint16_t)(data[6]  | ((uint16_t)data[7]  << 8));
    uint16_t fps = (uint16_t)(data[8]  | ((uint16_t)data[9]  << 8));
    uint32_t nf  = data[10]|((uint32_t)data[11]<<8)|((uint32_t)data[12]<<16)|((uint32_t)data[13]<<24);
    uint32_t an  = data[14]|((uint32_t)data[15]<<8)|((uint32_t)data[16]<<16)|((uint32_t)data[17]<<24);

    if (w == 0 || h == 0 || nf == 0) return -1;

    uint32_t fsize = (uint32_t)w * h * 3;
    if ((uint64_t)32 + an + (uint64_t)fsize * nf > (uint64_t)sz) return -1;

    bsp_w       = w;
    bsp_h       = h;
    bsp_nframes = nf;
    bsp_fsize   = fsize;
    bsp_audio      = (an > 0) ? (data + 32) : NULL;
    bsp_audio_n    = an;
    bsp_frames     = data + 32 + an;
    bsp_frames_end = bsp_frames + (uint32_t)fsize * nf;

    /* derive ticks-per-frame from file header fps */
    if (fps > 0) {
        bsp_tpf = 100u / fps;
        if (bsp_tpf < 1) bsp_tpf = 1;
    }

    if (bsp_audio_n > 0)
        sb16_play_pcm(bsp_audio, bsp_audio_n, 1);

    bsp_cur       = 0;
    bsp_active    = 1;
    bsp_last_tick = pit_get_ticks();

    blit_frame(bsp_frames);   /* display frame 0 immediately */
    return 0;
}

/* Call once per render loop. Advances frame when PIT timer expires.
 * Returns 1 when a new frame was blitted. */
int bsplash_tick(void) {
    if (!bsp_active) return 0;

    uint32_t now = pit_get_ticks();
    if (now - bsp_last_tick < bsp_tpf) return 0;  /* still within frame period */

    bsp_cur++;
    if (bsp_cur >= (int)bsp_nframes) {
        bsp_active = 0;
        return 0;
    }
    const uint8_t* fp = bsp_frames + (uint32_t)bsp_cur * bsp_fsize;
    if (fp + bsp_fsize > bsp_frames_end) {
        bsp_active = 0;   /* frame out of bounds — stop safely */
        return 0;
    }
    blit_frame(fp);
    return 1;
}

/* Call AFTER vesa_swap_buffers() — resets frame timer to prevent cascade
 * when the render+swap takes longer than one frame period. */
void bsplash_reset_tick(void) {
    if (bsp_active) bsp_last_tick = pit_get_ticks();
}

int bsplash_is_playing(void) { return bsp_active; }

void bsplash_stop(void) {
    bsp_active     = 0;
    bsp_frames     = NULL;
    bsp_frames_end = NULL;
    bsp_audio      = NULL;
    bsp_audio_n    = 0;
    bsp_cur        = 0;
    sb16_stop();
}

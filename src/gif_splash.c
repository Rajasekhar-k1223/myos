/* gif_splash.c — GIF89a animated boot splash for ElseaOS
 *
 * Loads "splash.gif" from initrd and plays it frame-by-frame.
 * Frame delays come from each frame's Graphic Control Extension (GCE)
 * and are enforced with PIT (100Hz) — 1 centisecond = ~10ms = 1 tick.
 *
 * GIF89a mini-spec used:
 *   Header 6B → Logical Screen Descriptor 7B → optional GCT
 *   Extensions: 0x21 label ... blocks 0x00
 *     GCE  0xF9 (4-byte block)
 *     App  0xFF (11-byte block for Netscape loop)
 *   Image:  0x2C left top w h flags [LCT] lzw_min sub-blocks... 0x00
 *   Trailer: 0x3B
 *
 * LZW decompressor uses 12KB static table + 4KB output stack (BSS).
 */

#include "gif_splash.h"
#include "tar.h"
#include "pit.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t  vesa_width, vesa_height;
extern uint32_t* vesa_get_backbuffer(void);

/* ── limits ───────────────────────────────────────────────────────────────── */
#define GIF_MAX_W    1920
#define GIF_MAX_H    1080
#define GIF_MAX_PIX  (GIF_MAX_W * GIF_MAX_H)   /* 2073600 — fits 1080p frames */
#define LZW_MAXCODES 4096

/* ── LZW code table ───────────────────────────────────────────────────────── */
typedef struct { uint16_t prefix; uint8_t color; } lzw_entry_t;
static lzw_entry_t lzw_table[LZW_MAXCODES];   /* 12 KB BSS */
static uint8_t     lzw_stack[LZW_MAXCODES];   /*  4 KB BSS */

/* ── GIF player state ─────────────────────────────────────────────────────── */
static int            gsp_active    = 0;
static const uint8_t* gsp_data      = NULL;
static uint32_t       gsp_size      = 0;
static uint32_t       gsp_pos       = 0;   /* read cursor into gsp_data */

/* Logical screen */
static uint16_t gsp_lsw = 0, gsp_lsh = 0;
static uint8_t  gsp_gct[256][3];
static int      gsp_gct_n = 0;
static uint8_t  gsp_bg = 0;

/* Current frame */
static uint8_t  gsp_pixels[GIF_MAX_PIX];  /* palette-indexed pixels */
static uint8_t  gsp_canvas[GIF_MAX_PIX];  /* composed RGB screen (palette-indexed) */
static uint16_t gsp_fw = 0, gsp_fh = 0;
static uint16_t gsp_fx = 0, gsp_fy = 0;
static int      gsp_delay_ms  = 100;  /* ms */
static int      gsp_transp    = -1;   /* transparent colour index, -1=none */
static int      gsp_dispose   = 0;    /* disposal method */
static uint8_t  gsp_lct[256][3];
static int      gsp_lct_n    = 0;

/* PIT timing */
static uint32_t gsp_last_tick = 0;
static uint32_t gsp_tpf       = 10;   /* ticks per frame */

/* ── bit-stream reader with GIF sub-blocks ──────────────────────────────── */
typedef struct {
    const uint8_t* src;
    uint32_t        src_end;
    uint32_t        pos;     /* byte index into src — current sub-block parse */
    /* current sub-block window */
    uint8_t  blk[256];
    int      blk_len, blk_pos;
    /* bit accumulator */
    uint32_t bits;
    int      nbits;
} bitrd_t;

static void bitrd_init(bitrd_t* b, const uint8_t* src, uint32_t size, uint32_t start) {
    b->src     = src;
    b->src_end = size;
    b->pos     = start;
    b->blk_len = 0; b->blk_pos = 0;
    b->bits = 0; b->nbits = 0;
}

/* Load next GIF sub-block into blk[]. Returns block length (0 = terminator). */
static int bitrd_fill(bitrd_t* b) {
    if (b->pos >= b->src_end) return 0;
    int len = b->src[b->pos++];
    if (len == 0 || b->pos + len > b->src_end) { b->blk_len = 0; return 0; }
    for (int i = 0; i < len; i++) b->blk[i] = b->src[b->pos + i];
    b->pos     += len;
    b->blk_len  = len;
    b->blk_pos  = 0;
    return len;
}

/* Read n bits from LZW sub-block stream. Returns -1 on end. */
static int bitrd_read(bitrd_t* b, int n) {
    while (b->nbits < n) {
        if (b->blk_pos >= b->blk_len) {
            if (bitrd_fill(b) == 0) return -1;
        }
        b->bits  |= (uint32_t)b->blk[b->blk_pos++] << b->nbits;
        b->nbits += 8;
    }
    int code = (int)(b->bits & ((1u << n) - 1u));
    b->bits  >>= n;
    b->nbits  -= n;
    return code;
}

/* Skip all remaining sub-blocks at current position. */
static void skip_blocks(uint32_t* pos, const uint8_t* data, uint32_t size) {
    while (*pos < size) {
        uint8_t len = data[(*pos)++];
        if (len == 0) break;
        *pos += len;
    }
}

/* ── LZW decompressor ─────────────────────────────────────────────────────── */
static int lzw_decomp(bitrd_t* b, int mcs, uint8_t* out, int out_max) {
    int clr  = 1 << mcs;
    int eoi  = clr + 1;
    int tsz  = clr + 2;
    int cbits = mcs + 1;

    int out_pos  = 0;
    int prev     = -1;
    int prev_first = -1;

    for (;;) {
        int code = bitrd_read(b, cbits);
        if (code < 0 || code == eoi) break;

        if (code == clr) {
            tsz   = clr + 2;
            cbits = mcs + 1;
            prev  = -1;
            prev_first = -1;
            continue;
        }

        int stk_top = 0;

        if (code == tsz && prev >= 0) {
            /* Special case: code references the entry we're about to create.
               That entry = prev_string + prev_string[0] */
            lzw_stack[stk_top++] = (uint8_t)prev_first;
            /* fall through to traverse prev */
            int c = prev;
            while (c >= clr + 2 && stk_top < LZW_MAXCODES) {
                lzw_stack[stk_top++] = lzw_table[c].color;
                c = lzw_table[c].prefix;
            }
            if (stk_top < LZW_MAXCODES) lzw_stack[stk_top++] = (uint8_t)c;
        } else if (code < tsz) {
            /* Known code: traverse chain */
            int c = code;
            while (c >= clr + 2 && stk_top < LZW_MAXCODES) {
                lzw_stack[stk_top++] = lzw_table[c].color;
                c = lzw_table[c].prefix;
            }
            if (stk_top < LZW_MAXCODES) lzw_stack[stk_top++] = (uint8_t)c;
        } else {
            break;  /* corrupt stream */
        }

        /* first color of this code string = top of stack */
        int first_color = (int)(uint8_t)lzw_stack[stk_top - 1];

        /* output in reverse (stack is reversed) */
        while (stk_top > 0 && out_pos < out_max)
            out[out_pos++] = lzw_stack[--stk_top];

        /* add new entry: prev + first_color */
        if (prev >= 0 && tsz < LZW_MAXCODES) {
            lzw_table[tsz].prefix = (uint16_t)prev;
            lzw_table[tsz].color  = (uint8_t)first_color;
            tsz++;
            if (tsz == (1 << cbits) && cbits < 12) cbits++;
        }

        prev       = code;
        prev_first = first_color;
    }
    return out_pos;
}

/* ── scaled blit frame to VESA backbuffer ─────────────────────────────────── */
static void blit_canvas(void) {
    uint32_t* fb = vesa_get_backbuffer();
    if (!fb) return;

    uint8_t (*pal)[3] = (gsp_lct_n > 0) ? gsp_lct : gsp_gct;
    uint32_t vw = vesa_width, vh = vesa_height;
    uint32_t lw = gsp_lsw,    lh = gsp_lsh;

    if (vw == 0 || vh == 0 || lw == 0 || lh == 0) return;

    /* Stretch-to-fill with 16.16 fixed-point steps — no division in inner loop. */
    uint32_t step_y = (lh << 16) / vh;
    uint32_t step_x = (lw << 16) / vw;

    uint32_t fy = 0;
    for (uint32_t row = 0; row < vh; row++, fy += step_y) {
        uint32_t si_y = fy >> 16;
        if (si_y >= lh) si_y = lh - 1;
        uint32_t* dst = fb + row * vw;
        uint32_t fx = 0;
        for (uint32_t col = 0; col < vw; col++, fx += step_x) {
            uint32_t si_x = fx >> 16;
            if (si_x >= lw) si_x = lw - 1;
            uint8_t ci = gsp_canvas[si_y * lw + si_x];
            if ((int)ci == gsp_transp) {
                dst[col] = 0xFF000000u;
            } else {
                dst[col] = 0xFF000000u
                    | ((uint32_t)pal[ci][0] << 16)
                    | ((uint32_t)pal[ci][1] <<  8)
                    |  (uint32_t)pal[ci][2];
            }
        }
    }
}

/* Compose current decoded frame (gsp_pixels) onto gsp_canvas */
static void compose_frame(void) {
    /* disposal of previous frame */
    if (gsp_dispose == 2) {
        /* restore to background */
        for (uint32_t i = 0; i < (uint32_t)gsp_lsw * gsp_lsh; i++)
            gsp_canvas[i] = gsp_bg;
    }
    /* else dispose=0,1 → keep canvas as-is */

    /* paint new frame pixels */
    for (uint16_t y = 0; y < gsp_fh; y++) {
        for (uint16_t x = 0; x < gsp_fw; x++) {
            uint8_t ci = gsp_pixels[(uint32_t)y * gsp_fw + x];
            if ((int)ci == gsp_transp) continue;  /* transparent — keep canvas */
            uint32_t cx = (uint32_t)gsp_fx + x;
            uint32_t cy = (uint32_t)gsp_fy + y;
            if (cx < gsp_lsw && cy < gsp_lsh)
                gsp_canvas[cy * gsp_lsw + cx] = ci;
        }
    }
}

/* ── GIF frame parser ────────────────────────────────────────────────────── */

/* Decode next frame. Returns delay_ms on success, -1 on end/error. */
static int gif_next_frame(void) {
    if (!gsp_data) return -1;

    while (gsp_pos < gsp_size) {
        uint8_t intro = gsp_data[gsp_pos++];

        if (intro == 0x3B) return -1;   /* GIF trailer */

        if (intro == 0x21) {
            /* Extension */
            if (gsp_pos >= gsp_size) return -1;
            uint8_t lbl = gsp_data[gsp_pos++];

            if (lbl == 0xF9) {
                /* Graphic Control Extension */
                if (gsp_pos + 6 > gsp_size) return -1;
                gsp_pos++;   /* skip block size (=4) */
                uint8_t flags = gsp_data[gsp_pos++];
                uint16_t delay = (uint16_t)(gsp_data[gsp_pos] | ((uint16_t)gsp_data[gsp_pos+1] << 8));
                gsp_pos += 2;
                uint8_t tcol = gsp_data[gsp_pos++];
                gsp_pos++;   /* block terminator */
                gsp_delay_ms = (int)delay * 10;
                if (gsp_delay_ms < 20) gsp_delay_ms = 80;   /* min 20ms ≈ 50fps */
                gsp_dispose  = (flags >> 2) & 7;
                gsp_transp   = (flags & 1) ? (int)tcol : -1;
            } else {
                /* skip unknown extension */
                skip_blocks(&gsp_pos, gsp_data, gsp_size);
            }
        } else if (intro == 0x2C) {
            /* Image Descriptor */
            if (gsp_pos + 9 > gsp_size) return -1;
            gsp_fx = (uint16_t)(gsp_data[gsp_pos] | ((uint16_t)gsp_data[gsp_pos+1] << 8)); gsp_pos += 2;
            gsp_fy = (uint16_t)(gsp_data[gsp_pos] | ((uint16_t)gsp_data[gsp_pos+1] << 8)); gsp_pos += 2;
            gsp_fw = (uint16_t)(gsp_data[gsp_pos] | ((uint16_t)gsp_data[gsp_pos+1] << 8)); gsp_pos += 2;
            gsp_fh = (uint16_t)(gsp_data[gsp_pos] | ((uint16_t)gsp_data[gsp_pos+1] << 8)); gsp_pos += 2;
            uint8_t iflags = gsp_data[gsp_pos++];

            /* Local Color Table */
            gsp_lct_n = 0;
            if (iflags & 0x80) {
                int lct_n = 1 << ((iflags & 7) + 1);
                if (gsp_pos + (uint32_t)lct_n * 3 <= gsp_size) {
                    for (int i = 0; i < lct_n; i++) {
                        gsp_lct[i][0] = gsp_data[gsp_pos + i*3 + 0];
                        gsp_lct[i][1] = gsp_data[gsp_pos + i*3 + 1];
                        gsp_lct[i][2] = gsp_data[gsp_pos + i*3 + 2];
                    }
                    gsp_pos  += (uint32_t)lct_n * 3;
                    gsp_lct_n = lct_n;
                }
            }

            /* LZW minimum code size */
            if (gsp_pos >= gsp_size) return -1;
            int mcs = gsp_data[gsp_pos++];
            if (mcs < 2 || mcs > 11) { skip_blocks(&gsp_pos, gsp_data, gsp_size); return gsp_delay_ms; }

            /* Decode */
            int max_pix = (int)gsp_fw * gsp_fh;
            if (max_pix <= 0 || (uint32_t)max_pix > GIF_MAX_PIX) {
                skip_blocks(&gsp_pos, gsp_data, gsp_size);
                return gsp_delay_ms;
            }

            bitrd_t br;
            bitrd_init(&br, gsp_data, gsp_size, gsp_pos);
            lzw_decomp(&br, mcs, gsp_pixels, max_pix);
            gsp_pos = br.pos;   /* advance past all sub-blocks */

            compose_frame();
            return gsp_delay_ms;
        }
    }
    return -1;
}

/* ── public API ───────────────────────────────────────────────────────────── */

int gif_splash_play(void) {
    gif_splash_stop();

    size_t sz = 0;
    const uint8_t* data = (const uint8_t*)tar_get_file("splash.gif", &sz);
    if (!data || sz < 13) return -1;

    /* GIF header */
    if (data[0]!='G'||data[1]!='I'||data[2]!='F') return -1;

    gsp_data = data;
    gsp_size = (uint32_t)sz;
    gsp_pos  = 6;   /* after "GIF89a" */

    /* Logical Screen Descriptor */
    gsp_lsw = (uint16_t)(data[6] | ((uint16_t)data[7] << 8));
    gsp_lsh = (uint16_t)(data[8] | ((uint16_t)data[9] << 8));
    uint8_t lsd_flags = data[10];
    gsp_bg            = data[11];
    gsp_pos = 13;

    /* Global Color Table */
    gsp_gct_n = 0;
    if (lsd_flags & 0x80) {
        int gct_n = 1 << ((lsd_flags & 7) + 1);
        if (gsp_pos + (uint32_t)gct_n * 3 <= gsp_size) {
            for (int i = 0; i < gct_n; i++) {
                gsp_gct[i][0] = data[gsp_pos + i*3 + 0];
                gsp_gct[i][1] = data[gsp_pos + i*3 + 1];
                gsp_gct[i][2] = data[gsp_pos + i*3 + 2];
            }
            gsp_pos  += (uint32_t)gct_n * 3;
            gsp_gct_n = gct_n;
        }
    }

    if (gsp_lsw == 0 || gsp_lsh == 0) return -1;
    if ((uint32_t)gsp_lsw > GIF_MAX_W || (uint32_t)gsp_lsh > GIF_MAX_H) return -1;

    /* init canvas to background color */
    for (uint32_t i = 0; i < (uint32_t)gsp_lsw * gsp_lsh; i++)
        gsp_canvas[i] = gsp_bg;

    /* decode first frame */
    gsp_transp   = -1;
    gsp_dispose  = 0;
    gsp_delay_ms = 100;
    gsp_lct_n    = 0;

    int delay = gif_next_frame();
    if (delay < 0) return -1;

    gsp_tpf       = (uint32_t)(gsp_delay_ms / 10);   /* ms → ticks at 100Hz */
    if (gsp_tpf < 1) gsp_tpf = 1;
    gsp_last_tick = pit_get_ticks();
    gsp_active    = 1;

    blit_canvas();
    return 0;
}

int gif_splash_tick(void) {
    if (!gsp_active) return 0;

    uint32_t now = pit_get_ticks();
    if (now - gsp_last_tick < gsp_tpf) return 0;

    /* reset will be done by gif_splash_reset_tick() after swap */
    gsp_transp   = -1;
    gsp_dispose  = 0;
    gsp_lct_n    = 0;
    int delay = gif_next_frame();
    if (delay < 0) {
        gsp_active = 0;   /* all frames played — proceed to installer */
        return 0;
    }
    gsp_tpf = (uint32_t)(gsp_delay_ms / 10);
    if (gsp_tpf < 1) gsp_tpf = 1;

    blit_canvas();
    return 1;
}

void gif_splash_reset_tick(void) {
    if (gsp_active) gsp_last_tick = pit_get_ticks();
}

int gif_splash_is_playing(void) { return gsp_active; }

void gif_splash_stop(void) {
    gsp_active = 0;
    gsp_data   = NULL;
    gsp_size   = 0;
    gsp_pos    = 0;
}

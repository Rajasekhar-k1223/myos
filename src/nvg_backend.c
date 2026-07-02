#include "nvg_backend.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"
#include "mathf.h"

/* Freestanding-kernel software backend for NanoVG. nanovg.c's core is
 * renderer-agnostic by design (an NVGparams vtable the official library
 * ships GL backends for) -- the bulk of this file is a from-scratch CPU
 * rasterizer implementing that vtable, since there's no GPU driver in this
 * kernel to back the usual GL backend with.
 *
 * Allocation/stdio/assert/printf are redirected the same way as
 * src/svg.c -- see that file's comments for the general pattern. */
#define malloc(sz)        kmalloc(sz)
#define realloc(p, sz)    krealloc(p, sz)
#define free(p)           kfree(p)
#define NDEBUG /* makes stb_truetype's default STBTT_assert(x)->assert(x) a no-op */

#define NVG_NO_STB /* skip nanovg.c's bundled stb_image.h -- we already have
                    * our own freestanding port in src/jpeg.c; nvg_elseaos
                    * only uses nvgCreateImageRGBA (raw pixels), never the
                    * file/memory image loaders that need it. */
#define STBTT_STATIC /* fontstash.h pulls in its own separate instance of
                      * stb_truetype.h (src/stb_truetype.h, same file
                      * src/ttf.c already builds its own customized instance
                      * from) -- without this, both translation units emit
                      * the same non-static stbtt_* symbols and the linker
                      * sees duplicate definitions. fontstash never needs to
                      * call stb_truetype from outside this file anyway. */
#include "nanovg.c"

/* nanovg.c's nvgDebugDumpPathCache() (dead code -- we never call it) uses
 * printf for debug dumps. Unlike malloc/free above, this can't be a plain
 * macro: <stdio.h>'s own `int printf(const char* restrict, ...)`
 * declaration (pulled in transitively above) would get textually rewritten
 * too, conflicting with kernel.h's `void terminal_printf(...)`. A real
 * wrapper avoids that. */
#include <stdarg.h>
int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    terminal_vprintf(fmt, ap);
    va_end(ap);
    return 0;
}

/* ── Backend state ──────────────────────────────────────────────────────── */

#define NVG_MAX_TEXTURES 32

typedef struct {
    int in_use;
    int type;   /* NVG_TEXTURE_RGBA or NVG_TEXTURE_ALPHA */
    int w, h;
    uint8_t* data; /* RGBA8 (type RGBA) or A8 (type ALPHA), w*h*bpp bytes */
} nvg_texture_t;

typedef struct {
    uint32_t* canvas;
    int cw, ch;
    nvg_texture_t textures[NVG_MAX_TEXTURES];
} nvg_backend_t;

/* ── Texture management ─────────────────────────────────────────────────── */
/* NanoVG image handles are 1-based (0 means "no image"); slot i holds
 * handle i+1. */

static int nvg__renderCreate(void* uptr) { (void)uptr; return 1; }

static int nvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data) {
    (void)imageFlags;
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    int slot = -1;
    for (int i = 0; i < NVG_MAX_TEXTURES; i++) {
        if (!be->textures[i].in_use) { slot = i; break; }
    }
    if (slot < 0 || w <= 0 || h <= 0) return 0;

    int bpp = (type == NVG_TEXTURE_RGBA) ? 4 : 1;
    uint8_t* buf = (uint8_t*)kmalloc((size_t)w * (size_t)h * (size_t)bpp);
    if (!buf) return 0;

    nvg_texture_t* t = &be->textures[slot];
    t->in_use = 1;
    t->type = type;
    t->w = w;
    t->h = h;
    t->data = buf;
    if (data) memcpy(t->data, data, (size_t)w * (size_t)h * (size_t)bpp);
    else memset(t->data, 0, (size_t)w * (size_t)h * (size_t)bpp);

    return slot + 1;
}

static int nvg__renderDeleteTexture(void* uptr, int image) {
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    if (image < 1 || image > NVG_MAX_TEXTURES) return 0;
    nvg_texture_t* t = &be->textures[image - 1];
    if (!t->in_use) return 0;
    kfree(t->data);
    t->data = 0;
    t->in_use = 0;
    return 1;
}

static int nvg__renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data) {
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    if (image < 1 || image > NVG_MAX_TEXTURES) return 0;
    nvg_texture_t* t = &be->textures[image - 1];
    if (!t->in_use || !data) return 0;
    int bpp = (t->type == NVG_TEXTURE_RGBA) ? 4 : 1;
    /* `data` is NOT a tightly-packed w*h sub-image -- for the font-atlas
     * dirty-rect update path (nvg__flushTextTexture in nanovg.c), it's a
     * pointer into fontstash's *full* atlas buffer (fonsGetTextureData()),
     * with the full atlas width as its row stride. Source and dest must
     * use the same stride (t->w) here, not `w`. */
    for (int row = 0; row < h; row++) {
        memcpy(t->data + ((size_t)(y + row) * t->w + x) * bpp,
               data + ((size_t)(y + row) * t->w + x) * bpp,
               (size_t)w * bpp);
    }
    return 1;
}

static int nvg__renderGetTextureSize(void* uptr, int image, int* w, int* h) {
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    if (image < 1 || image > NVG_MAX_TEXTURES || !be->textures[image - 1].in_use) return 0;
    *w = be->textures[image - 1].w;
    *h = be->textures[image - 1].h;
    return 1;
}

static void nvg__renderViewport(void* uptr, float width, float height, float devicePixelRatio) {
    (void)uptr; (void)width; (void)height; (void)devicePixelRatio;
    /* Canvas size is owned by nvg_elseaos_set_canvas(), not the viewport
     * call -- nothing to do here. */
}

static void nvg__renderCancel(void* uptr) { (void)uptr; }
static void nvg__renderFlush(void* uptr) { (void)uptr; } /* immediate-mode: renderFill/Stroke/Triangles already drew directly to canvas */

static void nvg__renderDelete(void* uptr) {
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    for (int i = 0; i < NVG_MAX_TEXTURES; i++) {
        if (be->textures[i].in_use) kfree(be->textures[i].data);
    }
    kfree(be);
}

/* ── Paint evaluation ───────────────────────────────────────────────────────
 * Mirrors NanoVG's GL fragment shader formula (sdroundrect + feather lerp)
 * exactly, since that's the convention paint->xform/extent/radius/feather
 * were built for by nvgLinearGradient/nvgRadialGradient/nvgBoxGradient/
 * nvg__setPaintColor -- the only thing that differs per backend is *how*
 * you get from "pixel" to "evaluate the formula", not the formula itself. */

static float nvg__sdroundrect(float px, float py, float ex, float ey, float r) {
    float ex2 = ex - r, ey2 = ey - r;
    float dx = fabsf(px) - ex2;
    float dy = fabsf(py) - ey2;
    float maxd = (dx > dy) ? dx : dy;
    float mind0 = maxd < 0.0f ? maxd : 0.0f;
    float ddx = dx > 0.0f ? dx : 0.0f;
    float ddy = dy > 0.0f ? dy : 0.0f;
    return mind0 + sqrtf(ddx * ddx + ddy * ddy) - r;
}

/* Precomputed per-draw-call paint state, built once and reused per pixel. */
typedef struct {
    const NVGpaint* paint;
    nvg_backend_t* be;
    float inv[6];        /* inverse of paint->xform */
    nvg_texture_t* tex;  /* non-NULL if paint->image > 0 */
} nvg_painteval_t;

static void nvg__painteval_init(nvg_painteval_t* pe, const NVGpaint* paint, nvg_backend_t* be) {
    pe->paint = paint;
    pe->be = be;
    nvgTransformInverse(pe->inv, (float*)paint->xform);
    pe->tex = NULL;
    if (paint->image >= 1 && paint->image <= NVG_MAX_TEXTURES && be->textures[paint->image - 1].in_use) {
        pe->tex = &be->textures[paint->image - 1];
    }
}

/* out[4] = R,G,B,A in 0..255 */
static void nvg__painteval_sample(const nvg_painteval_t* pe, float wx, float wy, uint8_t out[4]) {
    const NVGpaint* paint = pe->paint;
    float lx, ly;
    nvgTransformPoint(&lx, &ly, pe->inv, wx, wy);

    if (pe->tex) {
        float u = (lx / paint->extent[0]);
        float v = (ly / paint->extent[1]);
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        int tx = (int)(u * (pe->tex->w - 1) + 0.5f);
        int ty = (int)(v * (pe->tex->h - 1) + 0.5f);
        if (pe->tex->type == NVG_TEXTURE_RGBA) {
            const uint8_t* px = pe->tex->data + ((size_t)ty * pe->tex->w + tx) * 4;
            out[0] = (uint8_t)(px[0] * paint->innerColor.r);
            out[1] = (uint8_t)(px[1] * paint->innerColor.g);
            out[2] = (uint8_t)(px[2] * paint->innerColor.b);
            out[3] = (uint8_t)(px[3] * paint->innerColor.a);
        } else { /* ALPHA texture (e.g. font atlas): innerColor is the glyph color */
            uint8_t a = pe->tex->data[(size_t)ty * pe->tex->w + tx];
            out[0] = (uint8_t)(paint->innerColor.r * 255.0f);
            out[1] = (uint8_t)(paint->innerColor.g * 255.0f);
            out[2] = (uint8_t)(paint->innerColor.b * 255.0f);
            out[3] = (uint8_t)(paint->innerColor.a * a);
        }
        return;
    }

    float feather = paint->feather;
    if (feather < 1e-6f) feather = 1e-6f;
    float d = nvg__sdroundrect(lx, ly, paint->extent[0], paint->extent[1], paint->radius);
    float t = (d + feather * 0.5f) / feather;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    out[0] = (uint8_t)((paint->innerColor.r + (paint->outerColor.r - paint->innerColor.r) * t) * 255.0f);
    out[1] = (uint8_t)((paint->innerColor.g + (paint->outerColor.g - paint->innerColor.g) * t) * 255.0f);
    out[2] = (uint8_t)((paint->innerColor.b + (paint->outerColor.b - paint->innerColor.b) * t) * 255.0f);
    out[3] = (uint8_t)((paint->innerColor.a + (paint->outerColor.a - paint->innerColor.a) * t) * 255.0f);
}

/* Scissor test in the same spirit -- world point inside the (possibly
 * rotated) scissor rect? extent[0] < 0 means "no scissor" (nvg__initState's
 * default). Returns coverage multiplier 0..1 with a 1px soft edge so
 * scissored shapes don't get a hard aliased clip boundary. */
static float nvg__scissorCoverage(const NVGscissor* scissor, float wx, float wy) {
    if (scissor->extent[0] < 0.0f) return 1.0f;
    float inv[6];
    nvgTransformInverse(inv, (float*)scissor->xform);
    float lx, ly;
    nvgTransformPoint(&lx, &ly, inv, wx, wy);
    float ex = scissor->extent[0], ey = scissor->extent[1];
    float dx = fabsf(lx) - ex;
    float dy = fabsf(ly) - ey;
    float d = (dx > dy) ? dx : dy;
    if (d <= -1.0f) return 1.0f;
    if (d >= 0.0f) return 0.0f;
    return -d; /* -1..0 -> 1..0 */
}

/* ── Core rasterizer ─────────────────────────────────────────────────────────
 * Nonzero-winding, multi-contour scanline fill over the (already-tessellated
 * by nanovg.c's core) fill/stroke point loops of an NVGpath array. Edge AA
 * is disabled in nanovg's core (see nvg_elseaos_create), so each path is a
 * plain closed polygon -- no separate AA fringe geometry to interpret. AA
 * here is done independently: 4 vertical sub-scanlines per pixel row plus
 * fractional horizontal span coverage, accumulated into a per-row coverage
 * buffer and blended against the paint color once per pixel. */

#define NVG_SS        4   /* vertical sub-scanlines per pixel row */
#define NVG_SS_SCALE  64  /* full single-subscanline pixel coverage; NVG_SS*NVG_SS_SCALE = 256 */

static void nvg__rasterize(nvg_backend_t* be, const NVGpath* paths, int npaths, int use_stroke,
                            const NVGscissor* scissor, const nvg_painteval_t* pe) {
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    int totalverts = 0;
    for (int i = 0; i < npaths; i++) {
        const NVGvertex* v = use_stroke ? paths[i].stroke : paths[i].fill;
        int n = use_stroke ? paths[i].nstroke : paths[i].nfill;
        totalverts += n;
        for (int j = 0; j < n; j++) {
            if (v[j].x < minx) minx = v[j].x;
            if (v[j].x > maxx) maxx = v[j].x;
            if (v[j].y < miny) miny = v[j].y;
            if (v[j].y > maxy) maxy = v[j].y;
        }
    }
    if (totalverts < 2 || maxx <= minx || maxy <= miny) return;

    int ix0 = (int)floorf(minx); if (ix0 < 0) ix0 = 0;
    int iy0 = (int)floorf(miny); if (iy0 < 0) iy0 = 0;
    int ix1 = (int)ceilf(maxx);  if (ix1 > be->cw) ix1 = be->cw;
    int iy1 = (int)ceilf(maxy);  if (iy1 > be->ch) iy1 = be->ch;
    if (ix1 <= ix0 || iy1 <= iy0) return;

    int roww = ix1 - ix0;
    uint16_t* coverage = (uint16_t*)kmalloc((size_t)roww * sizeof(uint16_t));
    float* xs   = (float*)kmalloc((size_t)totalverts * sizeof(float));
    int*   dirs = (int*)kmalloc((size_t)totalverts * sizeof(int));
    if (!coverage || !xs || !dirs) {
        if (coverage) kfree(coverage);
        if (xs) kfree(xs);
        if (dirs) kfree(dirs);
        return;
    }

    for (int y = iy0; y < iy1; y++) {
        memset(coverage, 0, (size_t)roww * sizeof(uint16_t));

        for (int s = 0; s < NVG_SS; s++) {
            float suby = (float)y + ((float)s + 0.5f) / (float)NVG_SS;
            int ncross = 0;

            for (int pi = 0; pi < npaths; pi++) {
                const NVGvertex* v = use_stroke ? paths[pi].stroke : paths[pi].fill;
                int n = use_stroke ? paths[pi].nstroke : paths[pi].nfill;
                if (n < 2) continue;
                for (int j = 0; j < n; j++) {
                    float x0 = v[j].x, y0 = v[j].y;
                    int j1 = (j + 1 == n) ? 0 : j + 1;
                    float x1 = v[j1].x, y1 = v[j1].y;
                    if (y0 == y1) continue;
                    int dir; float ya, yb, xa, xb;
                    if (y0 < y1) { dir = 1;  ya = y0; yb = y1; xa = x0; xb = x1; }
                    else         { dir = -1; ya = y1; yb = y0; xa = x1; xb = x0; }
                    if (suby < ya || suby >= yb) continue;
                    float t = (suby - ya) / (yb - ya);
                    xs[ncross] = xa + t * (xb - xa);
                    dirs[ncross] = dir;
                    ncross++;
                }
            }

            /* insertion sort by x -- ncross is small (path vertex counts) */
            for (int a = 1; a < ncross; a++) {
                float xv = xs[a]; int dv = dirs[a];
                int b = a - 1;
                while (b >= 0 && xs[b] > xv) { xs[b+1] = xs[b]; dirs[b+1] = dirs[b]; b--; }
                xs[b+1] = xv; dirs[b+1] = dv;
            }

            int winding = 0;
            for (int a = 0; a < ncross - 1; a++) {
                winding += dirs[a];
                if (winding == 0) continue;
                float xstart = xs[a], xend = xs[a+1];
                if (xstart < (float)ix0) xstart = (float)ix0;
                if (xend > (float)ix1) xend = (float)ix1;
                if (xend <= xstart) continue;

                int pxs = (int)floorf(xstart);
                int pxe = (int)floorf(xend);
                if (pxs == pxe) {
                    coverage[pxs - ix0] += (uint16_t)((xend - xstart) * NVG_SS_SCALE);
                } else {
                    coverage[pxs - ix0] += (uint16_t)(((float)(pxs + 1) - xstart) * NVG_SS_SCALE);
                    for (int px = pxs + 1; px < pxe; px++) coverage[px - ix0] += NVG_SS_SCALE;
                    if (pxe < ix1) coverage[pxe - ix0] += (uint16_t)((xend - (float)pxe) * NVG_SS_SCALE);
                }
            }
        }

        for (int x = 0; x < roww; x++) {
            if (coverage[x] == 0) continue;
            float cov = (float)coverage[x] / (float)(NVG_SS * NVG_SS_SCALE);
            if (cov > 1.0f) cov = 1.0f;

            int px = ix0 + x;
            float wx = (float)px + 0.5f, wy = (float)y + 0.5f;
            cov *= nvg__scissorCoverage(scissor, wx, wy);
            if (cov <= 0.0f) continue;

            uint8_t rgba[4];
            nvg__painteval_sample(pe, wx, wy, rgba);
            float a = (rgba[3] / 255.0f) * cov;
            if (a <= 0.0f) continue;

            uint32_t* dst = &be->canvas[(size_t)y * (size_t)be->cw + (size_t)px];
            uint32_t old = *dst;
            float ia = 1.0f - a;
            uint8_t nr = (uint8_t)(rgba[0] * a + ((old >> 16) & 0xFF) * ia);
            uint8_t ng = (uint8_t)(rgba[1] * a + ((old >> 8) & 0xFF) * ia);
            uint8_t nb = (uint8_t)(rgba[2] * a + (old & 0xFF) * ia);
            *dst = 0xFF000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
        }
    }

    kfree(xs);
    kfree(dirs);
    kfree(coverage);
}

/* ── NVGparams fill/stroke/triangles callbacks ──────────────────────────── */
/* compositeOperation is ignored throughout -- everything draws as plain
 * source-over alpha blending, regardless of the blend mode NanoVG was
 * asked for. That's a real, documented limitation (see nvg_backend.h),
 * not a silent gap: most UI work (the use case here) only ever uses the
 * default source-over mode anyway. */

static void nvg__renderFill(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
                             NVGscissor* scissor, float fringe, const float* bounds, const NVGpath* paths, int npaths) {
    (void)compositeOperation; (void)fringe; (void)bounds;
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    nvg_painteval_t pe;
    nvg__painteval_init(&pe, paint, be);
    nvg__rasterize(be, paths, npaths, 0, scissor, &pe);
}

/* nvg__expandStroke() (nanovg.c) emits stroke geometry as a triangle
 * *strip* (consecutive vertices i,i+1,i+2 form a triangle; degenerate
 * zero-area triangles appear at cap/join seams), not a closed polygon
 * boundary the way fill geometry is -- unlike nvg__rasterize() above,
 * which assumes the latter. 4x point-sampled AA per pixel (cheaper than
 * nvg__rasterize's scanline-coverage approach, but strokes are typically
 * thin so the bbox-per-triangle cost stays small). */
static void nvg__rasterizeStrokeTriangle(nvg_backend_t* be, NVGvertex v0, NVGvertex v1, NVGvertex v2,
                                          const NVGscissor* scissor, const nvg_painteval_t* pe) {
    float minx = v0.x, maxx = v0.x, miny = v0.y, maxy = v0.y;
    if (v1.x < minx) minx = v1.x;
    if (v1.x > maxx) maxx = v1.x;
    if (v2.x < minx) minx = v2.x;
    if (v2.x > maxx) maxx = v2.x;
    if (v1.y < miny) miny = v1.y;
    if (v1.y > maxy) maxy = v1.y;
    if (v2.y < miny) miny = v2.y;
    if (v2.y > maxy) maxy = v2.y;

    int ix0 = (int)floorf(minx); if (ix0 < 0) ix0 = 0;
    int iy0 = (int)floorf(miny); if (iy0 < 0) iy0 = 0;
    int ix1 = (int)ceilf(maxx);  if (ix1 > be->cw) ix1 = be->cw;
    int iy1 = (int)ceilf(maxy);  if (iy1 > be->ch) iy1 = be->ch;
    if (ix1 <= ix0 || iy1 <= iy0) return;

    float d = (v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y);
    if (fabsf(d) < 1e-9f) return; /* degenerate seam triangle */
    float invd = 1.0f / d;

    static const float sx[4] = {0.25f, 0.75f, 0.25f, 0.75f};
    static const float sy[4] = {0.25f, 0.25f, 0.75f, 0.75f};

    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            int hits = 0;
            for (int s = 0; s < 4; s++) {
                float px = (float)x + sx[s], py = (float)y + sy[s];
                float w0 = ((v1.y - v2.y) * (px - v2.x) + (v2.x - v1.x) * (py - v2.y)) * invd;
                float w1 = ((v2.y - v0.y) * (px - v2.x) + (v0.x - v2.x) * (py - v2.y)) * invd;
                float w2 = 1.0f - w0 - w1;
                if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) hits++;
            }
            if (hits == 0) continue;

            float wx = (float)x + 0.5f, wy = (float)y + 0.5f;
            float cov = (hits / 4.0f) * nvg__scissorCoverage(scissor, wx, wy);
            if (cov <= 0.0f) continue;

            uint8_t rgba[4];
            nvg__painteval_sample(pe, wx, wy, rgba);
            float a = (rgba[3] / 255.0f) * cov;
            if (a <= 0.0f) continue;

            uint32_t* dst = &be->canvas[(size_t)y * (size_t)be->cw + (size_t)x];
            uint32_t old = *dst;
            float ia = 1.0f - a;
            uint8_t nr = (uint8_t)(rgba[0] * a + ((old >> 16) & 0xFF) * ia);
            uint8_t ng = (uint8_t)(rgba[1] * a + ((old >> 8) & 0xFF) * ia);
            uint8_t nb = (uint8_t)(rgba[2] * a + (old & 0xFF) * ia);
            *dst = 0xFF000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
        }
    }
}

static void nvg__renderStroke(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
                               NVGscissor* scissor, float fringe, float strokeWidth, const NVGpath* paths, int npaths) {
    (void)compositeOperation; (void)fringe; (void)strokeWidth;
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    nvg_painteval_t pe;
    nvg__painteval_init(&pe, paint, be);

    for (int i = 0; i < npaths; i++) {
        const NVGvertex* v = paths[i].stroke;
        int n = paths[i].nstroke;
        for (int j = 0; j + 2 < n; j++) {
            /* Alternate winding order each step, standard triangle-strip
             * convention, so all triangles face the same way. */
            if (j & 1) nvg__rasterizeStrokeTriangle(be, v[j+1], v[j], v[j+2], scissor, &pe);
            else       nvg__rasterizeStrokeTriangle(be, v[j], v[j+1], v[j+2], scissor, &pe);
        }
    }
}

/* Used for text glyph quads (2 triangles each, UV into the font atlas) and
 * any other raw textured/colored triangle lists nanovg.c's core emits.
 * Unlike renderFill/renderStroke, geometry here carries its own per-vertex
 * UV -- sample the bound texture directly rather than going through the
 * paint->xform gradient formula. */
static void nvg__renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
                                  NVGscissor* scissor, const NVGvertex* verts, int nverts, float fringe) {
    (void)compositeOperation; (void)fringe;
    nvg_backend_t* be = (nvg_backend_t*)uptr;
    nvg_texture_t* tex = NULL;
    if (paint->image >= 1 && paint->image <= NVG_MAX_TEXTURES && be->textures[paint->image - 1].in_use) {
        tex = &be->textures[paint->image - 1];
    }

    for (int i = 0; i + 2 < nverts; i += 3) {
        const NVGvertex* v0 = &verts[i];
        const NVGvertex* v1 = &verts[i+1];
        const NVGvertex* v2 = &verts[i+2];

        float minx = v0->x, maxx = v0->x, miny = v0->y, maxy = v0->y;
        if (v1->x < minx) minx = v1->x;
        if (v1->x > maxx) maxx = v1->x;
        if (v2->x < minx) minx = v2->x;
        if (v2->x > maxx) maxx = v2->x;
        if (v1->y < miny) miny = v1->y;
        if (v1->y > maxy) maxy = v1->y;
        if (v2->y < miny) miny = v2->y;
        if (v2->y > maxy) maxy = v2->y;

        int ix0 = (int)floorf(minx); if (ix0 < 0) ix0 = 0;
        int iy0 = (int)floorf(miny); if (iy0 < 0) iy0 = 0;
        int ix1 = (int)ceilf(maxx);  if (ix1 > be->cw) ix1 = be->cw;
        int iy1 = (int)ceilf(maxy);  if (iy1 > be->ch) iy1 = be->ch;

        float d = (v1->y - v2->y) * (v0->x - v2->x) + (v2->x - v1->x) * (v0->y - v2->y);
        if (fabsf(d) < 1e-9f) continue;
        float invd = 1.0f / d;

        for (int y = iy0; y < iy1; y++) {
            for (int x = ix0; x < ix1; x++) {
                float px = (float)x + 0.5f, py = (float)y + 0.5f;
                float w0 = ((v1->y - v2->y) * (px - v2->x) + (v2->x - v1->x) * (py - v2->y)) * invd;
                float w1 = ((v2->y - v0->y) * (px - v2->x) + (v0->x - v2->x) * (py - v2->y)) * invd;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                float u = w0 * v0->u + w1 * v1->u + w2 * v2->u;
                float v = w0 * v0->v + w1 * v1->v + w2 * v2->v;

                uint8_t rgba[4];
                if (tex) {
                    int tx = (int)(u * (tex->w - 1) + 0.5f); if (tx < 0) tx = 0; if (tx >= tex->w) tx = tex->w - 1;
                    int ty = (int)(v * (tex->h - 1) + 0.5f); if (ty < 0) ty = 0; if (ty >= tex->h) ty = tex->h - 1;
                    if (tex->type == NVG_TEXTURE_RGBA) {
                        const uint8_t* sp = tex->data + ((size_t)ty * tex->w + tx) * 4;
                        rgba[0] = (uint8_t)(sp[0] * paint->innerColor.r);
                        rgba[1] = (uint8_t)(sp[1] * paint->innerColor.g);
                        rgba[2] = (uint8_t)(sp[2] * paint->innerColor.b);
                        rgba[3] = (uint8_t)(sp[3] * paint->innerColor.a);
                    } else {
                        uint8_t cover = tex->data[(size_t)ty * tex->w + tx];
                        rgba[0] = (uint8_t)(paint->innerColor.r * 255.0f);
                        rgba[1] = (uint8_t)(paint->innerColor.g * 255.0f);
                        rgba[2] = (uint8_t)(paint->innerColor.b * 255.0f);
                        rgba[3] = (uint8_t)(paint->innerColor.a * cover);
                    }
                } else {
                    rgba[0] = (uint8_t)(paint->innerColor.r * 255.0f);
                    rgba[1] = (uint8_t)(paint->innerColor.g * 255.0f);
                    rgba[2] = (uint8_t)(paint->innerColor.b * 255.0f);
                    rgba[3] = (uint8_t)(paint->innerColor.a * 255.0f);
                }

                float cov = nvg__scissorCoverage(scissor, px, py);
                float a = (rgba[3] / 255.0f) * cov;
                if (a <= 0.0f) continue;

                uint32_t* dst = &be->canvas[(size_t)y * (size_t)be->cw + (size_t)x];
                uint32_t old = *dst;
                float ia = 1.0f - a;
                uint8_t nr = (uint8_t)(rgba[0] * a + ((old >> 16) & 0xFF) * ia);
                uint8_t ng = (uint8_t)(rgba[1] * a + ((old >> 8) & 0xFF) * ia);
                uint8_t nb = (uint8_t)(rgba[2] * a + (old & 0xFF) * ia);
                *dst = 0xFF000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
            }
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

NVGcontext* nvg_elseaos_create(uint32_t* canvas, int width, int height) {
    nvg_backend_t* be = (nvg_backend_t*)kmalloc(sizeof(nvg_backend_t));
    if (!be) return NULL;
    memset(be, 0, sizeof(*be));
    be->canvas = canvas;
    be->cw = width;
    be->ch = height;

    NVGparams params;
    memset(&params, 0, sizeof(params));
    params.userPtr = be;
    params.edgeAntiAlias = 0; /* see nvg__rasterize's comment: we do our own AA */
    params.renderCreate = nvg__renderCreate;
    params.renderCreateTexture = nvg__renderCreateTexture;
    params.renderDeleteTexture = nvg__renderDeleteTexture;
    params.renderUpdateTexture = nvg__renderUpdateTexture;
    params.renderGetTextureSize = nvg__renderGetTextureSize;
    params.renderViewport = nvg__renderViewport;
    params.renderCancel = nvg__renderCancel;
    params.renderFlush = nvg__renderFlush;
    params.renderFill = nvg__renderFill;
    params.renderStroke = nvg__renderStroke;
    params.renderTriangles = nvg__renderTriangles;
    params.renderDelete = nvg__renderDelete;

    NVGcontext* ctx = nvgCreateInternal(&params);
    if (!ctx) { kfree(be); return NULL; }
    return ctx;
}

void nvg_elseaos_delete(NVGcontext* ctx) {
    if (ctx) nvgDeleteInternal(ctx); /* calls renderDelete, frees `be` */
}

void nvg_elseaos_set_canvas(NVGcontext* ctx, uint32_t* canvas, int width, int height) {
    if (!ctx) return;
    nvg_backend_t* be = (nvg_backend_t*)nvgInternalParams(ctx)->userPtr;
    be->canvas = canvas;
    be->cw = width;
    be->ch = height;
}

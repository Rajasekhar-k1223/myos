/*
 * sdl_shim.h -- SDL 1.x compatibility shim for ElseaOS
 *
 * Include this header instead of <SDL/SDL.h> when porting SDL1 games to
 * ElseaOS.  All SDL calls are mapped to equivalent ElseaOS kernel functions.
 *
 * Supported subset:
 *   - SDL_Init / SDL_Quit
 *   - SDL_SetVideoMode -> allocates a pixel buffer (wm_create_window equivalent)
 *   - SDL_Flip -> blit pixel buffer to VESA framebuffer
 *   - SDL_FillRect
 *   - SDL_PollEvent (stub — keyboard/mouse integration TODO)
 *   - SDL_Delay / SDL_GetTicks
 *   - Basic key constants (SDLK_*)
 */
#pragma once
#include <stdint.h>

/* ── Types ───────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t* pixels;
    int       w, h;
    int       pitch;    /* bytes per row = w * 4 */
} SDL_Surface;

typedef struct {
    int x, y;
    int w, h;
} SDL_Rect;

/* ── Init flags ───────────────────────────────────────────────────────────── */
#define SDL_INIT_VIDEO  0x00000020u
#define SDL_INIT_AUDIO  0x00000010u
#define SDL_INIT_ALL    0x0000FFFFu

/* ── Video mode flags ─────────────────────────────────────────────────────── */
#define SDL_SWSURFACE   0x00000000u
#define SDL_HWSURFACE   0x00000001u
#define SDL_DOUBLEBUF   0x40000000u
#define SDL_FULLSCREEN  0x80000000u

/* ── Key codes (XT scan codes, matching ElseaOS keyboard.c) ──────────────── */
#define SDLK_UP      0x148
#define SDLK_DOWN    0x150
#define SDLK_LEFT    0x14B
#define SDLK_RIGHT   0x14D
#define SDLK_ESCAPE  0x01
#define SDLK_SPACE   ' '
#define SDLK_RETURN  '\r'
#define SDLK_BACKSPACE '\b'
#define SDLK_a       'a'
#define SDLK_z       'z'

/* ── Event types ─────────────────────────────────────────────────────────── */
#define SDL_KEYDOWN         1
#define SDL_KEYUP           2
#define SDL_QUIT            3
#define SDL_MOUSEMOTION     4
#define SDL_MOUSEBUTTONDOWN 5
#define SDL_MOUSEBUTTONUP   6

#define SDL_BUTTON_LEFT     1
#define SDL_BUTTON_MIDDLE   2
#define SDL_BUTTON_RIGHT    3

/* ── Event structs ────────────────────────────────────────────────────────── */

typedef struct {
    uint16_t type;
    struct { uint32_t sym; uint16_t mod; } keysym;
} SDL_KeyboardEvent;

typedef struct {
    uint16_t type;
} SDL_QuitEvent;

typedef struct {
    uint16_t type;
    int16_t  x, y, xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct {
    uint16_t type;
    uint8_t  button;
    uint8_t  state;
    int16_t  x, y;
} SDL_MouseButtonEvent;

typedef union {
    uint16_t              type;
    SDL_KeyboardEvent     key;
    SDL_QuitEvent         quit;
    SDL_MouseMotionEvent  motion;
    SDL_MouseButtonEvent  button;
} SDL_Event;

/* ── Global shim state (defined in sdl_shim.c) ────────────────────────────── */
extern uint32_t* __sdl_framebuf;
extern int       __sdl_screen_w;
extern int       __sdl_screen_h;

/* ── Inline implementations ───────────────────────────────────────────────── */

static inline int SDL_Init(uint32_t flags) {
    (void)flags;
    return 0;
}

static inline void SDL_Quit(void) {
}

/*
 * SDL_SetVideoMode — allocate a back-buffer and record the screen size.
 * In a full port this would call wm_create_window(); for simplicity we
 * allocate a plain pixel buffer and blit it to the VESA framebuffer in
 * SDL_Flip().
 */
static inline SDL_Surface* SDL_SetVideoMode(int w, int h, int bpp, uint32_t flags) {
    (void)bpp; (void)flags;
    extern void* kmalloc(uint32_t);
    static SDL_Surface __sdl_surf;
    __sdl_screen_w = w;
    __sdl_screen_h = h;
    __sdl_framebuf = (uint32_t*)kmalloc((uint32_t)(w * h * 4));
    __sdl_surf.pixels = __sdl_framebuf;
    __sdl_surf.w      = w;
    __sdl_surf.h      = h;
    __sdl_surf.pitch  = w * 4;
    return &__sdl_surf;
}

/*
 * SDL_Flip — copy the back-buffer to the VESA framebuffer at (0,0).
 */
static inline void SDL_Flip(SDL_Surface* s) {
    extern void vesa_blit_region(uint32_t*, int, int, int, int);
    if (s && s->pixels)
        vesa_blit_region(s->pixels, 0, 0, s->w, s->h);
}

static inline void SDL_UpdateRect(SDL_Surface* s, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    SDL_Flip(s);
}

/*
 * SDL_FillRect — fill a rectangle on the surface with a packed 32-bit color.
 * If rect is NULL, fill the entire surface.
 */
static inline void SDL_FillRect(SDL_Surface* s, SDL_Rect* r, uint32_t color) {
    if (!s || !s->pixels) return;
    int x0 = r ? r->x : 0,       y0 = r ? r->y : 0;
    int rw  = r ? r->w : s->w,   rh = r ? r->h : s->h;
    for (int y = y0; y < y0 + rh && y < s->h; y++)
        for (int x = x0; x < x0 + rw && x < s->w; x++)
            s->pixels[y * s->w + x] = color;
}

/*
 * SDL_BlitSurface — software blit from src to dst.
 * srcrect == NULL means whole src; dstrect gives the destination origin.
 */
static inline int SDL_BlitSurface(SDL_Surface* src, SDL_Rect* srcrect,
                                   SDL_Surface* dst, SDL_Rect* dstrect) {
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;
    int sx = srcrect ? srcrect->x : 0, sy = srcrect ? srcrect->y : 0;
    int sw = srcrect ? srcrect->w : src->w, sh = srcrect ? srcrect->h : src->h;
    int dx = dstrect ? dstrect->x : 0,  dy = dstrect ? dstrect->y : 0;
    for (int y = 0; y < sh; y++) {
        int srow = sy + y, drow = dy + y;
        if (srow < 0 || srow >= src->h || drow < 0 || drow >= dst->h) continue;
        for (int x = 0; x < sw; x++) {
            int sc = sx + x, dc = dx + x;
            if (sc < 0 || sc >= src->w || dc < 0 || dc >= dst->w) continue;
            dst->pixels[drow * dst->w + dc] = src->pixels[srow * src->w + sc];
        }
    }
    return 0;
}

/*
 * SDL_Delay — sleep for ms milliseconds using ElseaOS task_sleep.
 */
static inline void SDL_Delay(uint32_t ms) {
    extern void task_sleep(uint32_t);
    task_sleep(ms);
}

/*
 * SDL_GetTicks — milliseconds since boot (PIT tick counter / 100 Hz * 10).
 */
static inline uint32_t SDL_GetTicks(void) {
    extern uint32_t pit_get_ticks(void);
    /* PIT is 100 Hz, each tick = 10 ms */
    return pit_get_ticks() * 10u;
}

/*
 * SDL_PollEvent — poll events from the ElseaOS WM key ring buffer.
 * sdl_push_keydown/keyup are called by wm.c when an SDL window is focused.
 */
void sdl_push_keydown(uint32_t sym);
void sdl_push_keyup(uint32_t sym);
void sdl_push_mousemove(int x, int y, int dx, int dy);
void sdl_push_mousebutton(int down, uint8_t button, int x, int y);
void sdl_push_quit(void);
uint32_t sdl_map_key(char c);
int  sdl_poll_event(SDL_Event* e);

static inline int SDL_PollEvent(SDL_Event* e) {
    return sdl_poll_event(e);
}

/*
 * SDL_WM_SetCaption — set window title (no-op in ElseaOS shim).
 */
static inline void SDL_WM_SetCaption(const char* title, const char* icon) {
    (void)title; (void)icon;
}

/*
 * SDL_MapRGB — map 8-bit R,G,B to packed 32-bit ARGB (0x00RRGGBB).
 * The surface format parameter is ignored.
 */
static inline uint32_t SDL_MapRGB(void* fmt, uint8_t r, uint8_t g, uint8_t b) {
    (void)fmt;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline uint32_t SDL_MapRGBA(void* fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    (void)fmt;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Minimal PixelFormat stub so SDL_MapRGB/A calls compile */
typedef struct { int unused; } SDL_PixelFormat;

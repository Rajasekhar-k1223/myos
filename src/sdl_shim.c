/*
 * sdl_shim.c — global storage + event ring buffer for the SDL 1.x shim.
 */
#include "sdl_shim.h"

uint32_t*   __sdl_framebuf  = 0;
int         __sdl_screen_w  = 0;
int         __sdl_screen_h  = 0;
int         sdl_app_active  = 0;
SDL_Surface __sdl_surf      = {0, 0, 0, 0};

void sdl_emergency_quit(void) {
    sdl_app_active = 0;
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

/* ── Event ring buffer ────────────────────────────────────────────────────── */
#define SDL_EVT_BUF 64
static SDL_Event sdl_evt_buf[SDL_EVT_BUF];
static volatile int sdl_evt_head = 0;
static volatile int sdl_evt_tail = 0;

static inline void _sdl_cli(void) { asm volatile("cli"); }
static inline void _sdl_sti(void) { asm volatile("sti"); }

void sdl_push_keydown(uint32_t sym) {
    _sdl_cli();
    int next = (sdl_evt_head + 1) % SDL_EVT_BUF;
    if (next != sdl_evt_tail) {
        sdl_evt_buf[sdl_evt_head].type           = SDL_KEYDOWN;
        sdl_evt_buf[sdl_evt_head].key.keysym.sym = sym;
        sdl_evt_buf[sdl_evt_head].key.keysym.mod = 0;
        sdl_evt_head = next;
    }
    _sdl_sti();
}

void sdl_push_keyup(uint32_t sym) {
    _sdl_cli();
    int next = (sdl_evt_head + 1) % SDL_EVT_BUF;
    if (next != sdl_evt_tail) {
        sdl_evt_buf[sdl_evt_head].type           = SDL_KEYUP;
        sdl_evt_buf[sdl_evt_head].key.keysym.sym = sym;
        sdl_evt_buf[sdl_evt_head].key.keysym.mod = 0;
        sdl_evt_head = next;
    }
    _sdl_sti();
}

int sdl_poll_event(SDL_Event* e) {
    _sdl_cli();
    if (sdl_evt_head == sdl_evt_tail) { _sdl_sti(); return 0; }
    if (e) *e = sdl_evt_buf[sdl_evt_tail];
    sdl_evt_tail = (sdl_evt_tail + 1) % SDL_EVT_BUF;
    _sdl_sti();
    return 1;
}

/* Map ElseaOS key codes to SDL key symbols */
uint32_t sdl_map_key(char c) {
    if (c == '\x10') return SDLK_UP;
    if (c == '\x11') return SDLK_DOWN;
    if (c == '\x12') return SDLK_LEFT;
    if (c == '\x13') return SDLK_RIGHT;
    if (c == '\x1B') return SDLK_ESCAPE;
    if (c == '\n' || c == '\r') return SDLK_RETURN;
    if (c == '\b') return SDLK_BACKSPACE;
    if (c == ' ') return SDLK_SPACE;
    return (uint32_t)(unsigned char)c;
}

void sdl_push_mousemove(int x, int y, int dx, int dy) {
    _sdl_cli();
    int next = (sdl_evt_head + 1) % SDL_EVT_BUF;
    if (next != sdl_evt_tail) {
        sdl_evt_buf[sdl_evt_head].type        = SDL_MOUSEMOTION;
        sdl_evt_buf[sdl_evt_head].motion.x    = (int16_t)x;
        sdl_evt_buf[sdl_evt_head].motion.y    = (int16_t)y;
        sdl_evt_buf[sdl_evt_head].motion.xrel = (int16_t)dx;
        sdl_evt_buf[sdl_evt_head].motion.yrel = (int16_t)dy;
        sdl_evt_head = next;
    }
    _sdl_sti();
}

void sdl_push_mousebutton(int down, uint8_t button, int x, int y) {
    _sdl_cli();
    int next = (sdl_evt_head + 1) % SDL_EVT_BUF;
    if (next != sdl_evt_tail) {
        sdl_evt_buf[sdl_evt_head].type          = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        sdl_evt_buf[sdl_evt_head].button.button = button;
        sdl_evt_buf[sdl_evt_head].button.state  = (uint8_t)down;
        sdl_evt_buf[sdl_evt_head].button.x      = (int16_t)x;
        sdl_evt_buf[sdl_evt_head].button.y      = (int16_t)y;
        sdl_evt_head = next;
    }
    _sdl_sti();
}

void sdl_push_quit(void) {
    _sdl_cli();
    int next = (sdl_evt_head + 1) % SDL_EVT_BUF;
    if (next != sdl_evt_tail) {
        sdl_evt_buf[sdl_evt_head].type = SDL_QUIT;
        sdl_evt_head = next;
    }
    _sdl_sti();
}

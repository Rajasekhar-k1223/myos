#include "nk_backend.h"
#include "kheap.h"
#include "string.h"
#include "mouse.h"

// Basic math for Nuklear
#define NK_INCLUDE_FIXED_TYPES
#define NK_IMPLEMENTATION
#define NK_MEMSET memset
#define NK_MEMCPY memcpy
#define NK_ASSERT(expr) ((void)0)

static double custom_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 16; i++) r = (r + x / r) * 0.5;
    return r;
}
static double custom_sin(double x) {
    /* reduce to [-π, π] */
    while (x >  3.14159265358979) x -= 6.28318530717959;
    while (x < -3.14159265358979) x += 6.28318530717959;
    double x2 = x * x;
    return x * (1.0 - x2 * (1.0/6.0 - x2 * (1.0/120.0 - x2 * (1.0/5040.0 - x2/362880.0))));
}
static double custom_cos(double x) {
    while (x >  3.14159265358979) x -= 6.28318530717959;
    while (x < -3.14159265358979) x += 6.28318530717959;
    double x2 = x * x;
    return 1.0 - x2 * (0.5 - x2 * (1.0/24.0 - x2 * (1.0/720.0 - x2/40320.0)));
}

#define NK_SIN custom_sin
#define NK_COS custom_cos
#define NK_INV_SQRT(x) (1.0f / (float)custom_sqrt(x))

#include "nuklear.h"

static struct nk_context ctx;
static struct nk_user_font user_font;

static float nk_custom_text_width(nk_handle handle, float h, const char* str, int len) {
    (void)handle; (void)str;
    int ih = (int)h;
    if (ih <= 0) ih = 16;
    return (float)(len * (ih * 6 / 10)); 
}

static uint8_t nk_memory[512 * 1024];

void nk_elseaos_init(void) {
    // Set up dummy user font since we hook NK_COMMAND_TEXT directly
    user_font.userdata.ptr = 0;
    user_font.height = 16;
    user_font.width = nk_custom_text_width;
    
    nk_init_fixed(&ctx, nk_memory, sizeof(nk_memory), &user_font);
}

struct nk_user_font nk_elseaos_create_font(float size) {
    struct nk_user_font f;
    f.userdata.ptr = (void*)0;
    f.height = size;
    f.width = nk_custom_text_width;
    return f;
}

struct nk_user_font nk_elseaos_create_font_bold(float size) {
    struct nk_user_font f;
    f.userdata.ptr = (void*)1;
    f.height = size;
    f.width = nk_custom_text_width;
    return f;
}

struct nk_context* nk_elseaos_get_context(void) {
    return &ctx;
}

void nk_elseaos_process_input(void) {
    extern int mouse_get_x(void);
    extern int mouse_get_y(void);
    extern uint8_t mouse_get_buttons(void);
    
    int mx = mouse_get_x();
    int my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();
    
    nk_input_begin(&ctx);
    nk_input_motion(&ctx, mx, my);
    nk_input_button(&ctx, NK_BUTTON_LEFT, mx, my, (btns & 1) ? nk_true : nk_false);
    nk_input_button(&ctx, NK_BUTTON_RIGHT, mx, my, (btns & 2) ? nk_true : nk_false);
    nk_input_end(&ctx);
}

// Helper to calculate squared distance
static int dist_sq(int x1, int y1, int x2, int y2) {
    return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
}

void nk_elseaos_render(void) {
    const struct nk_command *cmd;
    extern uint32_t vesa_width, vesa_height;

    int clip_x = 0;
    int clip_y = 0;
    int clip_w = vesa_width;
    int clip_h = vesa_height;
    
    // Iterate over Nuklear commands and draw using vesa/wm
    nk_foreach(cmd, &ctx) {
        switch (cmd->type) {
            case NK_COMMAND_NOP: break;
            case NK_COMMAND_SCISSOR: {
                const struct nk_command_scissor *s = (const struct nk_command_scissor *)cmd;
                clip_x = s->x;
                clip_y = s->y;
                clip_w = s->w;
                clip_h = s->h;
            } break;
            case NK_COMMAND_LINE: {
                const struct nk_command_line *l = (const struct nk_command_line *)cmd;
                uint32_t c = (l->color.r << 16) | (l->color.g << 8) | l->color.b;
                int x0 = l->begin.x, y0 = l->begin.y;
                int x1 = l->end.x, y1 = l->end.y;
                int dx = x1 > x0 ? x1 - x0 : x0 - x1;
                int dy = y1 > y0 ? y0 - y1 : y1 - y0;
                int sx = x0 < x1 ? 1 : -1;
                int sy = y0 < y1 ? 1 : -1;
                int err = dx + dy, e2;
                while (1) {
                    if (x0 >= clip_x && y0 >= clip_y && x0 < clip_x + clip_w && y0 < clip_y + clip_h) {
                        // Thick lines (Nuklear uses l->line_thickness)
                        for (int ty = 0; ty < l->line_thickness; ty++) {
                            for (int tx = 0; tx < l->line_thickness; tx++) {
                                vesa_putpixel_alpha(x0 + tx, y0 + ty, c, l->color.a);
                            }
                        }
                    }
                    if (x0 == x1 && y0 == y1) break;
                    e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x0 += sx; }
                    if (e2 <= dx) { err += dx; y0 += sy; }
                }
            } break;
            case NK_COMMAND_RECT: {
                const struct nk_command_rect *r = (const struct nk_command_rect *)cmd;
                uint32_t c = (r->color.r << 16) | (r->color.g << 8) | r->color.b;
                int rad = r->rounding;
                if (rad > r->w / 2) rad = r->w / 2;
                if (rad > r->h / 2) rad = r->h / 2;
                for (int y = r->y; y < r->y + r->h; y++) {
                    for (int x = r->x; x < r->x + r->w; x++) {
                        if (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h) continue;
                        if (x == r->x || x == r->x + r->w - 1 || y == r->y || y == r->y + r->h - 1) {
                            // Skip drawing sharp corners if rounded
                            if (rad > 0) {
                                if (x < r->x + rad && y < r->y + rad) continue;
                                if (x >= r->x + r->w - rad && y < r->y + rad) continue;
                                if (x < r->x + rad && y >= r->y + r->h - rad) continue;
                                if (x >= r->x + r->w - rad && y >= r->y + r->h - rad) continue;
                            }
                            vesa_putpixel_alpha(x, y, c, r->color.a);
                        }
                    }
                }
            } break;
            case NK_COMMAND_RECT_FILLED: {
                const struct nk_command_rect_filled *r = (const struct nk_command_rect_filled *)cmd;
                uint32_t c = (r->color.r << 16) | (r->color.g << 8) | r->color.b;
                int rad = r->rounding;
                if (rad > r->w / 2) rad = r->w / 2;
                if (rad > r->h / 2) rad = r->h / 2;

                /* Fast path: opaque rect with no rounding — direct backbuffer row fill */
                if (r->color.a == 255 && rad == 0) {
                    extern uint32_t* vesa_get_backbuffer(void);
                    uint32_t* buf = vesa_get_backbuffer();
                    if (buf) {
                        int y0 = r->y > clip_y ? r->y : clip_y;
                        int y1 = (r->y + r->h) < (clip_y + clip_h) ? (r->y + r->h) : (clip_y + clip_h);
                        int x0 = r->x > clip_x ? r->x : clip_x;
                        int x1 = (r->x + r->w) < (clip_x + clip_w) ? (r->x + r->w) : (clip_x + clip_w);
                        if (x1 > x0) {
                            for (int y = y0; y < y1; y++) {
                                uint32_t* row = buf + (uint32_t)y * vesa_width + (uint32_t)x0;
                                int n = x1 - x0;
                                for (int i = 0; i < n; i++) row[i] = c;
                            }
                        }
                        break;
                    }
                }

                /* Slow path: rounded corners or alpha blending */
                for (int y = r->y; y < r->y + r->h; y++) {
                    for (int x = r->x; x < r->x + r->w; x++) {
                        if (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h) continue;
                        if (rad > 0) {
                            int r2 = rad * rad;
                            if (x < r->x + rad && y < r->y + rad) {
                                if (dist_sq(x, y, r->x + rad - 1, r->y + rad - 1) > r2) continue;
                            } else if (x >= r->x + r->w - rad && y < r->y + rad) {
                                if (dist_sq(x, y, r->x + r->w - rad, r->y + rad - 1) > r2) continue;
                            } else if (x < r->x + rad && y >= r->y + r->h - rad) {
                                if (dist_sq(x, y, r->x + rad - 1, r->y + r->h - rad) > r2) continue;
                            } else if (x >= r->x + r->w - rad && y >= r->y + r->h - rad) {
                                if (dist_sq(x, y, r->x + r->w - rad, r->y + r->h - rad) > r2) continue;
                            }
                        }
                        vesa_putpixel_alpha(x, y, c, r->color.a);
                    }
                }
            } break;
            case NK_COMMAND_TEXT: {
                const struct nk_command_text *t = (const struct nk_command_text *)cmd;
                uint32_t c = (t->foreground.r << 16) | (t->foreground.g << 8) | t->foreground.b;
                
                extern void ttf_draw_string(uint32_t* buffer, int width, int height, int x, int y, const char* str, int len, int font_size, uint32_t color);
                extern uint32_t* vesa_get_backbuffer(void);
                extern uint32_t vesa_width, vesa_height;
                
                int font_sz = (int)t->height;
                if (font_sz <= 0) font_sz = 16;
                // baseline is usually ~80% of height from the top.
                int baseline_y = t->y + (font_sz * 4) / 5;
                
                ttf_draw_string(vesa_get_backbuffer(), vesa_width, vesa_height, t->x, baseline_y, (const char*)t->string, t->length, font_sz, c);
                
                int is_bold = (int)(long)t->font->userdata.ptr;
                if (is_bold) {
                    ttf_draw_string(vesa_get_backbuffer(), vesa_width, vesa_height, t->x + 1, baseline_y, (const char*)t->string, t->length, font_sz, c);
                }
            } break;
            case NK_COMMAND_IMAGE: {
                const struct nk_command_image *i = (const struct nk_command_image *)cmd;
                const char* filename = (const char*)i->img.handle.ptr;
                if (filename) {
                    extern void bmp_load_to_buffer_scaled(const char* filename, uint32_t* buffer, int buf_w, int buf_h, int offset_x, int offset_y, int scale_w, int scale_h);
                    extern uint32_t* vesa_get_backbuffer(void);
                    extern uint32_t vesa_width, vesa_height;
                    bmp_load_to_buffer_scaled(filename, vesa_get_backbuffer(), vesa_width, vesa_height, i->x, i->y, i->w, i->h);
                }
            } break;
            default: break;
        }
    }

    nk_clear(&ctx);
}

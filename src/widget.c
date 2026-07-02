#include "widget.h"
#include "vesa.h"
#include "font16.h"
#include "string.h"
#include "pit.h"

// ─── Color helpers ────────────────────────────────────────────────────────────

uint32_t widget_blend_color(uint32_t a, uint32_t b, uint8_t factor) {
    uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    uint8_t inv = 255 - factor;
    uint8_t rr = (ar * inv + br * factor) >> 8;
    uint8_t rg = (ag * inv + bg * factor) >> 8;
    uint8_t rb = (ab * inv + bb * factor) >> 8;
    return ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
}

uint32_t widget_tint_color(uint32_t color, uint32_t tint) {
    uint8_t cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
    uint8_t tr = (tint  >> 16) & 0xFF, tg = (tint  >> 8) & 0xFF, tb = tint  & 0xFF;
    return (((uint32_t)(cr * tr / 255)) << 16) |
           (((uint32_t)(cg * tg / 255)) <<  8) |
            ((uint32_t)(cb * tb / 255));
}

uint32_t widget_lerp_pixel(uint32_t src, uint32_t dst, uint8_t t) {
    return widget_blend_color(src, dst, t);
}

// ─── Rounded rectangle ────────────────────────────────────────────────────────

static int _circle_mask(int cx, int cy, int px, int py, int r) {
    int dx = px - cx, dy = py - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void widget_draw_rounded_rect(int x, int y, int w, int h, int r,
                              uint32_t color, uint8_t alpha) {
    if (r <= 0) { vesa_draw_rect_alpha(x, y, w, h, color, alpha); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col, py = y + row;
            int in_corner = 0;
            if (col < r && row < r)
                in_corner = !_circle_mask(x + r - 1, y + r - 1, px, py, r);
            else if (col >= w - r && row < r)
                in_corner = !_circle_mask(x + w - r, y + r - 1, px, py, r);
            else if (col < r && row >= h - r)
                in_corner = !_circle_mask(x + r - 1, y + h - r, px, py, r);
            else if (col >= w - r && row >= h - r)
                in_corner = !_circle_mask(x + w - r, y + h - r, px, py, r);

            if (!in_corner) {
                vesa_putpixel_alpha((uint32_t)px, (uint32_t)py, color, alpha);
            }
        }
    }
}

// ─── Gradient rectangles ──────────────────────────────────────────────────────

void widget_draw_gradient_rect(int x, int y, int w, int h,
                               uint32_t top_color, uint32_t bottom_color,
                               uint8_t alpha) {
    for (int row = 0; row < h; row++) {
        uint8_t t = (uint8_t)((row * 255) / (h > 1 ? h - 1 : 1));
        uint32_t c = widget_blend_color(top_color, bottom_color, t);
        for (int col = 0; col < w; col++) {
            vesa_putpixel_alpha(x + col, y + row, c, alpha);
        }
    }
}

void widget_draw_hgradient_rect(int x, int y, int w, int h,
                                uint32_t left_color, uint32_t right_color,
                                uint8_t alpha) {
    for (int col = 0; col < w; col++) {
        uint8_t t = (uint8_t)((col * 255) / (w > 1 ? w - 1 : 1));
        uint32_t c = widget_blend_color(left_color, right_color, t);
        for (int row = 0; row < h; row++) {
            vesa_putpixel_alpha(x + col, y + row, c, alpha);
        }
    }
}

// ─── Glass / frosted panel ────────────────────────────────────────────────────

void widget_draw_glass(int x, int y, int w, int h,
                       uint32_t tint_color, uint8_t tint_alpha,
                       uint8_t blur_passes) {
    extern uint32_t vesa_width, vesa_height;

    for (uint8_t pass = 0; pass < blur_passes; pass++) {
        for (int row = y; row < y + h; row++) {
            for (int col = x; col < x + w; col++) {
                uint32_t sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = col + dx, ny = row + dy;
                        if (nx < 0 || ny < 0 || (uint32_t)nx >= vesa_width || (uint32_t)ny >= vesa_height) continue;
                        uint32_t p = vesa_getpixel((uint32_t)nx, (uint32_t)ny);
                        sum_r += (p >> 16) & 0xFF;
                        sum_g += (p >>  8) & 0xFF;
                        sum_b +=  p        & 0xFF;
                        count++;
                    }
                }
                if (count > 0) {
                    uint32_t blurred = ((sum_r / count) << 16) |
                                       ((sum_g / count) <<  8) |
                                        (sum_b / count);
                    vesa_putpixel((uint32_t)col, (uint32_t)row, blurred);
                }
            }
        }
    }

    vesa_draw_rect_alpha((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                         tint_color, tint_alpha);

    for (int col = x; col < x + w; col++)
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)y, 0xFFFFFF, 60);
    for (int row = y; row < y + h; row++)
        vesa_putpixel_alpha((uint32_t)x, (uint32_t)row, 0xFFFFFF, 40);
}

// ─── Internal: draw a single 8x16 character ──────────────────────────────────

static void _draw_char(int x, int y, unsigned char c, uint32_t fg, uint8_t alpha) {
    for (int row = 0; row < 16; row++) {
        uint8_t bits = font8x16[(unsigned int)c][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << (7 - col)))
                vesa_putpixel_alpha((uint32_t)(x + col), (uint32_t)(y + row), fg, alpha);
        }
    }
}

static void _draw_string(int x, int y, const char* s, uint32_t fg, uint8_t alpha) {
    for (int i = 0; s[i]; i++)
        _draw_char(x + i * 8, y, (unsigned char)s[i], fg, alpha);
}

// ─── Button ───────────────────────────────────────────────────────────────────

int widget_draw_button(int x, int y, int w, int h,
                       const char* label, uint32_t bg, uint32_t fg,
                       int hovered) {
    uint32_t top = hovered ? widget_blend_color(bg, 0xFFFFFF, 40)
                           : widget_blend_color(bg, 0xFFFFFF, 20);
    uint32_t bot = hovered ? widget_blend_color(bg, 0x000000, 30)
                           :  bg;

    widget_draw_gradient_rect(x, y, w, h, top, bot, 240);
    widget_draw_rounded_rect(x, y, w, h, 4, top, 255);

    for (int col = x + 1; col < x + w - 1; col++)
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)y, 0xFFFFFF, 80);
    for (int col = x + 1; col < x + w - 1; col++)
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)(y + h - 1), 0x000000, 80);

    int label_len = (int)strlen(label);
    int lx = x + (w - label_len * 8) / 2;
    int ly = y + (h - 16) / 2;
    _draw_string(lx, ly, label, fg, 230);
    return 1;
}

// ─── Progress bar ─────────────────────────────────────────────────────────────

void widget_draw_progress(int x, int y, int w, int h,
                          int value, uint32_t fill_color) {
    vesa_draw_rect_alpha((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                         0x1A1A2E, 220);

    if (value > 0) {
        int fill_w = (w * value) / 100;
        if (fill_w < 1) fill_w = 1;
        widget_draw_gradient_rect(x, y, fill_w, h,
                                  widget_blend_color(fill_color, 0xFFFFFF, 60),
                                  fill_color, 255);
        for (int col = x; col < x + fill_w; col++)
            vesa_putpixel_alpha((uint32_t)col, (uint32_t)y, 0xFFFFFF, 50);
    }

    for (int col = x; col < x + w; col++) {
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)y, 0x445566, 180);
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)(y + h - 1), 0x445566, 180);
    }
    for (int row = y; row < y + h; row++) {
        vesa_putpixel_alpha((uint32_t)x, (uint32_t)row, 0x445566, 180);
        vesa_putpixel_alpha((uint32_t)(x + w - 1), (uint32_t)row, 0x445566, 180);
    }
}

// ─── Badge / pill ─────────────────────────────────────────────────────────────

void widget_draw_badge(int x, int y, const char* text,
                       uint32_t bg, uint32_t fg) {
    int len = (int)strlen(text);
    int bw = len * 8 + 10;
    int bh = 18;

    widget_draw_rounded_rect(x, y, bw, bh, 6, bg, 220);
    _draw_string(x + 5, y + 1, text, fg, 230);
}

// ─── Combo Box / Dropdown ─────────────────────────────────────────────────────

#define COMBO_H      24   /* header height */
#define COMBO_ITEM_H 20   /* per-item height in open list */

void widget_draw_combobox(int x, int y, int w, const char** items, int item_count,
                          int selected, int open, int mx, int my, uint32_t accent) {
    // Draw the header box
    uint32_t bg_top = 0x1E2A3C;
    uint32_t bg_bot = 0x141C2B;
    widget_draw_gradient_rect(x, y, w, COMBO_H, bg_top, bg_bot, 245);

    // Border
    for (int col = x; col < x + w; col++) {
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)y,              0x334455, 200);
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)(y + COMBO_H - 1), 0x334455, 200);
    }
    for (int row = y; row < y + COMBO_H; row++) {
        vesa_putpixel_alpha((uint32_t)x,         (uint32_t)row, 0x334455, 200);
        vesa_putpixel_alpha((uint32_t)(x + w - 1), (uint32_t)row, 0x334455, 200);
    }

    // Selected item text (left, 4px pad, vertically centered in 24px)
    if (selected >= 0 && selected < item_count) {
        _draw_string(x + 4, y + (COMBO_H - 16) / 2, items[selected], 0xDDEEFF, 230);
    }

    // Arrow "v" on right (use simple ASCII 'v' for compatibility)
    int arrow_x = x + w - 14;
    int arrow_y = y + (COMBO_H - 16) / 2;
    _draw_char(arrow_x, arrow_y, (unsigned char)'v', open ? accent : 0x8899AA, 220);

    // If open: draw dropdown list below header
    if (open && item_count > 0) {
        int list_y = y + COMBO_H;
        int list_h = item_count * COMBO_ITEM_H;

        // List background
        vesa_draw_rect_alpha((uint32_t)x, (uint32_t)list_y, (uint32_t)w, (uint32_t)list_h,
                             0x0D1622, 245);

        // Border around list
        for (int col = x; col < x + w; col++) {
            vesa_putpixel_alpha((uint32_t)col, (uint32_t)(list_y + list_h - 1), 0x334455, 200);
        }
        for (int row = list_y; row < list_y + list_h; row++) {
            vesa_putpixel_alpha((uint32_t)x,           (uint32_t)row, 0x334455, 200);
            vesa_putpixel_alpha((uint32_t)(x + w - 1), (uint32_t)row, 0x334455, 200);
        }

        // Draw each item
        for (int i = 0; i < item_count; i++) {
            int iy = list_y + i * COMBO_ITEM_H;
            int hovered_item = (mx >= x && mx < x + w && my >= iy && my < iy + COMBO_ITEM_H);
            int is_selected  = (i == selected);

            if (hovered_item || is_selected) {
                uint32_t hi = is_selected ? widget_blend_color(accent, 0x000000, 80)
                                          : widget_blend_color(accent, 0x000000, 130);
                vesa_draw_rect_alpha((uint32_t)(x + 1), (uint32_t)iy,
                                     (uint32_t)(w - 2), (uint32_t)COMBO_ITEM_H, hi, 200);
            }

            // Item separator line
            if (i > 0) {
                for (int col = x + 2; col < x + w - 2; col++)
                    vesa_putpixel_alpha((uint32_t)col, (uint32_t)iy, 0x223344, 150);
            }

            uint32_t text_col = (hovered_item || is_selected) ? 0xFFFFFF : 0xBBCCDD;
            _draw_string(x + 6, iy + (COMBO_ITEM_H - 16) / 2, items[i], text_col, 230);
        }
    }
}

int widget_combobox_click(int x, int y, int w, const char** items, int item_count,
                          int selected, int* out_open, int mx, int my) {
    // Check header click
    if (mx >= x && mx < x + w && my >= y && my < y + COMBO_H) {
        *out_open = !(*out_open);
        return -1;
    }

    // Check item clicks when open
    if (*out_open) {
        int list_y = y + COMBO_H;
        for (int i = 0; i < item_count; i++) {
            int iy = list_y + i * COMBO_ITEM_H;
            if (mx >= x && mx < x + w && my >= iy && my < iy + COMBO_ITEM_H) {
                *out_open = 0;
                return i;
            }
        }
    }

    (void)items; (void)selected;
    return -1;
}

// ─── Text Input Widget ────────────────────────────────────────────────────────

void widget_textinput_init(widget_textinput_t* t) {
    memset(t->buf, 0, sizeof(t->buf));
    t->len     = 0;
    t->cursor  = 0;
    t->focused = 0;
}

void widget_textinput_draw(int x, int y, int w, widget_textinput_t* t, uint32_t accent) {
    int h = 24;

    // Background
    vesa_draw_rect_alpha((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                         0x1C2333, 245);

    // Border: blue when focused, dim otherwise
    uint32_t border_col = t->focused ? accent : 0x334455;
    uint8_t  border_alpha = t->focused ? 220 : 160;
    for (int col = x; col < x + w; col++) {
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)y,          border_col, border_alpha);
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)(y + h - 1), border_col, border_alpha);
    }
    for (int row = y; row < y + h; row++) {
        vesa_putpixel_alpha((uint32_t)x,           (uint32_t)row, border_col, border_alpha);
        vesa_putpixel_alpha((uint32_t)(x + w - 1), (uint32_t)row, border_col, border_alpha);
    }

    // Text with 4px padding; clip to widget width
    int text_x  = x + 4;
    int text_y  = y + (h - 16) / 2;
    int max_chars = (w - 8) / 8;  // visible character count

    // Determine scroll offset so cursor stays visible
    int scroll = 0;
    if (t->cursor > max_chars - 1)
        scroll = t->cursor - (max_chars - 1);

    for (int i = scroll; i < t->len && (i - scroll) < max_chars; i++) {
        unsigned char ch = t->is_password ? (unsigned char)'*' : (unsigned char)t->buf[i];
        _draw_char(text_x + (i - scroll) * 8, text_y, ch, 0xDDEEFF, 230);
    }

    // Blinking cursor when focused
    if (t->focused) {
        uint32_t ticks = pit_get_ticks();
        if ((ticks % 100) < 50) {
            int cx = text_x + (t->cursor - scroll) * 8;
            if (cx >= x + 2 && cx < x + w - 4) {
                for (int row = text_y; row < text_y + 16; row++)
                    vesa_putpixel_alpha((uint32_t)cx, (uint32_t)row, 0xFFFFFF, 220);
            }
        }
    }
}

void widget_textinput_key(widget_textinput_t* t, char c) {
    if (!t->focused) return;

    if (c == 0x08) {
        // Backspace: delete char before cursor
        if (t->cursor > 0) {
            for (int i = t->cursor - 1; i < t->len - 1; i++)
                t->buf[i] = t->buf[i + 1];
            t->len--;
            t->cursor--;
            t->buf[t->len] = '\0';
        }
    } else if (c == 0x7F) {
        // Delete: delete char after cursor
        if (t->cursor < t->len) {
            for (int i = t->cursor; i < t->len - 1; i++)
                t->buf[i] = t->buf[i + 1];
            t->len--;
            t->buf[t->len] = '\0';
        }
    } else if (c == 0x01) {
        // Ctrl-A / Home equivalent (keyboard.c may send 0x01 or a special code)
        t->cursor = 0;
    } else if (c == 0x05) {
        // Ctrl-E / End
        t->cursor = t->len;
    } else if (c == '\x1B') {
        // ESC — unfocus
        t->focused = 0;
    } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
        // Printable character: insert at cursor
        if (t->len < 127) {
            for (int i = t->len; i > t->cursor; i--)
                t->buf[i] = t->buf[i - 1];
            t->buf[t->cursor] = c;
            t->cursor++;
            t->len++;
            t->buf[t->len] = '\0';
        }
    }
    // Arrow keys: keyboard.c typically sends these as multi-byte sequences
    // via a special key code — handle common single-byte forms.
    // Left arrow often arrives as 0x04 (Ctrl-D style) in freestanding envs;
    // right as 0x06. Fall back to cursor movement if those match.
    // The actual values depend on keyboard.c; callers can also call this
    // function with a sentinel value and handle arrows externally.
}

// Convenience: move cursor left / right (for callers that decode arrows separately)
static void _textinput_move_left(widget_textinput_t* t)  { if (t->cursor > 0)      t->cursor--; }
static void _textinput_move_right(widget_textinput_t* t) { if (t->cursor < t->len) t->cursor++; }

// Expose cursor movement so callers can wire arrow keys without re-parsing
// (declared inline as static; callers in the same TU can use them directly,
//  or just call widget_textinput_key with the codes below).
//
// We also handle the most common Linux-console arrow-key bytes that many
// bare-metal keyboard drivers emit after decoding scan codes:
//   Left  = 0xCB (scan 0x4B),  Right = 0xCD (scan 0x4D)
//   Home  = 0xC7,              End   = 0xCF
// (These are the PS/2 extended key bytes before ASCII mapping.)
// If keyboard.c sends different values, callers handle it in their event loop.

// widget_textinput_key also accepts these extended bytes:
// We patch them in here to avoid a second function:
//   (nothing additional needed — callers can call _textinput_move_left/_right)
// Made non-static so linker won't warn; prefix to avoid symbol conflicts.
void widget_textinput_left(widget_textinput_t* t)  { _textinput_move_left(t); }
void widget_textinput_right(widget_textinput_t* t) { _textinput_move_right(t); }
void widget_textinput_home(widget_textinput_t* t)  { t->cursor = 0; }
void widget_textinput_end(widget_textinput_t* t)   { t->cursor = t->len; }

int widget_textinput_click(int x, int y, int w, int mx, int my) {
    int h = 24;
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

// ─── Resizable Splitter ───────────────────────────────────────────────────────

#define SPLITTER_H 4

void widget_splitter_init(widget_splitter_t* s, int initial_y,
                          int min_top, int min_bottom) {
    s->split_y    = initial_y;
    s->dragging   = 0;
    s->min_top    = min_top;
    s->min_bottom = min_bottom;
}

void widget_splitter_draw(int x, int y, int w, widget_splitter_t* s, int accent_color) {
    (void)s;   // split_y is used by the caller to position (x,y)
    uint32_t accent = (uint32_t)accent_color;

    // Base gradient: very dark at edges, slightly lighter center line
    uint32_t dark   = 0x111820;
    uint32_t center = 0x2A3A50;

    widget_draw_gradient_rect(x, y,         w, 1, dark,   dark,   230);
    widget_draw_gradient_rect(x, y + 1,     w, 2, center, dark,   230);
    widget_draw_gradient_rect(x, y + 3,     w, 1, dark,   dark,   230);

    // Top highlight line (subtle)
    for (int col = x; col < x + w; col++)
        vesa_putpixel_alpha((uint32_t)col, (uint32_t)y, 0x334455, 120);

    // Grab handle: 3 dots centered
    int dot_spacing = 6;
    int dot_x = x + w / 2 - dot_spacing;
    int dot_y = y + SPLITTER_H / 2;
    for (int d = 0; d < 3; d++) {
        int dx = dot_x + d * dot_spacing;
        vesa_putpixel_alpha((uint32_t)dx,     (uint32_t)dot_y, accent, 200);
        vesa_putpixel_alpha((uint32_t)(dx+1), (uint32_t)dot_y, accent, 150);
    }
}

int widget_splitter_handle(widget_splitter_t* s, int mx, int my,
                           int mouse_down, int total_h) {
    int bar_y = s->split_y;

    // Check if mouse is on the bar (within 4px vertically)
    int on_bar = (my >= bar_y - 2 && my <= bar_y + SPLITTER_H + 2);

    if (mouse_down) {
        if (on_bar || s->dragging) {
            s->dragging = 1;
            // Move split to mouse position, clamped
            int new_y = my - SPLITTER_H / 2;
            if (new_y < s->min_top)
                new_y = s->min_top;
            if (new_y > total_h - s->min_bottom - SPLITTER_H)
                new_y = total_h - s->min_bottom - SPLITTER_H;
            s->split_y = new_y;
        }
    } else {
        s->dragging = 0;
    }

    (void)mx; (void)on_bar;
    return s->split_y;
}

// ─── Tab Widget ───────────────────────────────────────────────────────────────

void widget_tab_init(widget_tab_t* t) {
    t->num_tabs = 0;
    t->active_tab = 0;
}

void widget_tab_add(widget_tab_t* t, const char* title) {
    if (t->num_tabs >= WIDGET_TAB_MAX) return;
    int i = 0;
    while (title[i] && i < 31) {
        t->tabs[t->num_tabs][i] = title[i];
        i++;
    }
    t->tabs[t->num_tabs][i] = '\0';
    t->num_tabs++;
}

void widget_tab_draw(int x, int y, int w, widget_tab_t* t, uint32_t bg, uint32_t active_bg, uint32_t fg) {
    if (t->num_tabs == 0) return;
    int tab_w = w / t->num_tabs;
    for (int i = 0; i < t->num_tabs; i++) {
        uint32_t c_bg = (i == t->active_tab) ? active_bg : bg;
        widget_draw_rounded_rect(x + i * tab_w, y, tab_w - 2, 24, 4, c_bg, 255);
        _draw_string(x + i * tab_w + 10, y + 4, t->tabs[i], fg, 255);
    }
}

int widget_tab_click(int x, int y, int w, widget_tab_t* t, int mx, int my) {
    if (t->num_tabs == 0) return 0;
    int tab_w = w / t->num_tabs;
    if (my >= y && my <= y + 24) {
        for (int i = 0; i < t->num_tabs; i++) {
            int tx = x + i * tab_w;
            if (mx >= tx && mx <= tx + tab_w - 2) {
                if (t->active_tab != i) {
                    t->active_tab = i;
                    return 1;
                }
            }
        }
    }
    return 0;
}

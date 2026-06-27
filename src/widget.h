#ifndef WIDGET_H
#define WIDGET_H

#include <stdint.h>

// ─── Primitive draw helpers (draw directly to backbuffer via vesa) ────────────

// Draw a filled rounded rectangle (radius in pixels)
void widget_draw_rounded_rect(int x, int y, int w, int h, int r,
                              uint32_t color, uint8_t alpha);

// Draw a gradient rect (top_color → bottom_color, vertical)
void widget_draw_gradient_rect(int x, int y, int w, int h,
                               uint32_t top_color, uint32_t bottom_color,
                               uint8_t alpha);

// Draw a horizontal gradient rect (left → right)
void widget_draw_hgradient_rect(int x, int y, int w, int h,
                                uint32_t left_color, uint32_t right_color,
                                uint8_t alpha);

// Draw a "glass" panel: frosted, semi-transparent blurred rect
void widget_draw_glass(int x, int y, int w, int h,
                       uint32_t tint_color, uint8_t tint_alpha,
                       uint8_t blur_passes);

// Draw a labelled button; returns 1 if the given (mx,my) is inside
int  widget_draw_button(int x, int y, int w, int h,
                        const char* label, uint32_t bg, uint32_t fg,
                        int hovered);

// Draw a progress bar (value 0..100)
void widget_draw_progress(int x, int y, int w, int h,
                          int value, uint32_t fill_color);

// Draw a badge/pill label (e.g. version tag, count)
void widget_draw_badge(int x, int y, const char* text,
                       uint32_t bg, uint32_t fg);

// Blend two 32-bit RGB colors by factor (0=a, 255=b)
uint32_t widget_blend_color(uint32_t a, uint32_t b, uint8_t factor);

// Tint a color (multiply each channel by tint's channels/255)
uint32_t widget_tint_color(uint32_t color, uint32_t tint);

// Lerp a single pixel toward target (for smooth animations)
uint32_t widget_lerp_pixel(uint32_t src, uint32_t dst, uint8_t t);

// ─── Combo Box / Dropdown ─────────────────────────────────────────────────────
// Draw a combo box at (x,y) with width w.
// items: array of C-strings, item_count: number of items.
// selected: currently selected index.
// open: 1 if dropdown is open (shows all items), 0 if closed (shows selected).
// mx, my: mouse position for hover highlighting (-1,-1 if not needed).
// accent: accent color used for hover highlight.
void widget_draw_combobox(int x, int y, int w, const char** items, int item_count,
                          int selected, int open, int mx, int my, uint32_t accent);

// Handle a click at (mx,my) on the combo box.
// *out_open is toggled when the header is clicked, or cleared when an item is chosen.
// Returns chosen item index (0..item_count-1), or -1 if nothing was selected yet.
int  widget_combobox_click(int x, int y, int w, const char** items, int item_count,
                           int selected, int* out_open, int mx, int my);

// ─── Text Input Widget ────────────────────────────────────────────────────────

typedef struct {
    char    buf[128];
    int     len;
    int     cursor;   // cursor position in buf (0..len)
    int     focused;
} widget_textinput_t;

void widget_textinput_init(widget_textinput_t* t);
void widget_textinput_draw(int x, int y, int w, widget_textinput_t* t, uint32_t accent);
void widget_textinput_key(widget_textinput_t* t, char c);   // handle keypress
int  widget_textinput_click(int x, int y, int w, int mx, int my); // 1 if hit

// ─── Resizable Splitter ───────────────────────────────────────────────────────

typedef struct {
    int split_y;      // current split Y position (from top of container)
    int dragging;     // 1 while mouse is held on the bar
    int min_top;      // minimum top pane height
    int min_bottom;   // minimum bottom pane height
} widget_splitter_t;

void widget_splitter_init(widget_splitter_t* s, int initial_y,
                          int min_top, int min_bottom);
// Draw the 4-px splitter bar. x/y are the absolute screen position of the bar.
void widget_splitter_draw(int x, int y, int w, widget_splitter_t* s, int accent_color);
// Process mouse event; returns new split_y value.
int  widget_splitter_handle(widget_splitter_t* s, int mx, int my,
                            int mouse_down, int total_h);

#endif

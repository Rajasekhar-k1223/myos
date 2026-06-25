#ifndef KERNEL_H
#define KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ── VGA color palette ───────────────────────────────────────────────────── */
enum vga_color {
    VGA_COLOR_BLACK        = 0,  VGA_COLOR_BLUE         = 1,
    VGA_COLOR_GREEN        = 2,  VGA_COLOR_CYAN         = 3,
    VGA_COLOR_RED          = 4,  VGA_COLOR_MAGENTA      = 5,
    VGA_COLOR_BROWN        = 6,  VGA_COLOR_LIGHT_GREY   = 7,
    VGA_COLOR_DARK_GREY    = 8,  VGA_COLOR_LIGHT_BLUE   = 9,
    VGA_COLOR_LIGHT_GREEN  = 10, VGA_COLOR_LIGHT_CYAN   = 11,
    VGA_COLOR_LIGHT_RED    = 12, VGA_COLOR_LIGHT_MAGENTA= 13,
    VGA_COLOR_LIGHT_BROWN  = 14, VGA_COLOR_WHITE        = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return (uint8_t)(fg | (bg << 4));
}

/* ── Terminal API ────────────────────────────────────────────────────────── */
void terminal_initialize(void);
void terminal_clear(void);
void terminal_setcolor(uint8_t color);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_writehex(uint32_t n);
void terminal_writedec(uint32_t n);
void terminal_printf(const char* fmt, ...);
void terminal_vprintf(const char* fmt, va_list ap);

/* Cursor */
void terminal_setpos(uint32_t row, uint32_t col);
void terminal_getpos(uint32_t* row, uint32_t* col);
void terminal_cursor_show(int visible);

#endif

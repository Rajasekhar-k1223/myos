#include <stddef.h>
#include <stdint.h>
#include "kernel.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | (uint16_t)color << 8;
}

static size_t   terminal_row;
static size_t   terminal_column;
static uint8_t  terminal_color;
static uint16_t* terminal_buffer;

static void terminal_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[y * VGA_WIDTH + x] =
                terminal_buffer[(y + 1) * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            vga_entry(' ', terminal_color);
    terminal_row = VGA_HEIGHT - 1;
}

void terminal_initialize(void) {
    terminal_row    = 0;
    terminal_column = 0;
    terminal_color  = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = (uint16_t*)0xB8000;
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    terminal_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
}

static void terminal_advance(void) {
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) terminal_scroll();
    }
}

void terminal_putchar(char c) {
    switch (c) {
    case '\n':
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) terminal_scroll();
        return;
    case '\r':
        terminal_column = 0;
        return;
    case '\t':
        terminal_column = (terminal_column + 8) & ~(size_t)7;
        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) terminal_scroll();
        }
        return;
    case '\b':
        if (terminal_column > 0) {
            --terminal_column;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
        return;
    default:
        break;
    }
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    terminal_advance();
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++)
        terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
    while (*data) terminal_putchar(*data++);
}

void kernel_main(void) {
    terminal_initialize();

    gdt_init();
    idt_init();
    keyboard_init();

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("myOS Phase 2\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("GDT: loaded | IDT: loaded | Keyboard: ready\n");
    terminal_writestring("----------------------------------------\n");
    terminal_writestring("Type something:\n> ");
}

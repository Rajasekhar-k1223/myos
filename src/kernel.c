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
    terminal_clear();
}

void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            terminal_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    terminal_row = 0;
    terminal_column = 0;
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

static const char hex_chars[] = "0123456789ABCDEF";
void terminal_writehex(uint32_t n) {
    terminal_writestring("0x");
    for (int i = 28; i >= 0; i -= 4) {
        terminal_putchar(hex_chars[(n >> i) & 0xF]);
    }
}

#include "multiboot.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "shell.h"
#include "tar.h"

void kernel_main(uint32_t magic, struct multiboot_info* mbi) {
    terminal_initialize();

    gdt_init();
    idt_init();
    keyboard_init();

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("myOS Phase 3\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("GDT: loaded | IDT: loaded | Keyboard: ready\n");
    terminal_writestring("----------------------------------------\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        terminal_writestring("Invalid Multiboot magic!\n");
        return;
    }

    terminal_writestring("Initializing Memory Subsystems...\n");
    pmm_init(mbi);
    paging_init();
    kheap_init();
    terminal_writestring("Memory Managers: ONLINE\n");

    terminal_writestring("\n");

    terminal_writestring("\n");

    // Initialize Virtual Pendrive (RAM Disk)
    if (mbi->flags & (1 << 3)) { // Check if modules are present
        if (mbi->mods_count > 0) {
            struct multiboot_module* mod = (struct multiboot_module*)mbi->mods_addr;
            tar_init(mod->mod_start);
            terminal_writestring("Virtual Pendrive: MOUNTED\n");
        }
    } else {
        terminal_writestring("Virtual Pendrive: NOT FOUND\n");
    }

    terminal_writestring("----------------------------------------\n");
    shell_init();
}

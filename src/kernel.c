#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "kernel.h"
#include "string.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "pit.h"
#include "rtc.h"
#include "multiboot.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "tar.h"
#include "task.h"
#include "shell.h"
#include "io.h"

/* ── VGA constants ───────────────────────────────────────────────────────── */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_CTRL   0x3D4
#define VGA_DATA   0x3D5

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static uint32_t  term_row;
static uint32_t  term_col;
static uint8_t   term_color;
static uint16_t* term_buf;

/* ── Hardware cursor ─────────────────────────────────────────────────────── */
static void hw_cursor_update(void) {
    uint16_t pos = (uint16_t)(term_row * VGA_WIDTH + term_col);
    outb(VGA_CTRL, 0x0F); outb(VGA_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CTRL, 0x0E); outb(VGA_DATA, (uint8_t)(pos >> 8));
}

void terminal_cursor_show(int visible) {
    if (visible) {
        outb(VGA_CTRL, 0x0A); outb(VGA_DATA, (inb(VGA_DATA) & 0xC0) | 13);
        outb(VGA_CTRL, 0x0B); outb(VGA_DATA, (inb(VGA_DATA) & 0xE0) | 15);
    } else {
        outb(VGA_CTRL, 0x0A); outb(VGA_DATA, 0x20); /* bit 5 = disable */
    }
}

void terminal_setpos(uint32_t row, uint32_t col) {
    if (row < VGA_HEIGHT) term_row = row;
    if (col < VGA_WIDTH)  term_col = col;
    hw_cursor_update();
}

void terminal_getpos(uint32_t* row, uint32_t* col) {
    if (row) *row = term_row;
    if (col) *col = term_col;
}

/* ── Scroll ──────────────────────────────────────────────────────────────── */
static void terminal_scroll(void) {
    for (uint32_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
            term_buf[y * VGA_WIDTH + x] = term_buf[(y+1) * VGA_WIDTH + x];
    for (uint32_t x = 0; x < VGA_WIDTH; x++)
        term_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', term_color);
    term_row = VGA_HEIGHT - 1;
}

/* ── Core terminal API ───────────────────────────────────────────────────── */
void terminal_initialize(void) {
    term_row   = 0;
    term_col   = 0;
    term_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    term_buf   = (uint16_t*)0xB8000;
    terminal_clear();
    terminal_cursor_show(1);
}

void terminal_clear(void) {
    for (uint32_t y = 0; y < VGA_HEIGHT; y++)
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
            term_buf[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
    term_row = 0; term_col = 0;
    hw_cursor_update();
}

void terminal_setcolor(uint8_t color) {
    term_color = color;
}

static void putentryat(char c, uint8_t color, uint32_t x, uint32_t y) {
    term_buf[y * VGA_WIDTH + x] = vga_entry(c, color);
}

void terminal_putchar(char c) {
    switch (c) {
    case '\n':
        term_col = 0;
        if (++term_row == VGA_HEIGHT) terminal_scroll();
        break;
    case '\r':
        term_col = 0;
        break;
    case '\t':
        term_col = (term_col + 8) & ~(uint32_t)7;
        if (term_col >= VGA_WIDTH) { term_col = 0; if (++term_row == VGA_HEIGHT) terminal_scroll(); }
        break;
    case '\b':
        if (term_col > 0) { --term_col; putentryat(' ', term_color, term_col, term_row); }
        break;
    default:
        putentryat(c, term_color, term_col, term_row);
        if (++term_col == VGA_WIDTH) { term_col = 0; if (++term_row == VGA_HEIGHT) terminal_scroll(); }
    }
    hw_cursor_update();
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
    while (*data) terminal_putchar(*data++);
}

void terminal_writehex(uint32_t n) {
    static const char h[] = "0123456789ABCDEF";
    terminal_writestring("0x");
    for (int i = 28; i >= 0; i -= 4) terminal_putchar(h[(n >> i) & 0xF]);
}

void terminal_writedec(uint32_t n) {
    if (n == 0) { terminal_putchar('0'); return; }
    char buf[10]; int i = 0;
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i-- > 0) terminal_putchar(buf[i]);
}

void terminal_vprintf(const char* fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    terminal_writestring(buf);
}

void terminal_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    terminal_vprintf(fmt, ap);
    va_end(ap);
}

/* ── Boot UI helpers ─────────────────────────────────────────────────────── */
/* CP437 box-drawing bytes used directly */
#define BOX_TL  "\xC9"   /* ╔ */
#define BOX_TR  "\xBB"   /* ╗ */
#define BOX_BL  "\xC8"   /* ╚ */
#define BOX_BR  "\xBC"   /* ╝ */
#define BOX_ML  "\xCC"   /* ╠ */
#define BOX_MR  "\xB9"   /* ╣ */
#define BOX_H   "\xCD"   /* ═ */
#define BOX_V   "\xBA"   /* ║ */
#define FILL    "\xDB"   /* █ */
#define SHADE   "\xB0"   /* ░ */

static void hline(const char* l, char fill, const char* r) {
    terminal_writestring(l);
    for (int i = 0; i < 78; i++) terminal_putchar(fill);
    terminal_writestring(r);
    terminal_putchar('\n');
}

static void boot_ok(const char* label, const char* detail) {
    terminal_writestring(BOX_V "  ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[ OK ]");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring(label);
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ");
    terminal_writestring(detail);
    terminal_putchar('\n');
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void boot_section(const char* title) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    hline(BOX_ML, '\xCD', BOX_MR);
    terminal_writestring(BOX_V "  ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
    terminal_writestring(title);
    terminal_putchar('\n');
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* ── kernel_main ─────────────────────────────────────────────────────────── */
void kernel_main(uint32_t magic, struct multiboot_info* mbi) {
    terminal_initialize();

    /* ── Header box ── */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    hline(BOX_TL, '\xCD', BOX_TR);

    terminal_writestring(BOX_V "\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(BOX_V "    ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("  __  __  _  _  ___  ___");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("        myOS  v0.8\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(BOX_V "    ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring(" |  \\/  || \\| |/ _ \\/ __|");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("   32-bit Protected Mode OS\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(BOX_V "    ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring(" | |\\/| || .` | (_) \\__ \\");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("   Built in C + x86 ASM\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(BOX_V "    ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring(" |_|  |_||_|\\_|\\___/|___/");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("   GDT IDT PIT RTC PMM VFS\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(BOX_V "\n");

    /* ── Core subsystems ── */
    boot_section("Core Subsystems");

    gdt_init();
    boot_ok("GDT", "3 descriptors: null / kernel-code / kernel-data");

    idt_init();
    boot_ok("IDT", "256 vectors loaded, PIC remapped IRQ 0x20-0x2F");

    pit_init(100);
    boot_ok("PIT", "100 Hz system timer (IRQ0)");

    keyboard_init();
    boot_ok("KBD", "PS/2 keyboard driver ready (IRQ1)");

    /* ── Memory ── */
    boot_section("Memory Subsystem");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring(BOX_V "  [FAIL]  Invalid Multiboot magic — halting.\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    pmm_init(mbi);
    {
        char det[64];
        uint32_t total_mb = (pmm_get_max_frames() * 4) / 1024;
        snprintf(det, sizeof(det), "%u MB detected, %u frames",
                 total_mb, pmm_get_max_frames());
        boot_ok("PMM", det);
    }

    paging_init();
    boot_ok("PGN", "Paging enabled — first 16 MB identity mapped");

    kheap_init();
    boot_ok("HEP", "1 MB kernel heap initialized");

    /* ── Clock ── */
    boot_section("Real-Time Clock");
    {
        char dt[20];
        rtc_datetime_str(dt);
        char det[48];
        snprintf(det, sizeof(det), "%s", dt);
        boot_ok("RTC", det);
    }

    /* ── Storage ── */
    boot_section("Virtual Filesystem");
    {
        int mounted = 0;
        if (mbi->flags & (1 << 3)) {
            if (mbi->mods_count > 0) {
                struct multiboot_module* mod = (struct multiboot_module*)mbi->mods_addr;
                tar_init(mod->mod_start);
                mounted = 1;
            }
        }
        if (mounted) {
            boot_ok("VFS", "RAM disk (initrd.tar) mounted via multiboot module");
        } else {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            terminal_writestring(BOX_V "  [WARN]  No RAM disk found — 'ls' and 'cat' unavailable\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }

    /* ── Multitasking ── */
    boot_section("Multitasking");
    tasking_init();
    boot_ok("TSK", "Task 0 (kernel) adopted — round-robin scheduler ready");
    pit_enable_scheduling();
    boot_ok("SCH", "Preemptive scheduler active — 20 ms time slice (2 ticks)");

    /* ── Close boot box ── */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    hline(BOX_BL, '\xCD', BOX_BR);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_putchar('\n');

    /* ── Shell (runs as task 0) ── */
    shell_init();
}

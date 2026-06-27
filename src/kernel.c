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
#include "bmp.h"
#include "mouse.h"
#include "vesa.h"
#include "wm.h"
#include "syscall.h"
#include "user.h"
#include "ata.h"
#include "fs.h"
#include "fat16.h"
#include "speaker.h"
#include "rtl8139.h"
#include "sb16.h"
#include "uhci.h"
#include "ttf.h"

window_t* shell_window = 0;

/* ── VESA Terminal Driver ────────────────────────────────────────────────── */
#include "font16.h"   /* 8×16 Terminus Bold — replaces old 8×8 font */

#define FONT_W  8
#define FONT_H 16

// Convert old VGA colors to VESA 32-bit colors
static const uint32_t vesa_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static uint32_t  term_row;
static uint32_t  term_col;
static uint8_t   term_color;

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

void terminal_cursor_show(int visible) {
    (void)visible;
    // Hardware cursor is not available in VESA mode. We would need to draw a block manually.
}

void terminal_setpos(uint32_t row, uint32_t col) {
    term_row = row;
    term_col = col;
}

void terminal_getpos(uint32_t* row, uint32_t* col) {
    if (row) *row = term_row;
    if (col) *col = term_col;
}

static void terminal_scroll(void) {
    /* Scroll the raw framebuffer up by one FONT_H (16px) row */
    extern void vesa_scroll_by(uint32_t pixels);
    vesa_scroll_by(FONT_H);
    if (term_row > 0) term_row--;
}

void terminal_initialize(void) {
    term_row   = 0;
    term_col   = 0;
    term_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_clear();
}

void terminal_clear(void) {
    vesa_clear(vesa_palette[term_color >> 4]);
    term_row = 0; term_col = 0;
}

void terminal_setcolor(uint8_t color) {
    term_color = color;
}

static void putentryat(char c, uint8_t color, uint32_t x, uint32_t y) {
    uint32_t bg = vesa_palette[color >> 4];
    uint32_t fg = vesa_palette[color & 0x0F];
    const unsigned char* glyph = font8x16[(unsigned char)c];
    for (uint32_t cy = 0; cy < FONT_H; cy++) {
        for (uint32_t cx = 0; cx < FONT_W; cx++) {
            uint32_t color32 = (glyph[cy] & (1 << (7 - cx))) ? fg : bg;
            vesa_putpixel(x * FONT_W + cx, y * FONT_H + cy, color32);
        }
    }
}

static int ansi_state = 0;
static int ansi_param = 0;

void terminal_putchar(char c) {
    if (shell_window && shell_window->active) {
        wm_putchar(shell_window, c);
        return;
    }
    uint32_t cols = vesa_width  / FONT_W;
    uint32_t rows = vesa_height / FONT_H;
    if (cols == 0) return;

    /* ANSI Escape Parser State Machine */
    if (ansi_state == 1) {
        if (c == '[') { ansi_state = 2; ansi_param = 0; }
        else ansi_state = 0;
        return;
    } else if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            ansi_param = ansi_param * 10 + (c - '0');
            return;
        } else if (c == 'm') {
            if (ansi_param == 0) term_color = 0xAAAAAA;
            else if (ansi_param >= 30 && ansi_param <= 37) term_color = 0xFFFFFF;
            ansi_state = 0;
            return;
        } else if (c == 'J') {
            if (ansi_param == 2) {
                term_col = 0; term_row = 0;
                for (uint32_t y = 0; y < rows; y++)
                    for (uint32_t x = 0; x < cols; x++)
                        putentryat(' ', term_color, x, y);
            }
            ansi_state = 0;
            return;
        } else if (c == 'H' || c == 'f') {
            term_col = 0; term_row = 0;
            ansi_state = 0;
            return;
        } else if (c == ';') {
            ansi_param = 0;
            return;
        }
        ansi_state = 0;
        return;
    }

    if (c == '\033') {
        ansi_state = 1;
        return;
    }

    switch (c) {
    case '\n':
        term_col = 0;
        if (++term_row >= rows) terminal_scroll();
        break;
    case '\r':
        term_col = 0;
        break;
    case '\t':
        term_col = (term_col + 8) & ~(uint32_t)7;
        if (term_col >= cols) { term_col = 0; if (++term_row >= rows) terminal_scroll(); }
        break;
    case '\b':
        if (term_col > 0) { --term_col; putentryat(' ', term_color, term_col, term_row); }
        break;
    default:
        putentryat(c, term_color, term_col, term_row);
        if (++term_col >= cols) { term_col = 0; if (++term_row >= rows) terminal_scroll(); }
    }
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

/* ── Multiboot2 tag parser ────────────────────────────────────────────────── */
static void parse_mb2_tags(uint32_t mb2_addr,
                            uint32_t* out_fb_addr,   uint32_t* out_fb_w,
                            uint32_t* out_fb_h,      uint32_t* out_fb_pitch,
                            uint8_t*  out_fb_bpp,
                            uint32_t* out_mmap_addr, uint32_t* out_mmap_esize,
                            uint32_t* out_mmap_bytes,
                            uint32_t* out_initrd_start) {
    struct mb2_info* info = (struct mb2_info*)mb2_addr;
    struct mb2_tag*  tag  = mb2_first_tag(info);

    while (tag->type != MB2_TAG_END) {
        switch (tag->type) {

        case MB2_TAG_FRAMEBUFFER: {
            struct mb2_tag_framebuffer* fb =
                (struct mb2_tag_framebuffer*)tag;
            *out_fb_addr  = (uint32_t)(fb->framebuffer_addr & 0xFFFFFFFFu);
            *out_fb_w     = fb->framebuffer_width;
            *out_fb_h     = fb->framebuffer_height;
            *out_fb_pitch = fb->framebuffer_pitch;
            *out_fb_bpp   = fb->framebuffer_bpp;
            break;
        }

        case MB2_TAG_MMAP: {
            struct mb2_tag_mmap* mm = (struct mb2_tag_mmap*)tag;
            /* entry data starts right after the 16-byte tag header */
            *out_mmap_addr  = (uint32_t)mm + 16;
            *out_mmap_esize = mm->entry_size;
            *out_mmap_bytes = mm->size - 16;
            break;
        }

        case MB2_TAG_MODULE: {
            struct mb2_tag_module* mod = (struct mb2_tag_module*)tag;
            *out_initrd_start = mod->mod_start;
            break;
        }

        default: break;
        }
        tag = mb2_next_tag(tag);
    }
}

/* ── kernel_main ─────────────────────────────────────────────────────────── */
void kernel_main(uint32_t magic, uint32_t mb2_addr) {
    if (magic != MULTIBOOT2_MAGIC)
        for (;;) __asm__ volatile("cli; hlt");

    /* ── Parse Multiboot2 tags (GOP framebuffer address, mmap, initrd) ── */
    uint32_t fb_addr = 0, fb_w = 0, fb_h = 0, fb_pitch = 0;
    uint8_t  fb_bpp  = 0;
    uint32_t mmap_addr = 0, mmap_esize = 0, mmap_bytes = 0;
    uint32_t initrd_start = 0;

    parse_mb2_tags(mb2_addr,
                   &fb_addr, &fb_w, &fb_h, &fb_pitch, &fb_bpp,
                   &mmap_addr, &mmap_esize, &mmap_bytes,
                   &initrd_start);

    /* GOP / VBE framebuffer — same linear buffer regardless of BIOS or UEFI */
    vesa_init(fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);
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
    terminal_writestring("     ElseaOS  v1.0\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(BOX_V "    ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring(" |  \\/  || \\| |/ _ \\/ __|");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("   32-bit x86 OS with GOP/UEFI\n");

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
    terminal_writestring("   UEFI GOP WM GUI Shell ATA\n");

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
    boot_ok("KBD", "PS/2 Keyboard Driver Ready");

    mouse_init();
    boot_ok("MOU", "PS/2 Mouse Driver Ready (IRQ12)");

    /* ── Memory Subsystem ── */
    boot_section("Memory Subsystem");

    pmm_init(mmap_addr, mmap_esize, mmap_bytes);
    {
        char det[64];
        uint32_t total_mb = (pmm_get_max_frames() * 4) / 1024;
        snprintf(det, sizeof(det), "%u MB detected, %u frames",
                 total_mb, pmm_get_max_frames());
        boot_ok("PMM", det);
    }

    paging_init(); /* also maps GOP/VBE framebuffer region */
    {
        char det[64];
        snprintf(det, sizeof(det),
                 "Paging on — 16 MB mapped + GOP FB @ 0x%08X (%ux%u)",
                 fb_addr, fb_w, fb_h);
        boot_ok("PGN", det);
    }

    kheap_init();
    boot_ok("HEP", "1 MB kernel heap initialized");

    /* ── Real-Time Clock ── */
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
        if (initrd_start) {
            tar_init(initrd_start);
            boot_ok("VFS", "RAM disk (initrd.tar) mounted via Multiboot2 module");
            bmp_draw_file("logo.bmp", 850, 50);
        } else {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            terminal_writestring(BOX_V "  [WARN]  No RAM disk — ls/cat unavailable\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }

    /* ── ATA Disk + FAT16 ── */
    boot_section("Persistent Storage");
    {
        char det[64];
        if (ata_init()) {
            boot_ok("ATA", "Primary IDE disk detected (PIO mode)");
            if (fat16_init()) {
                boot_ok("FAT", "FAT16 filesystem mounted — 'fat ls/read/write' available");
            } else {
                terminal_setcolor(vga_entry_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK));
                terminal_writestring(BOX_V "  [WARN]  FAT16 not found — disk may need formatting\n");
                terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                (void)det;
            }
        } else {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            terminal_writestring(BOX_V "  [WARN]  No ATA disk — disk commands unavailable\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            (void)det;
        }
    }

    /* ── Multitasking ── */
    boot_section("Multitasking");
    tasking_init();
    boot_ok("TSK", "Task 0 (kernel) adopted — round-robin scheduler ready");
    pit_enable_scheduling();
    boot_ok("SCH", "Preemptive scheduler active — 20 ms time slice (2 ticks)");

    /* ── User Mode ── */
    boot_section("User Mode & Syscalls");
    syscall_init();
    boot_ok("SYS", "INT 0x80 syscall gate registered");
    boot_ok("GOP", "UEFI/BIOS GOP framebuffer active");

    /* ── Close boot box ── */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    hline(BOX_BL, '\xCD', BOX_BR);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_putchar('\n');

    boot_section("Window Manager");
    wm_init();
    boot_ok("WM ", "ElseaOS compositing window manager started");

    wm_create_window(50, 50, 400, 300, "System Monitor");
    shell_window = wm_create_window(150, 100, 600, 400, "Terminal");

    // Initialize Networking
    rtl8139_init();

    // Initialize Audio
    sb16_init();
    uhci_init();

    // Initialize TTF
    size_t font_size;
    void* font_data = tar_get_file("font.ttf", &font_size);
    if (font_data) {
        ttf_init(font_data);
        wm_draw_desktop_text("ElseaOS TTF Vector Math Engine!", 0.04f, 50, 150, 0xFFFFFF);
    } else {
        terminal_printf("[TTF] Error: font.ttf not found in initrd\n");
    }

    shell_init();

    while (1) {
        wm_process_events();
        __asm__ volatile("hlt");
    }
}

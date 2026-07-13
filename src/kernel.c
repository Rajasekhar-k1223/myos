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
#include "ahci.h"
#include "fs.h"
#include "fat16.h"
#include "fat32.h"
#include "speaker.h"
#include "rtl8139.h"
#include "sb16.h"
#include "uhci.h"
#include "ttf.h"

window_t* shell_window = 0;

/* ── VESA Terminal Driver ────────────────────────────────────────────────── */
#include "font16.h"   /* 8×16 Terminus Bold - replaces old 8×8 font */

#define FONT_W  8
#define FONT_H 16

/* 2× font scale at HD/FHD resolutions so boot text is readable */
static uint32_t font_scale = 1;

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
    extern void vesa_scroll_by(uint32_t pixels);
    vesa_scroll_by(FONT_H * font_scale);
    if (term_row > 0) term_row--;
}

void terminal_initialize(void) {
    /* Scale font 2× on HD+ screens so boot text is legible */
    font_scale = (vesa_width >= 1280) ? 2 : 1;
    term_row   = 0;
    term_col   = 0;
    term_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_clear();
}

void terminal_clear(void) {
    extern int terminal_quiet;
    if (!terminal_quiet)
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
    uint32_t fw = FONT_W * font_scale;
    uint32_t fh = FONT_H * font_scale;
    for (uint32_t cy = 0; cy < FONT_H; cy++) {
        for (uint32_t cx = 0; cx < FONT_W; cx++) {
            uint32_t color32 = (glyph[cy] & (1 << (7 - cx))) ? fg : bg;
            for (uint32_t sy = 0; sy < font_scale; sy++)
                for (uint32_t sx = 0; sx < font_scale; sx++)
                    vesa_putpixel(x * fw + cx * font_scale + sx,
                                  y * fh + cy * font_scale + sy,
                                  color32);
        }
    }
}

static int ansi_state = 0;
static int ansi_param = 0;

/* Set to 1 to suppress all terminal_printf output (e.g. during installer) */
int terminal_quiet = 0;

void terminal_putchar(char c) {
    if (terminal_quiet) return;
    if (shell_window && shell_window->active) {
        wm_putchar(shell_window, c);
        return;
    }
    uint32_t fw   = FONT_W * font_scale;
    uint32_t fh   = FONT_H * font_scale;
    uint32_t cols = vesa_width  / fw;
    uint32_t rows = vesa_height / fh;
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
            if (ansi_param == 0) {
                term_color = 7; /* reset → VGA light grey */
            } else if (ansi_param >= 30 && ansi_param <= 37) {
                /* ANSI 30-37 → VGA colour indices */
                static const uint8_t ansi_to_vga[8] = {0, 4, 2, 6, 1, 5, 3, 7};
                term_color = ansi_to_vga[ansi_param - 30];
            }
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

/* ── Graphical Boot Loading Screen ──────────────────────────────────────── */

#define BOOT_TOTAL  24
#define BOOT_LOGS    8
#define BLOG_W      72

static int  g_bstep      = 0;

/* Layout — computed once on first boot_redraw() call */
static int      g_boot_first  = 1;
static uint32_t g_ts, g_ss;
static uint32_t g_pb_x, g_pb_y, g_pb_w, g_pb_h;
static uint32_t g_log_y0, g_lh;
static uint32_t g_log_cursor  = 0;  /* Y of next log line (append-only) */
static uint32_t g_prev_fill   = 0;  /* px of bar already filled blue    */

/* Draw text — lit pixels only; caller pre-fills background. */
static void boot_draw_str(uint32_t x, uint32_t y,
                           const char* s, uint32_t scale, uint32_t fg) {
    while (*s) {
        const unsigned char* gl = font8x16[(unsigned char)*s++];
        for (uint32_t gy = 0; gy < 16; gy++)
            for (uint32_t gx = 0; gx < 8; gx++)
                if (gl[gy] & (0x80u >> gx))
                    vesa_draw_rect(x + gx * scale, y + gy * scale, scale, scale, fg);
        x += 8 * scale;
    }
}

/* Draw text WITH explicit background fill — writes every pixel (lit=fg, dark=bg)
 * so no separate pre-clear rect is needed and the area never flashes blank. */
static void boot_draw_str_bg(uint32_t x, uint32_t y, const char* s,
                              uint32_t scale, uint32_t fg, uint32_t bg) {
    while (*s) {
        const unsigned char* gl = font8x16[(unsigned char)*s++];
        for (uint32_t gy = 0; gy < 16; gy++)
            for (uint32_t gx = 0; gx < 8; gx++) {
                uint32_t col = (gl[gy] & (0x80u >> gx)) ? fg : bg;
                vesa_draw_rect(x + gx * scale, y + gy * scale, scale, scale, col);
            }
        x += 8 * scale;
    }
}

static uint32_t boot_slen(const char* s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

/* First call: paint the complete static boot UI (background, title, subtitle,
 * empty bar track) — ONCE.
 * Subsequent calls: extend the bar fill by only the NEW pixels (monotonic —
 * the bar never goes dark) and update the tiny percentage label. */
static void boot_redraw(void) {
    uint32_t W = vesa_width, H = vesa_height;
    if (!W || !H) return;

    if (g_boot_first) {
        g_boot_first = 0;
        uint32_t ts = W / 240; if (ts < 2) ts = 2; if (ts > 12) ts = 12;
        uint32_t ss = W / 640; if (ss < 1) ss = 1;
        g_ts = ts; g_ss = ss;

        g_pb_x  = W / 8;
        g_pb_h  = ts * 3;
        g_pb_w  = W - 2 * g_pb_x - 8 * ss * 5;
        g_pb_y  = H / 2 - g_pb_h / 2;
        g_log_y0 = g_pb_y + g_pb_h + ts * 2;
        g_lh     = 16 * ss + ss * 2;
        g_log_cursor = g_log_y0;

        vesa_draw_rect(0, 0, W, H, 0x04060A);
        vesa_draw_rect(0, 0, W, ts * 2, 0x1C6EF0);

        uint32_t tcw = 8 * ts, tch = 16 * ts;
        uint32_t title_x = (W > 7 * tcw) ? (W - 7 * tcw) / 2 : 0;
        uint32_t title_y = H / 7;
        boot_draw_str(title_x, title_y, "ElseaOS", ts, 0xFFFFFF);

        const char* sub = "Starting up...";
        uint32_t sl  = boot_slen(sub);
        uint32_t sub_x = (W > sl * 8 * ss) ? (W - sl * 8 * ss) / 2 : 0;
        boot_draw_str(sub_x, title_y + tch + ss * 4, sub, ss, 0x4466AA);

        vesa_draw_rect(g_pb_x, g_pb_y, g_pb_w, g_pb_h, 0x141B2C);
        return;
    }

    /* Progress bar: paint only the newly-filled segment — bar never goes dark */
    uint32_t ss = g_ss;
    uint32_t fill = g_pb_w * (uint32_t)g_bstep / BOOT_TOTAL;
    if (fill > g_pb_w) fill = g_pb_w;
    if (fill > g_prev_fill) {
        vesa_draw_rect(g_pb_x + g_prev_fill, g_pb_y,
                       fill - g_prev_fill, g_pb_h, 0x1C6EF0);
        g_prev_fill = fill;
    }

    /* Percentage label — tiny area, one clear + one draw */
    int pv = (g_bstep * 100) / BOOT_TOTAL; if (pv > 100) pv = 100;
    char pct[5];
    pct[0] = (pv >= 100) ? '1' : ' ';
    pct[1] = '0' + (char)((pv / 10) % 10); if (pv < 10) pct[1] = ' ';
    pct[2] = '0' + (char)(pv % 10);
    pct[3] = '%'; pct[4] = '\0';
    uint32_t pct_x = g_pb_x + g_pb_w + ss * 2;
    vesa_draw_rect(pct_x, g_pb_y, 8 * ss * 5, g_pb_h, 0x04060A);
    boot_draw_str(pct_x, g_pb_y + (g_pb_h - 16 * ss) / 2, pct, ss, 0x7090C0);
}

/* Append one log line to the screen without touching any previous pixel.
 * boot_draw_str_bg writes both fg and bg pixels together — no pre-clear
 * rect, so the line area never goes blank between clear and text. */
static void boot_append_line(const char* msg, int is_section) {
    if (g_boot_first) return;
    if (g_log_cursor + g_lh > vesa_height) return;

    uint32_t ss     = g_ss;
    uint32_t ly     = g_log_cursor;
    uint32_t text_x = g_pb_x + ss * 3;
    uint32_t col    = is_section ? 0xFFCC44u : 0xB8D4F0u;

    /* Blue bullet for regular lines. No margin pre-clear needed — the
     * initial full-screen dark fill in boot_redraw() already set the
     * entire area to bg; append-only lines never overwrite each other. */
    if (!is_section)
        vesa_draw_rect(g_pb_x, ly + ss, ss * 2, 16 * ss - ss * 2, 0x1C6EF0);

    /* Text with background in one pass — no blank-flash between clear & draw */
    boot_draw_str_bg(text_x, ly, msg, ss, col, 0x04060A);

    g_log_cursor += g_lh;
}

/* Concatenate two strings into buf (at most buf_len-1 chars). */
static void boot_cat(char* buf, const char* a, const char* b, uint32_t buf_len) {
    uint32_t i = 0;
    while (a[i] && i < buf_len - 1) { buf[i] = a[i]; i++; }
    uint32_t j = 0;
    while (b[j] && i < buf_len - 1) { buf[i++] = b[j++]; }
    buf[i] = '\0';
}

static void boot_ok(const char* label, const char* detail) {
    extern void com1_print(const char*);
    char msg[BLOG_W];
    boot_cat(msg, "[ OK ] ", label, BLOG_W);
    uint32_t l = boot_slen(msg);
    if (l < BLOG_W - 3) { msg[l++] = ' '; msg[l++] = ' '; msg[l] = '\0'; }
    uint32_t l2 = boot_slen(detail);
    for (uint32_t k = 0; k < l2 && l < BLOG_W - 1; k++) msg[l++] = detail[k];
    msg[l] = '\0';
    com1_print(msg); com1_print("\n");
    g_bstep++;
    boot_redraw();
    boot_append_line(msg, 0);
}

static void boot_section(const char* title) {
    extern void com1_print(const char*);
    char msg[BLOG_W];
    boot_cat(msg, "--- ", title, BLOG_W);
    com1_print(msg); com1_print("\n");
    boot_append_line(msg, 1);
}

/* ── Multiboot2 tag parser ────────────────────────────────────────────────── */
static void parse_mb2_tags(uint32_t mb2_addr,
                            uint32_t* out_fb_addr,   uint32_t* out_fb_w,
                            uint32_t* out_fb_h,      uint32_t* out_fb_pitch,
                            uint8_t*  out_fb_bpp,
                            uint32_t* out_mmap_addr, uint32_t* out_mmap_esize,
                            uint32_t* out_mmap_bytes,
                            uint32_t* out_initrd_start,
                            uint32_t* out_initrd_end) {
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
            *out_initrd_end   = mod->mod_end;
            break;
        }

        default: break;
        }
        tag = mb2_next_tag(tag);
    }
}

/* ── x87 FPU initialisation ─────────────────────────────────────────────── */
static void fpu_init(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1u << 2);   /* clear EM: use native x87, not software emulation */
    cr0 &= ~(1u << 3);   /* clear TS: FPU available in this task */
    cr0 |=  (1u << 1);   /* set MP: raise #NM on WAIT if future TS gets set */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
    __asm__ volatile("fninit");  /* reset FPU to initial state */
}

void init_thread(void) {
    extern int elf_load_and_run(const char*, const char*);
    terminal_printf("[Kernel] Spawning userspace init daemon...\n");
    elf_load_and_run("bin/init.elf", NULL);
    for (;;) __asm__ volatile("hlt");
}

/* ── kernel_main ─────────────────────────────────────────────────────────── */
void kernel_main(uint32_t magic, uint32_t mb2_addr) {
    if (magic != MULTIBOOT2_MAGIC)
        for (;;) __asm__ volatile("cli; hlt");

    /* ── Parse Multiboot2 tags (GOP framebuffer address, mmap, initrd) ── */
    uint32_t fb_addr = 0, fb_w = 0, fb_h = 0, fb_pitch = 0;
    uint8_t  fb_bpp  = 0;
    uint32_t mmap_addr = 0, mmap_esize = 0, mmap_bytes = 0;
    uint32_t initrd_start = 0, initrd_end = 0;

    parse_mb2_tags(mb2_addr,
                   &fb_addr, &fb_w, &fb_h, &fb_pitch, &fb_bpp,
                   &mmap_addr, &mmap_esize, &mmap_bytes,
                   &initrd_start, &initrd_end);

    /* GOP / VBE framebuffer - same linear buffer regardless of BIOS or UEFI */
    vesa_init(fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);

    /* ── Immediate alive indicator ──────────────────────────────────────────
     * Paint the top 16 rows blue via raw pointer — no abstraction layers,
     * no paging, no GDT change needed.  Visible in any window size.         */
    if (fb_addr && fb_w && fb_h && fb_pitch) {
        uint32_t* raw = (uint32_t*)fb_addr;
        uint32_t  stride = fb_pitch / 4;
        uint32_t  rows   = (fb_h < 16) ? fb_h : 16;
        for (uint32_t r = 0; r < rows; r++)
            for (uint32_t c = 0; c < fb_w; c++)
                raw[r * stride + c] = 0x1C6EF0u;   /* blue = alive */
    }

    /* Silence terminal BEFORE terminal_initialize() so terminal_clear()
     * inside it skips the full-screen vesa_clear() wipe — one less 8 MB
     * framebuffer write means one less black flash at startup. */
    extern int terminal_quiet;
    terminal_quiet = 1;
    terminal_initialize();

    /* Draw the boot screen immediately — first call paints full static UI */
    boot_redraw();

    /* ── Core subsystems ── */
    boot_section("Core Subsystems");

    gdt_init();
    boot_ok("GDT", "3 descriptors: null / kernel-code / kernel-data");

    idt_init();
    boot_ok("IDT", "256 vectors loaded, PIC remapped IRQ 0x20-0x2F");

    fpu_init();
    boot_ok("FPU", "x87 FPU enabled (CR0.EM=0, CR0.TS=0, fninit)");

    pit_init(100);
    boot_ok("PIT", "100 Hz system timer (IRQ0)");

    keyboard_init();
    boot_ok("KBD", "PS/2 Keyboard Driver Ready");

    mouse_init();
    boot_ok("MOU", "PS/2 Mouse Driver Ready (IRQ12)");

    /* ── Memory Subsystem ── */
    boot_section("Memory Subsystem");

    pmm_init(mmap_addr, mmap_esize, mmap_bytes);
    /* protect initrd so the heap doesn't overwrite it */
    if (initrd_start && initrd_end > initrd_start)
        pmm_mark_used(initrd_start, initrd_end - initrd_start);
    {
        char det[64];
        uint32_t total_mb = (pmm_get_max_frames() * 4) / 1024;
        snprintf(det, sizeof(det), "%u MB detected, %u frames",
                 total_mb, pmm_get_max_frames());
        boot_ok("PMM", det);
    }

    paging_init(); /* also maps GOP/VBE framebuffer region */
    paging_register_tlb_handler(); /* IPI vector 0x3E for SMP TLB shootdown */
    {
        char det[64];
        snprintf(det, sizeof(det),
                 "Paging on - 256 MB mapped + GOP FB @ 0x%08X (%ux%u)",
                 fb_addr, fb_w, fb_h);
        boot_ok("PGN", det);
    }

    kheap_init();
    boot_ok("HEP", "32 MB kernel heap initialized");

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
        } else {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            terminal_writestring(BOX_V "  [WARN]  No RAM disk - ls/cat unavailable\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }

    /* ── ATA/AHCI + FAT16 ── */
    boot_section("Persistent Storage");
    {
        char det[64];
        int disk_ok = 0;
        if (ata_init()) {
            boot_ok("ATA", "Primary IDE disk detected (PIO mode)");
            disk_ok = 1;
        } else {
            ahci_init();
            /* ahci_init prints its own status; check if it found a port */
            boot_ok("AHC", "SATA/AHCI controller probed (VMware / modern hardware)");
            disk_ok = 1;  /* attempt FAT16 regardless */
        }
        if (disk_ok) {
            if (fat16_init()) {
                boot_ok("FAT", "FAT16 filesystem mounted - 'fat ls/read/write' available");
            } else if (fat32_init()) {
                boot_ok("FAT", "FAT32 filesystem mounted");
            } else {
                terminal_setcolor(vga_entry_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK));
                terminal_writestring(BOX_V "  [WARN]  FAT16/FAT32 not found - disk may need formatting\n");
                terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                (void)det;
            }
        }
    }


    /* ── Multitasking ── */
    boot_section("Multitasking");
    tasking_init();
    boot_ok("TSK", "Task 0 (kernel) adopted - round-robin scheduler ready");
    pit_enable_scheduling();
    boot_ok("SCH", "Preemptive scheduler active - 20 ms time slice (2 ticks)");


    /* ── SMP: boot additional cores ── */
    {
        extern void smp_init(void);
        smp_init();
    }

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
    
    boot_section("Userspace Display Server");
    boot_ok("WM ", "Kernel GUI enabled.");
    /* wm_init() (double-buffer enable) is deferred until just before the
     * splash so that all boot logs render direct-to-front-FB with no blink. */

    // Initialize Networking, Audio, USB
    rtl8139_init();
    sb16_init();
    /* AC97 fallback: init after SB16 so hardware can coexist; ac97_init()
       returns 0 if not found, 1 if present — no-op if SB16 already handled audio */
    {
        extern int ac97_init(void);
        if (ac97_init())
            terminal_printf("[AC97] AC'97 audio controller detected as secondary backend\n");
    }
    //uhci_init();

    /* VFS: mount initrd + FAT16/FAT32 + EXT2 */
    {
        extern void vfs_init(void);
        vfs_init();
    }

    /* i18n */
    { extern void i18n_init(void); i18n_init(); }

    /* TPM */
    { extern void tpm_init(void); tpm_init(); }

    /* FDE */
    { extern void fde_init(void); fde_init(); }


    /* Voice assistant */
    { extern void voice_init(void); voice_init(); }

    /* Bluetooth */
    { extern void bluetooth_init(void); bluetooth_init(); }

    /* OTA + Package manager */
    { extern void ota_init(void); ota_init(); }
    { extern void pkg_init(void); pkg_init(); }

    /* TLS engine */
    { extern void ssl_init(void); ssl_init(); }

    // Initialize TTF
    size_t font_size;
    void* font_data = tar_get_file("font.ttf", &font_size);
    if (font_data) {
        ttf_init(font_data);
    } else {
        terminal_printf("[TTF] Error: font.ttf not found in initrd\n");
    }

    /* Enable double-buffering now — all boot logs are on front FB already.
     * wm_init() allocates the backbuffer (which copies current front FB),
     * so the first splash swap produces no visual jump. */
    wm_init();

    /* Boot complete — silence terminal before the graphical splash */
    terminal_quiet = 1;


    /* ── Boot splash ── */
    {
        extern int   gif_splash_play(void);
        extern int   gif_splash_tick(void);
        extern void  gif_splash_reset_tick(void);
        extern int   gif_splash_is_playing(void);
        extern void  gif_splash_stop(void);

        extern int   bsplash_play(void);
        extern int   bsplash_tick(void);
        extern void  bsplash_reset_tick(void);
        extern int   bsplash_is_playing(void);
        extern void  bsplash_stop(void);

        extern void  vesa_swap_buffers(void);
        extern void  wm_draw_mouse(void);
        extern int   keyboard_has_key(void);
        extern void  keyboard_flush(void);

        keyboard_flush();   /* drain stale boot keypresses */

        /* Try GIF first (confirmed working), bsplash as fallback. */
        int using_gif = (gif_splash_play() == 0);
        int using_bsp = !using_gif && (bsplash_play() == 0);

        if (using_gif) {
            while (gif_splash_is_playing()) {
                int adv = gif_splash_tick();
                if (adv) {
                    /* New frame was blitted — draw mouse and present */
                    wm_draw_mouse();
                    vesa_swap_buffers();
                    gif_splash_reset_tick();
                }
                if (keyboard_has_key()) gif_splash_stop();
            }
        } else if (using_bsp) {
            while (bsplash_is_playing()) {
                int adv = bsplash_tick();
                if (adv) {
                    /* New frame was blitted — draw mouse and present */
                    wm_draw_mouse();
                    vesa_swap_buffers();
                    bsplash_reset_tick();
                }
                if (keyboard_has_key()) bsplash_stop();
            }
        } else {
            /* No splash file — show brief text splash */
            extern uint32_t* vesa_get_backbuffer(void);
            extern void ttf_draw_string(uint32_t*,int,int,int,int,const char*,int,int,uint32_t);
            extern uint32_t pit_get_ticks(void);
            uint32_t* fb = vesa_get_backbuffer();
            int bw=(int)fb_w, bh=(int)fb_h;
            if (fb && bw>0 && bh>0) {
                for (int i=0;i<bw*bh;i++) fb[i]=0xFF04060Eu;
                int cx=bw/2, cy=bh/2;
                ttf_draw_string(fb,bw,bh,cx-148,cy-40,"ElseaOS",7,64,0x1C7EFF);
                ttf_draw_string(fb,bw,bh,cx-110,cy+30,"version 1.0.4  Aurora",21,14,0x334466);
                vesa_swap_buffers();
                uint32_t t0=pit_get_ticks();
                while(pit_get_ticks()-t0 < 200) {
                    if (keyboard_has_key()) break;
                }
            }
        }
    }

    vesa_clear(0x0B0E14);   /* fill with installer background colour */

    extern void wm_set_wallpaper(const char*);
    wm_set_wallpaper("elsea_bg.bmp");

    extern void wm_blur_desktop_bg(int);
    wm_blur_desktop_bg(15);

    // Run the graphical OS installer
    extern void installer_run(void);
    installer_run();

    extern int nk_installer_running;

    shell_init();
    int desktop_apps_started = 0;

    for (;;) {
        extern int nk_installer_running;

        if (nk_installer_running) {
            /* Clear screen by drawing desktop & dock */
            extern void wm_render(void);
            wm_render();

            /* Installer: spin freely — no hlt so mouse/clicks are immediate */
            extern void installer_render_frame(void);
            installer_render_frame();

            extern void lv_timer_handler(void);
            lv_timer_handler();

            extern void wm_draw_mouse(void);
            wm_draw_mouse();

            extern void vesa_swap_buffers(void);
            vesa_swap_buffers();
        } else {
            if (!desktop_apps_started) {
                extern void wm_set_wallpaper(const char*);
                wm_set_wallpaper("phoenix-hd.bmp");
                terminal_quiet = 0;
                desktop_apps_started = 1;
            }

            extern void wm_process_events(void);
            wm_process_events();

            /* Voice keepalive and USB poll only needed in desktop mode */
            { extern void voice_process_audio(const short*, int);
              static short _silence[512];
              voice_process_audio(_silence, 512); }

            extern void uhci_poll(void);
            uhci_poll();

            /* Desktop can yield to interrupts — no tight spin needed */
            __asm__ volatile("hlt");
        }
    }
}

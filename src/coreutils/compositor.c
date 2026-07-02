/* compositor.c — ElseaOS Wayland-like Display Server
 * Draws a full desktop: gradient wallpaper, taskbar, dock icons, cursor.
 * Communicates via syscalls only (no libc stdio/malloc). */

extern unsigned int syscall0(unsigned int num);
extern unsigned int syscall1(unsigned int num, unsigned int a1);
extern unsigned int syscall2(unsigned int num, unsigned int a1, unsigned int a2);

/* ---- FB info struct ---------------------------------------------------- */
struct fb_info {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int bpp;
};

#include "stb_math.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "ubuntu_font.h"
#include "gui_proto.h"

/* Global font info */
static stbtt_fontinfo g_font;
static int g_font_initialized = 0;

static const char* SNAME[20] = {
    "Welcome", "Language", "Keyboard", "Time Zone",
    "License", "Hardware Check", "Disk Selection", "Partition Mgr",
    "Install Type", "User Account", "Security", "Theme",
    "AI Setup", "Software", "Privacy", "Network",
    "Summary", "Installation", "Complete", "First Boot"
};
static int installer_step = 0;
static int in_installer_mode = 1;

static void print(const char* s) {
    unsigned int len = 0;
    while (s[len]) len++;
    unsigned int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(7), "b"(s), "c"(len));
}

static void print_num(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
    print(buf);
}

/* Blend fg over bg with simple 8-bit alpha (alpha=255 → full fg) */
static unsigned int blend(unsigned int bg, unsigned int fg, unsigned int alpha) {
    unsigned int inv = 255 - alpha;
    unsigned int r = ((fg >> 16 & 0xFF) * alpha + (bg >> 16 & 0xFF) * inv) >> 8;
    unsigned int g = ((fg >>  8 & 0xFF) * alpha + (bg >>  8 & 0xFF) * inv) >> 8;
    unsigned int b = ((fg       & 0xFF) * alpha + (bg       & 0xFF) * inv) >> 8;
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

static void fill_rect(unsigned int* fb, unsigned int pitch4,
                      int x, int y, unsigned int w, unsigned int h,
                      unsigned int color, unsigned int W, unsigned int H) {
    for (unsigned int row = 0; row < h; row++) {
        for (unsigned int col = 0; col < w; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < (int)W && py >= 0 && py < (int)H) {
                fb[py * pitch4 + px] = color;
            }
        }
    }
}

/* Draw TrueType string at (x,y) where y is the baseline */
static void draw_string(unsigned int* fb, unsigned int pitch4,
                        int x, int y, const char* str,
                        unsigned int fg, unsigned int bg,
                        unsigned int W, unsigned int H) {
    (void)bg;
    if (!g_font_initialized) return;

    /* Font scale for 16 pixel height */
    float scale = stbtt_ScaleForPixelHeight(&g_font, 16.0f);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    
    ascent = (int)(ascent * scale);
    y += ascent; /* Shift y to be the top edge roughly */

    for (int i = 0; str[i]; i++) {
        int advance, lsb, x0, y0, x1, y1;
        int codepoint = (unsigned char)str[i];
        
        stbtt_GetCodepointHMetrics(&g_font, codepoint, &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&g_font, codepoint, scale, scale, &x0, &y0, &x1, &y1);
        
        int w = x1 - x0;
        int h = y1 - y0;
        
        if (w > 0 && h > 0) {
            unsigned char* bitmap = stbtt_GetCodepointBitmap(&g_font, scale, scale, codepoint, &w, &h, 0, 0);
            if (bitmap) {
                for (int row = 0; row < h; row++) {
                    for (int col = 0; col < w; col++) {
                        int alpha = bitmap[row * w + col];
                        if (alpha > 0) {
                            int px = x + x0 + col;
                            int py = y + y0 + row;
                            if (px >= 0 && px < (int)W && py >= 0 && py < (int)H) {
                                unsigned int old_col = fb[py * pitch4 + px];
                                fb[py * pitch4 + px] = blend(old_col, fg, alpha);
                            }
                        }
                    }
                }
                STBTT_free(bitmap, 0);
            }
        }
        
        x += (int)(advance * scale);
        if (str[i+1]) {
            x += (int)(scale * stbtt_GetCodepointKernAdvance(&g_font, codepoint, str[i+1]));
        }
    }
}

/* Draw a filled rounded rectangle using simple corner masking */
static void fill_round_rect(unsigned int* fb, unsigned int pitch4,
                             int x, int y, unsigned int w, unsigned int h,
                             unsigned int r, unsigned int color, unsigned int W, unsigned int H) {
    for (unsigned int row = 0; row < h; row++) {
        for (unsigned int col = 0; col < w; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= (int)W || py < 0 || py >= (int)H) continue;
            
            int in = 1;
            /* top-left corner */
            if (row < r && col < r) {
                int dx = r - 1 - col, dy = r - 1 - row;
                in = (dx*dx + dy*dy) <= (int)(r*r);
            }
            /* top-right corner */
            else if (row < r && col >= (int)w - (int)r) {
                int dx = col - (w - r), dy = r - 1 - row;
                in = (dx*dx + dy*dy) <= (int)(r*r);
            }
            /* bottom-left corner */
            else if (row >= h - r && col < r) {
                int dx = r - 1 - col, dy = row - (h - r);
                in = (dx*dx + dy*dy) <= (int)(r*r);
            }
            /* bottom-right corner */
            else if (row >= h - r && col >= w - r) {
                int dx = col - (w - r), dy = row - (h - r);
                in = (dx*dx + dy*dy) <= (int)(r*r);
            }
            if (in)
                fb[py * pitch4 + px] = color;
        }
    }
}

/* Draw a simple X cursor (16×16 arrow shape) */
static void draw_cursor(unsigned int* fb, unsigned int pitch4,
                        unsigned int W, unsigned int H,
                        int cx, int cy) {
    /* 16×16 arrow — 1 = white, 2 = black outline */
    static const unsigned char arrow[16] = {
        0b11000000,
        0b11100000,
        0b11110000,
        0b11111000,
        0b11111100,
        0b11111110,
        0b11111111,
        0b11111100,
        0b11101100,
        0b11000110,
        0b10000110,
        0b00000011,
        0b00000011,
        0b00000000,
        0b00000000,
        0b00000000,
    };
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            int px = cx + col, py = cy + row;
            if (px < 0 || py < 0 || (unsigned)px >= W || (unsigned)py >= H) continue;
            if (arrow[row] & (0x80 >> col))
                fb[py * pitch4 + px] = 0xFFFFFFFF;
        }
    }
}

/* ---- Window Manager State ----------------------------------------------- */
struct window {
    int active;
    int qid;
    int x, y, w, h;
    char title[32];
    int shmid;
    unsigned int* buffer;
};

struct window windows[GUI_MAX_WINDOWS];
int window_order[GUI_MAX_WINDOWS]; /* index 0 is bottom, num_windows-1 is top */
int num_windows = 0;

/* Bring a window to the front */
static void focus_window(int idx) {
    if (num_windows <= 1 || window_order[num_windows - 1] == idx) return;
    int pos = -1;
    for (int i = 0; i < num_windows; i++) {
        if (window_order[i] == idx) { pos = i; break; }
    }
    if (pos == -1) return;
    for (int i = pos; i < num_windows - 1; i++) {
        window_order[i] = window_order[i + 1];
    }
    window_order[num_windows - 1] = idx;
}

/* ---- Desktop drawing ---------------------------------------------------- */

/* Draw a two-tone gradient wallpaper (top → bottom) */
static void draw_wallpaper(unsigned int* fb, unsigned int pitch4,
                           unsigned int W, unsigned int H) {
    for (unsigned int y = 0; y < H; y++) {
        /* Gradient: deep navy (#0D1B2A) → dark teal (#1A3A4A) */
        unsigned int t = (y * 255) / H;
        unsigned int r = 0x0D + ((0x1A - 0x0D) * t >> 8);
        unsigned int g = 0x1B + ((0x3A - 0x1B) * t >> 8);
        unsigned int b = 0x2A + ((0x4A - 0x2A) * t >> 8);
        unsigned int row_color = (0xFF << 24) | (r << 16) | (g << 8) | b;
        for (unsigned int x = 0; x < W; x++)
            fb[y * pitch4 + x] = row_color;
    }
}

/* Draw a bottom taskbar */
static void draw_taskbar(unsigned int* fb, unsigned int pitch4,
                         unsigned int W, unsigned int H) {
    unsigned int bar_h = 40;
    unsigned int bar_y = H - bar_h;
    /* Frosted dark bar */
    unsigned int bar_color = 0xCC111827; /* semi-transparent dark */
    fill_rect(fb, pitch4, 0, bar_y, W, bar_h, bar_color, W, H);

    /* Top highlight line */
    for (unsigned int x = 0; x < W; x++)
        fb[bar_y * pitch4 + x] = 0xFF2A4A6A;

    /* ElseaOS logo / start button on left */
    fill_round_rect(fb, pitch4, 8, bar_y + 6, 28, 28, 6, 0xFF3B82F6, W, H);

    /* Separator */
    for (unsigned int y = bar_y + 8; y < H - 8; y++)
        fb[y * pitch4 + 44] = 0xFF2A4A6A;

    /* Clock placeholder — 3 small squares representing time */
    unsigned int cx = W - 60;
    fill_round_rect(fb, pitch4, cx,      bar_y + 10, 14, 20, 3, 0xFF1E3A5F, W, H);
    fill_round_rect(fb, pitch4, cx + 18, bar_y + 10, 14, 20, 3, 0xFF1E3A5F, W, H);
    fill_round_rect(fb, pitch4, cx + 36, bar_y + 10, 14, 20, 3, 0xFF1E3A5F, W, H);

    /* System tray icons (3 dots) */
    unsigned int tx = W - 100;
    for (int i = 0; i < 3; i++) {
        fill_round_rect(fb, pitch4, tx + i*12, bar_y + 17, 6, 6, 3, 0xFF6B9FD4, W, H);
    }
}

/* Draw dock icons at the center-bottom */
static void draw_dock(unsigned int* fb, unsigned int pitch4,
                      unsigned int W, unsigned int H) {
    /* 5 icons, 52px each, centered */
    unsigned int n = 5;
    unsigned int icon_w = 44, icon_h = 44, gap = 8;
    unsigned int dock_w = n * icon_w + (n - 1) * gap;
    unsigned int dock_x = (W - dock_w) / 2;
    unsigned int dock_y = H - 40 - icon_h - 8;

    /* Dock background */
    fill_round_rect(fb, pitch4, dock_x - 8, dock_y - 4,
                    dock_w + 16, icon_h + 8, 8, 0x881B2A3E, W, H);

    /* Icon colors (app palette) */
    unsigned int colors[5] = {
        0xFF3B82F6,  /* Files - blue */
        0xFF10B981,  /* Terminal - green */
        0xFFF59E0B,  /* Settings - amber */
        0xFFEF4444,  /* Browser - red */
        0xFF8B5CF6,  /* Music - purple */
    };

    for (unsigned int i = 0; i < n; i++) {
        unsigned int ix = dock_x + i * (icon_w + gap);
        fill_round_rect(fb, pitch4, ix, dock_y, icon_w, icon_h, 10, colors[i], W, H);
        /* Icon highlight */
        fill_round_rect(fb, pitch4, ix + 4, dock_y + 2, icon_w - 8, 10, 4,
                        blend(colors[i], 0xFFFFFFFF, 60), W, H);
    }
}

/* Draw a single managed window */
static void draw_window(unsigned int* fb, unsigned int pitch4,
                        unsigned int W, unsigned int H, struct window* win, int is_focused) {
    int wx = win->x, wy = win->y, ww = win->w, wh = win->h;
    
    /* Window shadow */
    fill_round_rect(fb, pitch4, wx + 4, wy + 4, ww, wh, 10, 0x88000000, W, H);
    
    /* Window body (background) */
    fill_round_rect(fb, pitch4, wx, wy, ww, wh, 10, 0xFF1E293B, W, H);
    
    /* Title bar (brighter if focused) */
    unsigned int tb_color = is_focused ? 0xFF3B82F6 : 0xFF1E40AF;
    fill_round_rect(fb, pitch4, wx, wy, ww, 32, 8, tb_color, W, H);
    
    /* Title text */
    draw_string(fb, pitch4, wx + (ww / 2) - 30, wy + 12, win->title, 0xFFFFFFFF, 0, W, H);
    
    /* Traffic lights */
    fill_round_rect(fb, pitch4, wx + 10, wy + 10, 12, 12, 6, 0xFFEF4444, W, H);
    fill_round_rect(fb, pitch4, wx + 28, wy + 10, 12, 12, 6, 0xFFF59E0B, W, H);
    fill_round_rect(fb, pitch4, wx + 46, wy + 10, 12, 12, 6, 0xFF22C55E, W, H);
    
    /* Copy the application's shared memory canvas to the window content area (below title bar) */
    if (win->buffer) {
        for (int y = 0; y < wh - 32; y++) {
            for (int x = 0; x < ww; x++) {
                int px = wx + x;
                int py = wy + 32 + y;
                if (px >= 0 && px < (int)W && py >= 0 && py < (int)H) {
                    /* Assume app buffer is ARGB. Simple copy for now */
                    fb[py * pitch4 + px] = win->buffer[y * ww + x];
                }
            }
        }
    }
}

/* Redraw the entire screen from back to front */
static void repaint_all(unsigned int* fb, unsigned int pitch4, unsigned int W, unsigned int H) {
    draw_wallpaper(fb, pitch4, W, H);
    
    if (in_installer_mode) {
        int win_w = 600;
        int win_h = 400;
        int win_x = (W - win_w) / 2;
        int win_y = (H - win_h) / 2;
        
        /* Installer shadow */
        fill_round_rect(fb, pitch4, win_x + 8, win_y + 8, win_w, win_h, 16, 0x88000000, W, H);
        
        /* Installer Window */
        fill_round_rect(fb, pitch4, win_x, win_y, win_w, win_h, 16, 0xFF0B0E14, W, H);
        
        /* Title Bar */
        fill_round_rect(fb, pitch4, win_x, win_y, win_w, 40, 16, 0xFF282D41, W, H);
        
        /* Title Text */
        draw_string(fb, pitch4, win_x + 20, win_y + 12, "ElseaOS Installer", 0xFFFFFFFF, 0, W, H);
        
        /* Step Text */
        char step_text[64];
        int c_idx = 0;
        const char* p = "Step: ";
        while (*p) step_text[c_idx++] = *p++;
        p = SNAME[installer_step];
        while (*p) step_text[c_idx++] = *p++;
        step_text[c_idx] = '\0';
        draw_string(fb, pitch4, win_x + (win_w/2) - 50, win_y + win_h/2 - 20, step_text, 0xFFFFFFFF, 0, W, H);
        
        /* Next Button */
        fill_round_rect(fb, pitch4, win_x + win_w - 120, win_y + win_h - 60, 100, 40, 8, 0xFF141822, W, H);
        draw_string(fb, pitch4, win_x + win_w - 85, win_y + win_h - 45, "Next >", 0xFFFFFFFF, 0, W, H);
    } else {
        /* Draw windows from bottom to top */
        for (int i = 0; i < num_windows; i++) {
            int idx = window_order[i];
            if (windows[idx].active) {
                draw_window(fb, pitch4, W, H, &windows[idx], (i == num_windows - 1));
            }
        }
        
        draw_dock(fb, pitch4, W, H);
        draw_taskbar(fb, pitch4, W, H);
    }
}

/* ---- main --------------------------------------------------------------- */
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    print("[COMPOSITOR] Starting ElseaOS Display Server...\n");

    /* Create IPC Message Queue */
    int comp_qid = gui_msgget(GUI_QUEUE_KEY, 0666 | 01000); /* IPC_CREAT */

    struct fb_info info;
    unsigned int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(38), "b"(&info));

    unsigned int W = info.width;
    unsigned int H = info.height;
    unsigned int P4 = info.pitch / 4;  /* pitch in pixels */

    print("[COMPOSITOR] Framebuffer acquired.\n");

    unsigned int fb_ptr;
    __asm__ volatile("int $0x80" : "=a"(fb_ptr) : "a"(37));
    unsigned int* fb = (unsigned int*)fb_ptr;

    /* Init TrueType Font */
    if (stbtt_InitFont(&g_font, _usr_share_fonts_truetype_ubuntu_Ubuntu_R_ttf, 0)) {
        g_font_initialized = 1;
        print("[COMPOSITOR] TrueType font initialized successfully.\n");
    } else {
        print("[COMPOSITOR] Failed to initialize TrueType font.\n");
    }

    repaint_all(fb, P4, W, H);
    print("[COMPOSITOR] Desktop rendered.\n");

    int mouse_x = (int)W / 2;
    int mouse_y = (int)H / 2;
    int prev_x  = mouse_x, prev_y = mouse_y;

    draw_cursor(fb, P4, W, H, mouse_x, mouse_y);

    /* ---- Event loop ---- */
    while (1) {
        unsigned int ev[3];
        unsigned int has;

        /* Poll IPC messages */
        struct gui_msg msg;
        if (gui_msgrcv(comp_qid, &msg, 0) > 0) {
            if (msg.mtype == GUI_MSG_CONNECT) {
                print("[COMPOSITOR] Received Window Connect request\n");
                if (num_windows < GUI_MAX_WINDOWS) {
                    int idx = num_windows;
                    windows[idx].active = 1;
                    windows[idx].qid = msg.app_qid;
                    windows[idx].x = msg.x;
                    windows[idx].y = msg.y;
                    windows[idx].w = msg.w;
                    windows[idx].h = msg.h + 32; /* Add title bar height */
                    for(int c = 0; c < 31 && msg.title[c]; c++) {
                        windows[idx].title[c] = msg.title[c];
                    }
                    windows[idx].title[31] = '\0';
                    
                    /* Allocate Shared Memory for window canvas */
                    int canvas_sz = msg.w * msg.h * 4;
                    windows[idx].shmid = gui_shmget(GUI_QUEUE_KEY + 1 + idx, canvas_sz);
                    windows[idx].buffer = (unsigned int*)gui_shmat(windows[idx].shmid);
                    
                    window_order[num_windows] = idx;
                    num_windows++;
                    
                    /* Send Ready reply */
                    struct gui_msg reply;
                    reply.mtype = GUI_MSG_READY;
                    reply.win_id = idx;
                    reply.shmid = windows[idx].shmid;
                    gui_msgsnd(windows[idx].qid, &reply);
                    
                    repaint_all(fb, P4, W, H);
                    draw_cursor(fb, P4, W, H, mouse_x, mouse_y);
                }
            } else if (msg.mtype == GUI_MSG_FLUSH) {
                /* App requested a redraw */
                repaint_all(fb, P4, W, H);
                draw_cursor(fb, P4, W, H, mouse_x, mouse_y);
            }
        }

        /* Poll mouse (type 2) */
        __asm__ volatile("int $0x80" : "=a"(has) : "a"(39), "b"(2), "c"(ev));
        if (has) {
            int nx = (int)ev[0];
            int ny = (int)ev[1];

            if (nx != mouse_x || ny != mouse_y) {
                /* Full repaint is slower but easiest to prevent corruption with overlapping windows */
                repaint_all(fb, P4, W, H);
                
                mouse_x = nx; mouse_y = ny;
                draw_cursor(fb, P4, W, H, mouse_x, mouse_y);
            }
            
            /* Check for click (left button = bit 0) */
            int btn = (int)ev[2];
            static int prev_btn = 0;
            if ((btn & 1) && !(prev_btn & 1)) {
                /* Left click down */
                
                if (in_installer_mode) {
                    int win_w = 600;
                    int win_h = 400;
                    int win_x = (W - win_w) / 2;
                    int win_y = (H - win_h) / 2;
                    
                    int btn_x = win_x + win_w - 120;
                    int btn_y = win_y + win_h - 60;
                    if (mouse_x >= btn_x && mouse_x <= btn_x + 100 &&
                        mouse_y >= btn_y && mouse_y <= btn_y + 40) {
                        installer_step++;
                        if (installer_step >= 20) {
                            in_installer_mode = 0;
                        }
                        repaint_all(fb, P4, W, H);
                        draw_cursor(fb, P4, W, H, mouse_x, mouse_y);
                    }
                } else {
                    int clicked_win = -1;
                    /* Check windows from top to bottom */
                    for (int i = num_windows - 1; i >= 0; i--) {
                        int idx = window_order[i];
                        if (windows[idx].active && 
                            mouse_x >= windows[idx].x && mouse_x <= windows[idx].x + windows[idx].w &&
                            mouse_y >= windows[idx].y && mouse_y <= windows[idx].y + windows[idx].h) {
                            clicked_win = idx;
                            break;
                        }
                    }
                    
                    if (clicked_win != -1) {
                        /* Focus window */
                        focus_window(clicked_win);
                        
                        /* Send click event to app */
                        struct gui_msg click_msg;
                        click_msg.mtype = GUI_MSG_CLICK;
                        click_msg.win_id = clicked_win;
                        click_msg.mouse_x = mouse_x - windows[clicked_win].x;
                        click_msg.mouse_y = mouse_y - windows[clicked_win].y - 32;
                        gui_msgsnd(windows[clicked_win].qid, &click_msg);
                        
                        repaint_all(fb, P4, W, H);
                        draw_cursor(fb, P4, W, H, mouse_x, mouse_y);
                    } else {
                        /* Check dock bounds */
                        unsigned int n = 5;
                        unsigned int icon_w = 44, icon_h = 44, gap = 8;
                        unsigned int dock_w = n * icon_w + (n - 1) * gap;
                        unsigned int dock_x = (W - dock_w) / 2;
                        unsigned int dock_y = H - 40 - icon_h - 8;
                        
                        if (mouse_y >= (int)dock_y && mouse_y <= (int)(dock_y + icon_h) &&
                            mouse_x >= (int)dock_x && mouse_x <= (int)(dock_x + dock_w)) {
                            
                            int icon_idx = (mouse_x - dock_x) / (icon_w + gap);
                            if (icon_idx >= 0 && icon_idx < 5) {
                                print("[COMPOSITOR] Dock icon clicked!\n");
                                /* Spawn terminal.elf for now if terminal icon (index 1) is clicked */
                                if (icon_idx == 1) {
                                    print("[COMPOSITOR] Launching Terminal...\n");
                                    gui_spawn("bin/terminal.elf");
                                }
                            }
                        }
                    }
                }
            }
            prev_btn = btn;
        }
        /* Yield (sys_sleep 0ms) */
        gui_yield();
    }
    return 0;
}

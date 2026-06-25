#include "clock.h"
#include "rtc.h"

int sin60[60] = {-1000,-994,-978,-951,-913,-866,-809,-743,-669,-587,-499,-406,-309,-207,-104,0,104,207,309,406,499,587,669,743,809,866,913,951,978,994,1000,994,978,951,913,866,809,743,669,587,499,406,309,207,104,0,-104,-207,-309,-406,-500,-587,-669,-743,-809,-866,-913,-951,-978,-994};
int cos60[60] = {0,104,207,309,406,500,587,669,743,809,866,913,951,978,994,1000,994,978,951,913,866,809,743,669,587,500,406,309,207,104,0,-104,-207,-309,-406,-499,-587,-669,-743,-809,-866,-913,-951,-978,-994,-1000,-994,-978,-951,-913,-866,-809,-743,-669,-587,-500,-406,-309,-207,-104};

static int abs(int n) { return n < 0 ? -n : n; }

static void clock_draw_line(window_t* win, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1; 
    int err = dx + dy, e2;

    for (;;) {
        if (x0 >= 0 && x0 < (int)win->w && y0 >= 0 && y0 < (int)win->h) {
            win->buffer[y0 * win->w + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void clock_init(window_t* win) {
    if (!win) return;
    win->bg_color = 0x202020;
    clock_update(win);
}

void clock_update(window_t* win) {
    if (!win) return;
    
    // Clear buffer
    for (uint32_t i = 0; i < win->w * win->h; i++) {
        win->buffer[i] = win->bg_color;
    }
    
    int cx = win->w / 2;
    int cy = win->h / 2;
    int radius = (win->w < win->h ? win->w : win->h) / 2 - 20;
    
    // Draw clock face border (approximate circle with 60 segments)
    for (int i = 0; i < 60; i++) {
        int nx = (i + 1) % 60;
        int x0 = cx + (cos60[i] * radius) / 1000;
        int y0 = cy + (sin60[i] * radius) / 1000;
        int x1 = cx + (cos60[nx] * radius) / 1000;
        int y1 = cy + (sin60[nx] * radius) / 1000;
        clock_draw_line(win, x0, y0, x1, y1, 0xFFFFFF);
        
        // Draw hour ticks
        if (i % 5 == 0) {
            int ix = cx + (cos60[i] * (radius - 10)) / 1000;
            int iy = cy + (sin60[i] * (radius - 10)) / 1000;
            clock_draw_line(win, x0, y0, ix, iy, 0x00FF00);
        }
    }
    
    // Get time
    struct rtc_time t;
    rtc_read(&t);
    uint8_t h = t.hour % 12;
    uint8_t m = t.minute;
    uint8_t s = t.second;
    
    // Calculate 0..59 positions
    int sec_pos = s % 60;
    int min_pos = m % 60;
    int hr_pos = (h * 5) + (m / 12);
    
    // Draw hour hand
    int h_rad = radius - 30;
    int hx = cx + (cos60[hr_pos] * h_rad) / 1000;
    int hy = cy + (sin60[hr_pos] * h_rad) / 1000;
    clock_draw_line(win, cx, cy, hx, hy, 0xFF0000);
    
    // Draw minute hand
    int m_rad = radius - 15;
    int mx = cx + (cos60[min_pos] * m_rad) / 1000;
    int my = cy + (sin60[min_pos] * m_rad) / 1000;
    clock_draw_line(win, cx, cy, mx, my, 0x0000FF);
    
    // Draw second hand
    int s_rad = radius - 5;
    int sx = cx + (cos60[sec_pos] * s_rad) / 1000;
    int sy = cy + (sin60[sec_pos] * s_rad) / 1000;
    clock_draw_line(win, cx, cy, sx, sy, 0xFFFF00);
    
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

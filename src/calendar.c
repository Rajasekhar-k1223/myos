#include "calendar.h"
#include "rtc.h"
#include "string.h"

int sprintf(char *str, const char *format, ...);

static window_t* my_win = 0;
/* Simple event storage: up to 8 events per month */
#define MAX_EVENTS 8
static struct { int day; char text[32]; } events[MAX_EVENTS];
static int num_events = 0;
static int selected_day = 0;

/* Tomohiko Sakamoto's day-of-week algorithm (0=Sun ... 6=Sat) */
static int day_of_week(int y, int m, int d) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

/* Days in month (simplified: no century-leap-year correction needed for ~2020s) */
static int days_in_month(int y, int m) {
    static const int dim[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y%4==0 && y%100!=0) || y%400==0)) return 29;
    return dim[m];
}

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y+h; yy++)
        for (int xx = x; xx < x+w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

static void calendar_render(void) {
    if (!my_win) return;
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0x0D1117;

    struct rtc_time t;
    rtc_read(&t);
    int year  = 2000 + (int)t.year;
    int month = (int)t.month;
    int today = (int)t.day;

    char buf[64];
    static const char* mnames[] = {
        "","January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    sprintf(buf, "%s %d", mnames[month], year);
    wm_draw_string_window(my_win, 10, 8, buf, 0x58A6FF);

    /* Day headers */
    static const char* dnames[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    for (int i = 0; i < 7; i++)
        wm_draw_string_window(my_win, 12 + i*38, 30, dnames[i], 0x8B949E);

    draw_rect(8, 44, (int)my_win->w - 16, 1, 0x30363D);

    int first_dow = day_of_week(year, month, 1);
    int total_days = days_in_month(year, month);

    for (int d = 1; d <= total_days; d++) {
        int idx  = d - 1 + first_dow;
        int col  = idx % 7;
        int row  = idx / 7;
        int cx   = 12 + col * 38;
        int cy   = 50 + row * 34;

        /* Highlight today */
        if (d == today) {
            draw_rect(cx - 2, cy - 2, 28, 24, 0x1D4ED8);
        }
        /* Highlight selected */
        if (d == selected_day && d != today) {
            draw_rect(cx - 2, cy - 2, 28, 24, 0x334455);
        }

        /* Check if event on this day */
        int has_event = 0;
        for (int e = 0; e < num_events; e++)
            if (events[e].day == d) { has_event = 1; break; }

        char dbuf[4]; sprintf(dbuf, "%d", d);
        uint32_t col_fg = (d == today) ? 0xFFFFFF :
                          (col == 0 || col == 6) ? 0xFF6B6B : 0xC9D1D9;
        wm_draw_string_window(my_win, cx, cy, dbuf, col_fg);
        if (has_event)
            draw_rect(cx + 2, cy + 16, 4, 4, 0xF59E0B);
    }

    /* Event list for selected day */
    int ey = 50 + 6 * 34 + 8;
    if (selected_day > 0) {
        sprintf(buf, "Events for day %d:", selected_day);
        wm_draw_string_window(my_win, 10, ey, buf, 0x8B949E);
        int found = 0;
        for (int e = 0; e < num_events; e++) {
            if (events[e].day == selected_day) {
                wm_draw_string_window(my_win, 14, ey + 16 + found*16, events[e].text, 0xF59E0B);
                found++;
            }
        }
        if (!found)
            wm_draw_string_window(my_win, 14, ey + 16, "No events", 0x555555);
    } else {
        wm_draw_string_window(my_win, 10, ey, "Click a day to view events", 0x444444);
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void calendar_init(window_t* win) {
    my_win = win;
    selected_day = 0;
    num_events = 0;
    /* Seed a couple of demo events */
    events[0].day = 1; sprintf(events[0].text, "New Month Start");
    events[1].day = 15; sprintf(events[1].text, "Mid-Month Review");
    num_events = 2;
    calendar_render();
}

void calendar_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;

    struct rtc_time t;
    rtc_read(&t);
    int year  = 2000 + (int)t.year;
    int month = (int)t.month;
    int first_dow = day_of_week(year, month, 1);
    int total_days = days_in_month(year, month);

    for (int d = 1; d <= total_days; d++) {
        int idx = d - 1 + first_dow;
        int col = 12 + (idx % 7) * 38;
        int row = 50 + (idx / 7) * 34;
        if (lx >= col-2 && lx <= col+26 && ly >= row-2 && ly <= row+22) {
            selected_day = d;
            calendar_render();
            return;
        }
    }
}

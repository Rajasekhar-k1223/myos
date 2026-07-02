#include "weather.h"
#include "widget.h"
#include "string.h"
#include "dns.h"
#include "tcp.h"
#include "kernel.h"

int sprintf(char *str, const char *format, ...);

static window_t* my_win = 0;
static char weather_city[64]   = "London";
static char weather_result[128] = "";
static int  fetch_done  = 0;
static int  fetch_error = 0;
static widget_textinput_t city_input;

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (!my_win) return;
    for (int yy = y; yy < y+h; yy++)
        for (int xx = x; xx < x+w; xx++)
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h)
                my_win->buffer[yy * my_win->w + xx] = c;
}

static void weather_render(void) {
    if (!my_win) return;
    /* Sky gradient */
    for (int y = 0; y < (int)my_win->h; y++) {
        uint32_t r = 0x87 - (y * 0x30) / (int)my_win->h;
        uint32_t g = 0xCE - (y * 0x40) / (int)my_win->h;
        uint32_t b = 0xEB;
        uint32_t col = (r << 16) | (g << 8) | b;
        for (int x = 0; x < (int)my_win->w; x++)
            my_win->buffer[y * my_win->w + x] = col;
    }

    wm_draw_string_window(my_win, 10, 8, "Weather", 0x1E293B);

    /* City input field */
    wm_draw_string_window(my_win, 10, 30, "City:", 0x1E293B);
    widget_textinput_draw(my_win->x + 10, my_win->y + 46, (int)my_win->w - 80, &city_input, 0x2563EB);

    /* Fetch button */
    int bx = (int)my_win->w - 66, by = 46;
    draw_rect(bx, by, 58, 20, 0x2563EB);
    wm_draw_string_window(my_win, bx + 8, by + 4, "Fetch", 0xFFFFFF);

    /* Results area */
    if (!fetch_done) {
        wm_draw_string_window(my_win, 10, 80, "Ready. Press Fetch.", 0x334455);
    } else if (fetch_error) {
        wm_draw_string_window(my_win, 10, 80, "Error: network unavailable", 0xCC2222);
    } else {
        /* Sun icon */
        wm_draw_string_window(my_win, 20, 90,  "   \\  |  /  ", 0xFFD700);
        wm_draw_string_window(my_win, 20, 104, "  -(  o  )-  ", 0xFFD700);
        wm_draw_string_window(my_win, 20, 118, "   /  |  \\  ", 0xFFD700);

        char header[80];
        sprintf(header, "%s:", weather_city);
        wm_draw_string_window(my_win, 130, 92,  header, 0x1E293B);
        wm_draw_string_window(my_win, 130, 110, weather_result, 0x1E293B);
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

static void do_fetch(void) {
    fetch_done  = 0;
    fetch_error = 0;
    weather_render();

    uint32_t ip = 0;
    if (dns_resolve("wttr.in", &ip) != 0) { fetch_error = 1; fetch_done = 1; weather_render(); return; }

    int conn = tcp_connect(ip, 80);
    if (conn < 0) { fetch_error = 1; fetch_done = 1; weather_render(); return; }

    /* Build request for the entered city */
    char req[256];
    sprintf(req, "GET /%s?format=3 HTTP/1.0\r\nHost: wttr.in\r\nConnection: close\r\n\r\n",
            weather_city);
    tcp_send(conn, (const uint8_t*)req, strlen(req));

    uint8_t buf[1024];
    int len = tcp_recv(conn, buf, sizeof(buf)-1, 5000);
    tcp_close(conn);

    if (len > 0) {
        buf[len] = 0;
        char* body = strstr((char*)buf, "\r\n\r\n");
        if (body) {
            body += 4;
            strncpy(weather_result, body, 127);
            for (int i = 0; i < 127; i++) {
                if (weather_result[i]=='\n' || weather_result[i]=='\r')
                    { weather_result[i]='\0'; break; }
            }
        } else {
            strncpy(weather_result, "Unexpected response", 127);
        }
    } else {
        fetch_error = 1;
    }
    fetch_done = 1;
    weather_render();
}

void weather_init(window_t* win) {
    my_win = win;
    fetch_done = 0;
    fetch_error = 0;
    widget_textinput_init(&city_input);
    /* Pre-fill city */
    int cl = 0;
    const char* dc = "London";
    while (dc[cl]) { city_input.buf[cl] = dc[cl]; cl++; }
    city_input.buf[cl] = '\0';
    city_input.len = cl;
    city_input.focused = 1;
    weather_render();
}

void weather_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;

    /* Check textinput */
    if (widget_textinput_click(win->x + 10, win->y + 46, (int)win->w - 80, mx, my)) {
        city_input.focused = 1;
        weather_render();
        return;
    }

    /* Fetch button */
    int bx = (int)win->w - 66, by = 46;
    if (lx >= bx && lx <= bx+58 && ly >= by && ly <= by+20) {
        /* Copy textinput → city */
        strncpy(weather_city, city_input.buf, 63);
        if (weather_city[0] == '\0') strncpy(weather_city, "London", 63);
        do_fetch();
    }
}

void weather_handle_key(window_t* win, char c) {
    if (win != my_win || !city_input.focused) return;
    if (c == '\n' || c == '\r') {
        strncpy(weather_city, city_input.buf, 63);
        if (weather_city[0] == '\0') strncpy(weather_city, "London", 63);
        do_fetch();
    } else {
        widget_textinput_key(&city_input, c);
        weather_render();
    }
}

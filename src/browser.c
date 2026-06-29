// browser.c - Netscape Elsea - A proper browser for ElseaOS
// Supports: address bar, Go button, tab key, Enter, history, scrolling,
// basic HTML rendering (h1-h4, p, br, b, i, a, hr, title, ul/li)

#include "browser.h"
#include "widget.h"
#include "tcp.h"
#include "dns.h"
#include "string.h"
#include "kernel.h"
#include "ttf.h"
#include "pit.h"
#define TOOLBAR_H      36
#define STATUS_H       20
#define SCROLLBAR_W    12
#define ADDR_LEFT      80          // "Go" button right edge
#define ADDR_RIGHT_PAD 80          // "Go" button left pad from right
#define CONTENT_PAD_X  16
#define HISTORY_MAX    16
#define MAX_HTML       16384
#define MAX_URL        256

// Colour palette (24-bit packed RGB, upper byte 0)
#define CLR_TOOLBAR    0x2D2D2D
#define CLR_ADDR_BG    0x1A1A1A
#define CLR_ADDR_FOC   0xFFFFFF
#define CLR_ADDR_TXT   0xCCCCCC
#define CLR_GO_BG      0x0066CC
#define CLR_GO_HOV     0x0080FF
#define CLR_CONTENT_BG 0xF5F5F5
#define CLR_TEXT       0x111111
#define CLR_LINK       0x0066CC
#define CLR_H1         0x111111
#define CLR_H2         0x222222
#define CLR_H3         0x333333
#define CLR_BOLD       0x0A0A0A
#define CLR_EM         0x0000CC
#define CLR_HR         0xCCCCCC
#define CLR_STATUS_BG  0x222222
#define CLR_STATUS_TXT 0x888888
#define CLR_BACK_BG    0x3A3A3A
#define CLR_SCROLLBAR  0x555555
#define CLR_SCROLLGRIP 0x888888
#define CLR_ERROR      0xCC3333

// ─── State ────────────────────────────────────────────────────────────────────
window_t* browser_win = NULL;

static char  url_bar[MAX_URL]  = "10.0.2.2";
static int   url_len           = 8;
static int   url_focused       = 0;
static int   is_loading        = 0;
static char  html_buf[MAX_HTML];
static char  status_msg[128]   = "Ready";
static int   scroll_y          = 0;     // pixel offset scrolled down
static int   content_height    = 0;     // total rendered height in pixels
static int   go_hover          = 0;

// History
static char  history[HISTORY_MAX][MAX_URL];
static int   hist_top  = 0;  // points one past the current entry
static int   hist_cur  = -1; // current index in history (-1 = none)

// ─── Helpers ──────────────────────────────────────────────────────────────────
static int content_y_start(void) { return TOOLBAR_H; }
static int content_y_end(void)   { return (int)browser_win->h - STATUS_H; }
static int content_w(void)       { return (int)browser_win->w - SCROLLBAR_W; }
static int content_h(void)       { return content_y_end() - content_y_start(); }

// Draw filled rect inside window buffer
static void fill_rect(int x, int y, int w, int h, uint32_t col) {
    if (!browser_win) return;
    int bw = (int)browser_win->w, bh = (int)browser_win->h;
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= bh) continue;
        for (int col2 = x; col2 < x + w; col2++) {
            if (col2 < 0 || col2 >= bw) continue;
            browser_win->buffer[row * bw + col2] = col;
        }
    }
}

// Draw text clipped to window (offset by scroll for content area)
static void draw_text(int x, int y, const char* s, int size, uint32_t col) {
    if (!browser_win || !s || !*s) return;
    ttf_draw_string(browser_win->buffer, browser_win->w, browser_win->h,
                    x, y, s, size, col);
}

// Draw a horizontal gradient bar
static void fill_gradient_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    if (!browser_win) return;
    int bw = (int)browser_win->w, bh = (int)browser_win->h;
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= bh) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= bw) continue;
            float t = (float)(col - x) / (float)(w > 1 ? w - 1 : 1);
            uint8_t r = ((c1 >> 16) & 0xFF) * (1.0f - t) + ((c2 >> 16) & 0xFF) * t;
            uint8_t g = ((c1 >>  8) & 0xFF) * (1.0f - t) + ((c2 >>  8) & 0xFF) * t;
            uint8_t b = ( c1        & 0xFF) * (1.0f - t) + ( c2        & 0xFF) * t;
            browser_win->buffer[row * bw + col] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

// ─── HTML Renderer ────────────────────────────────────────────────────────────
// Returns total rendered height in pixels
static int render_html(int dry_run) {
    if (!browser_win) return 0;

    int cw     = content_w() - CONTENT_PAD_X * 2;
    int bw     = (int)browser_win->w;
    int cy0    = content_y_start();
    int cy1    = content_y_end();
    int x      = CONTENT_PAD_X;
    int y      = cy0 + CONTENT_PAD_X - scroll_y; // absolute y on screen
    int font   = 15;
    uint32_t color = CLR_TEXT;
    int bold   = 0;
    int in_ul  = 0;

    // Clear content background
    if (!dry_run) fill_rect(0, cy0, bw, cy1 - cy0, CLR_CONTENT_BG);

    char* p = html_buf;

    while (*p) {
        if (*p == '<') {
            char tag[64] = {0};
            int  ti = 0;
            p++; // skip '<'
            while (*p && *p != '>' && ti < 63) tag[ti++] = *p++;
            if (*p == '>') p++;

            // Strip attributes: tag is just the first word
            int k = 0;
            while (tag[k] && tag[k] != ' ') k++;
            tag[k] = '\0';

            // Convert to lower
            for (int i = 0; tag[i]; i++)
                if (tag[i] >= 'A' && tag[i] <= 'Z') tag[i] += 32;

            if (strcmp(tag, "h1") == 0)       { font = 28; color = CLR_H1; x = CONTENT_PAD_X; y += 18; }
            else if (strcmp(tag, "/h1") == 0) { x = CONTENT_PAD_X; y += 10; font = 15; color = CLR_TEXT; }
            else if (strcmp(tag, "h2") == 0)  { font = 22; color = CLR_H2; x = CONTENT_PAD_X; y += 14; }
            else if (strcmp(tag, "/h2") == 0) { x = CONTENT_PAD_X; y += 8;  font = 15; color = CLR_TEXT; }
            else if (strcmp(tag, "h3") == 0)  { font = 18; color = CLR_H3; x = CONTENT_PAD_X; y += 10; }
            else if (strcmp(tag, "/h3") == 0) { x = CONTENT_PAD_X; y += 6;  font = 15; color = CLR_TEXT; }
            else if (strcmp(tag, "p") == 0 || strcmp(tag, "/p") == 0) { x = CONTENT_PAD_X; y += font + 8; }
            else if (strcmp(tag, "br") == 0 || strcmp(tag, "br/") == 0) { x = CONTENT_PAD_X; y += font + 4; }
            else if (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0)  { bold = 1; }
            else if (strcmp(tag, "/b") == 0 || strcmp(tag, "/strong") == 0) { bold = 0; }
            else if (strcmp(tag, "i") == 0 || strcmp(tag, "em") == 0)  { color = CLR_EM; }
            else if (strcmp(tag, "/i") == 0 || strcmp(tag, "/em") == 0) { color = CLR_TEXT; }
            else if (strcmp(tag, "a") == 0)   { color = CLR_LINK; }
            else if (strcmp(tag, "/a") == 0)  { color = CLR_TEXT; }
            else if (strcmp(tag, "ul") == 0)  { in_ul = 1; x = CONTENT_PAD_X + 20; y += 6; }
            else if (strcmp(tag, "/ul") == 0) { in_ul = 0; x = CONTENT_PAD_X; y += 6; }
            else if (strcmp(tag, "li") == 0) {
                x = CONTENT_PAD_X + 20; y += font + 4;
                if (!dry_run && y > cy0 && y < cy1)
                    draw_text(CONTENT_PAD_X + 4, y, "•", font, color);
            }
            else if (strcmp(tag, "hr") == 0) {
                y += 8;
                if (!dry_run && y > cy0 && y < cy1)
                    fill_rect(CONTENT_PAD_X, y - 1, cw, 1, CLR_HR);
                y += 8;
            }
            else if (strcmp(tag, "title") == 0 || strcmp(tag, "script") == 0 ||
                     strcmp(tag, "style") == 0) {
                // Skip to closing tag
                while (*p) {
                    if (*p == '<') {
                        char ct[16] = {0}; int ci = 0;
                        p++;
                        while (*p && *p != '>' && ci < 15) ct[ci++] = *p++;
                        if (*p == '>') p++;
                        if (ct[0] == '/') break;
                    } else p++;
                }
            }
            continue;
        }

        // Collect word token
        char word[256]; int wi = 0;
        while (*p && *p != '<' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && wi < 255)
            word[wi++] = *p++;
        word[wi] = '\0';

        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\r') {
            while (*p == '\n' || *p == '\r') p++;
            // Treat newline in source as soft wrap — don't force hard break
        }

        if (wi == 0) continue;

        // Estimate word width
        int advance = (font * 6) / 10;   // ~0.6 * font per char
        int word_px = wi * advance;

        // Word wrap
        if (x + word_px > CONTENT_PAD_X + cw && x > CONTENT_PAD_X) {
            x  = CONTENT_PAD_X + (in_ul ? 20 : 0);
            y += font + 4;
        }

        // Draw if visible
        if (!dry_run && y > (cy0 - font) && y < cy1) {
            draw_text(x, y, word, font, bold ? CLR_BOLD : color);
        }
        x += word_px + advance; // advance + space
    }

    return y + font - content_y_start() + CONTENT_PAD_X;  // total height
}

// ─── Draw Toolbar ─────────────────────────────────────────────────────────────
static void draw_toolbar(void) {
    int bw = (int)browser_win->w;

    // Gradient toolbar background
    fill_gradient_h(0, 0, bw, TOOLBAR_H, 0x3A3A3A, 0x252525);

    // Back button
    uint32_t back_col = (hist_cur > 0) ? 0x555555 : 0x333333;
    fill_rect(4, 6, 24, 24, back_col);
    draw_text(7, 22, "<", 16, (hist_cur > 0) ? 0xCCCCCC : 0x666666);

    // Forward button
    uint32_t fwd_col = (hist_cur >= 0 && hist_cur < hist_top - 1) ? 0x555555 : 0x333333;
    fill_rect(32, 6, 24, 24, fwd_col);
    draw_text(35, 22, ">", 16, (hist_cur >= 0 && hist_cur < hist_top - 1) ? 0xCCCCCC : 0x666666);

    // Reload button
    fill_rect(60, 6, 14, 24, 0x404040);
    draw_text(63, 22, is_loading ? "X" : "R", 14, 0xAAAAAA);

    // Address bar
    int addr_x  = ADDR_LEFT;
    int addr_w  = bw - ADDR_LEFT - ADDR_RIGHT_PAD;
    uint32_t addr_bg = url_focused ? 0xFFFFFF : 0x1A1A1A;
    fill_rect(addr_x, 6, addr_w, 24, addr_bg);

    // Border on address bar
    for (int i = addr_x; i < addr_x + addr_w; i++) {
        browser_win->buffer[6 * bw + i]  = url_focused ? CLR_GO_BG : 0x555555;
        browser_win->buffer[29 * bw + i] = url_focused ? CLR_GO_BG : 0x555555;
    }
    for (int j = 6; j < 30; j++) {
        browser_win->buffer[j * bw + addr_x]              = url_focused ? CLR_GO_BG : 0x555555;
        browser_win->buffer[j * bw + addr_x + addr_w - 1] = url_focused ? CLR_GO_BG : 0x555555;
    }

    // Draw URL text (clip to bar)
    uint32_t txt_col = url_focused ? 0x111111 : 0xAAAAAA;
    draw_text(addr_x + 6, 22, url_bar, 14, txt_col);

    // Blinking cursor when focused
    if (url_focused) {
        int cur_x = addr_x + 6 + url_len * 8;  // approx
        if (cur_x < addr_x + addr_w - 6)
            fill_rect(cur_x, 9, 1, 17, 0x0066CC);
    }

    // "Go" button
    uint32_t go_col = go_hover ? CLR_GO_HOV : CLR_GO_BG;
    fill_rect(bw - ADDR_RIGHT_PAD + 4, 6, ADDR_RIGHT_PAD - 8, 24, go_col);
    draw_text(bw - ADDR_RIGHT_PAD + 14, 22, is_loading ? "Stop" : " Go", 14, 0xFFFFFF);
}

// ─── Draw Scrollbar ───────────────────────────────────────────────────────────
static void draw_scrollbar(void) {
    int bw = (int)browser_win->w;
    int cy0 = content_y_start(), cy1 = content_y_end();
    int track_h = cy1 - cy0;
    int sx = bw - SCROLLBAR_W;

    fill_rect(sx, cy0, SCROLLBAR_W, track_h, 0x303030);

    if (content_height <= 0) return;
    int vis = content_h();
    if (content_height <= vis) return;

    float ratio  = (float)vis / (float)content_height;
    int grip_h   = (int)(track_h * ratio);
    if (grip_h < 20) grip_h = 20;
    int grip_y   = cy0 + (int)((float)scroll_y / (float)(content_height - vis) * (track_h - grip_h));
    if (grip_y < cy0) grip_y = cy0;
    if (grip_y + grip_h > cy1) grip_y = cy1 - grip_h;

    fill_rect(sx + 2, grip_y, SCROLLBAR_W - 4, grip_h, CLR_SCROLLGRIP);
}

// ─── Draw Status Bar ─────────────────────────────────────────────────────────
static void draw_status(void) {
    int bw = (int)browser_win->w;
    int sy = content_y_end();
    fill_rect(0, sy, bw, STATUS_H, CLR_STATUS_BG);
    draw_text(8, sy + 14, status_msg, 12, CLR_STATUS_TXT);
}

// ─── Full Render ─────────────────────────────────────────────────────────────
void browser_render(void) {
    if (!browser_win) return;
    draw_toolbar();
    content_height = render_html(0);
    draw_scrollbar();
    draw_status();
}

// ─── Parse a bare IP string into a uint32 (big-endian network order) ─────────
static uint32_t parse_ip(const char* s) {
    uint32_t ip = 0;
    int      val = 0, part = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '.') {
            ip |= (val << (24 - part * 8));
            part++; val = 0;
        } else if (s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
        }
    }
    ip |= val;
    return ip;
}

// ─── Navigate to current URL ──────────────────────────────────────────────────
static void do_navigate(void) {
    is_loading = 1;
    strcpy(status_msg, "Connecting...");
    browser_render();

    /* HTTPS: show informational fallback page */
    if (strncmp(url_bar, "https://", 8) == 0) {
        const char* host = url_bar + 8;
        strcpy(html_buf,
            "<h1>HTTPS Not Supported</h1>"
            "<p>ElseaOS browser supports HTTP only.</p>"
            "<p>TLS/SSL requires a cryptography library not included in the kernel.</p>"
            "<p>Try: <b>http://");
        strncat(html_buf, host, 180);
        strcat(html_buf, "</b></p>");
        strcpy(status_msg, "HTTPS not supported");
        is_loading = 0;
        browser_render();
        return;
    }

    // Parse optional port from "ip:port" syntax
    char ip_only[MAX_URL] = {0};
    uint16_t port = 80;
    {
        int colon = -1;
        for (int i = 0; url_bar[i]; i++) { if (url_bar[i] == ':') { colon = i; break; } }
        if (colon > 0) {
            strncpy(ip_only, url_bar, colon);
            ip_only[colon] = '\0';
            port = 0;
            for (int i = colon + 1; url_bar[i]; i++) {
                if (url_bar[i] >= '0' && url_bar[i] <= '9')
                    port = port * 10 + (url_bar[i] - '0');
            }
        } else {
            strcpy(ip_only, url_bar);
        }
    }
    /* DNS: resolve hostname if ip_only contains letters */
    uint32_t ip = 0;
    {
        int _is_host = 0;
        for (int _i = 0; ip_only[_i]; _i++) {
            char _c = ip_only[_i];
            if ((_c >= 'a' && _c <= 'z') || (_c >= 'A' && _c <= 'Z') || _c == '-')
                { _is_host = 1; break; }
        }
        if (_is_host) {
            extern int dns_resolve(const char* hostname, uint32_t* ip_out);
            strcpy(status_msg, "Resolving DNS...");
            browser_render();
            if (!dns_resolve(ip_only, &ip)) {
                strcpy(html_buf,
                    "<h1>DNS Failed</h1>"
                    "<p>Could not resolve <b>");
                strcat(html_buf, ip_only);
                strcat(html_buf, "</b></p>"
                    "<p>QEMU user-net forwards to 8.8.8.8.</p>");
                strcpy(status_msg, "Error: DNS failed");
                is_loading = 0;
                browser_render();
                return;
            }
        } else {
            ip = parse_ip(ip_only);
        }
    }

    // Store in history
    if (hist_cur < 0 || strcmp(history[hist_cur], url_bar) != 0) {
        if (hist_top >= HISTORY_MAX) {
            for (int i = 0; i < HISTORY_MAX - 1; i++)
                memcpy(history[i], history[i+1], MAX_URL);
            hist_top = HISTORY_MAX - 1;
            hist_cur = hist_top - 1;
        }
        strcpy(history[hist_top], url_bar);
        hist_cur = hist_top;
        hist_top++;
    }

    scroll_y = 0;

    if (tcp_connect(ip, port) >= 0) {
        char req[512];
        strcpy(req, "GET / HTTP/1.1\r\nHost: ");
        strcat(req, url_bar);
        strcat(req, "\r\nConnection: close\r\n\r\n");
        tcp_send_data((uint8_t*)req, strlen(req));

        strcpy(status_msg, "Waiting for response...");
        browser_render();

        uint32_t timeout = pit_get_ticks() + 6000;
        while (!tcp_has_data && pit_get_ticks() < timeout) {}

        if (tcp_has_data) {
            // Skip HTTP headers
            char* body = strstr((char*)tcp_recv_buffer, "\r\n\r\n");
            if (body) body += 4;
            else       body = (char*)tcp_recv_buffer;

            strncpy(html_buf, body, MAX_HTML - 1);
            html_buf[MAX_HTML - 1] = '\0';
            strcpy(status_msg, "Done");
        } else {
            strcpy(html_buf,
                "<h1>Request Timed Out</h1>"
                "<p>The server did not respond in time.</p>"
                "<hr>"
                "<p>Make sure the server is running and try again.</p>");
            strcpy(status_msg, "Error: Timed out");
        }
    } else {
        strcpy(html_buf,
            "<h1>Connection Failed</h1>"
            "<p>Could not connect to <b>");
        strcat(html_buf, url_bar);
        strcat(html_buf, "</b> on port 80.</p>"
            "<hr>"
            "<p>Check that the IP address is correct and the host is reachable.</p>");
        strcpy(status_msg, "Error: Connection refused");
    }

    is_loading = 0;
    browser_render();
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void browser_init(window_t* win) {
    browser_win = win;
    strcpy(html_buf,
        "<h1>Welcome to Netscape Elsea</h1>"
        "<p>Type an IP address in the bar above and click <b>Go</b>.</p>"
        "<hr>"
        "<p>ElseaOS supports HTTP/1.1 over TCP/IP via the RTL8139 network driver.</p>"
        "<h2>Quick Start</h2>"
        "<ul>"
        "<li>Type an IP address (e.g. 10.0.2.2) and press Enter or click Go</li>"
        "<li>Use the &lt; and &gt; buttons to go Back and Forward</li>"
        "<li>Press R to reload the current page</li>"
        "</ul>"
        "<hr>"
        "<p>Netscape Elsea v1.0 — Built for ElseaOS</p>");
    scroll_y = 0; hist_cur = -1; hist_top = 0;
    browser_render();
}

// ─── Click Handler ────────────────────────────────────────────────────────────
void browser_handle_click(int mx, int my) {
    if (!browser_win) return;
    int lx = mx - (int)browser_win->x;
    int ly = my - (int)browser_win->y;
    int bw = (int)browser_win->w;

    if (ly >= 0 && ly < TOOLBAR_H) {
        // Back button
        if (lx >= 4 && lx <= 28 && hist_cur > 0) {
            hist_cur--;
            strcpy(url_bar, history[hist_cur]);
            url_len = strlen(url_bar);
            do_navigate();
            return;
        }
        // Forward button
        if (lx >= 32 && lx <= 56 && hist_cur >= 0 && hist_cur < hist_top - 1) {
            hist_cur++;
            strcpy(url_bar, history[hist_cur]);
            url_len = strlen(url_bar);
            do_navigate();
            return;
        }
        // Reload
        if (lx >= 60 && lx <= 74) {
            do_navigate();
            return;
        }
        // Address bar
        if (lx >= ADDR_LEFT && lx < bw - ADDR_RIGHT_PAD) {
            url_focused = 1;
            browser_render();
            return;
        }
        // Go button
        if (lx >= bw - ADDR_RIGHT_PAD) {
            url_focused = 0;
            do_navigate();
            return;
        }
    }

    // Scrollbar
    int sy = content_y_start();
    int ey = content_y_end();
    if (lx >= bw - SCROLLBAR_W && ly >= sy && ly <= ey) {
        int vis = content_h();
        if (content_height > vis) {
            float ratio = (float)(ly - sy) / (float)(ey - sy);
            scroll_y = (int)(ratio * (content_height - vis));
            if (scroll_y < 0) scroll_y = 0;
            if (scroll_y > content_height - vis) scroll_y = content_height - vis;
            browser_render();
        }
        return;
    }

    url_focused = 0;
    browser_render();
}

// ─── Keyboard Handler ─────────────────────────────────────────────────────────
void browser_handle_keypress(char c) {
    if (!browser_win) return;

    if (!url_focused) {
        // Global shortcuts when address bar is not focused
        if (c == 'r' || c == 'R') { do_navigate(); return; }
        // Scroll
        if (c == 6) { // PgDn (mapped to key 6 or similar)
            scroll_y += content_h() / 2;
            int max_s = content_height - content_h();
            if (scroll_y > max_s) scroll_y = max_s;
            if (scroll_y < 0) scroll_y = 0;
            browser_render(); return;
        }
        return;
    }

    if (c == '\b') {
        if (url_len > 0) url_bar[--url_len] = '\0';
    } else if (c == '\n' || c == '\r') {
        url_focused = 0;
        do_navigate();
        return;
    } else if (c >= ' ' && c <= '~') {
        if (url_len < MAX_URL - 1) {
            url_bar[url_len++] = c;
            url_bar[url_len]   = '\0';
        }
    }
    browser_render();
}

// ─── Scroll Wheel Handler (call from WM) ─────────────────────────────────────
void browser_handle_scroll(int delta) {
    if (!browser_win) return;
    scroll_y += delta * 30;
    int max_s = content_height - content_h();
    if (max_s < 0) max_s = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_s) scroll_y = max_s;
    browser_render();
}

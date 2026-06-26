#include "browser.h"
#include "wm.h"
#include "string.h"
#include "kernel.h"
#include "tcp.h"
#include "pit.h"
#include "ttf.h"

window_t* browser_win = NULL;

static char url_bar[256] = "10.0.2.2";
static int url_len = 8;
static int is_focused = 0;
static int is_loading = 0;
static char html_content[8192] = "Welcome to Netscape Elsea.\nType an IP address and click Go.";

static void parse_and_render_html(void) {
    int x = 10;
    int y = 50; // Below address bar
    int max_width = browser_win->w - 20;
    
    // Clear render area
    for (int i = 40; i < browser_win->h; i++) {
        for (int j = 0; j < browser_win->w; j++) {
            browser_win->buffer[i * browser_win->w + j] = 0xFFFFFF; // White bg
        }
    }
    
    char* p = html_content;
    int in_tag = 0;
    int font_scale = 16; // default 16px
    uint32_t color = 0x000000;
    
    char word[256];
    int word_idx = 0;
    
    while (*p) {
        if (*p == '<') {
            in_tag = 1;
            char tag[32] = {0};
            int t = 0;
            p++;
            while (*p && *p != '>' && t < 31) {
                tag[t++] = *p++;
            }
            if (*p == '>') p++;
            
            // Basic tag parsing
            if (strcmp(tag, "h1") == 0) { font_scale = 32; y += 40; x = 10; }
            else if (strcmp(tag, "/h1") == 0) { font_scale = 16; y += 40; x = 10; }
            else if (strcmp(tag, "p") == 0) { font_scale = 16; y += 20; x = 10; }
            else if (strcmp(tag, "/p") == 0) { y += 20; x = 10; }
            else if (strcmp(tag, "b") == 0) { color = 0xFF0000; } // bold is red for now
            else if (strcmp(tag, "/b") == 0) { color = 0x000000; }
            else if (strcmp(tag, "br") == 0) { y += 20; x = 10; }
            
            in_tag = 0;
            continue;
        }
        
        // Read a word
        word_idx = 0;
        while (*p && *p != '<' && *p != ' ' && *p != '\n' && word_idx < 255) {
            word[word_idx++] = *p++;
        }
        word[word_idx] = '\0';
        
        if (word_idx > 0) {
            // Rough width estimate
            int w = word_idx * (font_scale / 2);
            if (x + w > max_width) {
                x = 10;
                y += font_scale + 4;
            }
            
            // Draw word
            if (y + font_scale < browser_win->h) {
                ttf_draw_string(browser_win->buffer, browser_win->w, browser_win->h, x, y + font_scale, word, font_scale, color);
            }
            x += w + (font_scale / 2); // Space
        }
        
        if (*p == ' ' || *p == '\n') p++;
    }
}

void browser_render(void) {
    if (!browser_win) return;
    
    // Draw top bar
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < browser_win->w; x++) {
            browser_win->buffer[y * browser_win->w + x] = 0xCCCCCC;
        }
    }
    
    // Address bar bg
    uint32_t url_bg = is_focused ? 0xFFFFFF : 0xEEEEEE;
    for (int y = 10; y < 30; y++) {
        for (int x = 10; x < browser_win->w - 80; x++) {
            browser_win->buffer[y * browser_win->w + x] = url_bg;
        }
    }
    
    // URL text
    ttf_draw_string(browser_win->buffer, browser_win->w, browser_win->h, 15, 25, url_bar, 16, 0x000000);
    
    // "Go" button
    for (int y = 10; y < 30; y++) {
        for (int x = browser_win->w - 70; x < browser_win->w - 10; x++) {
            browser_win->buffer[y * browser_win->w + x] = 0x888888;
        }
    }
    ttf_draw_string(browser_win->buffer, browser_win->w, browser_win->h, browser_win->w - 55, 25, is_loading ? "Wait" : "Go", 16, 0xFFFFFF);
    
    // Render HTML
    parse_and_render_html();
}

void browser_init(window_t* win) {
    browser_win = win;
    browser_render();
}

void browser_handle_click(int mx, int my) {
    int lx = mx - browser_win->x;
    int ly = my - browser_win->y;
    
    if (ly >= 10 && ly <= 30) {
        if (lx >= 10 && lx <= browser_win->w - 80) {
            is_focused = 1;
        } else if (lx >= browser_win->w - 70 && lx <= browser_win->w - 10) {
            is_focused = 0;
            // Clicked Go
            is_loading = 1;
            browser_render();
            
            // Parse IP
            uint32_t a, b, c, d;
            int i = 0, part = 0, val = 0;
            uint32_t ip = 0;
            while (url_bar[i]) {
                if (url_bar[i] == '.') {
                    ip |= (val << (24 - part * 8));
                    part++;
                    val = 0;
                } else if (url_bar[i] >= '0' && url_bar[i] <= '9') {
                    val = val * 10 + (url_bar[i] - '0');
                }
                i++;
            }
            ip |= val;
            
            strcpy(html_content, "Connecting...");
            browser_render();
            
            if (tcp_connect(ip, 80)) {
                char get_req[512];
                strcpy(get_req, "GET / HTTP/1.1\r\nHost: myos\r\nConnection: close\r\n\r\n");
                tcp_send_data((uint8_t*)get_req, strlen(get_req));
                
                uint32_t timeout = pit_get_ticks() + 5000;
                while (!tcp_has_data && pit_get_ticks() < timeout);
                
                if (tcp_has_data) {
                    // Extract payload (skip headers)
                    char* body = strstr((char*)tcp_recv_buffer, "\r\n\r\n");
                    if (body) {
                        body += 4;
                        strncpy(html_content, body, 8191);
                    } else {
                        strncpy(html_content, (char*)tcp_recv_buffer, 8191);
                    }
                } else {
                    strcpy(html_content, "<h1>Error</h1><p>Connection Timed Out.</p>");
                }
            } else {
                strcpy(html_content, "<h1>Error</h1><p>Connection Failed.</p>");
            }
            is_loading = 0;
            browser_render();
        }
    } else {
        is_focused = 0;
    }
}

void browser_handle_keypress(char c) {
    if (!is_focused) return;
    
    if (c == '\b') {
        if (url_len > 0) {
            url_bar[--url_len] = '\0';
        }
    } else if (c >= ' ' && c <= '~') {
        if (url_len < 255) {
            url_bar[url_len++] = c;
            url_bar[url_len] = '\0';
        }
    } else if (c == '\n') {
        browser_handle_click(browser_win->x + browser_win->w - 40, browser_win->y + 20); // Simulate "Go" click
    }
    browser_render();
}

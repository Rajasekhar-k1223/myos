#include "html5.h"
#include "kernel.h"
#include "string.h"
#include "ttf.h"
#include "gpu.h"

void html5_init(void) {
    terminal_printf("[WEB] HTML5 DOM Renderer initialized.\n");
}

void html5_render(const char* html_text, int window_x, int window_y) {
    /* Very basic mock DOM parser */
    if (strstr(html_text, "<html>")) {
        /* Draw white background */
        gpu_draw_rect(window_x, window_y, 800, 600, 0xFFFFFF);
        
        /* Render H1 text if found */
        char* h1 = strstr((char*)html_text, "<h1>");
        if (h1) {
            ttf_render_text(window_x + 20, window_y + 40, "Parsed HTML5 Header", 24, 0x000000);
        }
    }
}

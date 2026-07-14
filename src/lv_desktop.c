#include "lvgl.h"
#include <stdint.h>
#include <stddef.h>
#include "wm.h"

extern void* kmalloc(size_t);
extern void kfree(void*);
extern void bmp_load_to_buffer_scaled(const char*, uint32_t*, int, int, int, int, int, int);
extern uint32_t vesa_width;
extern uint32_t vesa_height;

extern window_t windows[64]; // MAX_WINDOWS is usually 64
extern int num_windows;
extern int redraw_needed;

static uint32_t* desktop_bg_buf = NULL;
static lv_obj_t* desktop_canvas = NULL;
static lv_obj_t* taskbar = NULL;
static lv_timer_t* desktop_timer = NULL;
static lv_obj_t* clock_lbl = NULL;

static void clock_update_timer_cb(lv_timer_t* t) {
    (void)t;
    if (clock_lbl) {
        extern void rtc_datetime_str(char* buf);
        char buf[32];
        rtc_datetime_str(buf);
        lv_label_set_text(clock_lbl, buf);
    }
}

static void desktop_update_timer_cb(lv_timer_t* timer) {
    (void)timer;
    if (redraw_needed) {
        for (int i = 0; i < num_windows; i++) {
            if (windows[i].active && windows[i].lv_canvas) {
                lv_obj_invalidate(windows[i].lv_canvas);
            }
        }
        redraw_needed = 0;
    }
}

void lv_desktop_init(void) {
    static int lvgl_inited = 0;
    if (!lvgl_inited) {
        lvgl_inited = 1;
        extern void lv_init(void);
        extern void lv_port_disp_init(void);
        extern void lv_port_indev_init(void);
        lv_init();
        lv_port_disp_init();
        lv_port_indev_init();
    }

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // 1. Setup Desktop Background
    desktop_bg_buf = (uint32_t*)kmalloc(vesa_width * vesa_height * 4);
    if (desktop_bg_buf) {
        bmp_load_to_buffer_scaled("elsea_bg.bmp", desktop_bg_buf, vesa_width, vesa_height, 0, 0, vesa_width, vesa_height);
        
        // Fast Box Blur (Radius 8)
        int r = 8;
        uint32_t* temp_buf = (uint32_t*)kmalloc(vesa_width * vesa_height * 4);
        if (temp_buf) {
            // Horizontal blur
            for (uint32_t y = 0; y < vesa_height; y++) {
                for (uint32_t x = 0; x < vesa_width; x++) {
                    uint32_t tr = 0, tg = 0, tb = 0;
                    int count = 0;
                    for (int k = -r; k <= r; k++) {
                        int nx = (int)x + k;
                        if (nx >= 0 && nx < (int)vesa_width) {
                            uint32_t c = desktop_bg_buf[y * vesa_width + nx];
                            tr += (c >> 16) & 0xFF;
                            tg += (c >> 8) & 0xFF;
                            tb += c & 0xFF;
                            count++;
                        }
                    }
                    temp_buf[y * vesa_width + x] = ((tr / count) << 16) | ((tg / count) << 8) | (tb / count);
                }
            }
            // Vertical blur
            for (uint32_t x = 0; x < vesa_width; x++) {
                for (uint32_t y = 0; y < vesa_height; y++) {
                    uint32_t tr = 0, tg = 0, tb = 0;
                    int count = 0;
                    for (int k = -r; k <= r; k++) {
                        int ny = (int)y + k;
                        if (ny >= 0 && ny < (int)vesa_height) {
                            uint32_t c = temp_buf[ny * vesa_width + x];
                            tr += (c >> 16) & 0xFF;
                            tg += (c >> 8) & 0xFF;
                            tb += c & 0xFF;
                            count++;
                        }
                    }
                    desktop_bg_buf[y * vesa_width + x] = ((tr / count) << 16) | ((tg / count) << 8) | (tb / count);
                }
            }
            kfree(temp_buf);
        }
        
        for (uint32_t i = 0; i < vesa_width * vesa_height; i++) {
            desktop_bg_buf[i] |= 0xFF000000;
        }

        desktop_canvas = lv_canvas_create(scr);
        lv_canvas_set_buffer(desktop_canvas, desktop_bg_buf, vesa_width, vesa_height, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_obj_align(desktop_canvas, LV_ALIGN_CENTER, 0, 0);
    }

    // 2. Setup Taskbar
    taskbar = lv_obj_create(scr);
    lv_obj_set_size(taskbar, vesa_width, 40);
    lv_obj_align(taskbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(taskbar, lv_color_hex(0x101520), 0);
    lv_obj_set_style_bg_opa(taskbar, 200, 0);
    lv_obj_set_style_border_width(taskbar, 0, 0);
    lv_obj_set_style_radius(taskbar, 0, 0);
    
    lv_obj_t* title = lv_label_create(taskbar);
    lv_label_set_text(title, "ElseaOS");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    clock_lbl = lv_label_create(taskbar);
    lv_label_set_text(clock_lbl, "Loading...");
    lv_obj_set_style_text_color(clock_lbl, lv_color_white(), 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

    // Terminal icon removed as requested
    // 3. Start Window Update Timer (runs at 60fps)
    desktop_timer = lv_timer_create(desktop_update_timer_cb, 16, NULL);

    // 4. Start Clock Update Timer (runs once a second)
    lv_timer_create(clock_update_timer_cb, 1000, NULL);

    // 5. Setup Mouse Cursor via LVGL
    extern lv_indev_t* indev_mouse;
    if (indev_mouse) {
        static uint32_t cursor_buf[15 * 10];
        static const uint8_t cursor_bitmap[15][10] = {
            {1,0,0,0,0,0,0,0,0,0},
            {1,1,0,0,0,0,0,0,0,0},
            {1,2,1,0,0,0,0,0,0,0},
            {1,2,2,1,0,0,0,0,0,0},
            {1,2,2,2,1,0,0,0,0,0},
            {1,2,2,2,2,1,0,0,0,0},
            {1,2,2,2,2,2,1,0,0,0},
            {1,2,2,2,2,2,2,1,0,0},
            {1,2,2,2,2,2,2,2,1,0},
            {1,2,2,2,2,2,2,2,2,1},
            {1,2,2,2,2,2,1,1,1,1},
            {1,2,2,1,2,2,1,0,0,0},
            {1,2,1,0,1,2,2,1,0,0},
            {1,1,0,0,1,2,2,1,0,0},
            {1,0,0,0,0,1,1,0,0,0}
        };
        for(int y = 0; y < 15; y++) {
            for(int x = 0; x < 10; x++) {
                if(cursor_bitmap[y][x] == 1) cursor_buf[y * 10 + x] = 0xFF000000;
                else if(cursor_bitmap[y][x] == 2) cursor_buf[y * 10 + x] = 0xFFFFFFFF;
                else cursor_buf[y * 10 + x] = 0x00000000;
            }
        }
        lv_obj_t* cursor_canvas = lv_canvas_create(lv_layer_sys());
        lv_obj_set_style_bg_opa(cursor_canvas, 0, 0);
        lv_obj_set_style_border_width(cursor_canvas, 0, 0);
        lv_canvas_set_buffer(cursor_canvas, cursor_buf, 10, 15, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_indev_set_cursor(indev_mouse, cursor_canvas);
    }
}

#include "login.h"
#include "widget.h"
#include "string.h"
#include "kheap.h"
#include "kernel.h"
#include "mouse.h"
#include "vesa.h"

static window_t* login_win = NULL;
static int authenticated = 0;
static widget_textinput_t txt_user;
static widget_textinput_t txt_pass;

void login_init(window_t* desktop_win) {
    (void)desktop_win;
    extern uint32_t vesa_width, vesa_height;
    
    // Destroy existing if any
    if (login_win) {
        login_win->active = 0;
    }
    
    login_win = wm_create_window(vesa_width/2 - 200, vesa_height/2 - 225, 400, 450, "Login - ElseaOS");
    if (!login_win) return;
    
    widget_textinput_init(&txt_user);
    widget_textinput_init(&txt_pass);
    txt_pass.is_password = 1;
    
    authenticated = 0;
}

int login_is_authenticated(void) {
    return 1; // Login completely disabled per user request
}

void login_set_authenticated(int auth) {
    authenticated = auth;
}

void login_pump(void) {
    if (!login_win) return;
    
    // Simple custom pump/draw for login to match the sleek Glassmorphism mockup
    
    // Draw an avatar circle (approximation using a filled rect for now, or just text)
    // Centered at 200, 80
    vesa_draw_rect_alpha(login_win->x + 160, login_win->y + 40, 80, 80, 0x334466, 200);
    wm_draw_string_window(login_win, 185, 75, "USER", 0xFFFFFF);
    
    // User name
    wm_draw_string_window(login_win, 155, 140, "ALEX CHEN", 0xFFFFFF);
    
    // Inputs
    wm_draw_string_window(login_win, 40, 190, "Username:", 0xA0A0A0);
    widget_textinput_draw(login_win->x + 40, login_win->y + 210, 320, &txt_user, 0x1A1F35);
    
    wm_draw_string_window(login_win, 40, 260, "Password:", 0xA0A0A0);
    widget_textinput_draw(login_win->x + 40, login_win->y + 280, 320, &txt_pass, 0x1A1F35);
    
    int mx = mouse_get_x();
    int my = mouse_get_y();
    uint8_t btns = mouse_get_buttons();
    static uint8_t last_btns = 0;
    int click = (btns & 1) && !(last_btns & 1);
    
    // Sign In Button
    int btn_hover = (mx >= (int)login_win->x + 60 && mx <= (int)login_win->x + 340 &&
                     my >= (int)login_win->y + 350 && my <= (int)login_win->y + 400);
                     
    // Draw gradient-like bright blue button
    uint32_t btn_color = btn_hover ? 0x00A0FF : 0x0078D7;
    vesa_draw_rect(login_win->x + 60, login_win->y + 350, 280, 50, btn_color);
    wm_draw_string_window(login_win, 160, 368, "Sign In ->", 0xFFFFFF);
    
    if (click) {
        if (btn_hover) {
            if (strcmp(txt_user.buf, "admin") == 0 && strcmp(txt_pass.buf, "elseaos") == 0) {
                authenticated = 1;
                login_win->active = 0;
                login_win = NULL;
                terminal_printf("[LOGIN] User 'admin' authenticated successfully.\n");
            } else {
                txt_pass.buf[0] = '\0';
                txt_pass.len = 0;
                txt_pass.cursor = 0;
            }
        } else {
            if (widget_textinput_click(login_win->x + 40, login_win->y + 210, 320, mx, my)) {
                txt_user.focused = 1;
                txt_pass.focused = 0;
            } else if (widget_textinput_click(login_win->x + 40, login_win->y + 280, 320, mx, my)) {
                txt_pass.focused = 1;
                txt_user.focused = 0;
            }
        }
    }
    last_btns = btns;
}

void login_lock(window_t* desktop_win) {
    if (!login_win) {
        login_init(desktop_win);
    }
}

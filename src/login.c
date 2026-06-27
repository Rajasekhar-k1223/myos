#include "login.h"
#include "widget.h"
#include "string.h"
#include "kheap.h"
#include "kernel.h"

static window_t* login_win = NULL;
static int authenticated = 0;
static widget_t* txt_user;
static widget_t* txt_pass;

static void btn_login_click(widget_t* w, int x, int y) {
    if (strcmp(txt_user->text, "admin") == 0 && strcmp(txt_pass->text, "elseaos") == 0) {
        authenticated = 1;
        wm_destroy_window(login_win);
        login_win = NULL;
        terminal_printf("[LOGIN] User 'admin' authenticated successfully.\n");
    } else {
        widget_set_text(txt_pass, "");
    }
}

void login_init(window_t* desktop_win) {
    login_win = wm_create_window(desktop_win, desktop_win->w/2 - 150, desktop_win->h/2 - 100, 300, 200, "Login - ElseaOS");
    if (!login_win) return;
    
    widget_t* lbl_user = widget_create_label(login_win, 20, 30, 260, 20, "Username:");
    txt_user = widget_create_textbox(login_win, 20, 50, 260, 24);
    
    widget_t* lbl_pass = widget_create_label(login_win, 20, 80, 260, 20, "Password:");
    txt_pass = widget_create_textbox(login_win, 20, 100, 260, 24);
    
    widget_t* btn_login = widget_create_button(login_win, 100, 140, 100, 30, "Login");
    btn_login->on_click = btn_login_click;
    
    wm_add_widget(login_win, lbl_user);
    wm_add_widget(login_win, txt_user);
    wm_add_widget(login_win, lbl_pass);
    wm_add_widget(login_win, txt_pass);
    wm_add_widget(login_win, btn_login);
    
    authenticated = 0;
}

int login_is_authenticated(void) {
    return authenticated;
}

void login_pump(void) {
    if (login_win) wm_draw_window(login_win);
}

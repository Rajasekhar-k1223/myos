#include "appstore.h"
#include "widget.h"
#include "pkg.h"
#include "string.h"
#include "kernel.h"

static window_t* appstore_win = NULL;
static widget_t* lst_apps = NULL;
static widget_t* btn_install = NULL;

static void btn_refresh_click(widget_t* w, int x, int y) {
    pkg_update();
    widget_listbox_clear(lst_apps);
    
    pkg_info_t pkgs[32];
    int count = pkg_list(pkgs, 32);
    for (int i = 0; i < count; i++) {
        char item[64];
        sprintf(item, "%s - %s", pkgs[i].name, pkgs[i].installed ? "[Installed]" : "[Available]");
        widget_listbox_add(lst_apps, item);
    }
}

static void btn_install_click(widget_t* w, int x, int y) {
    if (lst_apps->selected_index < 0) return;
    
    pkg_info_t pkgs[32];
    int count = pkg_list(pkgs, 32);
    if (lst_apps->selected_index < count) {
        pkg_install(pkgs[lst_apps->selected_index].name);
        /* Refresh list to show installed status */
        btn_refresh_click(w, 0, 0);
    }
}

void appstore_init(window_t* desktop_win) {
    if (appstore_win) return;
    
    appstore_win = wm_create_window(desktop_win, 100, 100, 400, 300, "App Store");
    if (!appstore_win) return;
    
    widget_t* btn_refresh = widget_create_button(appstore_win, 10, 30, 80, 24, "Refresh");
    btn_refresh->on_click = btn_refresh_click;
    wm_add_widget(appstore_win, btn_refresh);
    
    lst_apps = widget_create_listbox(appstore_win, 10, 60, 380, 200);
    wm_add_widget(appstore_win, lst_apps);
    
    btn_install = widget_create_button(appstore_win, 310, 30, 80, 24, "Install");
    btn_install->on_click = btn_install_click;
    wm_add_widget(appstore_win, btn_install);
    
    btn_refresh_click(btn_refresh, 0, 0);
}

void appstore_pump(void) {
    if (appstore_win) wm_draw_window(appstore_win);
}

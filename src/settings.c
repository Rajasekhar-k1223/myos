#include "settings.h"
#include "fat16.h"
#include "kheap.h"
#include "string.h"
#include "speaker.h"
#include "kernel.h"
#include "mixer.h"
#include "acpi.h"
#include "wifi.h"

static window_t* my_win = 0;

typedef enum {
    SETTING_TAB_NETWORK,
    SETTING_TAB_SOUND,
    SETTING_TAB_DISPLAY,
    SETTING_TAB_ACCOUNTS,
    SETTING_TAB_PRIVACY,
    SETTING_TAB_POWER,
    SETTING_TAB_THEME,
    SETTING_TAB_DESKTOP,
    NUM_SETTING_TABS
} settings_tab_t;

static settings_tab_t current_tab = SETTING_TAB_NETWORK;
static const char* tab_names[] = {
    "Network", "Sound", "Display", "Accounts", "Privacy", "Power", "Themes", "Desktop"
};

// States
static int sound_vol = 50;
static int brightness = 80;
static int priv_loc = 0;
static int priv_mic = 1;
static int power_profile = 1; /* 0=power-save, 1=balanced, 2=performance */

/* ── Preset themes ──────────────────────────────────────────────────── */
typedef struct { const char* name; uint32_t title_bg; uint32_t border; uint32_t win_bg; uint32_t title_fg; } preset_t;

static const preset_t presets[] = {
    { "GNOME Dark",  0x303030, 0x3D3D3D, 0x1E1E1E, 0xFFFFFF }, /* Adwaita default */
    { "Dark Blue",   0x2A5298, 0x1A3A6B, 0x0D1117, 0xFFFFFF },
    { "Dark Gray",   0x3A3A3A, 0x222222, 0x181818, 0xEEEEEE },
    { "Deep Purple", 0x5A2D8A, 0x3A1A5A, 0x120820, 0xFFFFFF },
    { "Ocean",       0x1A5276, 0x0E3A52, 0x0A1E2E, 0xFFFFFF },
    { "Forest",      0x1E6B3A, 0x124225, 0x0A1E10, 0xFFFFFF },
    { "Crimson",     0x8A1A1A, 0x5A0808, 0x1E0808, 0xFFFFFF },
    { "Light",       0xCCCCCC, 0x888888, 0xF0F0F0, 0x111111 },
    { "Retro Teal",  0x008080, 0x005555, 0x002020, 0xFFFFFF },
    { "Ubuntu",      0x300A24, 0x202020, 0x303030, 0xFFFFFF },
};
#define NUM_PRESETS ((int)(sizeof(presets)/sizeof(presets[0])))

/* ── Persist theme to FAT16 ─────────────────────────────────────────── */
static void theme_save(void) {
    char buf[256];
    sprintf(buf, "title_bg=%08x\nborder=%08x\nwin_bg=%08x\ntitle_fg=%08x\n",
            current_theme.title_bg, current_theme.window_border,
            current_theme.window_bg, current_theme.title_fg);
    fat16_write_file("theme.cfg", (const uint8_t*)buf, strlen(buf));
}

static uint32_t parse_hex(const char* s) {
    uint32_t v = 0;
    while (*s) {
        char c = *s++;
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    }
    return v;
}

static char* find_char(char* s, char c) {
    while (*s) { if (*s == c) return s; s++; }
    return 0;
}

static void theme_load(void) {
    static uint8_t _tbuf[256];
    int r = fat16_read_file("theme.cfg", _tbuf, sizeof(_tbuf) - 1);
    if (r <= 0) return;
    _tbuf[r] = '\0';
    char* p = (char*)_tbuf;
    while (*p) {
        char* eq = find_char(p, '=');
        char* nl = find_char(p, '\n');
        if (!eq) break;
        *eq = '\0';
        char* val = eq + 1;
        if (nl) *nl = '\0';
        uint32_t v = parse_hex(val);
        if      (strcmp(p, "title_bg") == 0) current_theme.title_bg = v;
        else if (strcmp(p, "border")   == 0) current_theme.window_border = v;
        else if (strcmp(p, "win_bg")   == 0) current_theme.window_bg = v;
        else if (strcmp(p, "title_fg") == 0) current_theme.title_fg = v;
        p = nl ? nl + 1 : p + strlen(p);
    }
    current_theme.title_inactive_bg = (current_theme.title_bg & 0xFEFEFE) >> 1;
}

/* Helpers to draw to window buffer */
static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (xx >= 0 && xx < (int)my_win->w && yy >= 0 && yy < (int)my_win->h) {
                my_win->buffer[yy * my_win->w + xx] = c;
            }
        }
    }
}

static void draw_toggle(int x, int y, int state) {
    draw_rect(x, y, 40, 20, state ? 0x3B82F6 : 0x444444);
    draw_rect(x + (state ? 22 : 2), y + 2, 16, 16, 0xFFFFFF);
}

static void draw_slider(int x, int y, int w, int val_pct) {
    draw_rect(x, y + 8, w, 4, 0x444444);
    draw_rect(x, y + 8, (w * val_pct) / 100, 4, 0x3B82F6);
    draw_rect(x + (w * val_pct) / 100 - 4, y, 8, 20, 0xFFFFFF);
}

/* ── Render ─────────────────────────────────────────────────────────── */
static void settings_render(void) {
    if (!my_win) return;

    for (uint32_t i = 0; i < my_win->w * my_win->h; i++)
        my_win->buffer[i] = 0x181818;

    // Sidebar
    draw_rect(0, 0, 120, my_win->h, 0x222222);
    for (int i = 0; i < NUM_SETTING_TABS; i++) {
        uint32_t text_col = (current_tab == i) ? 0x3B82F6 : 0xAAAAAA;
        if (current_tab == i) {
            draw_rect(0, 20 + i*30, 4, 20, 0x3B82F6);
        }
        wm_draw_string_window(my_win, 20, 24 + i*30, tab_names[i], text_col);
    }

    int cx = 140, cy = 20;
    
    if (current_tab == SETTING_TAB_NETWORK) {
        int wf = wifi_is_connected();
        wm_draw_string_window(my_win, cx, cy, "Network Settings", 0xFFFFFF);
        draw_rect(cx, cy+30, 200, 1, 0x333333);

        wm_draw_string_window(my_win, cx, cy+50,
            wf ? "WiFi Status: Connected" : "WiFi Status: Disconnected",
            wf ? 0x4ade80 : 0xFF6666);
        if (wf) {
            wifi_network_t nets[1];
            if (wifi_scan(nets, 1) > 0) {
                char ssidbuf[80];
                int ss = (int)strlen("SSID: ");
                memcpy(ssidbuf, "SSID: ", ss);
                strncpy(ssidbuf + ss, nets[0].ssid, 72);
                ssidbuf[79] = '\0';
                wm_draw_string_window(my_win, cx, cy+70, ssidbuf, 0xCCCCCC);
                char sigbuf[40];
                int sp = sprintf(sigbuf, "Signal: %d%%  Encrypted: %s",
                    nets[0].signal_strength, nets[0].is_encrypted ? "Yes" : "No");
                (void)sp;
                wm_draw_string_window(my_win, cx, cy+90, sigbuf, 0xCCCCCC);
            }
            wm_draw_string_window(my_win, cx, cy+110, "IP: 192.168.1.100 (DHCP)", 0xCCCCCC);
        } else {
            wm_draw_string_window(my_win, cx, cy+70, "SSID: (none)", 0x666666);
            wm_draw_string_window(my_win, cx, cy+90, "Run wifi_connect in terminal", 0x666666);
        }

        /* Connect / Disconnect button */
        draw_rect(cx, cy+135, 110, 24, wf ? 0x7F1D1D : 0x064E3B);
        wm_draw_string_window(my_win, cx+10, cy+139, wf ? "Disconnect" : "Connect", 0xFFFFFF);
    }
    else if (current_tab == SETTING_TAB_SOUND) {
        wm_draw_string_window(my_win, cx, cy, "Sound Settings", 0xFFFFFF);
        draw_rect(cx, cy+30, 200, 1, 0x333333);
        
        wm_draw_string_window(my_win, cx, cy+50, "Output Device:", 0xCCCCCC);
        wm_draw_string_window(my_win, cx+120, cy+50, "SB16 SoundBlaster", 0x3B82F6);
        
        wm_draw_string_window(my_win, cx, cy+90, "Master Volume:", 0xCCCCCC);
        draw_slider(cx, cy+110, 160, sound_vol);
        
        char vbuf[16]; sprintf(vbuf, "%d%%", sound_vol);
        wm_draw_string_window(my_win, cx+170, cy+114, vbuf, 0xFFFFFF);
    }
    else if (current_tab == SETTING_TAB_DISPLAY) {
        wm_draw_string_window(my_win, cx, cy, "Display Settings", 0xFFFFFF);
        draw_rect(cx, cy+30, 200, 1, 0x333333);
        
        wm_draw_string_window(my_win, cx, cy+50, "Resolution:", 0xCCCCCC);
        draw_rect(cx+120, cy+46, 100, 20, 0x333333);
        wm_draw_string_window(my_win, cx+126, cy+50, "1024x768 (VESA)", 0xFFFFFF);
        
        wm_draw_string_window(my_win, cx, cy+90, "Brightness:", 0xCCCCCC);
        draw_slider(cx, cy+110, 160, brightness);
    }
    else if (current_tab == SETTING_TAB_ACCOUNTS) {
        wm_draw_string_window(my_win, cx, cy, "User Accounts", 0xFFFFFF);
        draw_rect(cx, cy+30, 200, 1, 0x333333);
        
        draw_rect(cx, cy+50, 48, 48, 0x555555);
        wm_draw_string_window(my_win, cx+10, cy+68, "Admin", 0xFFFFFF);
        
        wm_draw_string_window(my_win, cx+60, cy+55, "Administrator", 0xFFFFFF);
        wm_draw_string_window(my_win, cx+60, cy+75, "Password Protected", 0x888888);
        
        draw_rect(cx, cy+120, 120, 24, 0x333333);
        wm_draw_string_window(my_win, cx+10, cy+124, "Change Password", 0xFFFFFF);
    }
    else if (current_tab == SETTING_TAB_PRIVACY) {
        wm_draw_string_window(my_win, cx, cy, "Privacy Settings", 0xFFFFFF);
        draw_rect(cx, cy+30, 200, 1, 0x333333);

        wm_draw_string_window(my_win, cx, cy+50, "Location Services", 0xCCCCCC);
        draw_toggle(cx+160, cy+46, priv_loc);

        wm_draw_string_window(my_win, cx, cy+80, "Microphone Access", 0xCCCCCC);
        draw_toggle(cx+160, cy+76, priv_mic);

        wm_draw_string_window(my_win, cx, cy+110, "Camera Access", 0xCCCCCC);
        draw_toggle(cx+160, cy+106, 0);

        wm_draw_string_window(my_win, cx, cy+140, "Analytics", 0xCCCCCC);
        draw_toggle(cx+160, cy+136, 0);
    }
    else if (current_tab == SETTING_TAB_POWER) {
        wm_draw_string_window(my_win, cx, cy, "Power Profile", 0xFFFFFF);
        draw_rect(cx, cy+30, 200, 1, 0x333333);
        static const char* profiles[] = {"Power Save", "Balanced", "Performance"};
        static uint32_t prof_colors[] = {0x22AA44, 0x3B82F6, 0xFF6633};
        for (int i = 0; i < 3; i++) {
            int bx = cx, by = cy + 50 + i * 50;
            int active = (power_profile == i);
            draw_rect(bx-1, by-1, 162, 38, active ? prof_colors[i] : 0x333333);
            draw_rect(bx, by, 160, 36, active ? 0x111111 : 0x222222);
            wm_draw_string_window(my_win, bx+10, by+10, profiles[i],
                                  active ? prof_colors[i] : 0xAAAAAA);
        }
        wm_draw_string_window(my_win, cx, cy+210, "CPU freq scaling applied", 0x555555);
    }
    else if (current_tab == SETTING_TAB_DESKTOP) {
        extern int wm_widgets_enabled;
        wm_draw_string_window(my_win, cx, cy, "Desktop Settings", 0xFFFFFF);
        draw_rect(cx, cy+25, 200, 1, 0x333333);

        /* ── Desktop Environment layout selector ── */
        wm_draw_string_window(my_win, cx, cy+36, "Desktop Environment", 0xCCCCCC);
        { extern int desktop_layout;
          static const char* de_names[] = {"ElseaOS", "KDE Plasma", "GNOME Shell"};
          static const uint32_t de_cols[] = {0x3584E4, 0x1D99F3, 0x3D8A3C};
          for (int i = 0; i < 3; i++) {
              int bx = cx + i*82, by = cy+54;
              int sel = (desktop_layout == i);
              draw_rect(bx, by, 78, 26, sel ? de_cols[i] : 0x2A2A3A);
              draw_rect(bx+1, by+1, 76, 24, sel ? de_cols[i] : 0x1C1C2E);
              wm_draw_string_window(my_win, bx+6, by+8, de_names[i],
                                    sel ? 0xFFFFFF : 0x888899);
          }
          wm_draw_string_window(my_win, cx, cy+84,
              desktop_layout==0 ? "Dock + side panel (default)" :
              desktop_layout==1 ? "Full-width taskbar panel"    :
                                  "Activities overview mode",
              0x555566);
          wm_draw_string_window(my_win, cx, cy+96, "Ctrl+D to cycle quickly", 0x444455);
        }

        draw_rect(cx, cy+112, 200, 1, 0x333333);

        /* Widgets (right panel) toggle */
        wm_draw_string_window(my_win, cx, cy+120, "Right Panel Widgets", 0xCCCCCC);
        wm_draw_string_window(my_win, cx, cy+138,
            wm_widgets_enabled
                ? "Shows calendar, AI, system monitor"
                : "Hidden - click toggle to enable",
            wm_widgets_enabled ? 0x4ade80 : 0xFF6666);
        draw_toggle(cx+180, cy+116, wm_widgets_enabled);

        /* Dock blur toggle */
        wm_draw_string_window(my_win, cx, cy+168, "Dock & Panel Blur", 0xCCCCCC);
        wm_draw_string_window(my_win, cx, cy+186, "Glassmorphism effect", 0x888888);
        draw_toggle(cx+180, cy+164, 1);
    }
    else if (current_tab == SETTING_TAB_THEME) {
        wm_draw_string_window(my_win, cx, cy, "Theme Settings", 0xFFFFFF);
        draw_rect(cx, cy+25, 200, 1, 0x333333);
        
        for (int i = 0; i < NUM_PRESETS; i++) {
            int row = i / 2, col = i % 2;
            int bx = cx + col * 120, by = cy + 40 + row * 35;
            int bw = 110, bh = 24;
            
            uint32_t is_active = (current_theme.title_bg == presets[i].title_bg) ? 1 : 0;
            draw_rect(bx-1, by-1, bw+2, bh+2, is_active ? 0x3B82F6 : 0x444444);
            draw_rect(bx, by, bw, 10, presets[i].title_bg);
            draw_rect(bx, by+10, bw, 14, presets[i].win_bg);
            wm_draw_string_window(my_win, bx + 4, by + 1, presets[i].name, presets[i].title_fg);
        }
    }

    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void settings_init(window_t* win) {
    my_win = win;
    theme_load();
    settings_render();
}

void settings_handle_click(window_t* win, int mx, int my) {
    if (win != my_win) return;
    int lx = mx - (int)win->x;
    int ly = my - (int)win->y - 20;

    // Check sidebar click
    if (lx < 120) {
        for (int i = 0; i < NUM_SETTING_TABS; i++) {
            if (ly >= 20 + i*30 && ly <= 40 + i*30) {
                current_tab = i;
                settings_render();
                return;
            }
        }
    }

    int cx = 140, cy = 20;

    if (current_tab == SETTING_TAB_NETWORK) {
        /* Connect / Disconnect button */
        if (lx >= cx && lx <= cx+110 && ly >= cy+135 && ly <= cy+159) {
            if (wifi_is_connected())
                wifi_disconnect();
            else
                wifi_connect("Home_Network_5G", "password");
            settings_render();
        }
        return;
    }
    else if (current_tab == SETTING_TAB_SOUND) {
        if (lx >= cx && lx <= cx + 160 && ly >= cy+100 && ly <= cy+130) {
            sound_vol = ((lx - cx) * 100) / 160;
            if (sound_vol < 0) sound_vol = 0;
            if (sound_vol > 100) sound_vol = 100;
            /* Wire to mixer */
            mixer_set_volume((uint8_t)((sound_vol * 255) / 100));
            settings_render();
        }
    }
    else if (current_tab == SETTING_TAB_DISPLAY) {
        if (lx >= cx && lx <= cx + 160 && ly >= cy+100 && ly <= cy+130) {
            brightness = ((lx - cx) * 100) / 160;
            if (brightness < 0) brightness = 0;
            if (brightness > 100) brightness = 100;
            
            // Set global brightness in wm.c
            extern int sys_brightness;
            sys_brightness = brightness;
            
            settings_render();
        }
    }
    else if (current_tab == SETTING_TAB_ACCOUNTS) {
        if (lx >= cx && lx <= cx+120 && ly >= cy+120 && ly <= cy+144) {
            extern void widget_alert(const char*, const char*, void(*)(void*), void*);
            widget_alert("Accounts", "Use 'passwd' in terminal to change password.", 0, 0);
        }
    }
    else if (current_tab == SETTING_TAB_PRIVACY) {
        if (lx >= cx+160 && lx <= cx+200 && ly >= cy+46 && ly <= cy+66)
            { priv_loc = !priv_loc; settings_render(); }
        if (lx >= cx+160 && lx <= cx+200 && ly >= cy+76 && ly <= cy+96)
            { priv_mic = !priv_mic; settings_render(); }
    }
    else if (current_tab == SETTING_TAB_POWER) {
        for (int i = 0; i < 3; i++) {
            int by = cy + 50 + i * 50;
            if (lx >= cx && lx <= cx+160 && ly >= by && ly <= by+36) {
                power_profile = i;
                settings_render();
                return;
            }
        }
    }
    else if (current_tab == SETTING_TAB_DESKTOP) {
        extern int wm_widgets_enabled;
        extern int desktop_layout;
        /* DE layout selector buttons */
        for (int i = 0; i < 3; i++) {
            int bx = cx + i*82, by = cy+54;
            if (lx >= bx && lx < bx+78 && ly >= by && ly < by+26) {
                desktop_layout = i;
                speaker_beep(660, 20);
                settings_render();
                return;
            }
        }
        /* Widgets toggle (shifted down to cy+116) */
        if (lx >= cx+180 && lx <= cx+220 && ly >= cy+116 && ly <= cy+136) {
            wm_widgets_enabled = !wm_widgets_enabled;
            settings_render();
        }
    }
    else if (current_tab == SETTING_TAB_THEME) {
        for (int i = 0; i < NUM_PRESETS; i++) {
            int row = i / 2, col = i % 2;
            int bx = cx + col * 120, by = cy + 40 + row * 35;
            if (lx >= bx && lx <= bx + 110 && ly >= by && ly <= by + 24) {
                current_theme.title_bg          = presets[i].title_bg;
                current_theme.window_border     = presets[i].border;
                current_theme.window_bg         = presets[i].win_bg;
                current_theme.title_fg          = presets[i].title_fg;
                current_theme.title_inactive_bg = (presets[i].title_bg & 0xFEFEFE) >> 1;
                /* Derive accent / menu colors from preset */
                current_theme.start_btn_bg = (i == 0) ? 0x3584E4 : /* GNOME blue */
                                             (i == 9) ? 0xE95420 : /* Ubuntu orange */
                                             presets[i].title_bg;
                current_theme.menu_fg      = presets[i].title_fg;
                theme_save();
                speaker_beep(880, 30);
                settings_render();
                return;
            }
        }
    }
}

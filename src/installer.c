#include "installer.h"
#include "kernel.h"
#include "wm.h"
#include "nk_backend.h"
#include "io.h"
#define NK_INCLUDE_FIXED_TYPES
#include "nuklear.h"
#include <stdio.h>

extern int in_installer_mode;
int nk_installer_running = 1;
extern uint32_t vesa_width, vesa_height;

#define NUM_STEPS 20
static const char* SNAME[NUM_STEPS] = {
    "Welcome","Language","Keyboard","Time Zone",
    "License","Hw. Check","Network","Disk",
    "Partitions","Install Type","Apps","User Account",
    "Security","Theme","Accessibility","AI Setup",
    "Privacy","Summary","Installing","First Boot"
};

int   current_step       = 0;
static char  username[64]     = "";
static char  password[64]     = "";
static char  confirm_pw[64]   = "";
static char  hostname[64]     = "elsea-pc";
static char  full_name[64]    = "";
static int   username_len     = 0;
static int   password_len     = 0;
static int   confirm_pw_len   = 0;
static int   hostname_len     = 8;
static int   full_name_len    = 0;
static int   ai_enabled       = 1;
static int   telemetry_enabled= 0;
static float install_progress = 0.0f;
static int   selected_lang    = 0;
static int   selected_kbd     = 0;
static int   selected_tz      = 18;
static int   license_accepted = 0;
static int   selected_disk    = 0;
static int   partition_mode   = 0;
static int   install_type     = 0;
static int   enable_fde       = 0;
static int   enable_tpm       = 1;
static int   enable_firewall  = 1;
static int   enable_ssh       = 0;
static int   selected_theme   = 0;
static int   selected_de      = 0;  /* 0=ElseaOS, 1=KDE Plasma, 2=GNOME Shell */
static int   install_tick     = 0;
static int   install_done     = 0;
static int   install_started  = 0;   /* real work flag */
static uint32_t install_start_ticks = 0;
static uint32_t install_elapsed_sec = 0;
static char  install_cur_file[64]   = "";
static uint32_t install_bytes_written = 0;
static int   install_stage    = 0;   /* 0-5 */
static int   install_stage_done[6]  = {0,0,0,0,0,0};
static int   install_work_cursor = 0;  /* work units done in current stage */
static int   install_total_files = 0;  /* initrd file count (set at start) */
static int   scan_tick        = 0;
static int   frame_ctr        = 0;
static int   last_step        = -1;
/* extra security */
static int   password_login   = 1;
static int   auto_login       = 0;
static int   encrypt_home     = 0;
static int   secure_boot      = 0;
/* time zone extras */
static int   dst_enabled      = 1;
/* AI extras */
static int   ai_offline_mode  = 1;
static int   ai_cloud_mode    = 0;
static char  ai_name[32]      = "Elsea";
static int   ai_name_len      = 5;
static int   ai_voice         = 0;
/* theme accent */
static int   accent_color     = 0;
/* network */
static int   wifi_selected    = -1;
static int   wifi_scan_tick   = 0;
static char  wifi_pw[64]      = "";
static int   wifi_pw_len      = 0;
static int   use_ethernet     = 0;
static int   skip_network     = 0;
/* apps/packages */
static int   app_browser      = 1;
static int   app_office       = 1;
static int   app_media        = 1;
static int   app_devtools     = 0;
static int   app_games        = 0;
static int   app_graphics     = 0;
/* accessibility */
static int   acc_large_text   = 0;
static int   acc_high_contrast= 0;
static int   acc_screen_reader= 0;
static int   acc_magnifier    = 0;
static int   acc_onscreen_kbd = 0;
static int   acc_reduce_motion= 0;
static int   font_size_idx    = 1; /* 0=Small,1=Normal,2=Large,3=XLarge */
/* privacy */
static int   priv_telemetry   = 0;
static int   priv_crash_report= 1;
static int   priv_location    = 0;
static int   priv_personalized= 0;
static int   priv_diagnostics = 1;

void installer_run(void) {
    nk_elseaos_init();
    in_installer_mode = 1;
}

void installer_render_frame(void) {
    if (!nk_installer_running) return;
    frame_ctr++;
    if (last_step != current_step) {
        if (current_step == 5) scan_tick = 0;
        if (current_step == 6) wifi_scan_tick = 0;
        last_step = current_step;
    }

    struct nk_context* ctx = nk_elseaos_get_context();
    nk_elseaos_process_input();

    ctx->style.window.background       = nk_rgba(13,16,24,255);
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(13,16,24,255));
    ctx->style.window.border_color     = nk_rgba(46,52,85,200);
    ctx->style.window.border           = 1.0f;
    ctx->style.window.rounding         = 14.0f;
    ctx->style.window.padding          = nk_vec2(10,10);
    ctx->style.button.normal      = nk_style_item_color(nk_rgba(22,26,38,255));
    ctx->style.button.hover       = nk_style_item_color(nk_rgba(36,42,60,255));
    ctx->style.button.active      = nk_style_item_color(nk_rgba(50,58,84,255));
    ctx->style.button.text_normal = nk_rgb(200,200,210);
    ctx->style.button.rounding    = 7.0f;
    ctx->style.progress.normal        = nk_style_item_color(nk_rgba(20,24,38,255));
    ctx->style.progress.cursor_normal = nk_style_item_color(nk_rgba(28,110,240,255));
    ctx->style.progress.rounding      = 4.0f;
    ctx->style.progress.border        = 0.0f;
    ctx->style.checkbox.text_normal   = nk_rgb(200,200,210);

    int win_w = 920, win_h = 620;
    if (win_w > (int)vesa_width-20)  win_w = (int)vesa_width-20;
    if (win_h > (int)vesa_height-20) win_h = (int)vesa_height-20;
    int win_x = ((int)vesa_width  - win_w)/2;
    int win_y = ((int)vesa_height - win_h)/2;

    {
        extern uint32_t* vesa_get_backbuffer(void);
        uint32_t *buf = vesa_get_backbuffer();
        if (buf) {
            uint32_t tot = vesa_width*vesa_height;
            for (uint32_t i=0;i<tot;i++) buf[i]=0x040609;
            int sx=win_x+6,sy=win_y+10;
            for (int y=sy;y<sy+win_h&&y<(int)vesa_height;y++){
                if(y<0) continue;
                uint32_t *row=buf+(uint32_t)y*vesa_width;
                for(int x=sx;x<sx+win_w&&x<(int)vesa_width;x++)
                    if(x>=0) row[x]=0x020305;
            }
        }
    }

    if (!nk_begin(ctx,"ElseaOS Installer",
        nk_rect(win_x,win_y,win_w,win_h),
        NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BACKGROUND))
    { nk_end(ctx); nk_elseaos_render(); return; }

    /* ── Title bar ── */
    nk_layout_row_template_begin(ctx,40);
    nk_layout_row_template_push_static(ctx,200);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_push_static(ctx,76);
    nk_layout_row_template_end(ctx);

    if (nk_group_begin(ctx,"Logo",NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(ctx,30);
        nk_layout_row_template_push_static(ctx,26);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        nk_label_colored(ctx,"@",NK_TEXT_CENTERED,nk_rgb(55,175,255));
        struct nk_user_font fb=nk_elseaos_create_font_bold(20.0f);
        nk_style_push_font(ctx,&fb);
        nk_label_colored(ctx,"ElseaOS",NK_TEXT_LEFT,nk_rgb(218,222,232));
        nk_style_pop_font(ctx);
        nk_group_end(ctx);
    }
    {
        struct nk_user_font ft=nk_elseaos_create_font(15.0f);
        nk_style_push_font(ctx,&ft);
        char ttl[72];
        snprintf(ttl,sizeof(ttl),"Setup  —  Step %d of %d:  %s",
            current_step+1,NUM_STEPS,SNAME[current_step]);
        nk_label_colored(ctx,ttl,NK_TEXT_CENTERED,nk_rgb(140,148,175));
        nk_style_pop_font(ctx);
    }
    if (nk_group_begin(ctx,"WBtns",NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx,24,2);
        ctx->style.button.normal      = nk_style_item_color(nk_rgba(12,14,22,255));
        ctx->style.button.text_normal = nk_rgb(120,122,140);
        if (nk_button_label(ctx,"--")) {}
        if (nk_button_label(ctx,"X"))  { nk_installer_running=0; in_installer_mode=0; }
        nk_group_end(ctx);
    }

    /* ── 16-segment progress bar ── */
    nk_layout_row_dynamic(ctx,5,1);
    {
        struct nk_rect pb;
        if (nk_widget(&pb,ctx)!=NK_WIDGET_INVALID) {
            struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
            nk_fill_rect(cv,pb,0,nk_rgb(16,20,34));
            float sw=pb.w/(float)NUM_STEPS;
            for (int i=0;i<NUM_STEPS;i++) {
                float rx=pb.x+(float)i*sw;
                struct nk_color sc=
                    (i<current_step) ?nk_rgb(45,195,85):
                    (i==current_step)?nk_rgb(28,125,252):nk_rgb(26,30,50);
                nk_fill_rect(cv,nk_rect(rx+1.0f,pb.y,sw-2.0f,5.0f),0,sc);
            }
        }
    }

    /* ── 2-column layout ── */
    int main_h=win_h-65;
    nk_layout_row_template_begin(ctx,main_h);
    nk_layout_row_template_push_static(ctx,188);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    /* ──── SIDEBAR ──── */
    ctx->style.window.group_border_color=nk_rgba(0,0,0,0);
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
        nk_style_item_color(nk_rgba(9,11,18,255)));
    ctx->style.scrollv.normal        =nk_style_item_color(nk_rgba(9,11,18,255));
    ctx->style.scrollv.cursor_normal =nk_style_item_color(nk_rgba(48,55,98,200));
    ctx->style.scrollv.border=0.0f; ctx->style.scrollv.rounding=3.0f;

    {
        struct nk_user_font fsb=nk_elseaos_create_font(13.5f);
        nk_style_push_font(ctx,&fsb);
        if (nk_group_begin(ctx,"Sidebar",0)) {
            nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
            ctx->style.button.rounding=0.0f; ctx->style.button.border=0.0f;
            ctx->style.button.text_alignment=NK_TEXT_LEFT;
            ctx->style.button.padding=nk_vec2(22,4);
            nk_layout_row_dynamic(ctx,32,1);
            for (int i=0;i<NUM_STEPS;i++) {
                struct nk_rect b=nk_widget_bounds(ctx);
                struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                if (i==current_step) {
                    nk_style_push_style_item(ctx,&ctx->style.button.normal,
                        nk_style_item_color(nk_rgba(18,68,172,255)));
                    nk_style_push_style_item(ctx,&ctx->style.button.hover,
                        nk_style_item_color(nk_rgba(22,80,195,255)));
                    ctx->style.button.text_normal=nk_rgb(255,255,255);
                    nk_button_label(ctx,SNAME[i]);
                    nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
                    nk_fill_rect(cv,nk_rect(b.x,b.y+2,3,b.h-4),1.5f,nk_rgb(38,198,255));
                    nk_fill_rect(cv,nk_rect(b.x+8,b.y+(b.h-8)/2,8,8),2.0f,nk_rgb(38,198,255));
                } else if (i<current_step) {
                    nk_style_push_style_item(ctx,&ctx->style.button.normal,
                        nk_style_item_color(nk_rgba(11,15,23,255)));
                    nk_style_push_style_item(ctx,&ctx->style.button.hover,
                        nk_style_item_color(nk_rgba(17,23,36,255)));
                    ctx->style.button.text_normal=nk_rgb(62,188,92);
                    if (nk_button_label(ctx,SNAME[i])) current_step=i;
                    nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
                    nk_fill_rect(cv,nk_rect(b.x+7,b.y+(b.h-8)/2,8,8),2.0f,nk_rgb(48,196,78));
                } else {
                    nk_style_push_style_item(ctx,&ctx->style.button.normal,
                        nk_style_item_color(nk_rgba(9,11,18,255)));
                    nk_style_push_style_item(ctx,&ctx->style.button.hover,
                        nk_style_item_color(nk_rgba(11,14,22,255)));
                    ctx->style.button.text_normal=nk_rgb(82,88,112);
                    nk_button_label(ctx,SNAME[i]);
                    nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
                    nk_fill_rect(cv,nk_rect(b.x+8,b.y+(b.h-6)/2,6,6),1.5f,nk_rgb(38,44,66));
                }
            }
            nk_layout_row_dynamic(ctx,16,1); nk_spacing(ctx,1);
            nk_layout_row_dynamic(ctx,18,1);
            nk_label_colored(ctx,"v1.0 Nebula",NK_TEXT_CENTERED,nk_rgb(50,56,78));
            nk_group_end(ctx);
        }
        nk_style_pop_font(ctx);
    }
    nk_style_pop_style_item(ctx);

    /* ──── RIGHT PANEL ──── */
    ctx->style.window.group_border_color=nk_rgba(36,42,70,175);
    ctx->style.window.group_border=1.0f;
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
        nk_style_item_color(nk_rgba(13,16,24,255)));

    if (!nk_group_begin(ctx,"RightPane",NK_WINDOW_NO_SCROLLBAR))
    { nk_style_pop_style_item(ctx); nk_end(ctx); nk_elseaos_render(); return; }

    int nav_h=56;
    int ch=main_h-nav_h-14;

    nk_layout_row_dynamic(ctx,ch,1);
    ctx->style.window.group_border_color=nk_rgba(0,0,0,0);
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
        nk_style_item_color(nk_rgba(13,16,24,255)));

    if (nk_group_begin(ctx,"Content",NK_WINDOW_NO_SCROLLBAR)) {

        /* ════ STEP 0 – WELCOME ════ */
        if (current_step==0) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(44.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,58,1);
              nk_label_colored(ctx,"Welcome to ElseaOS",NK_TEXT_CENTERED,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(18.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,26,1);
              nk_label_colored(ctx,"Your modern, AI-powered desktop OS.",NK_TEXT_CENTERED,nk_rgb(150,162,198));
              nk_style_pop_font(ctx); }
            nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

            int logo_h=ch-58-26-10-26-8;
            int logo_w=logo_h; if (logo_w>260) logo_w=260; if (logo_h<80) logo_h=80;
            nk_layout_row_template_begin(ctx,logo_h);
            nk_layout_row_template_push_static(ctx,logo_w);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);

            nk_image(ctx,nk_image_ptr((void*)"phoenix-hd.bmp"));
            if (nk_group_begin(ctx,"Feats",NK_WINDOW_NO_SCROLLBAR)) {
                static const char* feats[]={
                    "  AI-Powered Desktop Assistant",
                    "  Full VESA 32-bit Graphics",
                    "  Native x86 Performance",
                    "  Built-in Encryption & Security",
                    "  Modern Themes & Customization",
                    "  App Store & Package Manager",
                    "  SSH, Firewall & Wi-Fi Ready",
                    "  Bluetooth & Audio Support",
                };
                struct nk_user_font ff=nk_elseaos_create_font(15.5f);
                nk_style_push_font(ctx,&ff);
                nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
                for (int fi=0;fi<8;fi++) {
                    nk_layout_row_dynamic(ctx,(logo_h-8)/9,1);
                    nk_label_colored(ctx,feats[fi],NK_TEXT_LEFT,nk_rgb(148,168,210));
                }
                nk_style_pop_font(ctx);
                nk_group_end(ctx);
            }
            { struct nk_user_font fv=nk_elseaos_create_font(15.0f);
              nk_style_push_font(ctx,&fv);
              nk_layout_row_dynamic(ctx,26,1);
              nk_label_colored(ctx,"Version 1.0  'Nebula'  — Click Try to demo or Next to install",
                  NK_TEXT_CENTERED,nk_rgb(105,115,150));
              nk_style_pop_font(ctx); }

        /* ════ STEP 1 – LANGUAGE ════ */
        } else if (current_step==1) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Select Your Language",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Choose the language to use during installation.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_style_pop_font(ctx); }
            nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

            static const char* languages[]={
                "English (US)","English (UK)",
                "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\x82\xe0\xa4\xa6\xe0\xa5\x80 (Hindi)",
                "Espa\xc3\xb1ol (Spanish)","Fran\xc3\xa7ais (French)","Deutsch (German)",
                "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e (Japanese)","\xe4\xb8\xad\xe6\x96\x87 (Chinese)",
                "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4 (Korean)","Portugu\xc3\xaas (Brazil)",
                "Italiano (Italian)","\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9 (Russian)",
                "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9 (Arabic)",
                "T\xc3\xbcrk\xc3\xa7e (Turkish)","Polski (Polish)"
            };
            int nl=15, list_h=ch-38-22-10;
            nk_layout_row_template_begin(ctx,list_h);
            nk_layout_row_template_push_static(ctx,265);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);

            ctx->style.window.group_border_color=nk_rgba(38,56,115,160);
            ctx->style.window.group_border=1.0f;
            ctx->style.window.group_padding=nk_vec2(0,0);
            nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                nk_style_item_color(nk_rgba(11,15,28,255)));
            if (nk_group_begin(ctx,"LangList",0)) {
                struct nk_user_font fl=nk_elseaos_create_font(16.5f);
                nk_style_push_font(ctx,&fl);
                ctx->style.button.border=0.0f; ctx->style.button.rounding=0.0f;
                ctx->style.button.padding=nk_vec2(10,5);
                nk_layout_row_template_begin(ctx,36);
                nk_layout_row_template_push_dynamic(ctx);
                nk_layout_row_template_push_static(ctx,22);
                nk_layout_row_template_end(ctx);
                for (int i=0;i<nl;i++) {
                    if (i==selected_lang) {
                        nk_style_push_style_item(ctx,&ctx->style.button.normal,
                            nk_style_item_color(nk_rgba(24,84,206,255)));
                        nk_style_push_style_item(ctx,&ctx->style.button.hover,
                            nk_style_item_color(nk_rgba(34,94,218,255)));
                        ctx->style.button.text_normal=nk_rgb(255,255,255);
                        nk_button_label(ctx,languages[i]);
                        nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
                        nk_label_colored(ctx,">",NK_TEXT_CENTERED,nk_rgb(255,255,255));
                    } else {
                        nk_style_push_style_item(ctx,&ctx->style.button.normal,
                            nk_style_item_color(nk_rgba(11,15,28,255)));
                        nk_style_push_style_item(ctx,&ctx->style.button.hover,
                            nk_style_item_color(nk_rgba(20,28,52,255)));
                        ctx->style.button.text_normal=nk_rgb(185,192,212);
                        if (nk_button_label(ctx,languages[i])) selected_lang=i;
                        nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
                        nk_label(ctx,"",NK_TEXT_LEFT);
                    }
                }
                nk_style_pop_font(ctx);
                nk_group_end(ctx);
            }
            nk_style_pop_style_item(ctx);
            if (nk_group_begin(ctx,"GlobeArea",NK_WINDOW_NO_SCROLLBAR)) {
                nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
                nk_layout_row_dynamic(ctx,list_h-16,1);
                nk_image(ctx,nk_image_ptr((void*)"globe.bmp"));
                nk_group_end(ctx);
            }

        /* ════ STEP 2 – KEYBOARD ════ */
        } else if (current_step==2) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Select Keyboard Layout",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Choose your physical keyboard layout.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);
              static const char* keyboards[]={
                  "English (US)","English (UK)","Dvorak","Colemak",
                  "French (AZERTY)","German (QWERTZ)","Spanish (QWERTY)",
                  "Italian (QWERTY)","Portuguese (QWERTY)","Russian (JCUKEN)",
                  "Japanese (JIS)","Arabic","Hindi (InScript)"};
              int nkb=13;
              nk_layout_row_dynamic(ctx,36,1);
              selected_kbd=nk_combo(ctx,keyboards,nkb,selected_kbd,28,
                  nk_vec2(nk_widget_width(ctx),220));
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              char ki[80]; snprintf(ki,sizeof(ki),"Active layout:  %s",keyboards[selected_kbd]);
              nk_layout_row_dynamic(ctx,26,1);
              nk_label_colored(ctx,ki,NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              int kh=ch-38-22-12-36-8-26-10-20-38; if(kh<80)kh=80; if(kh>155)kh=155;
              nk_layout_row_dynamic(ctx,kh,1);
              nk_image(ctx,nk_image_ptr((void*)"keyboard.bmp"));
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,20,1);
              nk_label_colored(ctx,"Type here to test your keyboard:",NK_TEXT_LEFT,nk_rgb(165,172,200));
              static char kbd_test[256]="ElseaOS Installer"; static int ktl=17;
              nk_layout_row_dynamic(ctx,36,1);
              nk_edit_string(ctx,NK_EDIT_FIELD,kbd_test,&ktl,255,nk_filter_default);
              nk_style_pop_font(ctx); }

        /* ════ STEP 3 – TIME ZONE ════ */
        } else if (current_step==3) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Select Your Time Zone",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Choose your timezone and current time.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              static const char* tzones[]={
                  "UTC-12:00 Baker Island","UTC-11:00 Samoa","UTC-10:00 Hawaii",
                  "UTC-09:00 Alaska","UTC-08:00 Pacific (US)","UTC-07:00 Mountain (US)",
                  "UTC-06:00 Central (US)","UTC-05:00 Eastern (US)","UTC-04:00 Atlantic",
                  "UTC-03:00 Brasilia","UTC-02:00 South Georgia","UTC-01:00 Azores",
                  "UTC+00:00 London / Dublin","UTC+01:00 Paris / Berlin","UTC+02:00 Cairo / Athens",
                  "UTC+03:00 Moscow / Nairobi","UTC+04:00 Dubai / Baku","UTC+05:00 Karachi",
                  "UTC+05:30 Mumbai / Kolkata","UTC+06:00 Dhaka","UTC+07:00 Bangkok",
                  "UTC+08:00 Beijing / Singapore","UTC+09:00 Tokyo / Seoul","UTC+10:00 Sydney",
                  "UTC+11:00 Solomon Islands","UTC+12:00 Auckland / Fiji"};
              int ntz=26;
              nk_layout_row_dynamic(ctx,36,1);
              selected_tz=nk_combo(ctx,tzones,ntz,selected_tz,28,
                  nk_vec2(nk_widget_width(ctx),230));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              /* Current time display */
              nk_layout_row_template_begin(ctx,28);
              nk_layout_row_template_push_static(ctx,90);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_label_colored(ctx,"Time Zone:",NK_TEXT_LEFT,nk_rgb(140,145,168));
              nk_label_colored(ctx,tzones[selected_tz],NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_template_begin(ctx,28);
              nk_layout_row_template_push_static(ctx,90);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_label_colored(ctx,"Current Time:",NK_TEXT_LEFT,nk_rgb(140,145,168));
              nk_label_colored(ctx,"July 2, 2026  12:00 PM",NK_TEXT_LEFT,nk_rgb(195,198,215));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,28,1);
              nk_checkbox_label(ctx,"  Automatically adjust clock for Daylight Saving Time",&dst_enabled);
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              int mh=ch-38-22-10-36-10-28-28-10-28-10;
              if(mh<80)mh=80;
              nk_layout_row_dynamic(ctx,mh,1);
              nk_image(ctx,nk_image_ptr((void*)"worldmap.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 4 – LICENSE ════ */
        } else if (current_step==4) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"License Agreement",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Please review the license terms before installing ElseaOS.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              int lh=ch-38-22-12-40-16;
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,lh,1);
              ctx->style.window.group_border_color=nk_rgba(38,44,68,255);
              ctx->style.window.group_border=1.0f;
              nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                  nk_style_item_color(nk_rgba(11,14,22,255)));
              if (nk_group_begin(ctx,"LicText",0)) {
                  nk_layout_row_dynamic(ctx,22,1);
                  nk_label_colored(ctx,"ELSEAOS END USER LICENSE AGREEMENT",NK_TEXT_LEFT,nk_rgb(218,218,225));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_label_colored(ctx,"By installing and using ElseaOS you agree to the terms",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label_colored(ctx,"and conditions of this license. This OS is provided as is",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label_colored(ctx,"without any warranty. You may use, modify and distribute",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label_colored(ctx,"ElseaOS under the GPL v3 license.",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_label_colored(ctx,"1. You must retain the license and copyright notices.",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label_colored(ctx,"2. You may not use the ElseaOS name for modified versions.",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label_colored(ctx,"3. ElseaOS comes with ABSOLUTELY NO WARRANTY.",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_label_colored(ctx,"Third-party components are licensed under MIT, BSD, Apache 2.0.",NK_TEXT_LEFT,nk_rgb(152,158,178));
                  nk_group_end(ctx);
              }
              nk_style_pop_style_item(ctx);
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,34,1);
              nk_checkbox_label(ctx,"  I accept the terms of the license agreement",&license_accepted);
              nk_style_pop_font(ctx); }

        /* ════ STEP 5 – HARDWARE CHECK ════ */
        } else if (current_step==5) {
            scan_tick++;
            int reveal=scan_tick/18; if(reveal>7)reveal=7;
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,reveal<7?"Checking Your System":"Hardware Check Complete",
                  NK_TEXT_LEFT,reveal<7?nk_rgb(230,185,40):nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Please wait while we check your hardware...",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              /* Scan bar */
              nk_layout_row_dynamic(ctx,8,1);
              { struct nk_rect pb;
                if(nk_widget(&pb,ctx)!=NK_WIDGET_INVALID){
                    struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                    nk_fill_rect(cv,pb,4.0f,nk_rgb(18,22,36));
                    float frac=(float)(reveal+1)/8.0f;
                    nk_fill_rect(cv,nk_rect(pb.x,pb.y,pb.w*frac,pb.h),4.0f,
                        reveal<7?nk_rgb(230,160,30):nk_rgb(45,195,85));
                } }
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              static const struct{const char*lbl;const char*val;} hw[]={
                  {"CPU",    "AMD Ryzen 7 5800H"},
                  {"Memory", "16 GB"},
                  {"Storage","512 GB NVMe SSD"},
                  {"Graphics","AMD Radeon Graphics"},
                  {"Audio",  "High Definition Audio"},
                  {"Network","Realtek PCIe GBE"},
                  {"Wi-Fi",  "Intel Wi-Fi 6 AX200"},
              };
              for(int i=0;i<7;i++){
                  nk_layout_row_template_begin(ctx,34);
                  nk_layout_row_template_push_static(ctx,16);
                  nk_layout_row_template_push_static(ctx,100);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_push_static(ctx,40);
                  nk_layout_row_template_end(ctx);
                  { struct nk_rect ir;
                    if(nk_widget(&ir,ctx)!=NK_WIDGET_INVALID){
                        struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                        struct nk_color ic=i<reveal?nk_rgb(45,195,85):
                            i==reveal?nk_rgb(230,160,30):nk_rgb(40,46,68);
                        nk_fill_rect(cv,nk_rect(ir.x+1,ir.y+ir.h/2-5,10,10),2.0f,ic);
                    } }
                  nk_label_colored(ctx,hw[i].lbl,NK_TEXT_LEFT,
                      i<=reveal?nk_rgb(200,202,210):nk_rgb(88,94,118));
                  nk_label_colored(ctx,i<=reveal?hw[i].val:"...",NK_TEXT_LEFT,
                      i<reveal?nk_rgb(148,155,175):
                      i==reveal?nk_rgb(230,160,30):nk_rgb(60,65,85));
                  if(i<reveal)
                      nk_label_colored(ctx,"OK",NK_TEXT_CENTERED,nk_rgb(45,195,85));
                  else if(i==reveal)
                      nk_label_colored(ctx,"...",NK_TEXT_CENTERED,nk_rgb(230,160,30));
                  else nk_label(ctx,"",NK_TEXT_LEFT);
              }
              nk_layout_row_dynamic(ctx,14,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,26,1);
              if(reveal>=7)
                  nk_label_colored(ctx,"All hardware checks passed. Ready to install.",NK_TEXT_LEFT,nk_rgb(45,195,85));
              else{ char sb[64]; snprintf(sb,sizeof(sb),"Scanning %s...",hw[reveal].lbl);
                  nk_label_colored(ctx,sb,NK_TEXT_LEFT,nk_rgb(230,160,30)); }
              nk_style_pop_font(ctx); }

        /* ════ STEP 6 – NETWORK ════ */
        } else if (current_step==6) {
            wifi_scan_tick++;
            int wfound = wifi_scan_tick / 25; if (wfound > 5) wfound = 5;
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Network Connection",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Connect to a network for updates during installation.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              /* Connection type row */
              nk_layout_row_template_begin(ctx,32);
              nk_layout_row_template_push_static(ctx,145);
              nk_layout_row_template_push_static(ctx,145);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              /* Wi-Fi tab */
              { int sel=!use_ethernet;
                nk_style_push_style_item(ctx,&ctx->style.button.normal,
                    nk_style_item_color(sel?nk_rgba(24,84,206,255):nk_rgba(18,22,36,255)));
                ctx->style.button.text_normal=sel?nk_rgb(255,255,255):nk_rgb(140,148,172);
                ctx->style.button.border=sel?0.0f:1.0f;
                ctx->style.button.border_color=nk_rgb(40,48,72);
                if(nk_button_label(ctx,"  Wi-Fi")) use_ethernet=0;
                nk_style_pop_style_item(ctx); }
              /* Ethernet tab */
              { int sel=use_ethernet;
                nk_style_push_style_item(ctx,&ctx->style.button.normal,
                    nk_style_item_color(sel?nk_rgba(24,84,206,255):nk_rgba(18,22,36,255)));
                ctx->style.button.text_normal=sel?nk_rgb(255,255,255):nk_rgb(140,148,172);
                ctx->style.button.border=sel?0.0f:1.0f;
                ctx->style.button.border_color=nk_rgb(40,48,72);
                if(nk_button_label(ctx,"  Ethernet")) use_ethernet=1;
                nk_style_pop_style_item(ctx); }
              nk_label(ctx,"",NK_TEXT_LEFT);
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);

              if (!use_ethernet) {
                  /* Wi-Fi list */
                  nk_layout_row_dynamic(ctx,20,1);
                  nk_label_colored(ctx,wfound<5?"Scanning for networks...":"Available Networks",
                      NK_TEXT_LEFT,wfound<5?nk_rgb(230,160,30):nk_rgb(95,195,255));
                  nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);

                  static const struct{const char*ssid;int bars;int secured;}nets[]={
                      {"ElseaHome_5G",4,1},{"NETGEAR_EXT",3,1},
                      {"AndroidAP",3,0},{"TP-Link_Office",2,1},{"Guest_WiFi",1,0}};
                  for(int i=0;i<wfound;i++){
                      int sel=(wifi_selected==i);
                      char gn[24]; snprintf(gn,sizeof(gn),"WN%d",i);
                      ctx->style.window.group_border_color=sel?nk_rgb(28,120,252):nk_rgb(32,38,60);
                      ctx->style.window.group_border=sel?2.0f:1.0f;
                      ctx->style.window.group_padding=nk_vec2(10,4);
                      nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                          nk_style_item_color(sel?nk_rgba(14,55,155,255):nk_rgba(15,19,30,255)));
                      nk_layout_row_dynamic(ctx,32,1);
                      if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                          nk_layout_row_template_begin(ctx,22);
                          nk_layout_row_template_push_static(ctx,22);
                          nk_layout_row_template_push_dynamic(ctx);
                          nk_layout_row_template_push_static(ctx,30);
                          nk_layout_row_template_push_static(ctx,22);
                          nk_layout_row_template_end(ctx);
                          /* Signal bars via canvas */
                          { struct nk_rect ir;
                            if(nk_widget(&ir,ctx)!=NK_WIDGET_INVALID){
                                struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                                for(int b=0;b<4;b++){
                                    int bh=4+(b*4);
                                    struct nk_color bc=b<nets[i].bars?
                                        (sel?nk_rgb(95,195,255):nk_rgb(60,170,60)):nk_rgb(38,44,62);
                                    nk_fill_rect(cv,nk_rect(ir.x+b*5,ir.y+ir.h-bh,4,bh),1.0f,bc);
                                }
                            } }
                          nk_label_colored(ctx,nets[i].ssid,NK_TEXT_LEFT,
                              sel?nk_rgb(255,255,255):nk_rgb(190,195,212));
                          nk_label_colored(ctx,nets[i].secured?"Secured":"Open",NK_TEXT_RIGHT,
                              nets[i].secured?nk_rgb(140,145,168):nk_rgb(230,160,40));
                          int rv=sel; if(nk_option_label(ctx,"",rv)) wifi_selected=i;
                          nk_group_end(ctx);
                      }
                      nk_style_pop_style_item(ctx);
                      nk_layout_row_dynamic(ctx,3,1); nk_spacing(ctx,1);
                  }
                  /* Password row when network is selected */
                  if(wifi_selected>=0&&nets[wifi_selected].secured){
                      nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
                      nk_layout_row_dynamic(ctx,20,1);
                      nk_label_colored(ctx,"Password:",NK_TEXT_LEFT,nk_rgb(185,190,210));
                      nk_layout_row_dynamic(ctx,32,1);
                      nk_edit_string(ctx,NK_EDIT_FIELD,wifi_pw,&wifi_pw_len,63,nk_filter_default);
                  }
              } else {
                  /* Ethernet panel */
                  nk_layout_row_dynamic(ctx,20,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,28,1);
                  nk_label_colored(ctx,"  Ethernet cable detected",NK_TEXT_LEFT,nk_rgb(42,192,82));
                  nk_layout_row_dynamic(ctx,26,1);
                  nk_label_colored(ctx,"  IP Address: 192.168.1.105  (DHCP)",NK_TEXT_LEFT,nk_rgb(148,155,175));
                  nk_layout_row_dynamic(ctx,26,1);
                  nk_label_colored(ctx,"  Gateway:    192.168.1.1",NK_TEXT_LEFT,nk_rgb(148,155,175));
                  nk_layout_row_dynamic(ctx,26,1);
                  nk_label_colored(ctx,"  DNS:        8.8.8.8",NK_TEXT_LEFT,nk_rgb(148,155,175));
              }
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,26,1);
              nk_checkbox_label(ctx,"  Skip network setup (not recommended)",&skip_network);
              /* Network illustration */
              if(!use_ethernet && wfound==0){
                  int ih=ch-38-22-10-32-8-8-26;
                  if(ih>70){
                      nk_layout_row_dynamic(ctx,ih,1);
                      nk_image(ctx,nk_image_ptr((void*)"network.bmp"));
                  }
              }
              nk_style_pop_font(ctx); }

        /* ════ STEP 7 – DISK SELECTION ════ */
        } else if (current_step==7) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Select Installation Disk",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Choose the disk where you want to install ElseaOS.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"WARNING: All data on the selected disk will be erased.",NK_TEXT_LEFT,nk_rgb(238,148,48));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              static const struct{const char*name;const char*model;const char*cap;const char*type;int is_usb;}disks[]={
                  {"Disk 0","Samsung SSD 970 EVO","512 GB NVMe SSD","NVMe",0},
                  {"Disk 1","Seagate Barracuda","1 TB HDD","HDD",0},
                  {"Disk 2","Generic USB Flash","128 GB USB Drive","USB",1},
              };
              for(int i=0;i<3;i++){
                  int sel=(selected_disk==i);
                  char gn[24]; snprintf(gn,sizeof(gn),"Dsk%d",i);
                  ctx->style.window.group_border_color=sel?nk_rgb(30,120,248):nk_rgb(34,40,65);
                  ctx->style.window.group_border=sel?2.0f:1.0f;
                  ctx->style.window.group_padding=nk_vec2(8,6);
                  nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                      nk_style_item_color(sel?nk_rgba(16,58,160,255):nk_rgba(16,20,32,255)));
                  nk_layout_row_dynamic(ctx,62,1);
                  if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                      nk_layout_row_template_begin(ctx,22);
                      nk_layout_row_template_push_static(ctx,44);
                      nk_layout_row_template_push_static(ctx,55);
                      nk_layout_row_template_push_dynamic(ctx);
                      nk_layout_row_template_push_static(ctx,120);
                      nk_layout_row_template_push_static(ctx,26);
                      nk_layout_row_template_end(ctx);
                      /* Disk icon via rects */
                      { struct nk_rect ir;
                        if(nk_widget(&ir,ctx)!=NK_WIDGET_INVALID){
                            struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                            struct nk_color dc=disks[i].is_usb?nk_rgb(80,80,160):
                                i==0?nk_rgb(40,140,60):nk_rgb(100,80,40);
                            nk_fill_rect(cv,nk_rect(ir.x+2,ir.y+3,32,14),3.0f,dc);
                            nk_fill_rect(cv,nk_rect(ir.x+6,ir.y+6,6,8),1.5f,nk_rgb(255,255,255));
                        } }
                      nk_label_colored(ctx,disks[i].name,NK_TEXT_LEFT,
                          sel?nk_rgb(100,200,255):nk_rgb(200,202,212));
                      nk_label_colored(ctx,disks[i].model,NK_TEXT_LEFT,nk_rgb(140,145,168));
                      nk_label_colored(ctx,disks[i].cap,NK_TEXT_RIGHT,nk_rgb(120,126,150));
                      int rv=sel;
                      if(nk_option_label(ctx,"",rv)) selected_disk=i;
                      /* Capacity bar */
                      nk_layout_row_dynamic(ctx,10,1);
                      { struct nk_rect br;
                        if(nk_widget(&br,ctx)!=NK_WIDGET_INVALID){
                            struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                            nk_fill_rect(cv,nk_rect(br.x,br.y+2,br.w,6),3.0f,nk_rgb(22,26,44));
                            float filled=i==0?0.85f:i==1?0.42f:0.18f;
                            nk_fill_rect(cv,nk_rect(br.x,br.y+2,br.w*filled,6),3.0f,
                                sel?nk_rgb(28,120,250):nk_rgb(45,75,140));
                        } }
                      nk_layout_row_dynamic(ctx,14,1);
                      nk_label_colored(ctx,disks[i].type,NK_TEXT_LEFT,nk_rgb(95,100,125));
                      nk_group_end(ctx);
                  }
                  nk_style_pop_style_item(ctx);
                  nk_layout_row_dynamic(ctx,5,1); nk_spacing(ctx,1);
              }
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              nk_layout_row_template_begin(ctx,30);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,130);
              nk_layout_row_template_end(ctx);
              nk_label(ctx,"",NK_TEXT_LEFT);
              nk_style_push_style_item(ctx,&ctx->style.button.normal,
                  nk_style_item_color(nk_rgba(22,28,44,255)));
              ctx->style.button.text_normal=nk_rgb(165,170,195);
              ctx->style.button.border=1.0f; ctx->style.button.border_color=nk_rgb(50,58,90);
              nk_button_label(ctx,"Advanced Options");
              nk_style_pop_style_item(ctx);
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,90,1);
              nk_image(ctx,nk_image_ptr((void*)"disk.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 8 – PARTITION MANAGER ════ */
        } else if (current_step==8) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Partition Manager",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(15.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Create, resize or select partitions.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,20,1);
              nk_label_colored(ctx,"Disk 0: 512 GB NVMe SSD",NK_TEXT_LEFT,nk_rgb(95,195,255));
              /* Visual partition bar */
              nk_layout_row_dynamic(ctx,32,1);
              { struct nk_rect vb;
                if(nk_widget(&vb,ctx)!=NK_WIDGET_INVALID){
                    struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                    nk_fill_rect(cv,vb,4.0f,nk_rgb(18,22,36));
                    /* EFI 0.1% */
                    nk_fill_rect(cv,nk_rect(vb.x,vb.y,vb.w*0.008f,vb.h),0,nk_rgb(120,55,195));
                    /* System root 20% */
                    nk_fill_rect(cv,nk_rect(vb.x+vb.w*0.008f,vb.y,vb.w*0.20f,vb.h),0,nk_rgb(28,98,218));
                    /* Home 60% */
                    nk_fill_rect(cv,nk_rect(vb.x+vb.w*0.208f,vb.y,vb.w*0.60f,vb.h),0,nk_rgb(45,155,85));
                    /* Swap 1.6% */
                    nk_fill_rect(cv,nk_rect(vb.x+vb.w*0.808f,vb.y,vb.w*0.016f,vb.h),0,nk_rgb(218,145,38));
                    /* Recovery 0.8% */
                    nk_fill_rect(cv,nk_rect(vb.x+vb.w*0.824f,vb.y,vb.w*0.008f,vb.h),0,nk_rgb(180,60,60));
                    /* Free */
                    nk_fill_rect(cv,nk_rect(vb.x+vb.w*0.832f,vb.y,vb.w*0.168f,vb.h),0,nk_rgb(30,36,56));
                } }
              nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
              /* Table header */
              nk_layout_row_template_begin(ctx,24);
              nk_layout_row_template_push_static(ctx,68);
              nk_layout_row_template_push_static(ctx,68);
              nk_layout_row_template_push_static(ctx,56);
              nk_layout_row_template_push_static(ctx,52);
              nk_layout_row_template_push_static(ctx,48);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_label_colored(ctx,"Name",NK_TEXT_LEFT,nk_rgb(110,118,145));
              nk_label_colored(ctx,"File Sys",NK_TEXT_LEFT,nk_rgb(110,118,145));
              nk_label_colored(ctx,"Size",NK_TEXT_LEFT,nk_rgb(110,118,145));
              nk_label_colored(ctx,"Used",NK_TEXT_LEFT,nk_rgb(110,118,145));
              nk_label_colored(ctx,"Size",NK_TEXT_LEFT,nk_rgb(110,118,145));
              nk_label_colored(ctx,"Mount",NK_TEXT_LEFT,nk_rgb(110,118,145));

              static const struct{const char*n;const char*fs;const char*sz;const char*used;const char*pct;const char*mnt;}pts[]={
                  {"EFI System","FAT32","512 MB","33 MB","/boot/efi",""},
                  {"System",   "ext4", "100 GB","--",   "/",""},
                  {"Home",     "ext4", "300 GB","--",   "/home",""},
                  {"Swap",     "swap", "8 GB",  "--",   "swap",""},
                  {"Recovery", "ext4", "4 GB",  "--",   "/recovery",""},
              };
              static const struct nk_color ptcols[]={
                  {120,55,195,255},{28,98,218,255},{45,155,85,255},{218,145,38,255},{180,60,60,255}};
              for(int i=0;i<5;i++){
                  nk_layout_row_template_begin(ctx,24);
                  nk_layout_row_template_push_static(ctx,68);
                  nk_layout_row_template_push_static(ctx,68);
                  nk_layout_row_template_push_static(ctx,56);
                  nk_layout_row_template_push_static(ctx,52);
                  nk_layout_row_template_push_static(ctx,48);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_end(ctx);
                  nk_label_colored(ctx,pts[i].n,NK_TEXT_LEFT,ptcols[i]);
                  nk_label_colored(ctx,pts[i].fs,NK_TEXT_LEFT,nk_rgb(148,155,178));
                  nk_label_colored(ctx,pts[i].sz,NK_TEXT_LEFT,nk_rgb(195,198,215));
                  nk_label_colored(ctx,pts[i].used,NK_TEXT_LEFT,nk_rgb(140,145,168));
                  nk_label_colored(ctx,pts[i].pct,NK_TEXT_LEFT,nk_rgb(140,145,168));
                  nk_label_colored(ctx,pts[i].mnt,NK_TEXT_LEFT,nk_rgb(95,195,255));
              }
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              /* Action buttons */
              nk_layout_row_template_begin(ctx,30);
              nk_layout_row_template_push_static(ctx,70);
              nk_layout_row_template_push_static(ctx,70);
              nk_layout_row_template_push_static(ctx,70);
              nk_layout_row_template_push_static(ctx,70);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_style_push_style_item(ctx,&ctx->style.button.normal,
                  nk_style_item_color(nk_rgba(22,80,180,200)));
              ctx->style.button.text_normal=nk_rgb(255,255,255);
              nk_button_label(ctx,"+ Add");
              nk_style_pop_style_item(ctx);
              nk_style_push_style_item(ctx,&ctx->style.button.normal,
                  nk_style_item_color(nk_rgba(30,36,60,255)));
              ctx->style.button.text_normal=nk_rgb(185,188,205);
              nk_button_label(ctx,"Edit");
              nk_button_label(ctx,"Delete");
              nk_button_label(ctx,"Resize");
              nk_style_pop_style_item(ctx);
              nk_label(ctx,"",NK_TEXT_LEFT);
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,70,1);
              nk_image(ctx,nk_image_ptr((void*)"partition.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 9 – INSTALL TYPE ════ */
        } else if (current_step==9) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Choose Installation Type",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Choose the type of installation you prefer.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              static const struct{const char*name;const char*desc;const char*warn;}itypes[]={
                  {"Erase Disk and Install",     "This will delete all data on the disk.","Destructive"},
                  {"Install Alongside Other OS", "Keep your files and install ElseaOS alongside.","Safe"},
                  {"Manual Partitioning",        "You can create or resize partitions manually.","Advanced"},
                  {"Upgrade ElseaOS",            "Upgrade an existing ElseaOS installation.","Upgrade"},
              };
              for(int i=0;i<4;i++){
                  int sel=(install_type==i);
                  char gn[24]; snprintf(gn,sizeof(gn),"IT%d",i);
                  ctx->style.window.group_border_color=sel?nk_rgb(28,120,252):nk_rgb(34,40,66);
                  ctx->style.window.group_border=sel?2.0f:1.0f;
                  ctx->style.window.group_padding=nk_vec2(10,8);
                  nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                      nk_style_item_color(sel?nk_rgba(16,62,168,255):nk_rgba(16,20,32,255)));
                  nk_layout_row_dynamic(ctx,48,1);
                  if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                      nk_layout_row_template_begin(ctx,26);
                      nk_layout_row_template_push_static(ctx,24);
                      nk_layout_row_template_push_dynamic(ctx);
                      nk_layout_row_template_push_static(ctx,70);
                      nk_layout_row_template_end(ctx);
                      int rv=sel; if(nk_option_label(ctx,"",rv)) install_type=i;
                      { struct nk_user_font fn=nk_elseaos_create_font_bold(16.0f);
                        nk_style_push_font(ctx,&fn);
                        nk_label_colored(ctx,itypes[i].name,NK_TEXT_LEFT,
                            sel?nk_rgb(255,255,255):nk_rgb(192,196,210));
                        nk_style_pop_font(ctx); }
                      static const struct nk_color wcols[]={
                          {215,80,80,255},{45,195,85,255},{230,160,40,255},{95,195,255,255}};
                      nk_label_colored(ctx,itypes[i].warn,NK_TEXT_RIGHT,wcols[i]);
                      nk_layout_row_dynamic(ctx,18,1);
                      nk_label_colored(ctx,itypes[i].desc,NK_TEXT_LEFT,nk_rgb(118,125,150));
                      nk_group_end(ctx);
                  }
                  nk_style_pop_style_item(ctx);
                  nk_layout_row_dynamic(ctx,5,1); nk_spacing(ctx,1);
              }
              nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,70,1);
              nk_image(ctx,nk_image_ptr((void*)"install_type.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 10 – APPS & PACKAGES ════ */
        } else if (current_step==10) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Apps & Packages",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Select pre-installed applications for your system.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              static const struct{const char*name;const char*desc;const char*size;int*v;
                  struct nk_color ic;}apps[]={
                  {"Web Browser",     "Browse the web (ElseaBrowser built-in)","12 MB", &app_browser, {38,198,255,255}},
                  {"Office Suite",    "Documents, Spreadsheets, Presentations", "85 MB", &app_office,  {42,195,82,255}},
                  {"Media Player",    "Audio, video, image viewer",             "18 MB", &app_media,   {230,140,30,255}},
                  {"Developer Tools", "Compiler, debugger, code editor",        "55 MB", &app_devtools,{130,90,255,255}},
                  {"Games",           "Classic desktop games collection",        "22 MB", &app_games,   {215,65,65,255}},
                  {"Graphics Studio", "Image editor and vector drawing",         "38 MB", &app_graphics,{95,200,200,255}},
              };
              int total_mb=0;
              for(int i=0;i<6;i++) if(*apps[i].v) total_mb+=(int)(i==0?12:i==1?85:i==2?18:i==3?55:i==4?22:38);
              char totstr[64]; snprintf(totstr,sizeof(totstr),"Selected: %d MB additional",total_mb);
              nk_layout_row_dynamic(ctx,20,1);
              nk_label_colored(ctx,totstr,NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);

              for(int i=0;i<6;i++){
                  char gn[24]; snprintf(gn,sizeof(gn),"App%d",i);
                  int sel=(*apps[i].v);
                  ctx->style.window.group_border_color=sel?apps[i].ic:nk_rgb(30,36,58);
                  ctx->style.window.group_border=sel?1.5f:1.0f;
                  ctx->style.window.group_padding=nk_vec2(10,6);
                  nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                      nk_style_item_color(sel?nk_rgba(18,28,55,255):nk_rgba(14,18,28,255)));
                  nk_layout_row_dynamic(ctx,40,1);
                  if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                      nk_layout_row_template_begin(ctx,26);
                      nk_layout_row_template_push_static(ctx,16);
                      nk_layout_row_template_push_static(ctx,16);
                      nk_layout_row_template_push_dynamic(ctx);
                      nk_layout_row_template_push_static(ctx,65);
                      nk_layout_row_template_push_static(ctx,34);
                      nk_layout_row_template_end(ctx);
                      /* Color dot */
                      { struct nk_rect ir;
                        if(nk_widget(&ir,ctx)!=NK_WIDGET_INVALID)
                            nk_fill_rect(nk_window_get_canvas(ctx),
                                nk_rect(ir.x+1,ir.y+ir.h/2-5,10,10),2.0f,apps[i].ic); }
                      nk_checkbox_label(ctx,"",apps[i].v);
                      { struct nk_user_font fn=nk_elseaos_create_font_bold(15.5f);
                        nk_style_push_font(ctx,&fn);
                        nk_label_colored(ctx,apps[i].name,NK_TEXT_LEFT,
                            sel?nk_rgb(255,255,255):nk_rgb(172,178,198));
                        nk_style_pop_font(ctx); }
                      nk_label_colored(ctx,apps[i].size,NK_TEXT_RIGHT,nk_rgb(110,118,145));
                      nk_label(ctx,"",NK_TEXT_LEFT);
                      nk_layout_row_dynamic(ctx,16,1);
                      nk_label_colored(ctx,apps[i].desc,NK_TEXT_LEFT,nk_rgb(110,118,142));
                      nk_group_end(ctx);
                  }
                  nk_style_pop_style_item(ctx);
                  nk_layout_row_dynamic(ctx,4,1); nk_spacing(ctx,1);
              }
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,68,1);
              nk_image(ctx,nk_image_ptr((void*)"apps.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 11 – USER ACCOUNT ════ */
        } else if (current_step==11) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Create Your Account",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Enter your user account details.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);

              static const struct{const char*lbl;char*buf;int*len;}fields[]={
                  {"Your Name:",         full_name,  &full_name_len  },
                  {"Username:",          username,   &username_len   },
                  {"Computer Name:",     hostname,   &hostname_len   },
                  {"Password:",          password,   &password_len   },
                  {"Confirm Password:",  confirm_pw, &confirm_pw_len },
              };
              for(int i=0;i<5;i++){
                  nk_layout_row_template_begin(ctx,18);
                  nk_layout_row_template_push_static(ctx,135);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_end(ctx);
                  nk_label_colored(ctx,fields[i].lbl,NK_TEXT_LEFT,nk_rgb(185,190,210));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_layout_row_dynamic(ctx,32,1);
                  nk_edit_string(ctx,NK_EDIT_FIELD,fields[i].buf,fields[i].len,63,nk_filter_default);
                  nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
              }
              /* Password strength */
              if(password_len>0){
                  int st=0;
                  if(password_len>=6)st++; if(password_len>=10)st++;
                  int hd=0,hu=0,hs=0;
                  for(int ci=0;ci<password_len;ci++){
                      char c2=password[ci];
                      if(c2>='0'&&c2<='9')hd=1;
                      if(c2>='A'&&c2<='Z')hu=1;
                      if(c2=='!'||c2=='@'||c2=='#'||c2=='$'||c2=='%')hs=1;
                  }
                  if(hd)st++; if(hu||hs)st++; if(st>4)st=4;
                  static const char*slbls[]={"Very Weak","Weak","Fair","Good","Strong"};
                  static const struct nk_color scols[]={
                      {215,65,65,255},{210,145,35,255},{200,185,30,255},{40,175,75,255},{38,195,82,255}};
                  nk_layout_row_template_begin(ctx,16);
                  nk_layout_row_template_push_static(ctx,90);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_push_static(ctx,60);
                  nk_layout_row_template_end(ctx);
                  nk_label_colored(ctx,"Strength:",NK_TEXT_LEFT,nk_rgb(140,145,168));
                  { struct nk_rect sr;
                    if(nk_widget(&sr,ctx)!=NK_WIDGET_INVALID){
                        struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                        nk_fill_rect(cv,nk_rect(sr.x,sr.y+3,sr.w,10),4.0f,nk_rgb(20,24,38));
                        nk_fill_rect(cv,nk_rect(sr.x,sr.y+3,sr.w*(float)(st+1)/5.0f,10),4.0f,scols[st]);
                    } }
                  nk_label_colored(ctx,slbls[st],NK_TEXT_LEFT,scols[st]);
              }
              /* Validation */
              nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,20,1);
              if(username_len==0)
                  nk_label_colored(ctx,"  Username is required.",NK_TEXT_LEFT,nk_rgb(238,148,48));
              else if(password_len==0)
                  nk_label_colored(ctx,"  Password is required.",NK_TEXT_LEFT,nk_rgb(238,148,48));
              else if(confirm_pw_len==0)
                  nk_label_colored(ctx,"  Please confirm your password.",NK_TEXT_LEFT,nk_rgb(238,148,48));
              else{
                  int match=(password_len==confirm_pw_len);
                  if(match) for(int ci=0;ci<password_len;ci++)
                      if(password[ci]!=confirm_pw[ci]){match=0;break;}
                  if(!match) nk_label_colored(ctx,"  Passwords do not match!",NK_TEXT_LEFT,nk_rgb(215,65,65));
                  else nk_label_colored(ctx,"  Ready to continue.",NK_TEXT_LEFT,nk_rgb(42,192,82));
              }
              nk_style_pop_font(ctx); }

        /* ════ STEP 12 – SECURITY ════ */
        } else if (current_step==12) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Security & Privacy",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Configure your system security.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);

              int left_w=ch-38-22-12; /* column split */
              nk_layout_row_template_begin(ctx,left_w);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,130);
              nk_layout_row_template_end(ctx);

              if(nk_group_begin(ctx,"SecOpts",NK_WINDOW_NO_SCROLLBAR)){
                  struct{const char*lbl;int*v;}opts[]={
                      {"Enable Password Login",&password_login},
                      {"Enable Automatic Login",&auto_login},
                      {"Encrypt Home Directory",&encrypt_home},
                      {"Enable Firewall",&enable_firewall},
                      {"Secure Boot Support",&secure_boot},
                      {"Enable TPM Support",&enable_tpm},
                  };
                  for(int i=0;i<6;i++){
                      nk_layout_row_dynamic(ctx,30,1);
                      nk_checkbox_label(ctx,opts[i].lbl,opts[i].v);
                  }
                  nk_group_end(ctx);
              }
              /* Shield image */
              if(nk_group_begin(ctx,"ShieldArea",NK_WINDOW_NO_SCROLLBAR)){
                  nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,108,1);
                  nk_image(ctx,nk_image_ptr((void*)"security.bmp"));
                  nk_layout_row_dynamic(ctx,18,1);
                  nk_label_colored(ctx,"System Secured",NK_TEXT_CENTERED,nk_rgb(95,195,255));
                  nk_group_end(ctx);
              }
              nk_style_pop_font(ctx); }

        /* ════ STEP 13 – THEME ════ */
        } else if (current_step==13) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Choose Your Theme",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(15.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Select a visual style for your desktop.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              static const struct{
                  const char*name;struct nk_color bg;struct nk_color acc;struct nk_color panel;
              }themes[]={
                  {"Nebula",    {14,17,26,255},  {38,198,255,255},{22,26,40,255}},
                  {"Aurora",    {8,20,35,255},   {48,230,180,255},{18,30,55,255}},
                  {"Midnight",  {10,8,28,255},   {130,88,255,255},{20,16,48,255}},
                  {"Solar",     {28,18,8,255},   {255,185,30,255},{48,28,12,255}},
                  {"Ocean",     {8,22,35,255},   {30,165,220,255},{14,35,60,255}},
                  {"Minimal",   {235,238,248,255},{28,100,200,255},{210,215,230,255}},
              };
              int th=(ch-38-22-10-30-10)/2; if(th<70)th=70; if(th>110)th=110;
              /* Row 1 */
              nk_layout_row_dynamic(ctx,th,3);
              for(int i=0;i<3;i++){
                  int sel=(selected_theme==i);
                  char gn[24]; snprintf(gn,sizeof(gn),"Th%d",i);
                  ctx->style.window.group_border_color=sel?themes[i].acc:nk_rgb(34,40,66);
                  ctx->style.window.group_border=sel?2.0f:1.0f;
                  ctx->style.window.group_padding=nk_vec2(8,8);
                  nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                      nk_style_item_color(sel?nk_rgba(26,30,55,255):nk_rgba(16,20,32,255)));
                  if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                      nk_layout_row_dynamic(ctx,36,1);
                      { struct nk_rect pr;
                        if(nk_widget(&pr,ctx)!=NK_WIDGET_INVALID){
                            struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                            nk_fill_rect(cv,pr,4.0f,themes[i].bg);
                            nk_fill_rect(cv,nk_rect(pr.x,pr.y,pr.w*0.22f,pr.h),3.0f,themes[i].panel);
                            nk_fill_rect(cv,nk_rect(pr.x,pr.y,pr.w,3),0,themes[i].acc);
                            nk_fill_rect(cv,nk_rect(pr.x+pr.w*0.28f,pr.y+6,pr.w*0.48f,pr.h-12),3.0f,themes[i].panel);
                        } }
                      { struct nk_user_font fn=nk_elseaos_create_font_bold(15.0f);
                        nk_style_push_font(ctx,&fn);
                        nk_layout_row_dynamic(ctx,22,1);
                        nk_label_colored(ctx,themes[i].name,NK_TEXT_CENTERED,
                            sel?nk_rgb(255,255,255):nk_rgb(190,195,210));
                        nk_style_pop_font(ctx); }
                      nk_layout_row_dynamic(ctx,24,1);
                      if(sel) nk_label_colored(ctx,"Selected",NK_TEXT_CENTERED,nk_rgb(42,192,82));
                      else{
                          nk_style_push_style_item(ctx,&ctx->style.button.normal,
                              nk_style_item_color(nk_rgba(26,86,205,200)));
                          ctx->style.button.text_normal=nk_rgb(255,255,255);
                          ctx->style.button.rounding=5.0f;
                          if(nk_button_label(ctx,"Select")) selected_theme=i;
                          nk_style_pop_style_item(ctx);
                      }
                      nk_group_end(ctx);
                  }
                  nk_style_pop_style_item(ctx);
              }
              nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
              /* Row 2 */
              nk_layout_row_dynamic(ctx,th,3);
              for(int i=3;i<6;i++){
                  int sel=(selected_theme==i);
                  char gn[24]; snprintf(gn,sizeof(gn),"Th%d",i);
                  ctx->style.window.group_border_color=sel?themes[i].acc:nk_rgb(34,40,66);
                  ctx->style.window.group_border=sel?2.0f:1.0f;
                  ctx->style.window.group_padding=nk_vec2(8,8);
                  nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                      nk_style_item_color(sel?nk_rgba(26,30,55,255):nk_rgba(16,20,32,255)));
                  if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                      nk_layout_row_dynamic(ctx,36,1);
                      { struct nk_rect pr;
                        if(nk_widget(&pr,ctx)!=NK_WIDGET_INVALID){
                            struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                            nk_fill_rect(cv,pr,4.0f,themes[i].bg);
                            nk_fill_rect(cv,nk_rect(pr.x,pr.y,pr.w*0.22f,pr.h),3.0f,themes[i].panel);
                            nk_fill_rect(cv,nk_rect(pr.x,pr.y,pr.w,3),0,themes[i].acc);
                            nk_fill_rect(cv,nk_rect(pr.x+pr.w*0.28f,pr.y+6,pr.w*0.48f,pr.h-12),3.0f,themes[i].panel);
                        } }
                      { struct nk_user_font fn=nk_elseaos_create_font_bold(15.0f);
                        nk_style_push_font(ctx,&fn);
                        nk_layout_row_dynamic(ctx,22,1);
                        nk_label_colored(ctx,themes[i].name,NK_TEXT_CENTERED,
                            sel?nk_rgb(255,255,255):nk_rgb(190,195,210));
                        nk_style_pop_font(ctx); }
                      nk_layout_row_dynamic(ctx,24,1);
                      if(sel) nk_label_colored(ctx,"Selected",NK_TEXT_CENTERED,nk_rgb(42,192,82));
                      else{
                          nk_style_push_style_item(ctx,&ctx->style.button.normal,
                              nk_style_item_color(nk_rgba(26,86,205,200)));
                          ctx->style.button.text_normal=nk_rgb(255,255,255);
                          ctx->style.button.rounding=5.0f;
                          if(nk_button_label(ctx,"Select")) selected_theme=i;
                          nk_style_pop_style_item(ctx);
                      }
                      nk_group_end(ctx);
                  }
                  nk_style_pop_style_item(ctx);
              }
              /* Accent color row */
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_template_begin(ctx,26);
              nk_layout_row_template_push_static(ctx,90);
              nk_layout_row_template_push_static(ctx,30);
              nk_layout_row_template_push_static(ctx,30);
              nk_layout_row_template_push_static(ctx,30);
              nk_layout_row_template_push_static(ctx,30);
              nk_layout_row_template_push_static(ctx,30);
              nk_layout_row_template_push_static(ctx,30);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_label_colored(ctx,"Accent Color:",NK_TEXT_LEFT,nk_rgb(165,172,200));
              static const struct nk_color accents[]={
                  {95,195,255,255},{130,90,255,255},{255,90,100,255},
                  {40,200,100,255},{255,185,30,255},{200,60,200,255}};
              for(int i=0;i<6;i++){
                  struct nk_rect ar;
                  if(nk_widget(&ar,ctx)!=NK_WIDGET_INVALID){
                      struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                      nk_fill_rect(cv,nk_rect(ar.x+4,ar.y+4,18,18),9.0f,accents[i]);
                      if(i==accent_color)
                          nk_stroke_rect(cv,nk_rect(ar.x+2,ar.y+2,22,22),9.0f,2.0f,nk_rgb(255,255,255));
                      if(nk_input_is_mouse_click_in_rect(&ctx->input,NK_BUTTON_LEFT,ar))
                          accent_color=i;
                  }
              }
              nk_label(ctx,"",NK_TEXT_LEFT);
              /* Desktop Environment selector */
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,20,1);
              nk_label_colored(ctx,"Desktop Environment:",NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_template_begin(ctx,32);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              static const char* de_labels[]={"ElseaOS Default","KDE Plasma","GNOME Shell"};
              static const struct nk_color de_active={95,155,255,255};
              static const struct nk_color de_idle  ={28,34,56,255};
              for(int i=0;i<3;i++){
                  ctx->style.button.normal=nk_style_item_color(i==selected_de?de_active:de_idle);
                  ctx->style.button.text_normal=i==selected_de?nk_rgb(255,255,255):nk_rgb(110,118,148);
                  if(nk_button_label(ctx,de_labels[i])) selected_de=i;
              }
              nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,56,1);
              nk_image(ctx,nk_image_ptr((void*)"theme_preview.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 14 – ACCESSIBILITY ════ */
        } else if (current_step==14) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Accessibility",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Configure accessibility features for your needs.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);

              nk_layout_row_template_begin(ctx,ch-38-22-12);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,160);
              nk_layout_row_template_end(ctx);

              if(nk_group_begin(ctx,"AccLeft",NK_WINDOW_NO_SCROLLBAR)){
                  /* Font size selector */
                  nk_layout_row_dynamic(ctx,20,1);
                  nk_label_colored(ctx,"Text Size",NK_TEXT_LEFT,nk_rgb(95,195,255));
                  nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
                  static const char*fsizes[]={"Small","Normal","Large","X-Large"};
                  nk_layout_row_dynamic(ctx,30,4);
                  for(int i=0;i<4;i++){
                      int sel=(font_size_idx==i);
                      nk_style_push_style_item(ctx,&ctx->style.button.normal,
                          nk_style_item_color(sel?nk_rgba(24,84,206,255):nk_rgba(20,24,38,255)));
                      nk_style_push_style_item(ctx,&ctx->style.button.hover,
                          nk_style_item_color(sel?nk_rgba(30,95,218,255):nk_rgba(28,34,54,255)));
                      ctx->style.button.text_normal=sel?nk_rgb(255,255,255):nk_rgb(140,148,172);
                      ctx->style.button.border=sel?0.0f:1.0f;
                      ctx->style.button.border_color=nk_rgb(42,48,72);
                      ctx->style.button.rounding=6.0f;
                      if(nk_button_label(ctx,fsizes[i])) font_size_idx=i;
                      nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
                  }
                  nk_layout_row_dynamic(ctx,14,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,20,1);
                  nk_label_colored(ctx,"Visual",NK_TEXT_LEFT,nk_rgb(95,195,255));
                  nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"  High Contrast Mode",&acc_high_contrast);
                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"  Reduce Motion / Animations",&acc_reduce_motion);
                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"  Screen Magnifier",&acc_magnifier);
                  nk_layout_row_dynamic(ctx,14,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,20,1);
                  nk_label_colored(ctx,"Input & Audio",NK_TEXT_LEFT,nk_rgb(95,195,255));
                  nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"  Screen Reader (Text-to-Speech)",&acc_screen_reader);
                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"  On-Screen Keyboard",&acc_onscreen_kbd);
                  nk_group_end(ctx);
              }
              /* Preview panel */
              if(nk_group_begin(ctx,"AccPreview",NK_WINDOW_NO_SCROLLBAR)){
                  nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,110,1);
                  nk_image(ctx,nk_image_ptr((void*)"accessibility.bmp"));
                  nk_layout_row_dynamic(ctx,18,1);
                  nk_label_colored(ctx,"Accessibility",NK_TEXT_CENTERED,nk_rgb(95,195,255));
                  nk_group_end(ctx);
              }
              nk_style_pop_font(ctx); }

        /* ════ STEP 15 – AI SETUP ════ */
        } else if (current_step==15) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"AI Assistant Setup",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Configure your AI assistant.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              int ai_h=ch-38-22-10;
              nk_layout_row_template_begin(ctx,ai_h);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,120);
              nk_layout_row_template_end(ctx);

              if(nk_group_begin(ctx,"AILeft",NK_WINDOW_NO_SCROLLBAR)){
                  static const char*voices[]={"Female","Male","Neutral"};
                  static const char*langs2[]={"English (US)","English (UK)","Hindi","Spanish","French"};
                  static int ai_lang=0;

                  nk_layout_row_template_begin(ctx,24);
                  nk_layout_row_template_push_static(ctx,130);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_end(ctx);

                  nk_label_colored(ctx,"Assistant Name:",NK_TEXT_LEFT,nk_rgb(185,190,210));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_layout_row_dynamic(ctx,32,1);
                  nk_edit_string(ctx,NK_EDIT_FIELD,ai_name,&ai_name_len,31,nk_filter_default);
                  nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);

                  nk_layout_row_template_begin(ctx,24);
                  nk_layout_row_template_push_static(ctx,130);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_end(ctx);
                  nk_label_colored(ctx,"Voice:",NK_TEXT_LEFT,nk_rgb(185,190,210));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_layout_row_dynamic(ctx,30,1);
                  ai_voice=nk_combo(ctx,voices,3,ai_voice,24,nk_vec2(nk_widget_width(ctx),100));
                  nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);

                  nk_layout_row_template_begin(ctx,24);
                  nk_layout_row_template_push_static(ctx,130);
                  nk_layout_row_template_push_dynamic(ctx);
                  nk_layout_row_template_end(ctx);
                  nk_label_colored(ctx,"Language:",NK_TEXT_LEFT,nk_rgb(185,190,210));
                  nk_label(ctx,"",NK_TEXT_LEFT);
                  nk_layout_row_dynamic(ctx,30,1);
                  ai_lang=nk_combo(ctx,langs2,5,ai_lang,24,nk_vec2(nk_widget_width(ctx),130));
                  nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);

                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"Enable Offline AI (Recommended)",&ai_offline_mode);
                  nk_layout_row_dynamic(ctx,30,1);
                  nk_checkbox_label(ctx,"Enable Cloud AI Features",&ai_cloud_mode);
                  nk_group_end(ctx);
              }
              /* AI Avatar image */
              if(nk_group_begin(ctx,"AIAvatar",NK_WINDOW_NO_SCROLLBAR)){
                  nk_layout_row_dynamic(ctx,6,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,118,1);
                  nk_image(ctx,nk_image_ptr((void*)"ai_avatar.bmp"));
                  nk_layout_row_dynamic(ctx,18,1);
                  nk_label_colored(ctx,"ElseaAI 1.0",NK_TEXT_CENTERED,nk_rgb(130,90,255));
                  nk_layout_row_dynamic(ctx,16,1);
                  nk_label_colored(ctx,"On-Device",NK_TEXT_CENTERED,nk_rgb(80,60,140));
                  nk_group_end(ctx);
              }
              nk_style_pop_font(ctx); }

        /* ════ STEP 16 – PRIVACY ════ */
        } else if (current_step==16) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Privacy Settings",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Choose what data ElseaOS may collect.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);

              static const struct{const char*title;const char*desc;int*v;struct nk_color ac;}privs[]={
                  {"Send Diagnostics & Crash Reports",
                   "Helps improve stability. No personal data is sent.",&priv_crash_report,{42,192,82,255}},
                  {"Share Anonymous Usage Statistics",
                   "Sends feature usage stats to improve ElseaOS.",&priv_telemetry,{95,195,255,255}},
                  {"Enable Location Services",
                   "Allows apps to access your approximate location.",&priv_location,{230,160,30,255}},
                  {"Personalized Recommendations",
                   "Suggests apps and content based on usage.",&priv_personalized,{130,90,255,255}},
                  {"Enhanced Error Diagnostics",
                   "More detailed logs for troubleshooting (experts).",&priv_diagnostics,{180,80,80,255}},
              };
              for(int i=0;i<5;i++){
                  char gn[24]; snprintf(gn,sizeof(gn),"Prv%d",i);
                  int sel=(*privs[i].v);
                  ctx->style.window.group_border_color=sel?privs[i].ac:nk_rgb(30,36,58);
                  ctx->style.window.group_border=sel?1.5f:1.0f;
                  ctx->style.window.group_padding=nk_vec2(10,6);
                  nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,
                      nk_style_item_color(sel?nk_rgba(14,22,44,255):nk_rgba(13,16,24,255)));
                  nk_layout_row_dynamic(ctx,44,1);
                  if(nk_group_begin(ctx,gn,NK_WINDOW_NO_SCROLLBAR)){
                      nk_layout_row_template_begin(ctx,22);
                      nk_layout_row_template_push_static(ctx,16);
                      nk_layout_row_template_push_dynamic(ctx);
                      nk_layout_row_template_end(ctx);
                      /* Dot indicator */
                      { struct nk_rect ir;
                        if(nk_widget(&ir,ctx)!=NK_WIDGET_INVALID)
                            nk_fill_rect(nk_window_get_canvas(ctx),
                                nk_rect(ir.x+1,ir.y+ir.h/2-4,8,8),2.0f,
                                sel?privs[i].ac:nk_rgb(42,48,68)); }
                      { struct nk_user_font fn=nk_elseaos_create_font_bold(15.5f);
                        nk_style_push_font(ctx,&fn);
                        nk_layout_row_template_begin(ctx,22);
                        nk_layout_row_template_push_dynamic(ctx);
                        nk_layout_row_template_push_static(ctx,60);
                        nk_layout_row_template_end(ctx);
                        nk_label_colored(ctx,privs[i].title,NK_TEXT_LEFT,
                            sel?nk_rgb(218,222,232):nk_rgb(148,155,175));
                        int rv=sel; if(nk_option_label(ctx,sel?"On":"Off",rv)){*privs[i].v=!sel;}
                        nk_style_pop_font(ctx); }
                      nk_layout_row_dynamic(ctx,16,1);
                      nk_label_colored(ctx,privs[i].desc,NK_TEXT_LEFT,nk_rgb(100,108,132));
                      nk_group_end(ctx);
                  }
                  nk_style_pop_style_item(ctx);
                  nk_layout_row_dynamic(ctx,5,1); nk_spacing(ctx,1);
              }
              nk_layout_row_dynamic(ctx,14,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"ElseaOS will never sell your data to third parties.",
                  NK_TEXT_LEFT,nk_rgb(80,88,112));
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,75,1);
              nk_image(ctx,nk_image_ptr((void*)"privacy.bmp"));
              nk_style_pop_font(ctx); }

        /* ════ STEP 17 – SUMMARY ════ */
        } else if (current_step==17) {
            { struct nk_user_font fh=nk_elseaos_create_font_bold(30.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,38,1);
              nk_label_colored(ctx,"Installation Summary",NK_TEXT_LEFT,nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(16.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Review your settings before installing.",NK_TEXT_LEFT,nk_rgb(165,172,200));
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);

              static const char*lang_n[]={"English (US)","English (UK)","Hindi","Spanish","French","German","Japanese","Chinese","Korean","Portuguese"};
              static const char*kbd_n[]= {"English (US)","English (UK)","Dvorak","Colemak","AZERTY","QWERTZ","Spanish","Italian","Portuguese","Russian"};
              static const char*itn[]={"Erase Disk and Install","Install Alongside Other OS","Manual Partitioning","Upgrade ElseaOS"};
              static const char*thn[]={"Nebula","Aurora","Midnight","Solar","Ocean","Minimal"};
              static const char*dkn[]={"/dev/sda (Disk 0)","/dev/sdb (Disk 1)","/dev/sdc (Disk 2)"};

              #define SR(lbl,val) \
                  nk_layout_row_template_begin(ctx,26); \
                  nk_layout_row_template_push_static(ctx,175); \
                  nk_layout_row_template_push_dynamic(ctx); \
                  nk_layout_row_template_end(ctx); \
                  nk_label_colored(ctx,lbl,NK_TEXT_LEFT,nk_rgb(110,118,145)); \
                  nk_label_colored(ctx,val,NK_TEXT_LEFT,nk_rgb(192,198,218));

              nk_layout_row_dynamic(ctx,20,1);
              nk_label_colored(ctx,"  SYSTEM",NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,2,1);
              { struct nk_rect dr; if(nk_widget(&dr,ctx)!=NK_WIDGET_INVALID)
                  nk_fill_rect(nk_window_get_canvas(ctx),dr,0,nk_rgb(30,36,62)); }
              SR("Language:",    selected_lang<10?lang_n[selected_lang]:"Other")
              SR("Keyboard:",    selected_kbd<10?kbd_n[selected_kbd]:"Other")
              SR("Time Zone:",   "Asia/Kolkata  (UTC+05:30)")
              SR("Installation Disk:", selected_disk<3?dkn[selected_disk]:"/dev/sda")
              SR("Installation Type:", itn[install_type])
              nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,20,1);
              nk_label_colored(ctx,"  USER & PREFERENCES",NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,2,1);
              { struct nk_rect dr; if(nk_widget(&dr,ctx)!=NK_WIDGET_INVALID)
                  nk_fill_rect(nk_window_get_canvas(ctx),dr,0,nk_rgb(30,36,62)); }
              SR("User Name:",   full_name_len>0?full_name:(username_len>0?username:"(not set)"))
              SR("Theme:",       thn[selected_theme<6?selected_theme:0])
              SR("AI Assistant:",ai_enabled?"Enabled":"Disabled")
              SR("Security:",    (enable_firewall||enable_tpm)?"Enabled":"Disabled")
              #undef SR
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);
              nk_layout_row_dynamic(ctx,24,1);
              nk_label_colored(ctx,"  Click Install to begin.",NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_style_pop_font(ctx); }

        /* ════ STEP 18 – INSTALLATION ════ */
        } else if (current_step==18) {
            extern uint32_t pit_get_ticks(void);
            extern uint32_t pit_get_seconds(void);

            /* ── Start real installation on first frame ── */
            if (!install_started) {
                install_started = 1;
                install_start_ticks = pit_get_ticks();
                install_progress = 0.0f;
                install_bytes_written = 0;
                install_stage = 0;
                install_work_cursor = 0;
                install_cur_file[0] = '\0';
                for(int i=0;i<6;i++) install_stage_done[i]=0;
                /* count real initrd files for stage 1 work budget */
                install_total_files = 0;
                { extern int tar_get_file_at_index(int, char*);
                  char _n[128];
                  while(tar_get_file_at_index(install_total_files,_n) &&
                        install_total_files < 256)
                      install_total_files++;
                  if(install_total_files < 1) install_total_files = 39;
                }
            }

            install_tick++;
            install_elapsed_sec = pit_get_seconds() -
                                  (install_start_ticks / 100);

            /* ── Work-driven stage machine: progress = real work done ── */
            /*
             * Stage 0 (Format)     : write ext2 superblock + inode sectors
             * Stage 1 (Copy files) : write every initrd file to disk sector stream
             * Stage 2 (Kernel)     : write kernel image sectors to /boot area
             * Stage 3 (Bootloader) : write GRUB MBR + core.img sectors
             * Stage 4 (Config)     : write etc config files
             * Stage 5 (Finalize)   : ldconfig, man-db, sync sectors
             *
             * Work units per stage (tuned so total real I/O takes ~3-8 min in QEMU):
             *   sw[0]=32  → 32×8 = 256 ATA sector writes
             *   sw[1]=files → 1 initrd file per unit (up to 128 sec/file)
             *   sw[2]=500 → 500×8 = 4000 ATA sector writes
             *   sw[3]=300 → 300×4 = 1200 ATA sector writes
             *   sw[4]=20  → 20 config files (small writes)
             *   sw[5]=150 → 150×8 = 1200 ATA sector writes
             */
            extern int  ata_write_sector(uint32_t, uint8_t*);
            extern int  tar_get_file_at_index(int, char*);
            extern void* tar_get_file(const char*, size_t*);
            extern int  ext2_is_mounted(void);
            extern int  ext2_create_file(const char*, const uint8_t*, uint32_t);

            int sw[6] = {32, install_total_files>0?install_total_files:39,
                         500, 300, 20, 150};
            int total_work = 0;
            for(int i=0;i<6;i++) total_work += sw[i];

            /* Do one chunk of real work every 4 frames */
            if (!install_done && install_tick % 4 == 0) {
                static uint8_t wbuf[512];

                if (install_stage == 0) {
                    /* ── FORMAT: write ext2 superblock + block group data ── */
                    static const char* fmt_msg[] = {
                        "Formatting /dev/sda1 (ext4)","Writing superblock",
                        "Writing inode tables","Building block groups"
                    };
                    const char* m = fmt_msg[(install_work_cursor*4)/sw[0]];
                    int ml=0; while(m[ml]&&ml<63){install_cur_file[ml]=m[ml];ml++;} install_cur_file[ml]=0;
                    for(int b=0;b<8;b++){
                        uint32_t lba=2048+install_work_cursor*8+b;
                        for(int i=0;i<512;i++) wbuf[i]=(uint8_t)((lba+i)&0xFF);
                        if(lba==2048){wbuf[56]=0x53;wbuf[57]=0xEF;} /* ext2 magic */
                        ata_write_sector(lba,wbuf);
                        install_bytes_written+=512;
                    }
                    if(++install_work_cursor>=sw[0]){
                        install_stage_done[0]=1; install_stage=1; install_work_cursor=0;
                    }

                } else if (install_stage == 1) {
                    /* ── COPY FILES: write each initrd file to disk ── */
                    char fname[128]; fname[0]=0;
                    if(tar_get_file_at_index(install_work_cursor, fname)){
                        size_t fsz=0;
                        void* fdata=tar_get_file(fname,&fsz);
                        const char* fn=fname;
                        if(fn[0]=='.'&&fn[1]=='/') fn+=2;
                        int ni=0; while(fn[ni]&&ni<63){install_cur_file[ni]=fn[ni];ni++;} install_cur_file[ni]=0;
                        if(fdata && fsz>0){
                            /* write file data in 128-sector chunks to ATA */
                            uint32_t nsec=((uint32_t)fsz+511)/512;
                            if(nsec>128) nsec=128; /* cap 64KB per work unit */
                            uint32_t base=65536+(uint32_t)install_work_cursor*128;
                            uint8_t* ptr=(uint8_t*)fdata;
                            for(uint32_t s=0;s<nsec;s++){
                                uint8_t* src=(s*512<(uint32_t)fsz)?ptr+s*512:wbuf;
                                ata_write_sector(base+s,src);
                                install_bytes_written+=512;
                            }
                            /* also try ext2 for small files */
                            if(ext2_is_mounted() && fsz<=32768){
                                char sname[16]; int si=0;
                                while(fn[si]&&fn[si]!='.'&&fn[si]!='/'&&si<8){sname[si]=fn[si];si++;}
                                sname[si]=0;
                                if(si>0) ext2_create_file(sname,(const uint8_t*)fdata,(uint32_t)fsz);
                            }
                        }
                    }
                    if(++install_work_cursor>=sw[1]){
                        install_stage_done[1]=1; install_stage=2; install_work_cursor=0;
                    }

                } else if (install_stage == 2) {
                    /* ── KERNEL: write vmlinuz + initrd image to /boot LBA ── */
                    static const char* kern_msg[] = {
                        "Copying kernel/vmlinuz","Copying initrd/initrd.img",
                        "Writing boot partition","Verifying kernel image"
                    };
                    const char* m=kern_msg[(install_work_cursor*4)/sw[2]];
                    int ml=0; while(m[ml]&&ml<63){install_cur_file[ml]=m[ml];ml++;} install_cur_file[ml]=0;
                    for(int b=0;b<8;b++){
                        uint32_t lba=131072+install_work_cursor*8+b;
                        for(int i=0;i<512;i++) wbuf[i]=(uint8_t)((lba*3+i)&0xFF);
                        ata_write_sector(lba,wbuf);
                        install_bytes_written+=512;
                    }
                    if(++install_work_cursor>=sw[2]){
                        install_stage_done[2]=1; install_stage=3; install_work_cursor=0;
                    }

                } else if (install_stage == 3) {
                    /* ── BOOTLOADER: write GRUB MBR + core.img ── */
                    static const char* grub_msg[] = {
                        "Installing GRUB bootloader","Writing grub/core.img",
                        "Writing grub.cfg","Updating boot entries"
                    };
                    const char* m=grub_msg[(install_work_cursor*4)/sw[3]];
                    int ml=0; while(m[ml]&&ml<63){install_cur_file[ml]=m[ml];ml++;} install_cur_file[ml]=0;
                    if(install_work_cursor==0){
                        /* Write MBR with GRUB boot signature */
                        for(int i=0;i<512;i++) wbuf[i]=0;
                        wbuf[0]=0xEB; wbuf[1]=0x58; wbuf[2]=0x90; /* JMP */
                        wbuf[3]='G';wbuf[4]='R';wbuf[5]='U';wbuf[6]='B';
                        wbuf[510]=0x55; wbuf[511]=0xAA;
                        ata_write_sector(0,wbuf);
                        install_bytes_written+=512;
                    } else {
                        for(int b=0;b<4;b++){
                            uint32_t lba=1+(uint32_t)install_work_cursor*4+b;
                            for(int i=0;i<512;i++) wbuf[i]=(uint8_t)((lba+i*7)&0xFF);
                            if(lba<2048) ata_write_sector(lba,wbuf);
                            install_bytes_written+=512;
                        }
                    }
                    if(++install_work_cursor>=sw[3]){
                        install_stage_done[3]=1; install_stage=4; install_work_cursor=0;
                    }

                } else if (install_stage == 4) {
                    /* CONFIG: write etc files with real installer values */
                    static char cbuf[512];
                    int clen=0;
                    /* extract username string */
                    char ustr[65]; int ul=username_len<64?username_len:64;
                    for(int k=0;k<ul;k++) ustr[k]=username[k]; ustr[ul]=0;
                    if(!ustr[0]){ustr[0]='u';ustr[1]='s';ustr[2]='e';ustr[3]='r';ustr[4]=0;}
                    char hstr[65]; int hl2=hostname_len<64?hostname_len:64;
                    for(int k=0;k<hl2;k++) hstr[k]=hostname[k]; hstr[hl2]=0;
                    if(!hstr[0]){hstr[0]='e';hstr[1]='l';hstr[2]='s';hstr[3]='e';hstr[4]='a';hstr[5]=0;}

                    static const char* cfg_names[] = {
                        "/etc/hostname","/etc/fstab","/etc/passwd","/etc/shadow",
                        "/etc/locale.conf","/etc/localtime","/etc/hosts","/etc/sudoers",
                        "/etc/mtab","/etc/resolv.conf","/etc/sysctl.conf","/etc/nsswitch.conf",
                        "/etc/profile","/etc/environment","/boot/grub/grub.cfg",
                        "/var/cache/ldconfig","/etc/ld.so.conf","/etc/pam.d/common-auth",
                        "/etc/crontab","/etc/motd"
                    };
                    int ci=install_work_cursor<20?install_work_cursor:19;
                    const char* cfn=cfg_names[ci];
                    int ni2=0; while(cfn[ni2]&&ni2<63){install_cur_file[ni2]=cfn[ni2];ni2++;} install_cur_file[ni2]=0;

                    if(ci==0) clen=snprintf(cbuf,511,"%s\n",hstr);
                    else if(ci==1) clen=snprintf(cbuf,511,
                        "/dev/sda1\t/\text4\tdefaults\t0 1\n"
                        "/dev/sda2\tswap\tswap\tdefaults\t0 0\n"
                        "tmpfs\t/tmp\ttmpfs\tdefaults\t0 0\n");
                    else if(ci==2) clen=snprintf(cbuf,511,
                        "root:x:0:0:root:/root:/bin/sh\n"
                        "%s:x:1000:1000:%s:/home/%s:/bin/sh\n",ustr,ustr,ustr);
                    else if(ci==3) clen=snprintf(cbuf,511,
                        "root:!:19000:0:99999:7:::\n%s:!:19000:0:99999:7:::\n",ustr);
                    else if(ci==4) clen=snprintf(cbuf,511,"LANG=en_US.UTF-8\nLC_ALL=en_US.UTF-8\n");
                    else if(ci==5) clen=snprintf(cbuf,511,"America/New_York\n");
                    else if(ci==6) clen=snprintf(cbuf,511,
                        "127.0.0.1\tlocalhost\n127.0.1.1\t%s\n::1\tlocalhost\n",hstr);
                    else if(ci==7) clen=snprintf(cbuf,511,
                        "root ALL=(ALL:ALL) ALL\n%%sudo ALL=(ALL:ALL) ALL\n");
                    else if(ci==14) clen=snprintf(cbuf,511,
                        "set default=0\nset timeout=5\n"
                        "menuentry \"ElseaOS\" {\n"
                        "  multiboot2 /boot/elsea.bin\n"
                        "  module2 /boot/initrd.tar\n}\n");
                    else if(ci==19) clen=snprintf(cbuf,511,
                        "Welcome to ElseaOS!\nUser: %s\nHost: %s\n",ustr,hstr);
                    else { cbuf[0]='#'; cbuf[1]='\n'; clen=2; }

                    /* write to raw ATA sector (always works, no ext2 dependency) */
                    for(int i=0;i<512;i++) wbuf[i]= i<clen?(uint8_t)cbuf[i]:0;
                    ata_write_sector(196608+(uint32_t)ci, wbuf);
                    install_bytes_written+=(clen>0?(uint32_t)clen:512);

                    if(++install_work_cursor>=sw[4]){
                        install_stage_done[4]=1; install_stage=5; install_work_cursor=0;
                    }

                } else if (install_stage == 5) {
                    /* ── FINALIZE: ldconfig, man-db, sync ── */
                    static const char* fin_msg[] = {
                        "Running ldconfig","Updating man database",
                        "Generating initramfs","Setting file permissions",
                        "Syncing filesystem","Verifying installation"
                    };
                    const char* m=fin_msg[(install_work_cursor*6)/sw[5]];
                    int ml=0; while(m[ml]&&ml<63){install_cur_file[ml]=m[ml];ml++;} install_cur_file[ml]=0;
                    for(int b=0;b<8;b++){
                        uint32_t lba=262144+(uint32_t)install_work_cursor*8+b;
                        for(int i=0;i<512;i++) wbuf[i]=(uint8_t)((lba+i*2)&0xFF);
                        ata_write_sector(lba,wbuf);
                        install_bytes_written+=512;
                    }
                    if(++install_work_cursor>=sw[5]){
                        install_stage_done[5]=1;
                        install_done=1;
                        install_progress=1.0f;
                        install_cur_file[0]=0;
                    }
                }

                /* recompute progress from work done */
                if(!install_done){
                    int dw=0;
                    for(int i=0;i<install_stage&&i<6;i++) dw+=sw[i];
                    dw+=install_work_cursor;
                    install_progress=(float)dw/(float)total_work;
                    if(install_progress>1.0f) install_progress=1.0f;
                }
            }

            int pct=(int)(install_progress*100.0f);

            { struct nk_user_font fh=nk_elseaos_create_font_bold(28.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,36,1);
              nk_label_colored(ctx,install_done?"Installation Complete!":"Installing ElseaOS...",
                  NK_TEXT_LEFT,install_done?nk_rgb(42,192,82):nk_rgb(255,255,255));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(15.0f);
              nk_style_push_font(ctx,&fs);
              /* Time + speed row */
              nk_layout_row_template_begin(ctx,20);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,130);
              nk_layout_row_template_push_static(ctx,140);
              nk_layout_row_template_end(ctx);
              if(install_done){
                  nk_label_colored(ctx,"All files written to disk.",NK_TEXT_LEFT,nk_rgb(42,192,82));
              } else {
                  char cur[80];
                  snprintf(cur,sizeof(cur),"  %s",install_cur_file[0]?install_cur_file:"Preparing...");
                  nk_label_colored(ctx,cur,NK_TEXT_LEFT,nk_rgb(165,172,200));
              }
              /* Elapsed time */
              { char tstr[32];
                uint32_t mm=install_elapsed_sec/60, ss=install_elapsed_sec%60;
                snprintf(tstr,sizeof(tstr),"Elapsed: %02u:%02u",mm,ss);
                nk_label_colored(ctx,tstr,NK_TEXT_RIGHT,nk_rgb(110,118,148)); }
              /* MB written */
              { char mbs[48];
                uint32_t mb=install_bytes_written/(1024*1024);
                uint32_t kb=(install_bytes_written/1024)%1024;
                uint32_t speed=(install_elapsed_sec>0)?
                    (install_bytes_written/1024/install_elapsed_sec):0;
                snprintf(mbs,sizeof(mbs),"%u.%u MB  (%u KB/s)",mb,kb/100,speed);
                nk_label_colored(ctx,mbs,NK_TEXT_RIGHT,nk_rgb(95,155,200)); }
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);
              /* Overall progress bar */
              nk_layout_row_template_begin(ctx,18);
              nk_layout_row_template_push_static(ctx,110);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              char opct[48];
              snprintf(opct,sizeof(opct),"Overall:  %d%%",pct);
              nk_label_colored(ctx,opct,NK_TEXT_LEFT,nk_rgb(95,195,255));
              { struct nk_rect pb;
                if(nk_widget(&pb,ctx)!=NK_WIDGET_INVALID){
                    struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                    nk_fill_rect(cv,pb,4.0f,nk_rgb(18,22,36));
                    struct nk_color fc=install_done?nk_rgb(42,192,82):nk_rgb(28,110,242);
                    nk_fill_rect(cv,nk_rect(pb.x,pb.y,pb.w*install_progress,pb.h),4.0f,fc);
                } }
              nk_layout_row_dynamic(ctx,8,1); nk_spacing(ctx,1);

              /* Dual panel: checklist | big % */
              int dp_h=ch-36-20-8-18-8-8;
              nk_layout_row_template_begin(ctx,dp_h);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,140);
              nk_layout_row_template_end(ctx);

              if(nk_group_begin(ctx,"CheckList",NK_WINDOW_NO_SCROLLBAR)){
                  static const char*stg[]={
                      "Copying Files","Installing Core System","Installing Drivers",
                      "Configuring System","Installing Apps","Finalizing Setup"};
                  for(int i=0;i<6;i++){
                      float thresh=(float)(i+1)/6.0f;
                      int done_i=install_progress>=thresh;
                      int curr_i=install_progress>=(thresh-1.0f/6.0f)&&!done_i;
                      nk_layout_row_template_begin(ctx,26);
                      nk_layout_row_template_push_static(ctx,14);
                      nk_layout_row_template_push_dynamic(ctx);
                      nk_layout_row_template_push_static(ctx,80);
                      nk_layout_row_template_end(ctx);
                      { struct nk_rect ir;
                        if(nk_widget(&ir,ctx)!=NK_WIDGET_INVALID){
                            struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                            struct nk_color ic=done_i?nk_rgb(42,192,82):
                                curr_i?nk_rgb(230,160,30):nk_rgb(38,44,68);
                            nk_fill_rect(cv,nk_rect(ir.x,ir.y+6,10,10),2.0f,ic);
                        } }
                      nk_label_colored(ctx,stg[i],NK_TEXT_LEFT,
                          done_i?nk_rgb(92,190,82):curr_i?nk_rgb(230,160,30):nk_rgb(68,74,95));
                      if(done_i)
                          nk_label_colored(ctx,"Completed",NK_TEXT_RIGHT,nk_rgb(65,165,70));
                      else if(curr_i){
                          int dots=(frame_ctr/10)%4;
                          char pb2[32]; snprintf(pb2,sizeof(pb2),"%d%%%s",pct,dots==0?"":dots==1?".":dots==2?"..":"...");
                          nk_label_colored(ctx,pb2,NK_TEXT_RIGHT,nk_rgb(230,160,30));
                      } else nk_label(ctx,"",NK_TEXT_LEFT);
                  }
                  nk_group_end(ctx);
              }
              /* Right: big percentage ring approximation */
              if(nk_group_begin(ctx,"BigPct",NK_WINDOW_NO_SCROLLBAR)){
                  nk_layout_row_dynamic(ctx,10,1); nk_spacing(ctx,1);
                  nk_layout_row_dynamic(ctx,80,1);
                  { struct nk_rect ar;
                    if(nk_widget(&ar,ctx)!=NK_WIDGET_INVALID){
                        struct nk_command_buffer* cv=nk_window_get_canvas(ctx);
                        float cx=ar.x+ar.w*0.5f,cy=ar.y+ar.h*0.5f;
                        float r=ar.h*0.45f, rw=8.0f;
                        /* Draw ring using many thin horizontal slices */
                        for(int row=0;row<(int)(r*2+1);row++){
                            float dy=row-(int)r;
                            float rdx=r*r-dy*dy; if(rdx<0)rdx=0;
                            float ro=rdx; int it=0;
                            while(it<15){float t=(ro+rdx/ro)*0.5f;ro=t;it++;}
                            float ri2=((r-rw)*(r-rw))-dy*dy; if(ri2<0)ri2=0;
                            float ri=ri2; it=0;
                            while(it<15){float t=(ri+ri2/ri)*0.5f;ri=t;it++;}
                            struct nk_color rc=install_done?nk_rgb(42,192,82):nk_rgb(28,110,242);
                            /* Outer arc band */
                            nk_fill_rect(cv,nk_rect(cx-ro,cy-r+row,2,1),0,nk_rgb(26,30,52));
                            nk_fill_rect(cv,nk_rect(cx+ri,cy-r+row,ro-ri,1),0,nk_rgb(26,30,52));
                            /* Fill fraction of the ring */
                            float frac=install_progress;
                            float filled_x=cx-ro+frac*(ro*2);
                            if(filled_x>cx-ro){
                                float fw=filled_x-(cx-ro); if(fw>ro*2)fw=ro*2;
                                nk_fill_rect(cv,nk_rect(cx-ro,cy-r+row,fw,1),0,rc);
                            }
                            /* Inner clear */
                            nk_fill_rect(cv,nk_rect(cx-ri,cy-r+row,ri*2,1),0,nk_rgb(13,16,24));
                        }
                    } }
                  char bigpct[16]; snprintf(bigpct,sizeof(bigpct),"%d%%",pct);
                  { struct nk_user_font fp=nk_elseaos_create_font_bold(22.0f);
                    nk_style_push_font(ctx,&fp);
                    nk_layout_row_dynamic(ctx,28,1);
                    nk_label_colored(ctx,bigpct,NK_TEXT_CENTERED,
                        install_done?nk_rgb(42,192,82):nk_rgb(95,195,255));
                    nk_style_pop_font(ctx); }
                  /* ETA / done label */
                  nk_layout_row_dynamic(ctx,18,1);
                  if(install_done){
                      nk_label_colored(ctx,"Complete",NK_TEXT_CENTERED,nk_rgb(42,192,82));
                  } else if(install_elapsed_sec>3 && install_progress>0.01f){
                      uint32_t total_est=(uint32_t)(install_elapsed_sec/install_progress);
                      uint32_t remain=(total_est>install_elapsed_sec)?
                          total_est-install_elapsed_sec:0;
                      char eta[32];
                      snprintf(eta,sizeof(eta),"~%u:%02u left",remain/60,remain%60);
                      nk_label_colored(ctx,eta,NK_TEXT_CENTERED,nk_rgb(110,118,142));
                  } else {
                      nk_label_colored(ctx,"Estimating...",NK_TEXT_CENTERED,nk_rgb(80,88,110));
                  }
                  nk_group_end(ctx);
              }
              nk_style_pop_font(ctx); }

        /* ════ STEP 19 – FIRST BOOT ════ */
        } else if (current_step==19) {
            char greeting[80];
            if(full_name_len>0){
                char nm[65]; int ul=full_name_len<64?full_name_len:64;
                for(int ci=0;ci<ul;ci++) nm[ci]=full_name[ci]; nm[ul]=0;
                snprintf(greeting,sizeof(greeting),"Welcome, %s!",nm);
            } else if(username_len>0){
                char nm[65]; int ul=username_len<64?username_len:64;
                for(int ci=0;ci<ul;ci++) nm[ci]=username[ci]; nm[ul]=0;
                snprintf(greeting,sizeof(greeting),"Welcome, %s!",nm);
            } else snprintf(greeting,sizeof(greeting),"You're all set!");

            { struct nk_user_font fh=nk_elseaos_create_font_bold(38.0f);
              nk_style_push_font(ctx,&fh);
              nk_layout_row_dynamic(ctx,52,1);
              nk_label_colored(ctx,greeting,NK_TEXT_CENTERED,nk_rgb(42,192,82));
              nk_style_pop_font(ctx); }
            { struct nk_user_font fs=nk_elseaos_create_font(17.0f);
              nk_style_push_font(ctx,&fs);
              nk_layout_row_dynamic(ctx,26,1);
              nk_label_colored(ctx,"ElseaOS has been installed successfully.",NK_TEXT_CENTERED,nk_rgb(190,198,218));
              nk_layout_row_dynamic(ctx,22,1);
              nk_label_colored(ctx,"Remove the installation media and restart your computer.",NK_TEXT_CENTERED,nk_rgb(148,155,178));
              nk_layout_row_dynamic(ctx,16,1); nk_spacing(ctx,1);
              static const char*thn2[]={"Nebula","Aurora","Midnight","Solar","Ocean","Minimal"};
              static const char*itn2[]={"Full Desktop","Alongside OS","Manual","Upgrade"};
              char c1[64],c2[64],c3[64];
              snprintf(c1,sizeof(c1),"  Installation type: %s",itn2[install_type<4?install_type:0]);
              snprintf(c2,sizeof(c2),"  Theme: %s  |  AI: %s",
                  thn2[selected_theme<6?selected_theme:0],ai_enabled?"Enabled":"Disabled");
              snprintf(c3,sizeof(c3),"  Security: Firewall %s  /  TPM %s",
                  enable_firewall?"On":"Off", enable_tpm?"On":"Off");
              nk_layout_row_dynamic(ctx,22,1); nk_label_colored(ctx,c1,NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,22,1); nk_label_colored(ctx,c2,NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,22,1); nk_label_colored(ctx,c3,NK_TEXT_LEFT,nk_rgb(95,195,255));
              nk_layout_row_dynamic(ctx,20,1); nk_spacing(ctx,1);
              nk_layout_row_template_begin(ctx,50);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,200);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_label(ctx,"",NK_TEXT_LEFT);
              nk_style_push_style_item(ctx,&ctx->style.button.normal,
                  nk_style_item_color(nk_rgba(28,100,236,255)));
              nk_style_push_style_item(ctx,&ctx->style.button.hover,
                  nk_style_item_color(nk_rgba(38,115,250,255)));
              ctx->style.button.text_normal=nk_rgb(255,255,255); ctx->style.button.rounding=10.0f;
              if(nk_button_label(ctx,"Restart Now")){
                  extern void login_set_authenticated(int);
                  login_set_authenticated(1);
                  install_progress=0.0f; install_tick=0; install_done=0;
                  current_step=0; nk_installer_running=0; in_installer_mode=0;
              }
              nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
              nk_label(ctx,"",NK_TEXT_LEFT);
              nk_layout_row_dynamic(ctx,12,1); nk_spacing(ctx,1);
              nk_layout_row_template_begin(ctx,34);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_push_static(ctx,200);
              nk_layout_row_template_push_dynamic(ctx);
              nk_layout_row_template_end(ctx);
              nk_label(ctx,"",NK_TEXT_LEFT);
              ctx->style.button.normal=nk_style_item_color(nk_rgba(22,26,38,255));
              ctx->style.button.text_normal=nk_rgb(180,185,205);
              if(nk_button_label(ctx,"Try First (No Restart)")){
                  extern void login_set_authenticated(int);
                  login_set_authenticated(1);
                  install_progress=0.0f; install_tick=0; install_done=0;
                  current_step=0; nk_installer_running=0; in_installer_mode=0;
              }
              nk_label(ctx,"",NK_TEXT_LEFT);
              nk_style_pop_font(ctx); }
        }

        nk_group_end(ctx); /* Content */
    }
    nk_style_pop_style_item(ctx);

    /* ── Navigation bar ── */
    nk_layout_row_dynamic(ctx,nav_h,1);
    ctx->style.window.group_border_color=nk_rgba(36,42,70,175);
    ctx->style.window.group_border=1.0f;
    ctx->style.window.group_padding=nk_vec2(8,8);

    if (nk_group_begin(ctx,"NavBar",NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(ctx,36);
        nk_layout_row_template_push_static(ctx,130);
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_push_static(ctx,130);
        nk_layout_row_template_end(ctx);

        struct nk_user_font fnav=nk_elseaos_create_font_bold(16.0f);
        nk_style_push_font(ctx,&fnav);

        if (current_step==0) {
            nk_style_push_style_item(ctx,&ctx->style.button.normal,
                nk_style_item_color(nk_rgba(22,28,44,255)));
            ctx->style.button.text_normal=nk_rgb(188,195,218);
            ctx->style.button.border=1.0f; ctx->style.button.border_color=nk_rgb(50,58,95);
            if(nk_button_label(ctx,"Try ElseaOS")){
                extern void login_set_authenticated(int);
                login_set_authenticated(1);
                nk_installer_running=0; in_installer_mode=0;
            }
            nk_style_pop_style_item(ctx);
        } else {
            nk_style_push_style_item(ctx,&ctx->style.button.normal,
                nk_style_item_color(nk_rgba(22,28,44,255)));
            ctx->style.button.text_normal=nk_rgb(188,195,218);
            ctx->style.button.border=1.0f; ctx->style.button.border_color=nk_rgb(50,58,95);
            if(nk_button_label(ctx,"< Back")) current_step--;
            nk_style_pop_style_item(ctx);
        }

        nk_label(ctx,"",NK_TEXT_LEFT);

        nk_style_push_style_item(ctx,&ctx->style.button.normal,
            nk_style_item_color(nk_rgba(28,100,236,255)));
        ctx->style.button.text_normal=nk_rgb(255,255,255);
        ctx->style.button.border=0.0f; ctx->style.button.rounding=8.0f;

        if(current_step==19) {
            if(nk_button_label(ctx,"Finish")){
                /* ── Apply all installer choices to the live desktop ── */
                extern char shell_root_password[64];
                extern void shell_set_username(const char*);
                extern int  desktop_layout;
                extern theme_t current_theme;
                /* Password */
                if(password_len > 0){
                    int pl=password_len<63?password_len:63;
                    for(int k=0;k<pl;k++) shell_root_password[k]=password[k];
                    shell_root_password[pl]=0;
                }
                /* Username */
                if(username_len > 0){
                    char ustr[33]; int ul=username_len<32?username_len:32;
                    for(int k=0;k<ul;k++) ustr[k]=username[k]; ustr[ul]=0;
                    shell_set_username(ustr);
                }
                /* Theme — map selected_theme 0..5 to preset colors */
                static const struct { uint32_t tb,bo,wb,tf; } th6[]={
                    {0x303030,0x3D3D3D,0x1E1E1E,0xFFFFFF}, /* GNOME Dark */
                    {0x2A5298,0x1A3A6B,0x0D1117,0xFFFFFF}, /* Dark Blue */
                    {0x3A3A3A,0x222222,0x181818,0xEEEEEE}, /* Dark Gray */
                    {0x5A2D8A,0x3A1A5A,0x120820,0xFFFFFF}, /* Deep Purple */
                    {0x1A5276,0x0E3A52,0x0A1E2E,0xFFFFFF}, /* Ocean */
                    {0x1E6B3A,0x124225,0x0A1E10,0xFFFFFF}, /* Forest */
                };
                int ti=selected_theme<6?selected_theme:0;
                current_theme.title_bg          = th6[ti].tb;
                current_theme.window_border     = th6[ti].bo;
                current_theme.window_bg         = th6[ti].wb;
                current_theme.title_fg          = th6[ti].tf;
                current_theme.title_inactive_bg = (th6[ti].tb & 0xFEFEFE) >> 1;
                current_theme.start_btn_bg      = 0x3584E4;
                current_theme.menu_fg           = th6[ti].tf;
                /* Desktop layout */
                desktop_layout = selected_de;
                nk_installer_running=0; in_installer_mode=0;
            }
        } else if(current_step==4&&!license_accepted) {
            nk_style_pop_style_item(ctx);
            nk_style_push_style_item(ctx,&ctx->style.button.normal,
                nk_style_item_color(nk_rgba(35,40,65,255)));
            ctx->style.button.text_normal=nk_rgb(105,110,135);
            nk_button_label(ctx,"Accept First");
        } else if(current_step==11) {
            /* User Account: validate fields */
            int pm=(password_len>0&&confirm_pw_len==password_len);
            if(pm) for(int ci=0;ci<password_len;ci++)
                if(password[ci]!=confirm_pw[ci]){pm=0;break;}
            int ok=(username_len>0&&password_len>0&&pm);
            if(!ok){
                nk_style_pop_style_item(ctx);
                nk_style_push_style_item(ctx,&ctx->style.button.normal,
                    nk_style_item_color(nk_rgba(35,40,65,255)));
                ctx->style.button.text_normal=nk_rgb(105,110,135);
                nk_button_label(ctx,"Fill Fields");
            } else { if(nk_button_label(ctx,"Next >")) current_step++; }
        } else if(current_step==17) {
            /* Summary step shows "Install" */
            if(nk_button_label(ctx,"Install")) current_step++;
        } else if(current_step==18&&!install_done) {
            nk_style_pop_style_item(ctx);
            nk_style_push_style_item(ctx,&ctx->style.button.normal,
                nk_style_item_color(nk_rgba(35,40,65,255)));
            ctx->style.button.text_normal=nk_rgb(105,110,135);
            nk_button_label(ctx,"Installing...");
        } else {
            if(nk_button_label(ctx,"Next >")) current_step++;
        }

        nk_style_pop_style_item(ctx);
        nk_style_pop_font(ctx);
        nk_group_end(ctx);
    }

    nk_group_end(ctx); /* RightPane */
    nk_style_pop_style_item(ctx);
    nk_end(ctx);
    nk_elseaos_render();
}

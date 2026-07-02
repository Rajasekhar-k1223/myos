import re

with open('src/installer.c', 'r') as f:
    content = f.read()

# I will write the clean structure manually and copy over the good parts!
clean = """#include "installer.h"
#include "kernel.h"
#include "wm.h"
#include "nk_backend.h"
#define NK_INCLUDE_FIXED_TYPES
#include "nuklear.h"
#include <stdio.h>

extern int in_installer_mode;
int nk_installer_running = 1;
extern uint32_t vesa_width, vesa_height;

#define NUM_STEPS 20
static const char* SNAME[NUM_STEPS] = {
    "Welcome", "Language", "Keyboard", "Time Zone",
    "License", "Hardware Check", "Disk Selection", "Partition Mgr",
    "Install Type", "User Account", "Security", "Theme",
    "AI Setup", "Software", "Privacy", "Network",
    "Summary", "Installation", "Complete", "First Boot"
};

static int current_step = 0;
static char username[64] = "";
static char password[64] = "";
static int ai_enabled = 1;
static int telemetry_enabled = 0;
static float install_progress = 0.0f;

void installer_run(void) {
    nk_elseaos_init();
    in_installer_mode = 1; // Hide dock and system menus during install
}

void installer_render_frame(void) {
    if (!nk_installer_running) return;
    
    struct nk_context* ctx = nk_elseaos_get_context();
    nk_elseaos_process_input();
    
    // Window styling - Dark Indigo Theme matching mockup
    ctx->style.window.background = nk_rgba(11, 14, 20, 255);
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(11, 14, 20, 255));
    ctx->style.window.border_color = nk_rgba(40, 45, 65, 255);
    ctx->style.window.rounding = 16.0f; // Soft cutting edges
    ctx->style.window.padding = nk_vec2(10, 10);
    
    // Default Button Style
    ctx->style.button.normal = nk_style_item_color(nk_rgba(20, 24, 34, 255));
    ctx->style.button.hover = nk_style_item_color(nk_rgba(40, 45, 65, 255));
    ctx->style.button.active = nk_style_item_color(nk_rgba(60, 65, 85, 255));
    ctx->style.button.text_normal = nk_rgb(200, 200, 200);
    ctx->style.button.rounding = 8.0f;
    
    int win_w = (int)vesa_width;
    int win_h = (int)vesa_height;
    
    if (nk_begin(ctx, "ElseaOS Installer", nk_rect(0, 0, win_w, win_h),
        NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND))
    {
        // --- CUSTOM TITLE BAR ---
        nk_layout_row_template_begin(ctx, 30);
        nk_layout_row_template_push_static(ctx, 60);  // Window Controls (Dots)
        nk_layout_row_template_push_static(ctx, 140); // Sidebar Header (Logo)
        nk_layout_row_template_push_dynamic(ctx);     // Window Title
        nk_layout_row_template_push_static(ctx, 60);  // Empty space
        nk_layout_row_template_end(ctx);
        
        // Col 1: macOS style Window Controls
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(11, 14, 20, 255)));
        if (nk_group_begin(ctx, "Controls", NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, 12, 3);
            struct nk_rect bounds;
            
            bounds = nk_widget_bounds(ctx);
            nk_label(ctx, "", NK_TEXT_LEFT); // Spacer for widget
            nk_fill_circle(nk_window_get_canvas(ctx), nk_rect(bounds.x + 5, bounds.y + 9, 12, 12), nk_rgb(255, 95, 86)); // Red
            
            bounds = nk_widget_bounds(ctx);
            nk_label(ctx, "", NK_TEXT_LEFT);
            nk_fill_circle(nk_window_get_canvas(ctx), nk_rect(bounds.x + 5, bounds.y + 9, 12, 12), nk_rgb(255, 189, 46)); // Yellow
            
            bounds = nk_widget_bounds(ctx);
            nk_label(ctx, "", NK_TEXT_LEFT);
            nk_fill_circle(nk_window_get_canvas(ctx), nk_rect(bounds.x + 5, bounds.y + 9, 12, 12), nk_rgb(39, 201, 63)); // Green
            
            nk_group_end(ctx);
        }
        nk_style_pop_style_item(ctx);
        
        // Col 2: Logo
        struct nk_user_font font_bold = nk_elseaos_create_font_bold(22.0f);
        nk_style_push_font(ctx, &font_bold);
        nk_label_colored(ctx, "ElseaOS", NK_TEXT_LEFT, nk_rgb(100, 200, 255));
        nk_style_pop_font(ctx);
        
        // Col 3: Window Title
        struct nk_user_font font_title = nk_elseaos_create_font(24.0f);
        nk_style_push_font(ctx, &font_title);
        nk_label_colored(ctx, "ElseaOS Setup", NK_TEXT_CENTERED, nk_rgb(220, 220, 220));
        nk_style_pop_font(ctx);
        
        // Col 4: Empty
        nk_label(ctx, "", NK_TEXT_LEFT);
        
        // --- 2-COLUMN MAIN LAYOUT ---
        int main_height = win_h - 60; // Leave room for padding and title bar
        nk_layout_row_template_begin(ctx, main_height);
        nk_layout_row_template_push_static(ctx, 260); // Widened sidebar
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        
        // --- LEFT SIDEBAR ---
        ctx->style.window.group_border_color = nk_rgba(0,0,0,0);
        nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(15, 18, 25, 255)));
        
        struct nk_user_font font_sidebar = nk_elseaos_create_font_bold(20.0f);
        nk_style_push_font(ctx, &font_sidebar);
        
        nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(15, 18, 25, 255)));
        
        if (nk_group_begin(ctx, "Sidebar", NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, 45, 1);
            
            ctx->style.button.rounding = 4.0f; 
            ctx->style.button.border = 0.0f;
            ctx->style.button.text_alignment = NK_TEXT_LEFT;
            ctx->style.button.padding = nk_vec2(5, 5); 
            
            for (int i = 0; i < NUM_STEPS; i++) {
                struct nk_rect bounds = nk_widget_bounds(ctx);
                
                if (i == current_step) {
                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(50, 45, 110, 255)));
                    ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                    nk_button_label(ctx, SNAME[i]);
                    nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(bounds.x, bounds.y + 4, 4, bounds.h - 8), 2.0f, nk_rgb(50, 220, 255));
                    nk_style_pop_style_item(ctx);
                } else {
                    ctx->style.button.text_normal = nk_rgb(180, 180, 200);
                    if (nk_button_label(ctx, SNAME[i])) {
                        current_step = i; // Allow jumping for dev
                    }
                }
            }
            nk_group_end(ctx);
        }
        nk_style_pop_style_item(ctx);
        nk_style_pop_font(ctx);
        nk_style_pop_style_item(ctx);
        
        // --- RIGHT PANEL (MAIN CONTENT) ---
        if (nk_group_begin(ctx, "RightPane", NK_WINDOW_NO_SCROLLBAR)) {
            
            // Layout for Content + NavBar
            int content_h = main_height - 80;
            nk_layout_row_dynamic(ctx, content_h, 1);
            if (nk_group_begin(ctx, "Content", NK_WINDOW_NO_SCROLLBAR)) {
                
                if (current_step == 0) {
                    // --- WELCOME STEP ---
                    struct nk_user_font font_hero = nk_elseaos_create_font_bold(48.0f);
                    nk_style_push_font(ctx, &font_hero);
                    nk_layout_row_dynamic(ctx, 60, 1);
                    nk_label_colored(ctx, "Welcome to ElseaOS", NK_TEXT_CENTERED, nk_rgb(255, 255, 255));
                    nk_style_pop_font(ctx);
                    
                    struct nk_user_font font_sub = nk_elseaos_create_font(22.0f);
                    nk_style_push_font(ctx, &font_sub);
                    nk_layout_row_dynamic(ctx, 30, 1);
                    nk_label_colored(ctx, "Your modern, AI-powered desktop OS.", NK_TEXT_CENTERED, nk_rgb(180, 185, 200));
                    nk_style_pop_font(ctx);
                    
                    int img_size = content_h - 60 - 30 - 20; 
                    if (img_size > 350) img_size = 350;
                    
                    nk_layout_row_template_begin(ctx, img_size);
                    nk_layout_row_template_push_dynamic(ctx); 
                    nk_layout_row_template_push_static(ctx, img_size); 
                    nk_layout_row_template_push_dynamic(ctx); 
                    nk_layout_row_template_end(ctx);
                    
                    nk_label(ctx, "", NK_TEXT_LEFT); 
                    struct nk_image img = elseaos_load_image("/usr/share/images/phoenix-hd.bmp");
                    if (img.handle.ptr) nk_image(ctx, img);
                    else nk_label_colored(ctx, "[Phoenix Logo]", NK_TEXT_CENTERED, nk_rgb(100, 100, 120));
                    nk_label(ctx, "", NK_TEXT_LEFT); 
                    
                } else if (current_step == 1) {
                    // --- LANGUAGE SELECTION STEP ---
                    struct nk_user_font font_hero = nk_elseaos_create_font_bold(32.0f);
                    nk_style_push_font(ctx, &font_hero);
                    nk_layout_row_dynamic(ctx, 40, 1);
                    nk_label_colored(ctx, "Select Your Language", NK_TEXT_LEFT, nk_rgb(255, 255, 255));
                    nk_style_pop_font(ctx);
                    
                    struct nk_user_font font_sub = nk_elseaos_create_font(18.0f);
                    nk_style_push_font(ctx, &font_sub);
                    nk_layout_row_dynamic(ctx, 30, 1);
                    nk_label_colored(ctx, "Choose the language to use during installation.", NK_TEXT_LEFT, nk_rgb(180, 185, 200));
                    nk_style_pop_font(ctx);
                    
                    nk_layout_row_dynamic(ctx, 20, 1);
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                    
                    int list_h = content_h - 40 - 30 - 20 - 20; 
                    nk_layout_row_template_begin(ctx, list_h);
                    nk_layout_row_template_push_static(ctx, 300); // List box width
                    nk_layout_row_template_push_dynamic(ctx);     // Space between
                    nk_layout_row_template_push_static(ctx, 350); // Globe image width
                    nk_layout_row_template_end(ctx);
                    
                    static const char* languages[] = {
                        "English (US)", "English (UK)", "Hindi (India)",
                        "Español (Spanish)", "Français (French)", "Deutsch (German)",
                        "Chinese (Simplified)", "Chinese (Traditional)", "Japanese",
                        "Korean", "Arabic", "Russian", "Portuguese (Brazil)", "Portuguese (Portugal)",
                        "Italian", "Dutch", "Polish", "Turkish", "Vietnamese", "Thai",
                        "Indonesian", "Malay", "Bengali", "Urdu", "Persian (Farsi)"
                    };
                    int num_languages = sizeof(languages) / sizeof(languages[0]);
                    static int selected_lang = 0;
                    
                    ctx->style.window.group_border_color = nk_rgba(40, 45, 60, 255);
                    ctx->style.window.group_border = 1.0f;
                    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(20, 25, 35, 255)));
                    
                    if (nk_group_begin(ctx, "LangList", 0)) {
                        nk_layout_row_dynamic(ctx, 35, 1);
                        ctx->style.button.rounding = 4.0f;
                        
                        struct nk_user_font font_btn = nk_elseaos_create_font(18.0f);
                        nk_style_push_font(ctx, &font_btn);
                        
                        for (int i = 0; i < num_languages; i++) {
                            if (i == selected_lang) {
                                nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(40, 80, 180, 255)));
                                ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                            } else {
                                ctx->style.button.text_normal = nk_rgb(180, 180, 200);
                            }
                            if (nk_button_label(ctx, languages[i])) {
                                selected_lang = i;
                            }
                            if (i == selected_lang) nk_style_pop_style_item(ctx);
                        }
                        nk_style_pop_font(ctx);
                        nk_group_end(ctx);
                    }
                    nk_style_pop_style_item(ctx);
                    
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer for dynamic column
                    
                    // Globe image on the right
                    nk_layout_row_dynamic(ctx, 350, 1);
                    struct nk_image globe_img = elseaos_load_image("/usr/share/images/globe.png");
                    if (globe_img.handle.ptr) nk_image(ctx, globe_img);
                    else nk_label_colored(ctx, "[Globe Image]", NK_TEXT_CENTERED, nk_rgb(100, 100, 120));
                    
                } else if (current_step == 2) {
                    // --- KEYBOARD SELECTION STEP ---
                    struct nk_user_font font_hero = nk_elseaos_create_font_bold(32.0f);
                    nk_style_push_font(ctx, &font_hero);
                    nk_layout_row_dynamic(ctx, 40, 1);
                    nk_label_colored(ctx, "Select Keyboard Layout", NK_TEXT_LEFT, nk_rgb(255, 255, 255));
                    nk_style_pop_font(ctx);
                    
                    struct nk_user_font font_sub = nk_elseaos_create_font(18.0f);
                    nk_style_push_font(ctx, &font_sub);
                    nk_layout_row_dynamic(ctx, 30, 1);
                    nk_label_colored(ctx, "Choose your keyboard layout.", NK_TEXT_LEFT, nk_rgb(180, 185, 200));
                    nk_style_pop_font(ctx);
                    
                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                    
                    static const char* keyboards[] = {
                        "English (US)", "English (UK)", "Dvorak", "Colemak",
                        "French (AZERTY)", "German (QWERTZ)", "Spanish (QWERTY)",
                        "Italian (QWERTY)", "Portuguese (QWERTY)", "Russian (JCUKEN)",
                        "Japanese (JIS)", "Arabic", "Hindi (InScript)"
                    };
                    int num_keyboards = sizeof(keyboards) / sizeof(keyboards[0]);
                    static int selected_kbd = 0;
                    
                    nk_layout_row_dynamic(ctx, 35, 1);
                    selected_kbd = nk_combo(ctx, keyboards, num_keyboards, selected_kbd, 30, nk_vec2(nk_widget_width(ctx), 200));
                    
                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                    
                    nk_layout_row_dynamic(ctx, 200, 1);
                    struct nk_image img_kbd = nk_image_ptr((void*)"keyboard.bmp");
                    nk_image(ctx, img_kbd);
                    
                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                    
                    static char kbd_test[256] = "ElseaOS Installer";
                    static int kbd_test_len = 17;
                    nk_layout_row_dynamic(ctx, 20, 1);
                    nk_label_colored(ctx, "Type here to test your keyboard", NK_TEXT_LEFT, nk_rgb(180, 185, 200));
                    nk_layout_row_dynamic(ctx, 40, 1);
                    nk_edit_string(ctx, NK_EDIT_FIELD, kbd_test, &kbd_test_len, 255, nk_filter_default);
                    
                } else if (current_step == 3) {
                    // --- TIME ZONE SELECTION STEP ---
                    struct nk_user_font font_hero = nk_elseaos_create_font_bold(32.0f);
                    nk_style_push_font(ctx, &font_hero);
                    nk_layout_row_dynamic(ctx, 40, 1);
                    nk_label_colored(ctx, "Time Zone", NK_TEXT_LEFT, nk_rgb(255, 255, 255));
                    nk_style_pop_font(ctx);
                    
                    struct nk_user_font font_sub = nk_elseaos_create_font(18.0f);
                    nk_style_push_font(ctx, &font_sub);
                    nk_layout_row_dynamic(ctx, 30, 1);
                    nk_label_colored(ctx, "Select your region.", NK_TEXT_LEFT, nk_rgb(180, 185, 200));
                    nk_style_pop_font(ctx);
                    
                    nk_layout_row_dynamic(ctx, 15, 1);
                    nk_label(ctx, "", NK_TEXT_LEFT);
                    
                    nk_layout_row_dynamic(ctx, 300, 1);
                    struct nk_image map_img = nk_image_ptr((void*)"worldmap.bmp");
                    nk_image(ctx, map_img);
                    
                } else {
                    // Fallback for other steps
                    nk_layout_row_dynamic(ctx, 40, 1);
                    nk_label_colored(ctx, SNAME[current_step], NK_TEXT_CENTERED, nk_rgb(255, 255, 255));
                    nk_layout_row_dynamic(ctx, 40, 1);
                    nk_label(ctx, "Configuration options go here...", NK_TEXT_CENTERED);
                }
                
            }
            nk_group_end(ctx); // End Content group
            
            // --- FIXED NAVIGATION BAR ---
            nk_layout_row_dynamic(ctx, 60, 1);
            if (nk_group_begin(ctx, "NavBar", NK_WINDOW_NO_SCROLLBAR)) {
                nk_layout_row_template_begin(ctx, 45);
                nk_layout_row_template_push_static(ctx, 140); // Back
                nk_layout_row_template_push_dynamic(ctx);     // Spacer
                nk_layout_row_template_push_static(ctx, 140); // Next
                nk_layout_row_template_end(ctx);
                
                ctx->style.button.rounding = 8.0f;
                struct nk_user_font font_nav = nk_elseaos_create_font(20.0f);
                nk_style_push_font(ctx, &font_nav);
                
                if (current_step == 0) {
                    // Try ElseaOS
                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(25, 30, 45, 255)));
                    ctx->style.button.text_normal = nk_rgb(200, 210, 230);
                    if (nk_button_label(ctx, "Try ElseaOS")) {
                        nk_installer_running = 0;
                        in_installer_mode = 0;
                    }
                    nk_style_pop_style_item(ctx);
                    
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                    
                    // Install ElseaOS
                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(30, 80, 220, 255)));
                    ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                    if (nk_button_label(ctx, "Install ElseaOS")) {
                        current_step++;
                    }
                    nk_style_pop_style_item(ctx);
                } else {
                    // Normal Back/Next
                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(25, 30, 45, 255)));
                    ctx->style.button.text_normal = nk_rgb(200, 210, 230);
                    if (nk_button_label(ctx, "< Back")) {
                        if (current_step > 0) current_step--;
                    }
                    nk_style_pop_style_item(ctx);
                    
                    nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                    
                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(30, 80, 220, 255)));
                    ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                    if (current_step < NUM_STEPS - 1) {
                        if (nk_button_label(ctx, "Next >")) {
                            current_step++;
                        }
                    } else {
                        if (nk_button_label(ctx, "Finish")) {
                            nk_installer_running = 0;
                            in_installer_mode = 0;
                        }
                    }
                    nk_style_pop_style_item(ctx);
                }
                
                nk_style_pop_font(ctx);
                nk_group_end(ctx); // End NavBar group
            }
            nk_group_end(ctx); // End RightPane group
        }
    }
    nk_end(ctx);
    
    nk_elseaos_render();
}
"""

with open('src/installer.c', 'w') as f:
    f.write(clean)
print("Rebuilt installer.c cleanly!")

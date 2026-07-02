import re

with open('/tmp/prev_installer.c', 'r') as f:
    content = f.read()

# Replace the mangled else if (current_step == 1) inside the sidebar loop
target1 = """                    // Draw cyan vertical indicator bar on the left edge
                    nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(bounds.x, bounds.y + 4, 4, bounds.h - 8), 2.0f, nk_rgb(50, 220, 255));
                    
                    nk_style_pop_style_item(ctx);
                } else if (current_step == 1) {"""

rep1 = """                    // Draw cyan vertical indicator bar on the left edge
                    nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(bounds.x, bounds.y + 4, 4, bounds.h - 8), 2.0f, nk_rgb(50, 220, 255));
                    
                    nk_style_pop_style_item(ctx);
                } else {
                    ctx->style.button.text_normal = nk_rgb(180, 180, 200);
                    nk_button_label(ctx, SNAME[i]);
                }
            }
            nk_group_end(ctx);
        }
        
        // --- RIGHT PANEL (MAIN CONTENT) ---
        nk_layout_row_template_push_dynamic(ctx);
        nk_layout_row_template_end(ctx);
        
        if (current_step == 0) {
            // --- WELCOME STEP ---
            // Existing logic
            nk_layout_row_dynamic(ctx, 40, 1);
            nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
            
            struct nk_user_font font_hero = nk_elseaos_create_font_bold(48.0f);
            nk_style_push_font(ctx, &font_hero);
            nk_layout_row_dynamic(ctx, 60, 1);
            nk_label_colored(ctx, "Welcome to ElseaOS", NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            nk_style_pop_font(ctx);
            
            struct nk_user_font font_sub = nk_elseaos_create_font(20.0f);
            nk_style_push_font(ctx, &font_sub);
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label_colored(ctx, "Let's get your system set up.", NK_TEXT_LEFT, nk_rgb(180, 185, 200));
            nk_style_pop_font(ctx);
            
            // Add a large logo or icon here
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_label(ctx, "", NK_TEXT_LEFT);
            
            nk_layout_row_dynamic(ctx, 200, 3);
            nk_label(ctx, "", NK_TEXT_LEFT);
            struct nk_image logo_img = elseaos_load_image("/usr/share/images/logo.png");
            if (logo_img.handle.ptr) {
                nk_image(ctx, logo_img);
            } else {
                nk_label_colored(ctx, "[ElseaOS Logo]", NK_TEXT_CENTERED, nk_rgb(100, 100, 120));
            }
            nk_label(ctx, "", NK_TEXT_LEFT);
            
        } else if (current_step == 1) {"""

content = content.replace(target1, rep1)

# Now delete the duplicate mangled step 1 from inside current_step == 1
# And replace with proper step 1
start_idx = content.find("                // --- LANGUAGE SELECTION STEP ---")
end_idx = content.find("            } else if (current_step == 2) {")

if start_idx != -1 and end_idx != -1:
    correct_step_1 = """                // --- LANGUAGE SELECTION STEP ---
                
                // Title
                struct nk_user_font font_hero = nk_elseaos_create_font_bold(32.0f);
                nk_style_push_font(ctx, &font_hero);
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label_colored(ctx, "Select Your Language", NK_TEXT_LEFT, nk_rgb(255, 255, 255));
                nk_style_pop_font(ctx);
                
                // Subtitle
                struct nk_user_font font_sub = nk_elseaos_create_font(18.0f);
                nk_style_push_font(ctx, &font_sub);
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label_colored(ctx, "Choose the language to use during installation.", NK_TEXT_LEFT, nk_rgb(180, 185, 200));
                nk_style_pop_font(ctx);
                
                nk_layout_row_dynamic(ctx, 20, 1);
                nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                
                // Content area (List on left, Globe on right)
                int content_height = main_height - 40 - 30 - 20 - 45 - 30; // Titles + Buttons + padding
                nk_layout_row_template_begin(ctx, content_height);
                nk_layout_row_template_push_static(ctx, 300); // List box width
                nk_layout_row_template_push_dynamic(ctx);     // Space between
                nk_layout_row_template_push_static(ctx, 400); // Globe image width
                nk_layout_row_template_end(ctx);
                
                // --- Language List ---
                static const char* languages[] = {
                    "English (US)", "English (UK)", "Hindi (India)",
                    "Español (Spanish)", "Français (French)", "Deutsch (German)",
                    "Chinese (Simplified)", "Chinese (Traditional)", "Japanese",
                    "Korean", "Arabic", "Russian", "Portuguese (Brazil)", "Portuguese (Portugal)",
                    "Italian", "Dutch", "Polish", "Turkish", "Vietnamese", "Thai",
                    "Indonesian", "Malay", "Bengali", "Urdu", "Persian (Farsi)",
                    "Swahili", "Amharic", "Yoruba", "Hausa", "Igbo",
                    "Zulu", "Xhosa", "Afrikaans", "Greek", "Hebrew",
                    "Swedish", "Norwegian", "Danish", "Finnish", "Hungarian",
                    "Czech", "Slovak", "Romanian", "Bulgarian", "Serbian",
                    "Croatian", "Ukrainian", "Filipino (Tagalog)", "Tamil", "Telugu",
                    "Marathi", "Gujarati", "Kannada", "Malayalam", "Punjabi",
                    "Javanese", "Sundanese", "Burmese", "Khmer", "Lao"
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
                        if (i == selected_lang) {
                            nk_style_pop_style_item(ctx);
                        }
                    }
                    nk_style_pop_font(ctx);
                    nk_group_end(ctx);
                }
                
                // Globe image on the right
                nk_label(ctx, "", NK_TEXT_LEFT); // Spacer for dynamic column
                
                struct nk_image globe_img = elseaos_load_image("/usr/share/images/globe.png");
                if (globe_img.handle.ptr) {
                    nk_image(ctx, globe_img);
                } else {
                    nk_label_colored(ctx, "[Globe Image]", NK_TEXT_CENTERED, nk_rgb(100, 100, 120));
                }
                
"""
    content = content[:start_idx] + correct_step_1 + content[end_idx:]

with open('src/installer.c', 'w') as f:
    f.write(content)
print("Applied fixes")

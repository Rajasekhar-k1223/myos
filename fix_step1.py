import re

with open('src/installer.c', 'r') as f:
    content = f.read()

# We need to find the start of `} else if (current_step == 1) {`
# And replace everything until `} else if (current_step == 2) {`

start_marker = "                } else if (current_step == 1) {"
end_marker = "            } else if (current_step == 2) {"

start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx != -1 and end_idx != -1:
    print(f"Found range: {start_idx} to {end_idx}")
    
    clean_step_1 = """                } else if (current_step == 1) {
                // --- LANGUAGE SELECTION STEP ---
                
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
                // Use nk_layout_row_dynamic to split equally, or template to give specific width
                int list_h = main_height - 80 - 40 - 30 - 20 - 45 - 20; // Title+Sub+Spacer+Nav
                nk_layout_row_template_begin(ctx, list_h);
                nk_layout_row_template_push_static(ctx, 300); // List box width
                nk_layout_row_template_push_dynamic(ctx);     // Space between
                nk_layout_row_template_push_static(ctx, 350); // Globe image width
                nk_layout_row_template_end(ctx);
                
                // --- Language List ---
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
                        if (i == selected_lang) {
                            nk_style_pop_style_item(ctx);
                        }
                    }
                    nk_style_pop_font(ctx);
                    nk_group_end(ctx);
                }
                
                // Globe image on the right
                nk_label(ctx, "", NK_TEXT_LEFT); // Spacer for dynamic column
                
                // Draw globe image inside a group to restrict height or just directly
                if (nk_group_begin(ctx, "GlobeGroup", NK_WINDOW_NO_SCROLLBAR)) {
                    nk_layout_row_dynamic(ctx, 350, 1);
                    struct nk_image globe_img = elseaos_load_image("/usr/share/images/globe.png");
                    if (globe_img.handle.ptr) {
                        nk_image(ctx, globe_img);
                    } else {
                        // Fallback draw
                        struct nk_rect b = nk_widget_bounds(ctx);
                        nk_fill_circle(nk_window_get_canvas(ctx), nk_rect(b.x + 50, b.y, 250, 250), nk_rgb(30, 80, 150));
                        nk_label_colored(ctx, "[Globe Missing]", NK_TEXT_CENTERED, nk_rgb(200, 200, 200));
                    }
                    nk_group_end(ctx);
                }
"""
    content = content[:start_idx] + clean_step_1 + content[end_idx:]
    print("Replaced step 1")
    
    with open('src/installer.c', 'w') as f:
        f.write(content)
else:
    print("Could not find start or end marker!")
    

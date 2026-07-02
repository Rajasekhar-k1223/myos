import re

with open('src/installer.c', 'r') as f:
    content = f.read()

# Let's replace the RIGHT PANE container
target_pane = """        // --- RIGHT PANEL (MAIN CONTENT) ---
        if (nk_group_begin(ctx, "Content", NK_WINDOW_NO_SCROLLBAR)) {"""

rep_pane = """        // --- RIGHT PANEL (MAIN CONTENT) ---
        if (nk_group_begin(ctx, "RightPane", NK_WINDOW_NO_SCROLLBAR)) {
            
            // Layout for Content + NavBar
            int content_h = main_height - 80;
            nk_layout_row_dynamic(ctx, content_h, 1);
            if (nk_group_begin(ctx, "Content", NK_WINDOW_NO_SCROLLBAR)) {"""

if target_pane in content:
    content = content.replace(target_pane, rep_pane)
    print("Replaced target_pane!")

target_end = """            } else {
                // Fallback for other steps
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label_colored(ctx, SNAME[current_step], NK_TEXT_CENTERED, nk_rgb(255, 255, 255));
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label(ctx, "Configuration options go here...", NK_TEXT_CENTERED);
                
                // Push to bottom
                int remaining = main_height - 40 - 40 - 45 - 30; // Titles + Buttons + padding
                if (remaining > 0) {
                    nk_layout_row_dynamic(ctx, remaining, 1);
                    nk_label(ctx, "", NK_TEXT_LEFT);
                }
                
                // Basic bottom navigation
                nk_layout_row_template_begin(ctx, 45);
                nk_layout_row_template_push_static(ctx, 120); // Back
                nk_layout_row_template_push_dynamic(ctx);     // Spacer
                nk_layout_row_template_push_static(ctx, 120); // Next/Finish
                nk_layout_row_template_end(ctx);
                
                ctx->style.button.rounding = 8.0f;
                
                if (nk_button_label(ctx, "Back")) current_step--;
                
                nk_label(ctx, "", NK_TEXT_LEFT); // Spacer
                
                // Blue accent for primary action
                nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(30, 80, 220, 255)));
                ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                
                if (current_step < NUM_STEPS - 1) {
                    if (nk_button_label(ctx, "Next")) current_step++;
                } else {
                    if (nk_button_label(ctx, "Finish")) {
                        nk_installer_running = 0;
                        in_installer_mode = 0;
                    }
                }
                nk_style_pop_style_item(ctx);
            }
            
            nk_group_end(ctx);
        }
    }
    nk_end(ctx);
    
    nk_elseaos_render();
}"""

rep_end = """            } else {
                // Fallback for other steps
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label_colored(ctx, SNAME[current_step], NK_TEXT_CENTERED, nk_rgb(255, 255, 255));
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label(ctx, "Configuration options go here...", NK_TEXT_CENTERED);
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
}"""

if target_end in content:
    content = content.replace(target_end, rep_end)
    print("Replaced target_end!")

with open('src/installer.c', 'w') as f:
    f.write(content)

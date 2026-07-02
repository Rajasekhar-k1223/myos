import re

with open('src/installer.c', 'r') as f:
    content = f.read()

# 1. Fix the title bar layout to add macOS dots
title_bar_target = """        // --- CUSTOM TITLE BAR ---
        nk_layout_row_template_begin(ctx, 30);
        nk_layout_row_template_push_static(ctx, 200); // Sidebar Header
        nk_layout_row_template_push_dynamic(ctx);     // Window Title
        nk_layout_row_template_push_static(ctx, 60);  // Window Controls
        nk_layout_row_template_end(ctx);
        
        // Col 1: Logo
        struct nk_user_font font_bold = nk_elseaos_create_font_bold(22.0f);
        nk_style_push_font(ctx, &font_bold);
        nk_label_colored(ctx, "    @ ElseaOS", NK_TEXT_LEFT, nk_rgb(100, 200, 255));
        nk_style_pop_font(ctx);
        
        // Col 2: Window Title
        struct nk_user_font font_title = nk_elseaos_create_font(24.0f);
        nk_style_push_font(ctx, &font_title);
        nk_label_colored(ctx, "ElseaOS Setup", NK_TEXT_CENTERED, nk_rgb(220, 220, 220));
        nk_style_pop_font(ctx);
        
        // Col 3: Controls (... and X)
        nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(0,0,0,0)));
        if (nk_group_begin(ctx, "Controls", NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, 24, 2);
            nk_button_label(ctx, "...");
            if (nk_button_label(ctx, "X")) {
                // Quit button logic
            }
            nk_group_end(ctx);
        }
        nk_style_pop_style_item(ctx);"""

title_bar_replace = """        // --- CUSTOM TITLE BAR ---
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
        nk_label(ctx, "", NK_TEXT_LEFT);"""

if title_bar_target in content:
    content = content.replace(title_bar_target, title_bar_replace)
    print("Replaced title bar!")
else:
    print("Could not find title bar target.")

with open('src/installer.c', 'w') as f:
    f.write(content)

import re

with open('src/installer.c', 'r') as f:
    text = f.read()

# Replace Right Pane start
text = text.replace(
    'if (nk_group_begin(ctx, "Content", NK_WINDOW_NO_SCROLLBAR)) {',
    'if (nk_group_begin(ctx, "ContentWrapper", NK_WINDOW_NO_SCROLLBAR)) {\n            nk_layout_row_dynamic(ctx, main_height - 70, 1);\n            if (nk_group_begin(ctx, "StepContent", NK_WINDOW_NO_SCROLLBAR)) {'
)

# Step 0 deletion
text = re.sub(
    r'// Spacer\n\s*int space_left = main_height - 60 - 30 - 300 - 30 - 45 - 20;.*?\n\s*nk_style_pop_font\(ctx\);\n\s*\} else if \(current_step == 1\)',
    '            } else if (current_step == 1)',
    text, flags=re.DOTALL
)

# Step 1-X deletion (all that say "Bottom spacing for Back/Next buttons")
text = re.sub(
    r'// Bottom spacing for Back/Next buttons.*?\n\s*nk_style_pop_font\(ctx\);\n\s*\} else if ',
    '            } else if ',
    text, flags=re.DOTALL
)

# Step 14 deletion
text = re.sub(
    r'// Bottom spacing for Back/Next buttons.*?\n\s*nk_style_pop_font\(ctx\);\n\s*\} else \{',
    '            } else {',
    text, flags=re.DOTALL
)

# Fallback deletion
text = re.sub(
    r'// Push to bottom.*?\n\s*nk_style_pop_style_item\(ctx\);\n\s*\}\n\s*nk_group_end\(ctx\);\n\s*\}',
    '            }\n            nk_group_end(ctx); // End StepContent\n\n            // --- GLOBAL NAVIGATION BAR ---\n            nk_layout_row_dynamic(ctx, 60, 1);\n            if (nk_group_begin(ctx, "NavBar", NK_WINDOW_NO_SCROLLBAR)) {\n                ctx->style.button.rounding = 12.0f;\n                struct nk_user_font font_nav = nk_elseaos_create_font(20.0f);\n                nk_style_push_font(ctx, &font_nav);\n                \n                if (current_step == 0) {\n                    nk_layout_row_template_begin(ctx, 45);\n                    nk_layout_row_template_push_dynamic(ctx);\n                    nk_layout_row_template_push_static(ctx, 160);\n                    nk_layout_row_template_push_static(ctx, 30);\n                    nk_layout_row_template_end(ctx);\n                    nk_label(ctx, "", NK_TEXT_LEFT);\n                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(30, 100, 240, 255)));\n                    nk_style_push_style_item(ctx, &ctx->style.button.hover, nk_style_item_color(nk_rgba(50, 130, 255, 255)));\n                    ctx->style.button.text_normal = nk_rgb(255, 255, 255);\n                    struct nk_rect next_bounds = nk_widget_bounds(ctx);\n                    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);\n                    nk_fill_rect(canvas, nk_rect(next_bounds.x-2, next_bounds.y-2, next_bounds.w+4, next_bounds.h+4), 16.0f, nk_rgba(30, 100, 240, 60));\n                    nk_fill_rect(canvas, nk_rect(next_bounds.x-4, next_bounds.y-4, next_bounds.w+8, next_bounds.h+8), 18.0f, nk_rgba(30, 100, 240, 30));\n                    if (nk_button_label(ctx, "Next >")) current_step++;\n                    nk_style_pop_style_item(ctx);\n                    nk_style_pop_style_item(ctx);\n                    nk_label(ctx, "", NK_TEXT_LEFT);\n                } else {\n                    nk_layout_row_template_begin(ctx, 45);\n                    nk_layout_row_template_push_static(ctx, 120);\n                    nk_layout_row_template_push_dynamic(ctx);\n                    nk_layout_row_template_push_static(ctx, 120);\n                    nk_layout_row_template_push_static(ctx, 30);\n                    nk_layout_row_template_end(ctx);\n                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(25, 30, 45, 255)));\n                    ctx->style.button.text_normal = nk_rgb(200, 210, 230);\n                    if (nk_button_label(ctx, "< Back")) current_step--;\n                    nk_style_pop_style_item(ctx);\n                    nk_label(ctx, "", NK_TEXT_LEFT);\n                    nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(30, 80, 220, 255)));\n                    ctx->style.button.text_normal = nk_rgb(255, 255, 255);\n                    if (current_step < 14) {\n                        if (nk_button_label(ctx, "Next >")) current_step++;\n                    } else {\n                        if (nk_button_label(ctx, "Finish")) {\n                            extern int nk_installer_running;\n                            extern int in_installer_mode;\n                            nk_installer_running = 0;\n                            in_installer_mode = 0;\n                        }\n                    }\n                    nk_style_pop_style_item(ctx);\n                    nk_label(ctx, "", NK_TEXT_LEFT);\n                }\n                nk_style_pop_font(ctx);\n                nk_group_end(ctx);\n            }\n            nk_group_end(ctx);\n        }',
    text, flags=re.DOTALL
)

with open('src/installer.c', 'w') as f:
    f.write(text)

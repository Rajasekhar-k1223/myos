import re

with open('src/installer.c', 'r') as f:
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
        nk_style_pop_style_item(ctx);
        nk_style_pop_font(ctx);
        nk_style_pop_style_item(ctx);
        
        // --- RIGHT PANEL (MAIN CONTENT) ---
        if (nk_group_begin(ctx, "Content", NK_WINDOW_NO_SCROLLBAR)) {
"""

if target1 in content:
    content = content.replace(target1, rep1)
    print("Replaced target1!")
else:
    print("target1 NOT FOUND!")

# We also have an incomplete end to the Language list that bleeds into current_step == 2
# Let's fix that. We'll find:
# "                        } else if (current_step == 1) {"
# inside the language list loop
target2 = """                        if (i == selected_lang) {
                            nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(40, 80, 180, 255)));
                            ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                        } else if (current_step == 1) {"""

rep2 = """                        if (i == selected_lang) {
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
            } else if (current_step == 999) {"""

if target2 in content:
    content = content.replace(target2, rep2)
    print("Replaced target2!")
else:
    print("target2 NOT FOUND!")

# The second target is current_step == 2
target3 = """                        if (i == selected_lang) {
                            nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(40, 80, 180, 255)));
                            ctx->style.button.text_normal = nk_rgb(255, 255, 255);
                        } else if (current_step == 2) {"""

rep3 = """                        if (i == selected_lang) {
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
            } else if (current_step == 2) {"""

if target3 in content:
    content = content.replace(target3, rep3)
    print("Replaced target3!")
else:
    print("target3 NOT FOUND!")

with open('src/installer.c', 'w') as f:
    f.write(content)

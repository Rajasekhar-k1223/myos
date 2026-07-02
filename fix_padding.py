import re

with open('src/installer.c', 'r') as f:
    content = f.read()

# 1. Update the layout row template to push an extra 30 static column for right margin
content = re.sub(
    r'(nk_layout_row_template_push_static\(ctx, 120\);.*?nk_layout_row_template_push_dynamic\(ctx\);.*?nk_layout_row_template_push_static\(ctx, 120\);.*?\n)(\s*)(nk_layout_row_template_end\(ctx\);)',
    r'\1\2nk_layout_row_template_push_static(ctx, 30);\n\2\3',
    content
)

# 2. Find all instances where "Next >" is drawn and add nk_label(ctx, "", NK_TEXT_LEFT); AFTER nk_style_pop_style_item(ctx);
# We need to look for `nk_button_label(ctx, "Next >")` or `nk_button_label(ctx, "Install")` or `nk_button_label(ctx, "Finish")`
content = re.sub(
    r'(if \(nk_button_label\(ctx, "(?:Next >|Install|Finish)"\)\).*?\n\s*nk_style_pop_style_item\(ctx\);)\n(\s*)',
    r'\1\n\2nk_label(ctx, "", NK_TEXT_LEFT);\n\2',
    content
)

with open('src/installer.c', 'w') as f:
    f.write(content)

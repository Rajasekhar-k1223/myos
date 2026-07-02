#ifndef NK_BACKEND_H
#define NK_BACKEND_H

#include "kernel.h"
#include "vesa.h"
#include "wm.h"

// Initialize Nuklear for ElseaOS
void nk_elseaos_init(void);

// Provide Nuklear context
struct nk_context* nk_elseaos_get_context(void);

// Process input
void nk_elseaos_process_input(void);

// Create a font
struct nk_user_font nk_elseaos_create_font(float size);
struct nk_user_font nk_elseaos_create_font_bold(float size);

// Render the Nuklear UI
void nk_elseaos_render(void);

#endif

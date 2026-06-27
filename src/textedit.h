#pragma once
#include "wm.h"

void textedit_init(window_t* win, const char* filename);
void textedit_handle_key(window_t* win, char c);
void textedit_render(window_t* win);

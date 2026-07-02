#pragma once
#include "wm.h"

void search_toggle(void);
void search_render(void);
void search_handle_keypress(char c);
void search_handle_click(int mx, int my);
int search_is_open(void);

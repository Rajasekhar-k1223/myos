#pragma once
#include "wm.h"

void spreadsheet_init(window_t* win);
void spreadsheet_handle_key(window_t* win, char c);
void spreadsheet_handle_click(window_t* win, int mx, int my);

#pragma once
#include <stdint.h>
#include "wm.h"

void bmp_draw_file(const char* filename, uint32_t x, uint32_t y);
void bmp_load_to_window(const char* filename, window_t* win);

#pragma once
#include <stdint.h>

void wm_init(void);
void wm_request_redraw(void);
void wm_create_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* title);
void wm_process_events(void);

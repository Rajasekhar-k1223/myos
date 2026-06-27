#pragma once
#include "wm.h"

void video_player_init(window_t* win);
void video_player_handle_key(window_t* win, char c);
int  video_player_tick(window_t* win);
